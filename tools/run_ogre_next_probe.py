#!/usr/bin/env python3
"""Build and validate the isolated, pinned OGRE-Next capability probe."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import ntpath
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
PROBE_SOURCE = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
LOCK_PATH = PROBE_SOURCE / "ogre-next.lock.json"
LINUX_SHADER_TOOLCHAIN_LOCK_PATH = (
    PROBE_SOURCE / "linux-shader-toolchain.lock.json"
)
LINUX_SHADER_TOOLCHAIN_LOCK_SHA256 = (
    "02d2a965f817786e295212161686c8fc1ff33f0000946b5f90ebd4c161eac35e"
)
REPORT_NAME = "ror-ogre-next-probe-report.json"
BUILD_CONTRACT_NAME = "ogre-next-build-contract.json"
FRAME_REPORT_NAME = "ror-ogre-next-frame-probe-report.json"
FRAME_IMAGE_NAME = "ror-ogre-next-frame-probe.ppm"
N1_REPORT_NAME = "ror-ogre-next-frontend-n1-report.json"
N1_IMAGE_NAME = "ror-ogre-next-frontend-n1.ppm"
N1_PACKAGE_NAME = "ror-ogre-next-n1-package"
N2_REPORT_NAME = "ror-ogre-next-metal-n2-report.json"
N2_PROBE_NAME = "ror-ogre-next-metal-n2-probe.bin"
N2_ATTESTATION_NAME = "ror-ogre-next-metal-n2-attestation.json"
LINUX_STATIC_CLOSURE_MANIFEST_NAME = "ogre-next-linux-static-closure.json"
N3_REPORT_NAME = "ror-ogre-next-metal-n3-report.json"
N3_RASTER_NAME = "ror-ogre-next-metal-n3-raster.bin"
N3_CONTRIBUTION_NAME = "ror-ogre-next-metal-n3-contribution.bin"
N3_HYBRID_NAME = "ror-ogre-next-metal-n3-hybrid.bin"
N3_ATTESTATION_NAME = "ror-ogre-next-metal-n3-attestation.json"
FRAME_VALIDATOR = REPOSITORY_ROOT / "tools" / "validate_ogre_next_frame_probe.py"
BUILD_SENTINEL_NAME = ".ror-ogre-next-probe-build-v1"
BUILD_SENTINEL_CONTENT = "ror-ogre-next-probe-build-v1\n"
REQUIRED_CONFIG = "Release"
ROR_SOURCE_REPOSITORY = "https://github.com/oasiz-ai/rigs-of-rods"
RELEVANT_SOURCE_PATHS = (
    "source/main/gfx/render",
    "tools/ogre_next_probe",
    "tools/run_ogre_next_probe.py",
    "tools/validate_ogre_next_frame_probe.py",
    "tools/verify_ogre_next_artifact_set.py",
)


class ProbeError(RuntimeError):
    """Raised when a capability or provenance contract fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def relevant_source_manifest(
    repository_root: Path = REPOSITORY_ROOT,
    paths: tuple[str, ...] = RELEVANT_SOURCE_PATHS,
) -> dict[str, Any]:
    roots = tuple(repository_root / relative for relative in paths)
    selected_paths: set[Path] = set()
    for root in roots:
        if root.is_symlink():
            raise ProbeError(
                "RoR relevant source contains a symbolic link: "
                + root.relative_to(repository_root).as_posix()
            )
        if root.is_dir():
            selected_paths.update(root.rglob("*"))
        else:
            selected_paths.add(root)
    entries: list[tuple[str, int, str]] = []
    for path in sorted(selected_paths, key=lambda item: item.as_posix()):
        relative = path.relative_to(repository_root)
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


def require_relevant_source_clean(
    repository_root: Path = REPOSITORY_ROOT,
    paths: tuple[str, ...] = RELEVANT_SOURCE_PATHS,
) -> None:
    try:
        status_result = subprocess.run(
            [
                "git",
                "status",
                "--porcelain=v1",
                "--untracked-files=all",
                "--",
                *paths,
            ],
            cwd=repository_root,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ProbeError(
            f"could not inspect Metal N2 relevant source state: {error}"
        ) from error
    if status_result.returncode != 0:
        detail = status_result.stderr.decode("utf-8", errors="replace").strip()
        raise ProbeError(
            "could not inspect Metal N2 relevant source state"
            + (f": {detail}" if detail else "")
        )
    dirty = status_result.stdout.decode("utf-8", errors="replace").strip()
    if dirty:
        raise ProbeError(
            "Metal N2 provenance requires a clean relevant source set:\n"
            + dirty
        )


def repository_identity(
    repository_root: Path = REPOSITORY_ROOT,
) -> tuple[str, str, str]:
    require_relevant_source_clean(repository_root)
    try:
        commit_result = subprocess.run(
            ["git", "rev-parse", "--verify", "HEAD"],
            cwd=repository_root,
            check=True,
            capture_output=True,
            text=True,
        )
        commit = commit_result.stdout.strip()
        ref_result = subprocess.run(
            ["git", "symbolic-ref", "--short", "HEAD"],
            cwd=repository_root,
            check=False,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise ProbeError(f"could not resolve RoR source provenance: {error}") from error
    if re.fullmatch(r"[0-9a-f]{40}", commit) is None:
        raise ProbeError("checked-out RoR commit is not a full lowercase Git SHA")
    ref = ref_result.stdout.strip() if ref_result.returncode == 0 else "detached"
    if not ref:
        raise ProbeError("checked-out RoR ref is empty")
    manifest = relevant_source_manifest(repository_root)
    return commit, ref, manifest["sha256"]


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
    if type(lock.get("schema_version")) is not int or lock.get("schema_version") != 2:
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


def load_linux_shader_toolchain_lock(
    path: Path = LINUX_SHADER_TOOLCHAIN_LOCK_PATH,
) -> dict[str, Any]:
    try:
        source = path.read_text(encoding="utf-8")
        lock = json.loads(source)
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(
            f"could not read Linux shader toolchain lock: {error}"
        ) from error
    if source != json.dumps(lock, indent=2) + "\n":
        raise ProbeError("Linux shader toolchain lock is not canonical JSON")
    if sha256_file(path) != LINUX_SHADER_TOOLCHAIN_LOCK_SHA256:
        raise ProbeError("Linux shader toolchain lock moved without review")
    if (
        lock.get("schema") != "ror.ogre_next_linux_shader_toolchain.v1"
        or lock.get("platform_policy") != "linux-x86_64-vulkan"
        or lock.get("provider") != "pinned-source"
    ):
        raise ProbeError("Linux shader source policy changed")

    expected_sources = {
        "shaderc": (
            lock.get("shaderc_release", {}),
            "https://github.com/google/shaderc",
            "v2025.3",
            "8c2e602ce440b7739c95ff3d69cecb1adf6becda",
            "1a17c01614debaacd5c3674a540368119e93bd299991e0f1c3554875c92ef5e2",
            "c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4",
            "licenses/Apache-2.0.txt",
        ),
        "glslang": (
            lock.get("dependencies", {}).get("glslang", {}),
            "https://github.com/KhronosGroup/glslang",
            "15.3.0+efd24d75",
            "efd24d75bcbc55620e759f6bf42c45a32abac5f8",
            "9427deccbdf4bde6a269938df38c6bd75247493786a310d8d733a2c82065ef47",
            "adb783e734e906d1f46db5df29991dbde84bdb0ceab502ac2febb44fe3c2b5f4",
            "licenses/glslang-LICENSE.txt",
        ),
        "spirv-tools": (
            lock.get("dependencies", {}).get("spirv_tools", {}),
            "https://github.com/KhronosGroup/SPIRV-Tools",
            "v2025.3",
            "33e02568181e3312f49a3cf33df470bf96ef293a",
            "44d1005880c583fc00a0fb41c839214c68214b000ea8dcb54d352732fee600ff",
            "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30",
            "licenses/SPIRV-Tools-LICENSE.txt",
        ),
        "spirv-headers": (
            lock.get("dependencies", {}).get("spirv_headers", {}),
            "https://github.com/KhronosGroup/SPIRV-Headers",
            "1.5.5+2a611a97",
            "2a611a970fdbc41ac2e3e328802aed9985352dca",
            "c2225a49c3d7efa5c4f4ce4a6b42081e6ea3daca376f3353d9d7c2722d77a28a",
            "ea43b1de38a6f90c488800d66dec1ed671e68cda530266bc96951fb5b6307613",
            "licenses/SPIRV-Headers-LICENSE.txt",
        ),
    }
    for component, expected in expected_sources.items():
        record, repository, version, commit, archive_hash, license_hash, notice = (
            expected
        )
        observed_version = record.get(
            "tag" if component == "shaderc" else "version"
        )
        checks = (
            record.get("repository") == repository,
            observed_version == version,
            record.get("commit") == commit,
            record.get("archive_url")
            == f"{repository}/archive/{commit}.tar.gz",
            record.get("archive_sha256") == archive_hash,
            record.get("license_sha256") == license_hash,
            record.get("package_notice_path") == notice,
            record.get("package_notice_sha256") == license_hash,
        )
        if not all(checks):
            raise ProbeError(f"Linux shader source pin changed for {component}")
        _require_sha256(record.get("archive_sha256"), f"{component} archive")
        _require_sha256(record.get("license_sha256"), f"{component} license")

    shaderc = lock["shaderc_release"]
    if (
        shaderc.get("dependency_manifest_path") != "DEPS"
        or shaderc.get("dependency_manifest_sha256")
        != "586935f05d12137e2aa587bd96a96b66e75320b1e311907e20567ab352b19a48"
    ):
        raise ProbeError("shaderc dependency manifest contract changed")
    shaderc_patch = shaderc.get("compatibility_patch", {})
    if (
        shaderc_patch.get("path")
        != "patches/0003-shaderc-disable-glslang-install.patch"
        or shaderc_patch.get("sha256")
        != "9742f2a9fcb5aef762298d823acb85352e536402ba7c4587173cc52130012b0b"
    ):
        raise ProbeError("shaderc CMake compatibility patch contract changed")
    shaderc_patch_path = path.parent / shaderc_patch["path"]
    if (
        not shaderc_patch_path.is_file()
        or sha256_file(shaderc_patch_path) != shaderc_patch["sha256"]
    ):
        raise ProbeError("shaderc CMake compatibility patch SHA-256 mismatch")

    expected_targets = [
        "shaderc_combined",
        "shaderc",
        "shaderc_util",
        "glslang",
        "SPIRV",
        "SPIRV-Tools-opt",
        "SPIRV-Tools-static",
    ]
    targets = lock.get("static_closure_targets")
    if not isinstance(targets, list) or [
        record.get("target") for record in targets
    ] != expected_targets:
        raise ProbeError("Linux shader static closure inventory changed")

    patch = lock.get("ogre_compatibility_patch", {})
    if (
        patch.get("path") != "patches/0002-vulkan-use-glslang-spv-options.patch"
        or patch.get("sha256")
        != "4242ad130cff4e70245d151d0b1a0a63959d3d9b25d11a5587a74f48b15b7897"
    ):
        raise ProbeError("OGRE/glslang compatibility patch contract changed")
    patch_path = path.parent / patch["path"]
    if not patch_path.is_file() or sha256_file(patch_path) != patch["sha256"]:
        raise ProbeError("OGRE/glslang compatibility patch SHA-256 mismatch")

    reflect = lock.get("ogre_embedded_components", {}).get("spirv_reflect", {})
    if (
        reflect.get("source_sha256")
        != "41394a0cfed351240dc811758d398117ec2cd13ba95dc9f1a1e346546ac7b4d2"
        or reflect.get("header_sha256")
        != "2f3823ea53c6c86902841b5bef3c0b604d56a1e18b97ca46498b6e764573ab03"
        or reflect.get("license_expression") != "Apache-2.0"
        or reflect.get("package_notice_path") != "licenses/Apache-2.0.txt"
    ):
        raise ProbeError("embedded SPIRV-Reflect contract changed")
    if lock.get("host_dynamic_boundary") != {
        "component": "Vulkan-Loader",
        "cmake_library": "Vulkan_LIBRARY",
        "policy": (
            "host-provided dynamic system API; never copied or statically "
            "linked into the N1 package"
        ),
    }:
        raise ProbeError("Linux Vulkan host dynamic boundary changed")
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
        cache_prefix = "CMAKE_HOME_DIRECTORY:INTERNAL="
        cached_sources = [
            line[len(cache_prefix) :]
            for line in cache_text.splitlines()
            if line.startswith(cache_prefix)
        ]
        cached_source = cached_sources[0] if len(cached_sources) == 1 else None
        windows_paths = os.name == "nt"
        exact_source = (
            cached_source is not None
            and _normalize_cmake_source_path(cached_source, windows_paths)
            == _normalize_cmake_source_path(
                str(PROBE_SOURCE.resolve()), windows_paths
            )
        )
        if sentinel_content != BUILD_SENTINEL_CONTENT or not exact_source:
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


def _normalize_cmake_source_path(value: str, windows: bool) -> str:
    """Normalize CMake cache paths without assuming its separator spelling."""

    if not value or "\0" in value or "\n" in value or "\r" in value:
        return ""
    if windows:
        return ntpath.normcase(ntpath.normpath(value))
    return os.path.normcase(os.path.realpath(value))


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
    missing = [
        path.name for path in (report_path, image_path) if not path.is_file()
    ]
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


def validate_n2_checkpoint(
    report: dict[str, Any],
    probe_path: Path | None,
    executable_path: Path,
    lock: dict[str, Any],
    policy: dict[str, str],
    expected_source_commit: str,
    expected_source_ref: str,
    expected_source_manifest_sha256: str,
) -> None:
    if policy["name"] != "macos-arm64-metal":
        raise ProbeError("Metal N2 validation is Apple-only")
    if re.fullmatch(r"[0-9a-f]{40}", expected_source_commit) is None:
        raise ProbeError("expected RoR source commit is not a full Git SHA")
    _require_sha256(
        expected_source_manifest_sha256,
        "expected Metal N2 relevant-source manifest",
    )
    try:
        executable_bytes = executable_path.stat().st_size
        executable_sha256 = sha256_file(executable_path)
    except OSError as error:
        raise ProbeError(f"could not attest Metal N2 executable: {error}") from error

    def object_field(name: str) -> dict[str, Any]:
        value = report.get(name, {})
        return value if isinstance(value, dict) else {}

    def nonnegative_int(mapping: dict[str, Any], name: str) -> int | None:
        value = mapping.get(name)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            return None
        return value

    provenance = object_field("provenance")
    device = object_field("device")
    admission = object_field("admission")
    geometry = object_field("geometry")
    synchronization = object_field("synchronization")
    acceleration = object_field("acceleration_structures")
    probe = object_field("probe")
    lifecycle = object_field("lifecycle")
    status = report.get("status")
    common_checks = {
        "schema": report.get("schema") == "ror.ogre_next_metal_rt_n2.v3",
        "status": status in ("pass", "skip"),
        "scope": report.get("scope")
        == (
            "same-device single-ray geometry interop capability probe; no "
            "rendered image, view-dependent result, GPU timing, material, "
            "lighting, denoising, or compositing claim"
        ),
        "ror_repository": provenance.get("ror_repository")
        == ROR_SOURCE_REPOSITORY,
        "ror_ref": provenance.get("ror_ref") == expected_source_ref,
        "ror_commit": provenance.get("ror_commit") == expected_source_commit,
        "relevant_source_clean": provenance.get("relevant_source_clean")
        is True,
        "relevant_source_manifest": provenance.get(
            "relevant_source_manifest_sha256"
        )
        == expected_source_manifest_sha256,
        "ogre_repository": provenance.get("ogre_next_repository")
        == lock["repository"],
        "ogre_commit": provenance.get("ogre_next_commit") == lock["commit"],
        "ogre_archive": provenance.get("ogre_next_archive_sha256")
        == lock["archive_sha256"],
        "build_artifact": provenance.get("build_artifact")
        == executable_path.name,
        "build_artifact_bytes": provenance.get("build_artifact_bytes")
        == executable_bytes
        and executable_bytes > 0,
        "build_artifact_sha256": provenance.get("build_artifact_sha256")
        == executable_sha256,
        "backend_compiled": admission.get("backend_compiled") is True,
        "no_render_claim": probe.get("rendered_image_produced") is False
        and probe.get("view_dependent") is False
        and probe.get("gpu_timestamp_measured") is False,
        "legacy_dispatch_absent": "dispatch" not in report,
    }
    failed = [name for name, passed in common_checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next Metal N2 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )

    if status == "skip":
        skip = object_field("skip")
        skip_checks = {
            "probe_absent": probe_path is None or not probe_path.exists(),
            "device": isinstance(device.get("name"), str)
            and bool(device["name"])
            and nonnegative_int(device, "context_id") not in (None, 0),
            "same_device_queue": device.get("same_ogre_device") is True
            and device.get("same_ogre_queue") is True,
            "interop_context": admission.get("interop_context_exported") is True,
            "hardware_unavailable": admission.get("hardware_floor_met") is False
            and isinstance(admission.get("supports_raytracing"), bool)
            and isinstance(admission.get("supports_family_apple9"), bool)
            and (
                admission.get("supports_raytracing") is False
                or admission.get("supports_family_apple9") is False
            ),
            "skip_code": skip.get("initialization_code") == "UNSUPPORTED",
            "skip_reason": isinstance(skip.get("reason"), str)
            and bool(skip["reason"]),
            "hardware_floor": skip.get("required_metal_ray_tracing") is True
            and skip.get("required_apple_gpu_family") == 9,
            "probe_not_executed": probe.get("executed") is False
            and probe.get("probe_readback_bytes") == 0,
            "pass_evidence_absent": not geometry
            and not synchronization
            and not acceleration
            and not lifecycle,
        }
        failed = [name for name, passed in skip_checks.items() if not passed]
        if failed:
            raise ProbeError(
                "OGRE-Next Metal N2 capability skip failed closed: "
                + ", ".join(sorted(failed))
            )
        return

    if probe_path is None:
        raise ProbeError("passed Metal N2 checkpoint has no probe artifact")
    try:
        probe_bytes = probe_path.read_bytes()
    except OSError as error:
        raise ProbeError(f"could not read Metal N2 probe: {error}") from error
    if len(probe_bytes) != 8:
        raise ProbeError("Metal N2 artifact is not the exact eight-byte probe")
    hit_magic, hit_distance = struct.unpack("<If", probe_bytes)
    if hit_magic != 0x52545254 or abs(hit_distance - 1.0) > 0.0001:
        raise ProbeError("Metal N2 probe does not encode the proven unit hit")

    vertex_offset = nonnegative_int(geometry, "vertex_pool_offset_bytes")
    vertex_size = nonnegative_int(geometry, "vertex_slice_bytes")
    index_offset = nonnegative_int(geometry, "index_pool_offset_bytes")
    index_size = nonnegative_int(geometry, "index_slice_bytes")
    vertex_length = nonnegative_int(geometry, "vertex_buffer_length_bytes")
    index_length = nonnegative_int(geometry, "index_buffer_length_bytes")
    vertex_end = (
        vertex_offset + vertex_size
        if vertex_offset is not None and vertex_size is not None
        else None
    )
    index_end = (
        index_offset + index_size
        if index_offset is not None and index_size is not None
        else None
    )
    context_id = nonnegative_int(device, "context_id")
    frontend_complete = nonnegative_int(
        synchronization, "frontend_complete_value"
    )
    external_complete = nonnegative_int(
        synchronization, "external_complete_value"
    )
    checks = {
        "device": isinstance(device.get("name"), str)
        and bool(device["name"])
        and context_id is not None
        and context_id > 0,
        "same_device_queue": device.get("same_ogre_device") is True
        and device.get("same_ogre_queue") is True,
        "admission": all(
            admission.get(field) is True
            for field in (
                "frontend_api_reported",
                "backend_compiled",
                "api_supported",
                "supports_raytracing",
                "supports_family_apple9",
                "hardware_accelerated",
                "dispatch_readback_probe_passed",
                "geometry_interop_ready",
            )
        ),
        "deformed_triangle": geometry.get("frame_id") == 1
        and geometry.get("snapshot_id") == 1
        and geometry.get("instance_id") == 1
        and geometry.get("topology_revision") == 1
        and geometry.get("deformation_revision") == 2
        and geometry.get("vertex_count") == 3
        and geometry.get("index_count") == 3,
        "vertex_generation": geometry.get("vertex_buffer_generation")
        == geometry.get("frame_id"),
        "index_generation": geometry.get("index_buffer_generation")
        == geometry.get("frame_id"),
        "vertex_slice": vertex_size == 60
        and geometry.get("vertex_stride_bytes") == 24
        and vertex_end is not None
        and vertex_length is not None
        and vertex_end <= vertex_length,
        "index_slice": index_size == 6
        and geometry.get("index_stride_bytes") == 2
        and index_end is not None
        and index_length is not None
        and index_end <= index_length,
        "exact_slices": geometry.get("exact_exported_vertex_slice_used") is True
        and geometry.get("exact_exported_index_slice_used") is True,
        "timeline_values": frontend_complete is not None
        and external_complete is not None
        and frontend_complete > 0
        and external_complete > frontend_complete,
        "timeline_order": all(
            synchronization.get(field) is True
            for field in (
                "same_shared_event",
                "external_encoders_ended_before_signal",
                "cpu_wait_after_commit_only",
            )
        ),
        "blas_tlas": all(
            isinstance(acceleration.get(field), int)
            and acceleration[field] > 0
            for field in (
                "blas_bytes",
                "blas_scratch_bytes",
                "tlas_bytes",
                "tlas_scratch_bytes",
            )
        ),
        "ray_hit": probe.get("kind") == "single_ray_geometry_interop"
        and probe.get("rays") == 1
        and probe.get("hit_magic") == hit_magic
        and isinstance(probe.get("hit_distance"), (int, float))
        and abs(probe["hit_distance"] - hit_distance) <= 0.0001,
        "probe_readback": probe.get("probe_readback_bytes") == len(probe_bytes)
        and probe.get("probe_readback_sha256")
        == hashlib.sha256(probe_bytes).hexdigest(),
        "lifecycle": all(
            lifecycle.get(field) is True
            for field in (
                "stale_generation_rejected",
                "revision_n_plus_one_blocked_while_n_live",
                "frontend_shutdown_blocked_before_backend",
                "backend_shutdown_before_frontend",
                "frontend_revoke_clears_backend_readiness",
                "frontend_destructor_before_backend_safe",
                "backend_destructor_before_frontend_safe",
                "native_submission_precedes_injected_observation",
                "injected_device_lost_abandon_allows_frontend_shutdown",
                "injected_timeout_abandon_allows_frontend_shutdown",
                "post_release_revision_n_plus_one_rendered",
                "interop_report_geometry_proven",
            )
        ),
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next Metal N2 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )


def run_n2_checkpoint(
    build_dir: Path,
    config: str,
    jobs: int,
    lock: dict[str, Any],
    policy: dict[str, str],
) -> None:
    if policy["name"] != "macos-arm64-metal":
        return
    source_commit, source_ref, source_manifest_sha256 = repository_identity()
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_metal_n2_report",
            "--config",
            config,
            "--parallel",
            str(jobs),
        ]
    )
    report_path = build_dir / N2_REPORT_NAME
    probe_candidate = build_dir / N2_PROBE_NAME
    probe_path = probe_candidate if probe_candidate.is_file() else None
    executable_candidates = (
        build_dir / "bin" / "ror_ogre_next_metal_n2_smoke",
        build_dir / "bin" / config / "ror_ogre_next_metal_n2_smoke",
    )
    executable_path = next(
        (candidate for candidate in executable_candidates if candidate.is_file()),
        executable_candidates[0],
    )
    missing = [path.name for path in (report_path, executable_path) if not path.is_file()]
    if missing:
        raise ProbeError(
            "OGRE-Next Metal N2 checkpoint did not produce required artifacts: "
            + ", ".join(missing)
        )
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read Metal N2 report: {error}") from error
    rebuilt_identity = repository_identity()
    if rebuilt_identity != (source_commit, source_ref, source_manifest_sha256):
        raise ProbeError(
            "RoR source provenance changed while building the Metal N2 proof"
        )
    validate_n2_checkpoint(
        report,
        probe_path,
        executable_path,
        lock,
        policy,
        source_commit,
        source_ref,
        source_manifest_sha256,
    )

    attestation = {
        "schema": "ror.ogre_next_metal_rt_n2.attestation.v2",
        "status": report.get("status"),
        "source": {
            "ror_commit": source_commit,
            "ror_ref": source_ref,
            "relevant_source_clean": True,
            "relevant_source_manifest_sha256": source_manifest_sha256,
        },
        "executable": {
            "path": executable_path.name,
            "bytes": executable_path.stat().st_size,
            "sha256": sha256_file(executable_path),
        },
        "report": {
            "path": report_path.name,
            "bytes": report_path.stat().st_size,
            "sha256": sha256_file(report_path),
        },
        "probe": (
            {
                "path": probe_path.name,
                "bytes": probe_path.stat().st_size,
                "sha256": sha256_file(probe_path),
            }
            if probe_path is not None
            else None
        ),
    }
    attestation_path = build_dir / N2_ATTESTATION_NAME
    temporary_attestation = attestation_path.with_suffix(".json.tmp")
    try:
        temporary_attestation.write_text(
            json.dumps(attestation, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary_attestation.replace(attestation_path)
        persisted_attestation = json.loads(
            attestation_path.read_text(encoding="utf-8")
        )
    except OSError as error:
        raise ProbeError(f"could not write Metal N2 attestation: {error}") from error
    except json.JSONDecodeError as error:
        raise ProbeError(f"could not verify Metal N2 attestation: {error}") from error
    if persisted_attestation != attestation:
        raise ProbeError("persisted Metal N2 attestation differs from verified data")


def validate_linux_static_closure_manifest(
    manifest: dict[str, Any], linux_lock: dict[str, Any]
) -> None:
    expected_source_records = []
    for component, record in (
        ("shaderc", linux_lock["shaderc_release"]),
        ("glslang", linux_lock["dependencies"]["glslang"]),
        ("spirv-tools", linux_lock["dependencies"]["spirv_tools"]),
        ("spirv-headers", linux_lock["dependencies"]["spirv_headers"]),
    ):
        expected_source_records.append(
            {
                "component": component,
                "repository": record["repository"],
                "version": record[
                    "tag" if component == "shaderc" else "version"
                ],
                "commit": record["commit"],
                "archive_sha256": record["archive_sha256"],
                "license_expression": record["license_expression"],
                "license_sha256": record["license_sha256"],
                "package_notice_path": record["package_notice_path"],
                "package_notice_sha256": record["package_notice_sha256"],
            }
        )
    artifacts = manifest.get("artifacts")
    expected_artifacts = (
        ("shaderc_combined", "libshaderc_combined.a"),
        ("shaderc", "libshaderc.a"),
        ("shaderc_util", "libshaderc_util.a"),
        ("glslang", "libglslang.a"),
        ("SPIRV", "libSPIRV.a"),
        ("SPIRV-Tools-opt", "libSPIRV-Tools-opt.a"),
        ("SPIRV-Tools-static", "libSPIRV-Tools.a"),
    )
    artifact_inventory_valid = isinstance(artifacts, list) and len(artifacts) == 7
    if artifact_inventory_valid:
        artifact_inventory_valid = all(
            artifact.get("target") == target
            and artifact.get("file") == filename
            and isinstance(artifact.get("sha256"), str)
            and re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]) is not None
            for artifact, (target, filename) in zip(artifacts, expected_artifacts)
        )
    source_lock = manifest.get("source_lock", {})
    compiler = manifest.get("compiler", {})
    host = manifest.get("host", {})
    checks = {
        "schema": manifest.get("schema")
        == "ror.ogre_next_linux_static_closure.v1",
        "status": manifest.get("status") == "pass",
        "provider": manifest.get("provider") == "pinned-source",
        "platform": manifest.get("platform_policy")
        == "linux-x86_64-vulkan",
        "source_lock": source_lock
        == {
            "schema": "ror.ogre_next_linux_shader_toolchain.v1",
            "sha256": LINUX_SHADER_TOOLCHAIN_LOCK_SHA256,
            "package_path": (
                "provenance/ogre-next-linux-shader-toolchain.lock.json"
            ),
        },
        "compiler": isinstance(compiler.get("id"), str)
        and bool(compiler["id"])
        and isinstance(compiler.get("version"), str)
        and bool(compiler["version"])
        and compiler.get("build_type") == REQUIRED_CONFIG,
        "host": host.get("system") == "Linux"
        and host.get("processor") in {"AMD64", "amd64", "x86_64"},
        "sources": manifest.get("sources") == expected_source_records,
        "artifacts": artifact_inventory_valid,
        "dynamic_boundary": manifest.get("host_dynamic_boundary")
        == "Vulkan-Loader",
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "Linux shader static closure manifest failed closed: "
            + ", ".join(sorted(failed))
        )


def _n3_image_metrics(
    payload: bytes, width: int, height: int
) -> dict[str, int | float | str]:
    expected_bytes = width * height * 8
    if width <= 0 or height <= 0 or len(payload) != expected_bytes:
        raise ProbeError("Metal N3 RGBA16F artifact extent/byte count differs")
    nontrivial = 0
    luminance_sum = 0.0
    for offset in range(0, len(payload), 8):
        channels = struct.unpack_from("<4e", payload, offset)
        if not all(math.isfinite(channel) for channel in channels):
            raise ProbeError("Metal N3 RGBA16F artifact contains non-finite data")
        luminance_sum += (
            0.2126 * channels[0]
            + 0.7152 * channels[1]
            + 0.0722 * channels[2]
        )
        if any(abs(channel) > 1.0e-6 for channel in channels[:3]):
            nontrivial += 1
    return {
        "width": width,
        "height": height,
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "mean_luminance": luminance_sum / (width * height),
        "nontrivial_pixels": nontrivial,
    }


def _validate_n3_reported_metrics(
    reported: dict[str, Any], computed: dict[str, int | float | str], label: str
) -> None:
    checks = {
        "width": reported.get("width") == computed["width"],
        "height": reported.get("height") == computed["height"],
        "format": reported.get("format") == "RGBA16_FLOAT",
        "bytes": reported.get("bytes") == computed["bytes"],
        "sha256": reported.get("sha256") == computed["sha256"],
        "nontrivial_pixels": reported.get("nontrivial_pixels")
        == computed["nontrivial_pixels"],
        "mean_luminance": isinstance(reported.get("mean_luminance"), (int, float))
        and math.isclose(
            float(reported["mean_luminance"]),
            float(computed["mean_luminance"]),
            rel_tol=1.0e-9,
            abs_tol=1.0e-12,
        ),
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            f"Metal N3 {label} metrics failed closed: "
            + ", ".join(sorted(failed))
        )


def validate_n1_package_provenance(
    build_dir: Path,
    lock: dict[str, Any],
    linux_lock: dict[str, Any],
    policy: dict[str, str],
) -> None:
    package_root = build_dir / N1_PACKAGE_NAME
    expected_common = {
        "licenses/Rigs-of-Rods-GPL-3.0.txt": sha256_file(
            REPOSITORY_ROOT / "COPYING"
        ),
        "licenses/Ogre-Next-MIT.txt": lock["license"]["sha256"],
        "licenses/RapidJSON-license.txt": lock["dependencies"]["rapidjson"][
            "license_sha256"
        ],
        lock["shader_media"]["third_party_notice"]["notice_path"]: lock[
            "shader_media"
        ]["third_party_notice"]["notice_sha256"],
    }
    expected = dict(expected_common)
    if policy["name"] == "linux-x86_64-vulkan":
        for record in (
            linux_lock["shaderc_release"],
            linux_lock["dependencies"]["glslang"],
            linux_lock["dependencies"]["spirv_tools"],
            linux_lock["dependencies"]["spirv_headers"],
        ):
            expected[record["package_notice_path"]] = record[
                "package_notice_sha256"
            ]
    for relative_path, expected_hash in expected.items():
        staged_path = package_root / relative_path
        if not staged_path.is_file():
            raise ProbeError(f"N1 package notice is missing: {relative_path}")
        if sha256_file(staged_path) != expected_hash:
            raise ProbeError(f"N1 package notice SHA-256 mismatch: {relative_path}")

    if policy["name"] != "linux-x86_64-vulkan":
        return
    packaged_lock = (
        package_root
        / "provenance"
        / "ogre-next-linux-shader-toolchain.lock.json"
    )
    if (
        not packaged_lock.is_file()
        or sha256_file(packaged_lock) != LINUX_SHADER_TOOLCHAIN_LOCK_SHA256
        or packaged_lock.read_bytes()
        != LINUX_SHADER_TOOLCHAIN_LOCK_PATH.read_bytes()
    ):
        raise ProbeError("N1 package Linux shader source lock is missing or changed")

    source_manifest = build_dir / LINUX_STATIC_CLOSURE_MANIFEST_NAME
    packaged_manifest = (
        package_root / "provenance" / LINUX_STATIC_CLOSURE_MANIFEST_NAME
    )
    if not source_manifest.is_file() or not packaged_manifest.is_file():
        raise ProbeError("N1 package Linux static closure manifest is missing")
    if source_manifest.read_bytes() != packaged_manifest.read_bytes():
        raise ProbeError("N1 package Linux static closure manifest changed in staging")
    try:
        manifest = json.loads(packaged_manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(
            f"could not read Linux static closure manifest: {error}"
        ) from error
    validate_linux_static_closure_manifest(manifest, linux_lock)


def validate_linux_dynamic_boundary(
    build_dir: Path,
    *,
    require_frame_probe: bool,
    require_packaged_frontend: bool,
) -> None:
    package_bin = build_dir / N1_PACKAGE_NAME / "bin"
    executables: list[Path] = []
    if require_frame_probe:
        executables.append(build_dir / "bin" / "ror_ogre_next_frame_probe")
    if require_packaged_frontend:
        if not package_bin.is_dir():
            raise ProbeError("N1 package Linux executable directory is missing")
        packaged_executables = sorted(
            path for path in package_bin.iterdir() if path.is_file()
        )
        if len(packaged_executables) != 1:
            raise ProbeError("N1 package must contain exactly one Linux executable")
        executables.append(packaged_executables[0])
    if not executables:
        raise ProbeError("Linux linkage audit has no required executable")
    forbidden = re.compile(
        r"lib(?:shaderc|glslang|SPIRV(?:-Tools(?:-opt)?)?|"
        r"MachineIndependent|GenericCodeGen|OSDependent)",
        re.IGNORECASE,
    )
    for executable in executables:
        if not executable.is_file():
            raise ProbeError(f"Linux linkage input is missing: {executable}")
        try:
            result = subprocess.run(
                ["ldd", str(executable)],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
        except (OSError, subprocess.CalledProcessError) as error:
            raise ProbeError(f"could not audit Linux linkage: {executable}") from error
        match = forbidden.search(result.stdout)
        if match:
            raise ProbeError(
                f"Linux executable crossed static shader boundary: {match.group(0)}"
            )
def validate_n3_checkpoint(
    report: dict[str, Any],
    raster_path: Path | None,
    contribution_path: Path | None,
    hybrid_path: Path | None,
    executable_path: Path,
    lock: dict[str, Any],
    policy: dict[str, str],
    expected_source_commit: str,
    expected_source_ref: str,
    expected_source_manifest_sha256: str,
) -> None:
    if policy["name"] != "macos-arm64-metal":
        raise ProbeError("Metal N3 validation is Apple-only")
    if re.fullmatch(r"[0-9a-f]{40}", expected_source_commit) is None:
        raise ProbeError("expected N3 RoR source commit is not a full Git SHA")
    _require_sha256(
        expected_source_manifest_sha256,
        "expected Metal N3 relevant-source manifest",
    )
    try:
        executable_bytes = executable_path.stat().st_size
        executable_sha256 = sha256_file(executable_path)
    except OSError as error:
        raise ProbeError(f"could not attest Metal N3 executable: {error}") from error

    def object_field(name: str) -> dict[str, Any]:
        value = report.get(name, {})
        return value if isinstance(value, dict) else {}

    provenance = object_field("provenance")
    status = report.get("status")
    common_checks = {
        "schema": report.get("schema") == "ror.ogre_next_metal_rt_n3.v2",
        "status": status in ("pass", "skip"),
        "scope": report.get("scope")
        == (
            "same-device Metal primary-ray hit contribution composited into "
            "exact UI-free Ogre-Next HDR target; no GI, reflection, denoising, "
            "multi-bounce, or material parity claim"
        )
        if status == "pass"
        else isinstance(report.get("scope"), str),
        "ror_repository": provenance.get("ror_repository")
        == ROR_SOURCE_REPOSITORY,
        "ror_ref": provenance.get("ror_ref") == expected_source_ref,
        "ror_commit": provenance.get("ror_commit") == expected_source_commit,
        "relevant_source_clean": provenance.get("relevant_source_clean") is True,
        "relevant_source_manifest": provenance.get(
            "relevant_source_manifest_sha256"
        )
        == expected_source_manifest_sha256,
        "ogre_commit": provenance.get("ogre_next_commit") == lock["commit"],
        "build_artifact": provenance.get("build_artifact")
        == executable_path.name,
        "build_artifact_bytes": provenance.get("build_artifact_bytes")
        == executable_bytes
        and executable_bytes > 0,
        "build_artifact_sha256": provenance.get("build_artifact_sha256")
        == executable_sha256,
    }
    failed = [name for name, passed in common_checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next Metal N3 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )
    if status == "skip":
        if any(
            path is not None and path.exists()
            for path in (raster_path, contribution_path, hybrid_path)
        ):
            raise ProbeError("skipped Metal N3 checkpoint emitted image artifacts")
        if not isinstance(report.get("reason"), str) or not report["reason"]:
            raise ProbeError("skipped Metal N3 checkpoint has no reason")
        return

    if raster_path is None or contribution_path is None or hybrid_path is None:
        raise ProbeError("passed Metal N3 checkpoint is missing an image artifact")
    try:
        raster = raster_path.read_bytes()
        contribution = contribution_path.read_bytes()
        hybrid = hybrid_path.read_bytes()
    except OSError as error:
        raise ProbeError(f"could not read Metal N3 image artifacts: {error}") from error
    raster_metrics = _n3_image_metrics(raster, 96, 64)
    contribution_metrics = _n3_image_metrics(contribution, 96, 64)
    hybrid_metrics = _n3_image_metrics(hybrid, 96, 64)
    _validate_n3_reported_metrics(
        object_field("raster_only_hdr"), raster_metrics, "raster"
    )
    _validate_n3_reported_metrics(
        object_field("rt_contribution"), contribution_metrics, "contribution"
    )
    _validate_n3_reported_metrics(
        object_field("hybrid_hdr"), hybrid_metrics, "hybrid"
    )

    applied = 0
    untouched = 0
    for offset in range(0, len(raster), 8):
        raster_values = struct.unpack_from("<4e", raster, offset)
        contribution_values = struct.unpack_from("<4e", contribution, offset)
        hybrid_values = struct.unpack_from("<4e", hybrid, offset)
        contribution_channels = struct.unpack_from("<4H", contribution, offset)
        applies = any((channel & 0x7FFF) != 0 for channel in contribution_channels[:3])
        if contribution_channels[3] != 0:
            raise ProbeError("Metal N3 contribution changed straight alpha")
        if hybrid[offset + 6 : offset + 8] != raster[offset + 6 : offset + 8]:
            raise ProbeError("Metal N3 hybrid changed raster alpha")
        if applies:
            applied += 1
            if hybrid[offset : offset + 6] == raster[offset : offset + 6]:
                raise ProbeError("Metal N3 contribution did not change hybrid RGB")
            for channel in range(3):
                expected = max(
                    -65504.0,
                    min(65504.0, raster_values[channel] + contribution_values[channel]),
                )
                if not math.isclose(
                    hybrid_values[channel],
                    expected,
                    rel_tol=2.0e-3,
                    abs_tol=5.0e-4,
                ):
                    raise ProbeError(
                        "Metal N3 hybrid RGB is not the reported GPU contribution"
                    )
        else:
            untouched += 1
            if hybrid[offset : offset + 8] != raster[offset : offset + 8]:
                raise ProbeError("Metal N3 changed a pixel outside its contribution")

    device = object_field("device")
    contract = object_field("contract")
    proof = object_field("proof")
    second = object_field("second_view_contribution")
    resized = object_field("resized_hybrid")
    second_sha256 = second.get("sha256")
    resized_sha256 = resized.get("sha256")
    if not isinstance(second_sha256, str) or not isinstance(resized_sha256, str):
        raise ProbeError("Metal N3 follow-up image hashes are missing")
    _require_sha256(second_sha256, "Metal N3 second-view contribution")
    _require_sha256(resized_sha256, "Metal N3 resized hybrid")
    pass_checks = {
        "device": isinstance(device.get("name"), str)
        and bool(device["name"])
        and device.get("same_ogre_device") is True
        and device.get("same_ogre_queue") is True
        and device.get("apple_family_9") is True,
        "image_contract": type(contract.get("image_version")) is int
        and contract.get("image_version") == 2
        and type(contract.get("image_generation")) is int
        and contract["image_generation"] > 0
        and contract.get("usage")
        == "COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE"
        and contract.get("release_state") == "GENERAL_READ_WRITE"
        and contract.get("return_state") == "GENERAL_READ_WRITE",
        "distinct_artifacts": len(
            {
                raster_metrics["sha256"],
                contribution_metrics["sha256"],
                hybrid_metrics["sha256"],
            }
        )
        == 3
        and int(raster_metrics["nontrivial_pixels"]) > 0
        and int(contribution_metrics["nontrivial_pixels"]) > 0
        and int(hybrid_metrics["nontrivial_pixels"]) > 0,
        "mapping": applied > 0
        and untouched > 0
        and type(proof.get("contribution_pixels")) is int
        and proof.get("contribution_pixels") == applied,
        "far_plane_edge": type(
            proof.get("off_axis_far_plane_contribution_pixels")
        )
        is int
        and proof["off_axis_far_plane_contribution_pixels"] > 0,
        "second_view": second.get("width") == 96
        and second.get("height") == 64
        and second.get("format") == "RGBA16_FLOAT"
        and second.get("bytes") == 96 * 64 * 8
        and second_sha256 != contribution_metrics["sha256"]
        and type(second.get("nontrivial_pixels")) is int
        and second["nontrivial_pixels"] > 0,
        "resize": resized.get("width") == 80
        and resized.get("height") == 48
        and resized.get("format") == "RGBA16_FLOAT"
        and resized.get("bytes") == 80 * 48 * 8
        and type(resized.get("nontrivial_pixels")) is int
        and resized["nontrivial_pixels"] > 0,
        "proof": all(
            proof.get(field) is True
            for field in (
                "exact_exported_vertex_slice_used",
                "exact_exported_index_slice_used",
                "exact_exported_color_image_used",
                "gpu_composite_not_cpu_postprocess",
                "hybrid_changes_only_on_contribution",
                "all_channels_finite",
                "second_camera_changes_contribution_hash",
                "camera_mismatch_rejected",
                "snapshot_transform_mismatch_rejected",
                "off_axis_far_plane_hit_passed",
                "released_frame_allows_extent_change",
                "submitted_device_loss_and_timeout_paths_tested",
                "view_dependent_output_ready",
                "hybrid_composite_ready",
            )
        ),
    }
    failed = [name for name, passed in pass_checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next Metal N3 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )


def run_n3_checkpoint(
    build_dir: Path,
    config: str,
    jobs: int,
    lock: dict[str, Any],
    policy: dict[str, str],
) -> None:
    if policy["name"] != "macos-arm64-metal":
        return
    source_commit, source_ref, source_manifest_sha256 = repository_identity()
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_metal_n3_report",
            "--config",
            config,
            "--parallel",
            str(jobs),
        ]
    )
    report_path = build_dir / N3_REPORT_NAME
    candidates = {
        "raster": build_dir / N3_RASTER_NAME,
        "contribution": build_dir / N3_CONTRIBUTION_NAME,
        "hybrid": build_dir / N3_HYBRID_NAME,
    }
    artifacts = {
        name: path if path.is_file() else None for name, path in candidates.items()
    }
    executable_candidates = (
        build_dir / "bin" / "ror_ogre_next_metal_n3_smoke",
        build_dir / "bin" / config / "ror_ogre_next_metal_n3_smoke",
    )
    executable_path = next(
        (candidate for candidate in executable_candidates if candidate.is_file()),
        executable_candidates[0],
    )
    missing = [path.name for path in (report_path, executable_path) if not path.is_file()]
    if missing:
        raise ProbeError(
            "OGRE-Next Metal N3 checkpoint did not produce required artifacts: "
            + ", ".join(missing)
        )
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read Metal N3 report: {error}") from error
    if repository_identity() != (
        source_commit,
        source_ref,
        source_manifest_sha256,
    ):
        raise ProbeError("RoR source provenance changed while building Metal N3")
    validate_n3_checkpoint(
        report,
        artifacts["raster"],
        artifacts["contribution"],
        artifacts["hybrid"],
        executable_path,
        lock,
        policy,
        source_commit,
        source_ref,
        source_manifest_sha256,
    )

    def attest(path: Path | None) -> dict[str, Any] | None:
        if path is None:
            return None
        return {
            "path": path.name,
            "bytes": path.stat().st_size,
            "sha256": sha256_file(path),
        }

    attestation = {
        "schema": "ror.ogre_next_metal_rt_n3.attestation.v1",
        "status": report.get("status"),
        "source": {
            "ror_commit": source_commit,
            "ror_ref": source_ref,
            "relevant_source_clean": True,
            "relevant_source_manifest_sha256": source_manifest_sha256,
        },
        "executable": attest(executable_path),
        "report": attest(report_path),
        "raster_only_hdr": attest(artifacts["raster"]),
        "rt_contribution": attest(artifacts["contribution"]),
        "hybrid_hdr": attest(artifacts["hybrid"]),
    }
    attestation_path = build_dir / N3_ATTESTATION_NAME
    temporary = attestation_path.with_suffix(".json.tmp")
    try:
        temporary.write_text(
            json.dumps(attestation, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary.replace(attestation_path)
        persisted = json.loads(attestation_path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ProbeError(f"could not write Metal N3 attestation: {error}") from error
    except json.JSONDecodeError as error:
        raise ProbeError(f"could not verify Metal N3 attestation: {error}") from error
    if persisted != attestation:
        raise ProbeError("persisted Metal N3 attestation differs from verified data")


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
        choices=("all", "n1", "n2", "n3", "legacy"),
        default="all",
        help=(
            "run all gates, an independent N1/N2/N3 gate, or the legacy probes"
        ),
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
        linux_shader_lock = load_linux_shader_toolchain_lock()
        policy = detect_policy(platform.system(), platform.machine())
        if args.validate_contract_only:
            print(
                json.dumps(
                    {
                        "schema_version": 2,
                        "status": "pass",
                        "commit": lock["commit"],
                        "linux_shader_source_lock_sha256": (
                            LINUX_SHADER_TOOLCHAIN_LOCK_SHA256
                        ),
                        "linux_shader_provider": linux_shader_lock["provider"],
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
                "--reuse-build-dir requires --checkpoint n1, n2, n3, or legacy"
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
            if policy["name"] == "linux-x86_64-vulkan":
                run(
                    [
                        "cmake",
                        "--build",
                        str(build_dir),
                        "--target",
                        "ror_ogre_next_linux_static_closure_verify",
                        "--config",
                        args.config,
                        "--parallel",
                        str(args.jobs),
                    ]
                )
                require_source_identity_unchanged(source_identity)
            validate_n1_package_provenance(
                build_dir, lock, linux_shader_lock, policy
            )
            if policy["name"] == "linux-x86_64-vulkan":
                validate_linux_dynamic_boundary(
                    build_dir,
                    require_frame_probe=False,
                    require_packaged_frontend=True,
                )

        if args.checkpoint in ("all", "n2"):
            run_n2_checkpoint(
                build_dir,
                args.config,
                args.jobs,
                lock,
                policy,
            )
            require_source_identity_unchanged(source_identity)

        if args.checkpoint in ("all", "n3"):
            run_n3_checkpoint(
                build_dir,
                args.config,
                args.jobs,
                lock,
                policy,
            )
            require_source_identity_unchanged(source_identity)

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
            if policy["name"] == "linux-x86_64-vulkan":
                run(
                    [
                        "cmake",
                        "--build",
                        str(build_dir),
                        "--target",
                        "ror_ogre_next_linux_static_closure_verify",
                        "--config",
                        args.config,
                        "--parallel",
                        str(args.jobs),
                    ]
                )
                require_source_identity_unchanged(source_identity)
                closure_path = build_dir / LINUX_STATIC_CLOSURE_MANIFEST_NAME
                try:
                    closure = json.loads(closure_path.read_text(encoding="utf-8"))
                except (OSError, json.JSONDecodeError) as error:
                    raise ProbeError(
                        f"could not read Linux static closure manifest: {error}"
                    ) from error
                validate_linux_static_closure_manifest(
                    closure, linux_shader_lock
                )
                validate_linux_dynamic_boundary(
                    build_dir,
                    require_frame_probe=True,
                    require_packaged_frontend=(
                        args.checkpoint == "all" or args.reuse_build_dir
                    ),
                )
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except ProbeError as error:
        print(f"OGRE-Next probe failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
