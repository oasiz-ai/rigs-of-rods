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
EXPECTED_MACOS_DEPLOYMENT_TARGET = "11.0"
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
    "patches/14.5.2/relocatable-install-paths.patch": (
        "6292eb8bf8a9b373f68e7ff180b5750051eff2e21d39cacc84ae020410d06dc2"
    ),
    "patches/14.5.2/bounds-safe-shadow-texture-projectors.patch": (
        "13bbbd974dfe0106dc51a8846caff800394b371a57fc98bcdd0dbcf783823d51"
    ),
    "patches/14.5.2/defer-glsl-program-validation.patch": (
        "d60d2684b6fd29ba1d3bdc4aaa34bb21463488ab16af03592e3b19594f249e72"
    ),
    "patches/14.5.2/always-lock-zip-archive.patch": (
        "7674db9811bdf80abb0248b39504f259b85ecd9331f5bb1ca19c9b5d7a9db1b4"
    ),
    "patches/14.5.2/archive-manager-load-rollback.patch": (
        "cf7aaac084432441167a384245b65400c07f23ea80e2af386cebd41832cc967a"
    ),
    "patches/14.5.2/terrain-composite-revision-metal-readback.patch": (
        "cb8bf0aa200793a02574725396c407e7e58eded5b242c2e7dd617c745a6dbaf5"
    ),
    "patches/14.5.2/exact-material-script-preopen.patch": (
        "3344cd639959553bda2ec978ad66e4b42df00e2f56f75d39a2d780ce4aa38478"
    ),
}
EXPECTED_OGRE_OPTIONS = {
    "codec_rsimage": "False",
    "nodeless_positioning": "True",
    "profiling": "False",
    "profiling_remotery": "False",
    "resourcemanager_strict": "off",
    "with_vulkan": "False",
}
EXPECTED_DEPENDENCY_REVISIONS = {
    "brotli/1.1.0#3f631ef77008f7b5eb388780116371a3",
    "bzip2/1.0.8#c470882369c2d95c5c77e970c0c7e321",
    "cmake/4.4.0#a5dd5d73bd9fc56b0fc3163a04573dc8",
    (
        "freeimage/3.18.0@anotherfoxguy/stable"
        "#8b69961fa00ad36b37d77dd40502fcbf"
    ),
    "freetype/2.14.3#4ee27b7918b546a96d7e6898e3a02b34",
    "jasper/4.2.4#694a46302082de14f5af08d15d959dc6",
    "jxrlib/cci.20170615#576979230d06d6cc445342b2731b517f",
    "lcms/2.19.1#a6579472795f0cd5578ff9f6511f8b80",
    "libjpeg/9e#41e2469373bb9d9a678c8aaf2af326f0",
    "libpng/1.6.58#19cb72905ae54f54948401f753faa2c1",
    "libraw/0.20.2#ce19752ee79b6b1e23b3bcb567ba4831",
    "libtiff/4.6.0#61cbca1281f1ac78681dbd3b0eaf2026",
    "libwebp/1.3.2#48800423705f4739ed8e3eba9bb4e2db",
    "meson/1.10.2#9d2d10681fe7fe61c788c58626c89b25",
    "ninja/1.13.2#c8c5dc2a52ed6e4e42a66d75b4717ceb",
    "openexr/2.5.7#191290b5cb7da90a05ed351fae29d9c4",
    "openjpeg/2.5.2#6f7b733e151d1bbf5ed05cbabb846828",
    "pkgconf/2.5.1#93c2051284cba1279494a43a4fcfeae2",
    "pugixml/1.16#aa265531e325d44acf11ae7903ec9410",
    "sdl/2.32.10#19432981a8779c918a13682d4186fa3b",
    "xz_utils/5.8.3#a8432fead347c69d8b2737c35f936132",
    "zlib/1.3.2#1cb806da49011867778ffb6ac7190fcb",
}
EXPECTED_BUILD_DEPENDENCY_REVISIONS = {
    "cmake/4.4.0#a5dd5d73bd9fc56b0fc3163a04573dc8",
    "meson/1.10.2#9d2d10681fe7fe61c788c58626c89b25",
    "ninja/1.13.2#c8c5dc2a52ed6e4e42a66d75b4717ceb",
    "pkgconf/2.5.1#93c2051284cba1279494a43a4fcfeae2",
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


def registered_patch_paths(conandata: Path) -> set[str]:
    return set(
        re.findall(
            r"(?m)^\s*-\s+patch_file:\s*(\S+)\s*$",
            conandata.read_text(encoding="utf-8"),
        )
    )


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
    registered_paths = registered_patch_paths(recipe_dir / "conandata.yml")
    if registered_paths != set(EXPECTED_PATCH_SHA256):
        raise AssertionError(
            "OGRE registered patch set changed: "
            f"expected {sorted(EXPECTED_PATCH_SHA256)}, "
            f"found {sorted(registered_paths)}"
        )
    for relative_path, expected_hash in EXPECTED_PATCH_SHA256.items():
        patch_hash = hashlib.sha256(
            (recipe_dir / relative_path).read_bytes()
        ).hexdigest()
        if patch_hash != expected_hash:
            raise AssertionError(
                f"Pinned OGRE patch changed: {relative_path}"
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


def assert_exact_dependency_revisions(
    references: list[str],
    exported_reference: str,
) -> None:
    actual = {
        reference
        for reference in references
        if reference not in {"conanfile", exported_reference}
    }
    if actual != EXPECTED_DEPENDENCY_REVISIONS:
        raise AssertionError(
            "OGRE dependency graph changed: "
            f"expected {sorted(EXPECTED_DEPENDENCY_REVISIONS)}, "
            f"found {sorted(actual)}"
        )


def lock_reference_without_timestamp(reference: str) -> str:
    return reference.partition("%")[0]


def assert_exact_lockfile(
    lockfile: dict[str, Any],
    exported_reference: str,
) -> None:
    if lockfile.get("version") != "0.5":
        raise AssertionError(
            f"unsupported OGRE lockfile version: {lockfile.get('version')!r}"
        )
    expected_host = (
        EXPECTED_DEPENDENCY_REVISIONS
        - EXPECTED_BUILD_DEPENDENCY_REVISIONS
    ) | {exported_reference}
    actual_host = {
        lock_reference_without_timestamp(reference)
        for reference in lockfile.get("requires", [])
        if isinstance(reference, str)
    }
    actual_build = {
        lock_reference_without_timestamp(reference)
        for reference in lockfile.get("build_requires", [])
        if isinstance(reference, str)
    }
    if actual_host != expected_host:
        raise AssertionError(
            "OGRE host lock changed: "
            f"expected {sorted(expected_host)}, "
            f"found {sorted(actual_host)}"
        )
    if actual_build != EXPECTED_BUILD_DEPENDENCY_REVISIONS:
        raise AssertionError(
            "OGRE build lock changed: "
            f"expected {sorted(EXPECTED_BUILD_DEPENDENCY_REVISIONS)}, "
            f"found {sorted(actual_build)}"
        )
    for empty_key in (
        "python_requires",
        "config_requires",
    ):
        if lockfile.get(empty_key) != []:
            raise AssertionError(
                f"OGRE lockfile unexpectedly contains {empty_key}"
            )


def assert_macos_arm64_ogre_node(
    graph: dict[str, Any],
    exported_reference: str,
) -> None:
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
    if node.get("ref") != exported_reference:
        raise AssertionError(
            "graph did not resolve the exact locally exported recipe: "
            f"{node.get('ref')!r} != {exported_reference!r}"
        )
    if (
        node.get("context") != "host"
        or settings.get("os") != "Macos"
        or settings.get("os.version")
        != EXPECTED_MACOS_DEPLOYMENT_TARGET
        or settings.get("arch") != "armv8"
    ):
        raise AssertionError(
            f"OGRE graph node is not macOS arm64 host context: {node}"
        )
    if node.get("recipe") != "Cache":
        raise AssertionError(
            f"OGRE graph did not use the local recipe: {node}"
        )
    if node.get("binary") not in {"Build", "Cache", "Download"}:
        raise AssertionError(
            f"OGRE package is not resolvable: {node.get('binary')!r}"
        )
    if node.get("options") != EXPECTED_OGRE_OPTIONS:
        raise AssertionError(
            "OGRE graph options changed: "
            f"expected {EXPECTED_OGRE_OPTIONS}, "
            f"found {node.get('options')}"
        )


def assert_host_graph_target_consistency(graph: dict[str, Any]) -> None:
    nodes = graph.get("graph", {}).get("nodes", {})
    if not isinstance(nodes, dict):
        raise AssertionError("Conan graph JSON has no graph.nodes mapping")
    mismatches: list[str] = []
    for node in nodes.values():
        if not isinstance(node, dict) or node.get("context") != "host":
            continue
        reference = node.get("ref")
        settings = node.get("settings", {})
        if not isinstance(reference, str):
            continue
        if (
            not isinstance(settings, dict)
            or settings.get("os") != "Macos"
            or settings.get("os.version")
            != EXPECTED_MACOS_DEPLOYMENT_TARGET
            or settings.get("arch") != "armv8"
        ):
            mismatches.append(reference)
    if mismatches:
        raise AssertionError(
            "host dependencies do not share the macOS 11 arm64 target: "
            f"{sorted(mismatches)}"
        )


def write_macos_arm64_profile(
    path: Path,
    *,
    host_target: bool,
) -> None:
    settings = [
        "[settings]",
        "os=Macos",
    ]
    if host_target:
        settings.append(
            f"os.version={EXPECTED_MACOS_DEPLOYMENT_TARGET}"
        )
    settings.extend(
        [
            "arch=armv8",
            "compiler=apple-clang",
            "compiler.version=15",
            "compiler.libcxx=libc++",
            "compiler.cppstd=17",
            "build_type=Release",
            "",
        ]
    )
    path.write_text(
        "\n".join(settings),
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
    lockfile_path = (
        repository_root
        / "cmake"
        / "conan"
        / "locks"
        / "ogre3d-14.5.2-macos-arm64-release.lock"
    )
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
        # Only dependency recipes receive the CMake 4 compatibility floor.
        # RoR's own policy behavior is not weakened.
        env["CMAKE_POLICY_VERSION_MINIMUM"] = "3.5"
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

        host_profile = conan_home / "profiles" / "macos-arm64-host"
        build_profile = conan_home / "profiles" / "macos-arm64-build"
        host_profile.parent.mkdir(parents=True, exist_ok=True)
        write_macos_arm64_profile(
            host_profile,
            host_target=True,
        )
        write_macos_arm64_profile(
            build_profile,
            host_target=False,
        )

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
        try:
            lockfile = json.loads(lockfile_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise AssertionError(
                f"cannot read pinned OGRE lockfile: {error}"
            ) from error
        if not isinstance(lockfile, dict):
            raise AssertionError("pinned OGRE lockfile is not an object")
        assert_exact_lockfile(lockfile, exported_reference)

        graph = run(
            [
                conan_command,
                "graph",
                "info",
                f"--requires=ogre3d/{EXPECTED_VERSION}",
                f"--profile:host={host_profile}",
                f"--profile:build={build_profile}",
                f"--lockfile={lockfile_path}",
                "--build=missing",
                "--format=json",
            ],
            env=env,
            capture_json=True,
        )
        references = graph_references(graph)
        assert_macos_arm64_ogre_node(graph, exported_reference)
        assert_host_graph_target_consistency(graph)
        assert_exact_dependency_revisions(references, exported_reference)
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
                    "graph_profile": (
                        "Macos/"
                        f"{EXPECTED_MACOS_DEPLOYMENT_TARGET}/armv8"
                    ),
                    "ogre_source_sha256": EXPECTED_SOURCE_SHA256,
                    "lockfile": str(lockfile_path),
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
