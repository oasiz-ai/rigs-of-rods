#!/usr/bin/env python3
"""Verify each pinned cross-platform OGRE 14 application graph."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import subprocess
import sys
from typing import Any


OGRE_VERSION = "14.5.2"
MYGUI_VERSION = "3.4.0"
EXPECTED_OGRE_OPTIONS = {
    "codec_rsimage": "False",
    "nodeless_positioning": "True",
    "profiling": "False",
    "profiling_remotery": "False",
    "resourcemanager_strict": "off",
    "with_vulkan": "False",
}
FORBIDDEN_REFERENCES = ("cg-toolkit", "imgui")


@dataclass(frozen=True)
class PlatformGraph:
    key: str
    conan_os: str
    conan_arch: str
    profile: str
    lockfile: str
    ogre_lockfile: str
    os_version: str | None = None
    required_references: tuple[str, ...] = ()
    forbidden_references: tuple[str, ...] = ()


PLATFORMS = {
    platform.key: platform
    for platform in (
        PlatformGraph(
            key="macos-arm64",
            conan_os="Macos",
            conan_arch="armv8",
            os_version="11.0",
            profile="cmake/conan/profiles/macos-arm64-release",
            lockfile=(
                "cmake/conan/locks/"
                "ror-ogre14-macos-arm64-release.lock"
            ),
            ogre_lockfile=(
                "cmake/conan/locks/"
                "ogre3d-14.5.2-macos-arm64-release.lock"
            ),
            forbidden_references=(
                "egl/system",
                "opengl/system",
                "wayland/",
                "xorg/system",
            ),
        ),
        PlatformGraph(
            key="linux-x86_64",
            conan_os="Linux",
            conan_arch="x86_64",
            profile="cmake/conan/profiles/linux-x86_64-release",
            lockfile=(
                "cmake/conan/locks/"
                "ror-ogre14-linux-x86_64-release.lock"
            ),
            ogre_lockfile=(
                "cmake/conan/locks/"
                "ogre3d-14.5.2-linux-x86_64-release.lock"
            ),
            required_references=(
                "opengl/system",
                "xorg/system",
            ),
        ),
        PlatformGraph(
            key="windows-x86_64",
            conan_os="Windows",
            conan_arch="x86_64",
            profile="cmake/conan/profiles/windows-x86_64-release",
            lockfile=(
                "cmake/conan/locks/"
                "ror-ogre14-windows-x86_64-release.lock"
            ),
            ogre_lockfile=(
                "cmake/conan/locks/"
                "ogre3d-14.5.2-windows-x86_64-release.lock"
            ),
            forbidden_references=(
                "egl/system",
                "opengl/system",
                "wayland/",
                "xorg/system",
            ),
        ),
    )
}


def run_json(command: list[str], *, cwd: Path) -> dict[str, Any]:
    result = subprocess.run(
        command,
        check=False,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(
            f"command did not emit JSON: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        ) from error
    if not isinstance(payload, dict):
        raise RuntimeError("command JSON root is not an object")
    return payload


def reference_without_timestamp(reference: str) -> str:
    return reference.partition("%")[0]


def graph_nodes(graph: dict[str, Any]) -> list[dict[str, Any]]:
    nodes = graph.get("graph", {}).get("nodes", {})
    if not isinstance(nodes, dict):
        raise AssertionError("Conan graph JSON has no graph.nodes mapping")
    return [node for node in nodes.values() if isinstance(node, dict)]


def graph_references(graph: dict[str, Any]) -> list[str]:
    return sorted(
        reference
        for node in graph_nodes(graph)
        if isinstance((reference := node.get("ref")), str)
    )


def assert_lock_contract(
    lockfile: dict[str, Any],
    *,
    ogre_reference: str,
    mygui_reference: str,
) -> None:
    if lockfile.get("version") != "0.5":
        raise AssertionError(
            f"unsupported Conan lockfile version: {lockfile.get('version')!r}"
        )
    for key in ("requires", "build_requires"):
        references = lockfile.get(key)
        if not isinstance(references, list) or not all(
            isinstance(reference, str) for reference in references
        ):
            raise AssertionError(f"lockfile {key} is not a string list")
        if len(references) != len(set(references)):
            raise AssertionError(f"lockfile {key} contains duplicate entries")
    locked_host = {
        reference_without_timestamp(reference)
        for reference in lockfile["requires"]
    }
    for required in (ogre_reference, mygui_reference):
        if required not in locked_host:
            raise AssertionError(
                f"application lockfile omits local recipe {required}"
            )
    for empty_key in ("python_requires", "config_requires"):
        if lockfile.get(empty_key) != []:
            raise AssertionError(
                f"lockfile unexpectedly contains {empty_key}"
            )


def assert_isolated_ogre_lock_contract(
    lockfile: dict[str, Any],
    *,
    ogre_reference: str,
) -> None:
    if lockfile.get("version") != "0.5":
        raise AssertionError(
            "unsupported isolated OGRE lockfile version: "
            f"{lockfile.get('version')!r}"
        )
    locked_host = {
        reference_without_timestamp(reference)
        for reference in lockfile.get("requires", [])
        if isinstance(reference, str)
    }
    if ogre_reference not in locked_host:
        raise AssertionError(
            f"isolated OGRE lockfile omits {ogre_reference}"
        )
    if any(reference.startswith("mygui/") for reference in locked_host):
        raise AssertionError(
            "isolated OGRE lockfile unexpectedly contains MyGUI"
        )
    for empty_key in ("python_requires", "config_requires"):
        if lockfile.get(empty_key) != []:
            raise AssertionError(
                "isolated OGRE lockfile unexpectedly contains "
                f"{empty_key}"
            )


def assert_host_target(
    graph: dict[str, Any],
    platform: PlatformGraph,
) -> None:
    mismatches: list[str] = []
    for node in graph_nodes(graph):
        if node.get("context") != "host":
            continue
        reference = node.get("ref")
        if not isinstance(reference, str):
            reference = "<root>"
        settings = node.get("settings")
        if (
            not isinstance(settings, dict)
            or settings.get("os") != platform.conan_os
            or (
                "arch" in settings
                and settings.get("arch") != platform.conan_arch
            )
        ):
            mismatches.append(reference)
            continue
        if (
            platform.os_version is not None
            and settings.get("os.version") != platform.os_version
        ):
            mismatches.append(reference)
    if mismatches:
        raise AssertionError(
            f"{platform.key} host target drift: {sorted(mismatches)}"
        )


def assert_local_node(
    graph: dict[str, Any],
    *,
    exact_reference: str,
    platform: PlatformGraph | None = None,
    expected_options: dict[str, str] | None = None,
) -> None:
    matching = [
        node
        for node in graph_nodes(graph)
        if node.get("ref") == exact_reference
    ]
    if len(matching) != 1:
        raise AssertionError(
            f"expected exactly one graph node for {exact_reference}"
        )
    node = matching[0]
    if node.get("context") != "host" or node.get("recipe") != "Cache":
        raise AssertionError(
            f"local recipe is not a cached host node: {node}"
        )
    if platform is not None:
        settings = node.get("settings")
        if (
            not isinstance(settings, dict)
            or settings.get("os") != platform.conan_os
            or settings.get("arch") != platform.conan_arch
        ):
            raise AssertionError(
                f"local recipe target changed: {settings!r}"
            )
    if node.get("binary") not in {"Build", "Cache", "Download"}:
        raise AssertionError(
            f"local package is not resolvable: {node.get('binary')!r}"
        )
    if expected_options is not None and node.get("options") != expected_options:
        raise AssertionError(
            f"{exact_reference} options changed: {node.get('options')!r}"
        )


def assert_platform_reference_boundary(
    graph: dict[str, Any],
    platform: PlatformGraph,
) -> None:
    references = [reference.lower() for reference in graph_references(graph)]
    for required in platform.required_references:
        if not any(required.lower() in reference for reference in references):
            raise AssertionError(
                f"{platform.key} graph omits {required}"
            )
    for forbidden in (
        *FORBIDDEN_REFERENCES,
        *platform.forbidden_references,
    ):
        if any(forbidden.lower() in reference for reference in references):
            raise AssertionError(
                f"{platform.key} graph contains forbidden {forbidden}"
            )


def export_recipe(
    repository_root: Path,
    conan_command: str,
    recipe: str,
    version: str,
) -> str:
    result = run_json(
        [
            conan_command,
            "export",
            str(repository_root / "cmake/conan/recipes" / recipe),
            f"--version={version}",
            "--format=json",
        ],
        cwd=repository_root,
    )
    reference = result.get("reference")
    if not isinstance(reference, str):
        raise AssertionError(f"{recipe} export has no exact reference")
    return reference


def verify_platform(
    *,
    repository_root: Path,
    conan_command: str,
    platform: PlatformGraph,
    ogre_reference: str,
    mygui_reference: str,
) -> dict[str, Any]:
    profile = repository_root / platform.profile
    lockfile_path = repository_root / platform.lockfile
    ogre_lockfile_path = repository_root / platform.ogre_lockfile
    if not profile.is_file():
        raise AssertionError(f"missing Conan profile: {profile}")
    try:
        lockfile = json.loads(lockfile_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AssertionError(
            f"cannot read pinned lockfile {lockfile_path}: {error}"
        ) from error
    if not isinstance(lockfile, dict):
        raise AssertionError(f"{lockfile_path} is not a JSON object")
    assert_lock_contract(
        lockfile,
        ogre_reference=ogre_reference,
        mygui_reference=mygui_reference,
    )
    try:
        ogre_lockfile = json.loads(
            ogre_lockfile_path.read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise AssertionError(
            f"cannot read pinned lockfile {ogre_lockfile_path}: {error}"
        ) from error
    if not isinstance(ogre_lockfile, dict):
        raise AssertionError(f"{ogre_lockfile_path} is not a JSON object")
    assert_isolated_ogre_lock_contract(
        ogre_lockfile,
        ogre_reference=ogre_reference,
    )

    graph = run_json(
        [
            conan_command,
            "graph",
            "info",
            str(repository_root),
            f"--profile:host={profile}",
            f"--profile:build={profile}",
            f"--lockfile={lockfile_path}",
            "-o=&:ogre14=True",
            "--build=missing",
            (
                "-c:h=tools.cmake:configure_args="
                '["-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]'
            ),
            "--format=json",
        ],
        cwd=repository_root,
    )
    assert_host_target(graph, platform)
    assert_local_node(
        graph,
        exact_reference=ogre_reference,
        platform=platform,
        expected_options=EXPECTED_OGRE_OPTIONS,
    )
    assert_local_node(
        graph,
        exact_reference=mygui_reference,
        platform=platform,
    )
    assert_platform_reference_boundary(graph, platform)
    return {
        "host_nodes": sum(
            node.get("context") == "host" for node in graph_nodes(graph)
        ),
        "lockfile": platform.lockfile,
        "ogre_lockfile": platform.ogre_lockfile,
        "references": len(graph_references(graph)),
        "target": f"{platform.conan_os}/{platform.conan_arch}",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repository-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    parser.add_argument("--conan", default="conan")
    parser.add_argument(
        "--platform",
        action="append",
        choices=sorted(PLATFORMS),
        help="platform graph to verify; defaults to all",
    )
    arguments = parser.parse_args()
    repository_root = arguments.repository_root.resolve()
    selected = arguments.platform or sorted(PLATFORMS)
    try:
        ogre_reference = export_recipe(
            repository_root,
            arguments.conan,
            "ogre3d",
            OGRE_VERSION,
        )
        mygui_reference = export_recipe(
            repository_root,
            arguments.conan,
            "mygui",
            MYGUI_VERSION,
        )
        platform_reports = {
            key: verify_platform(
                repository_root=repository_root,
                conan_command=arguments.conan,
                platform=PLATFORMS[key],
                ogre_reference=ogre_reference,
                mygui_reference=mygui_reference,
            )
            for key in selected
        }
    except (AssertionError, OSError, RuntimeError) as error:
        print(f"OGRE 14 application graph assertion failed: {error}", file=sys.stderr)
        return 1
    print(
        json.dumps(
            {
                "mygui_reference": mygui_reference,
                "ogre_reference": ogre_reference,
                "platforms": platform_reports,
                "status": "ok",
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
