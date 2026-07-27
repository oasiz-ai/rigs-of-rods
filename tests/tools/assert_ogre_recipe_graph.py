#!/usr/bin/env python3
"""Assert the isolated OGRE 14.5.2 recipe's macOS arm64 dependency graph."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import Any


EXPECTED_VERSION = "14.5.2"
EXPECTED_SOURCE_SHA256 = (
    "1949fe62f3e4b8043e82e4dc94f9b0ab412a5bffc9e10d3b1dddc80fe54fe1e3"
)
DEFAULT_NEXUS_URL = (
    "https://nexus.anotherfoxguy.com/repository/rigs-of-rods/"
)
CONANCENTER_URL = "https://center2.conan.io"
EXPECTED_PATCH_SHA256 = {
    "patches/14.0.0/FindPkgMacros.cmake.patch": (
        "723a66c44a1fc311c2882c43094961e71c193c441b30e88f77e6afe6010f7a85"
    ),
    "patches/14.0.0/dl-remotery.patch": (
        "55e6c07f349c16c4acbf7b8a127175450d3682ffe27cab1aa6b9eec19e463fa0"
    ),
    "patches/14.0.0/pugixml-fix.patch": (
        "2999fad7982d3ea34d29252d01201aeac91dc4212b1198ba52c1f7bf874cc202"
    ),
}


def run(
    command: list[str],
    *,
    env: dict[str, str],
    capture_json: bool = False,
) -> Any:
    result = subprocess.run(
        command,
        check=False,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if not capture_json:
        return result.stdout
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(
            f"command did not emit JSON: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        ) from error


def source_sha256(conandata: Path) -> str:
    text = conandata.read_text(encoding="utf-8")
    _, separator, sources_text = text.partition("\nsources:\n")
    if not separator:
        raise AssertionError(f"{conandata} has no sources mapping")
    version_block = re.search(
        rf"(?ms)^  {re.escape(EXPECTED_VERSION)}:\s*\n"
        r"(?P<body>(?:^    .*(?:\n|$))+)",
        sources_text,
    )
    if version_block is None:
        raise AssertionError(
            f"{conandata} has no sources entry for {EXPECTED_VERSION}"
        )
    hash_match = re.search(
        r"(?m)^    sha256:\s*([0-9a-fA-F]{64})\s*$",
        version_block.group("body"),
    )
    if hash_match is None:
        raise AssertionError(
            f"{conandata} has no SHA-256 for {EXPECTED_VERSION}"
        )
    return hash_match.group(1).lower()


def assert_exact_patch_set(recipe_dir: Path) -> None:
    patches_dir = recipe_dir / "patches"
    actual_paths = {
        path.relative_to(recipe_dir).as_posix()
        for path in patches_dir.rglob("*.patch")
    }
    if actual_paths != set(EXPECTED_PATCH_SHA256):
        raise AssertionError(
            "OGRE patch set changed: "
            f"expected {sorted(EXPECTED_PATCH_SHA256)}, "
            f"found {sorted(actual_paths)}"
        )
    for relative_path, expected_hash in EXPECTED_PATCH_SHA256.items():
        patch_hash = hashlib.sha256(
            (recipe_dir / relative_path).read_bytes()
        ).hexdigest()
        if patch_hash != expected_hash:
            raise AssertionError(
                f"Nexus-derived patch changed: {relative_path}"
            )


def graph_references(graph: dict[str, Any]) -> list[str]:
    nodes = graph.get("graph", {}).get("nodes", {})
    if not isinstance(nodes, dict):
        raise AssertionError("Conan graph JSON has no graph.nodes mapping")
    references: list[str] = []
    for node in nodes.values():
        if not isinstance(node, dict):
            continue
        reference = node.get("ref")
        if isinstance(reference, str):
            references.append(reference)
    return sorted(references)


def assert_macos_arm64_ogre_node(graph: dict[str, Any]) -> None:
    nodes = graph.get("graph", {}).get("nodes", {})
    if not isinstance(nodes, dict):
        raise AssertionError("Conan graph JSON has no graph.nodes mapping")
    matching_nodes = [
        node
        for node in nodes.values()
        if isinstance(node, dict)
        and str(node.get("ref", "")).startswith(
            f"ogre3d/{EXPECTED_VERSION}"
        )
    ]
    if len(matching_nodes) != 1:
        raise AssertionError(
            f"expected exactly one ogre3d/{EXPECTED_VERSION} graph node"
        )
    node = matching_nodes[0]
    settings = node.get("settings", {})
    if (
        node.get("context") != "host"
        or settings.get("os") != "Macos"
        or settings.get("arch") != "armv8"
    ):
        raise AssertionError(
            f"OGRE graph node is not macOS arm64 host context: {node}"
        )


def write_macos_arm64_profile(path: Path) -> None:
    path.write_text(
        "\n".join(
            [
                "[settings]",
                "os=Macos",
                "arch=armv8",
                "compiler=apple-clang",
                "compiler.version=15",
                "compiler.libcxx=libc++",
                "compiler.cppstd=17",
                "build_type=Release",
                "",
            ]
        ),
        encoding="utf-8",
    )


def verify(
    *,
    repository_root: Path,
    conan_command: str,
    nexus_url: str,
    retained_home: Path | None,
) -> None:
    recipe_dir = repository_root / "cmake/conan/recipes/ogre3d"
    local_conandata = recipe_dir / "conandata.yml"
    assert_exact_patch_set(recipe_dir)
    if source_sha256(local_conandata) != EXPECTED_SOURCE_SHA256:
        raise AssertionError("working-tree OGRE source SHA-256 changed")

    temporary_home = None
    if retained_home is None:
        temporary_home = tempfile.TemporaryDirectory(
            prefix="ror-ogre-conan-home-"
        )
        conan_home = Path(temporary_home.name)
    else:
        conan_home = retained_home.resolve()
        if conan_home.exists() and any(conan_home.iterdir()):
            raise AssertionError(
                f"--conan-home must be absent or empty: {conan_home}"
            )
        conan_home.mkdir(parents=True, exist_ok=True)

    try:
        env = os.environ.copy()
        env["CONAN_HOME"] = str(conan_home)
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        run(
            [
                conan_command,
                "remote",
                "add",
                "conancenter",
                CONANCENTER_URL,
                "--force",
            ],
            env=env,
        )
        run(
            [
                conan_command,
                "remote",
                "add",
                "nexus",
                nexus_url,
                "--force",
            ],
            env=env,
        )

        profile = conan_home / "profiles" / "macos-arm64"
        profile.parent.mkdir(parents=True, exist_ok=True)
        write_macos_arm64_profile(profile)

        export_result = run(
            [
                conan_command,
                "export",
                str(recipe_dir),
                f"--version={EXPECTED_VERSION}",
                "--format=json",
            ],
            env=env,
            capture_json=True,
        )
        exported_reference = export_result.get("reference")
        if not isinstance(exported_reference, str):
            raise AssertionError("Conan export JSON has no recipe reference")

        export_path_text = run(
            [
                conan_command,
                "cache",
                "path",
                exported_reference,
            ],
            env=env,
        ).strip()
        exported_conandata = Path(export_path_text) / "conandata.yml"
        if source_sha256(exported_conandata) != EXPECTED_SOURCE_SHA256:
            raise AssertionError("exported OGRE source SHA-256 changed")

        graph = run(
            [
                conan_command,
                "graph",
                "info",
                f"--requires=ogre3d/{EXPECTED_VERSION}",
                f"--profile:host={profile}",
                f"--profile:build={profile}",
                "--format=json",
            ],
            env=env,
            capture_json=True,
        )
        references = graph_references(graph)
        assert_macos_arm64_ogre_node(graph)
        lowered_references = [reference.lower() for reference in references]
        for forbidden in ("cg-toolkit", "imgui"):
            if any(forbidden in reference for reference in lowered_references):
                raise AssertionError(
                    f"forbidden dependency {forbidden!r} is present: "
                    f"{references}"
                )
        print(
            json.dumps(
                {
                    "conan_home": str(conan_home),
                    "graph_profile": "Macos/armv8",
                    "ogre_source_sha256": EXPECTED_SOURCE_SHA256,
                    "package_references": references,
                    "status": "ok",
                },
                indent=2,
                sort_keys=True,
            )
        )
    finally:
        if temporary_home is not None:
            temporary_home.cleanup()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repository-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    parser.add_argument("--conan", default="conan")
    parser.add_argument("--nexus-url", default=DEFAULT_NEXUS_URL)
    parser.add_argument(
        "--conan-home",
        type=Path,
        help="retain the isolated cache at this absent or empty directory",
    )
    arguments = parser.parse_args()
    try:
        verify(
            repository_root=arguments.repository_root.resolve(),
            conan_command=arguments.conan,
            nexus_url=arguments.nexus_url,
            retained_home=arguments.conan_home,
        )
    except (AssertionError, OSError, RuntimeError) as error:
        print(f"OGRE recipe graph assertion failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
