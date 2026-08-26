#!/usr/bin/env python3
"""Fail closed on the explicit Cg-free Ogre 1.11 developer graph."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


OGRE_REFERENCE = (
    "ogre3d/1.11.6.1@anotherfoxguy/stable"
    "#14a5ef79ac748a7159824954dc1b8a43"
)
OGRE_BASE_REFERENCE = "ogre3d/1.11.6.1@anotherfoxguy/stable"
CAELUM_REFERENCE = (
    "ogre3d-caelum/0.6.3.1@anotherfoxguy/stable"
    "#3bb39b61a989f2f7deaecbab2d4177db"
)
MYGUI_REFERENCE = (
    "mygui/3.4.0@anotherfoxguy/stable"
    "#d544e344e389c9b287124fea8b567d01"
)
PAGED_GEOMETRY_REFERENCE = (
    "ogre3d-pagedgeometry/1.2.0@anotherfoxguy/stable"
    "#f87be81bcfb192a40b575cd651bf516c"
)
OIS_REFERENCE = "ois/1.5.1#d5025190ec611a8e0851cb16a07437a2"
ANGELSCRIPT_REFERENCE = (
    "angelscript/2.38.0#8c9b8d736d0176a6e69c64a4501eeeb1"
)
EXACT_LEGACY_REFERENCES = {
    "ogre3d-caelum/": CAELUM_REFERENCE,
    "mygui/": MYGUI_REFERENCE,
    "ogre3d-pagedgeometry/": PAGED_GEOMETRY_REFERENCE,
    "ois/": OIS_REFERENCE,
}
EXPECTED_OGRE_OPTIONS = {
    "nodeless_positioning": "True",
    "profiling": "True",
    "profiling_remotery": "False",
    "resourcemanager_strict": "off",
}
EXPECTED_PLATFORM_SETTINGS = {
    "linux-x86_64": {
        "os": "Linux",
        "arch": "x86_64",
        "compiler": "gcc",
        "compiler.version": "11",
        "compiler.libcxx": "libstdc++11",
        "compiler.cppstd": "17",
        "build_type": "Release",
    },
    "macos-arm64": {
        "os": "Macos",
        "os.version": "11.0",
        "arch": "armv8",
        "compiler": "apple-clang",
        "compiler.version": "15",
        "compiler.libcxx": "libc++",
        "compiler.cppstd": "17",
        "build_type": "Release",
    },
    "windows-x86_64": {
        "os": "Windows",
        "arch": "x86_64",
        "compiler": "msvc",
        "compiler.version": "194",
        "compiler.cppstd": "17",
        "compiler.runtime": "dynamic",
        "compiler.runtime_type": "Release",
        "build_type": "Release",
    },
}
COMMON_HOST_REFERENCES = {
    ANGELSCRIPT_REFERENCE,
    "brotli/1.1.0#3f631ef77008f7b5eb388780116371a3",
    "bzip2/1.0.8#c470882369c2d95c5c77e970c0c7e321",
    "discord-rpc/3.4.0@anotherfoxguy/stable"
    "#a2905f22ab84faeceebe54e488ff9195",
    "fmt/12.1.0#6baf0fb8351783472b94ee6e36232391",
    "freeimage/3.18.0@anotherfoxguy/stable"
    "#8b69961fa00ad36b37d77dd40502fcbf",
    "freetype/2.14.3#4ee27b7918b546a96d7e6898e3a02b34",
    "jasper/4.2.4#694a46302082de14f5af08d15d959dc6",
    "jxrlib/cci.20170615#576979230d06d6cc445342b2731b517f",
    "lcms/2.19.1#a6579472795f0cd5578ff9f6511f8b80",
    "libcurl/8.2.1#3b6103c90eeb20e92a706cbf2dc0c04a",
    "libjpeg/9e#41e2469373bb9d9a678c8aaf2af326f0",
    "libpng/1.6.58#19cb72905ae54f54948401f753faa2c1",
    "libraw/0.20.2#ce19752ee79b6b1e23b3bcb567ba4831",
    "libtiff/4.6.0#61cbca1281f1ac78681dbd3b0eaf2026",
    "libwebp/1.6.0#eb5f8e35fc95980e32b5544a33a270b4",
    MYGUI_REFERENCE,
    CAELUM_REFERENCE,
    PAGED_GEOMETRY_REFERENCE,
    OGRE_REFERENCE,
    OIS_REFERENCE,
    "openal-soft/1.24.3#0f2e117218b8294276a3c56fa57d3bec",
    "openexr/2.5.7#191290b5cb7da90a05ed351fae29d9c4",
    "openjpeg/2.5.2#6f7b733e151d1bbf5ed05cbabb846828",
    "openssl/3.6.3#a81313131c78c6414f25b5fe0a83204b",
    "pugixml/1.16#aa265531e325d44acf11ae7903ec9410",
    "rapidjson/cci.20211112#0e610cba48da9ecd145a5f74afbe1607",
    "socketw/3.11.0@anotherfoxguy/stable"
    "#6630840d3f73fb6d6e60f6f88132d40a",
    "xz_utils/5.8.3#a8432fead347c69d8b2737c35f936132",
    "zlib/1.3.2#1cb806da49011867778ffb6ac7190fcb",
    "zziplib/0.13.78#a702ebdfc849d51f40651cfd8010aecb",
}
EXPECTED_HOST_REFERENCES = {
    "linux-x86_64": COMMON_HOST_REFERENCES
    | {
        "libalsa/1.2.10#e64d5e1ced869a2f676145bab4f4a181",
        "xorg/system#87ea3cbfa907ff490f4ece2f5fa3dbb9",
    },
    "macos-arm64": set(COMMON_HOST_REFERENCES),
    "windows-x86_64": set(COMMON_HOST_REFERENCES),
}
EXPECTED_BUILD_REFERENCES = {
    "linux-x86_64": {
        "autoconf/2.71#51077f068e61700d65bb05541ea1e4b0",
        "automake/1.16.5#b91b7c384c3deaa9d535be02da14d04f",
        "cmake/3.31.12#173a926abc2b77f03c826b6fd6539426",
        "cmake/4.4.2#8a0d360635c870b1d5c675489ee25074",
        "gnu-config/cci.20210814#466e9d4d7779e1c142443f7ea44b4284",
        "libtool/2.4.7#14e7739cc128bc1623d2ed318008e47e",
        "m4/1.4.19#1727f439cf74e83826ec96d0b4904eee",
        "meson/1.10.2#9d2d10681fe7fe61c788c58626c89b25",
        "ninja/1.13.2#c8c5dc2a52ed6e4e42a66d75b4717ceb",
        "pkgconf/2.5.1#93c2051284cba1279494a43a4fcfeae2",
    },
    "macos-arm64": {
        "autoconf/2.71#51077f068e61700d65bb05541ea1e4b0",
        "automake/1.16.5#b91b7c384c3deaa9d535be02da14d04f",
        "cmake/3.31.12#173a926abc2b77f03c826b6fd6539426",
        "cmake/4.4.2#8a0d360635c870b1d5c675489ee25074",
        "gnu-config/cci.20210814#466e9d4d7779e1c142443f7ea44b4284",
        "libtool/2.4.7#14e7739cc128bc1623d2ed318008e47e",
        "m4/1.4.19#1727f439cf74e83826ec96d0b4904eee",
        "meson/1.10.2#9d2d10681fe7fe61c788c58626c89b25",
        "ninja/1.13.2#c8c5dc2a52ed6e4e42a66d75b4717ceb",
        "pkgconf/2.5.1#93c2051284cba1279494a43a4fcfeae2",
    },
    "windows-x86_64": {
        "cmake/3.31.12#173a926abc2b77f03c826b6fd6539426",
        "cmake/4.4.2#8a0d360635c870b1d5c675489ee25074",
        "meson/1.10.2#9d2d10681fe7fe61c788c58626c89b25",
        "nasm/2.16.01#31e26f2ee3c4346ecd347911bd126904",
        "ninja/1.13.2#c8c5dc2a52ed6e4e42a66d75b4717ceb",
        "pkgconf/2.5.1#93c2051284cba1279494a43a4fcfeae2",
        "strawberryperl/5.32.1.1#8d114504d172cfea8ea1662d09b6333e",
    },
}
FORBIDDEN_REFERENCE_FRAGMENTS = (
    "angelscript/2.35",
    "cg-toolkit",
    "cg_program",
    "cgprogram",
    "nvidia-cg",
    "directx-sdk",
    "ogre3d/14.",
    "ois/1.4",
    "@rigsofrods/custom",
)
ALLOWED_OGRE_REQUIREMENTS = {
    OGRE_BASE_REFERENCE,
    OGRE_REFERENCE,
    "ogre3d/[>=1 <15]@anotherfoxguy/stable",
    "ogre3d/[~1.11]@anotherfoxguy/stable",
}


def graph_nodes(graph: dict[str, Any]) -> list[dict[str, Any]]:
    nodes = graph.get("graph", {}).get("nodes", {})
    if not isinstance(nodes, dict):
        raise AssertionError("Conan graph JSON has no graph.nodes mapping")
    return [node for node in nodes.values() if isinstance(node, dict)]


def nested_strings(value: Any) -> list[str]:
    if isinstance(value, str):
        return [value]
    if isinstance(value, dict):
        strings: list[str] = []
        for key, nested_value in value.items():
            strings.extend(nested_strings(key))
            strings.extend(nested_strings(nested_value))
        return strings
    if isinstance(value, (list, tuple)):
        strings = []
        for nested_value in value:
            strings.extend(nested_strings(nested_value))
        return strings
    return []


def dependency_edges(node: dict[str, Any]) -> list[dict[str, Any]]:
    dependencies = node.get("dependencies", {})
    if not isinstance(dependencies, dict):
        raise AssertionError("Conan graph node dependencies is not a mapping")
    return [
        dependency
        for dependency in dependencies.values()
        if isinstance(dependency, dict)
    ]


def assert_exact_ogre_dependency(
    node: dict[str, Any], node_label: str, *, force: bool
) -> None:
    dependencies = [
        dependency
        for dependency in dependency_edges(node)
        if isinstance(dependency.get("ref"), str)
        and dependency["ref"].lower().startswith("ogre3d/")
    ]
    if len(dependencies) != 1:
        raise AssertionError(
            f"{node_label} does not have exactly one Ogre dependency"
        )
    dependency = dependencies[0]
    if dependency.get("ref") != OGRE_BASE_REFERENCE:
        raise AssertionError(
            f"{node_label} resolved a non-exact Ogre dependency: "
            f"{dependency.get('ref')!r}"
        )
    if dependency.get("require") != OGRE_BASE_REFERENCE:
        raise AssertionError(
            f"{node_label} declared a non-exact Ogre requirement: "
            f"{dependency.get('require')!r}"
        )
    if dependency.get("force") is not force:
        raise AssertionError(
            f"{node_label} Ogre force policy changed: "
            f"{dependency.get('force')!r}"
        )


def assert_cg_free_legacy_graph(
    graph: dict[str, Any], platform: str
) -> None:
    expected_settings = EXPECTED_PLATFORM_SETTINGS.get(platform)
    if expected_settings is None:
        raise AssertionError(f"unsupported legacy graph platform: {platform}")

    nodes = graph_nodes(graph)
    root_nodes = [node for node in nodes if node.get("ref") == "Rigs of Rods/None"]
    if len(root_nodes) != 1 or root_nodes[0].get("options") != {
        "ogre14": "False"
    }:
        raise AssertionError("graph is not the explicit ogre14=False lane")
    if root_nodes[0].get("settings") != expected_settings:
        raise AssertionError(
            f"root settings do not match {platform}: "
            f"{root_nodes[0].get('settings')!r}"
        )

    package_nodes = [
        node
        for node in nodes
        if isinstance(node.get("ref"), str)
        and node["ref"] != "Rigs of Rods/None"
    ]
    unexpected_contexts = [
        f"{node['ref']} ({node.get('context')!r})"
        for node in package_nodes
        if node.get("context") not in {"host", "build"}
    ]
    if unexpected_contexts:
        raise AssertionError(
            f"{platform} package nodes use unsupported contexts: "
            f"{unexpected_contexts!r}"
        )

    observed_host_references = [
        node["ref"]
        for node in package_nodes
        if node.get("context") == "host"
    ]
    if len(observed_host_references) != len(set(observed_host_references)):
        raise AssertionError(
            f"{platform} host requirements contain duplicate references"
        )
    expected_host_references = EXPECTED_HOST_REFERENCES[platform]
    if set(observed_host_references) != expected_host_references:
        raise AssertionError(
            f"{platform} host requirements changed: expected "
            f"{sorted(expected_host_references)!r}, got "
            f"{sorted(observed_host_references)!r}"
        )

    build_nodes = [node for node in nodes if node.get("context") == "build"]
    observed_build_references = {
        node["ref"]
        for node in build_nodes
        if isinstance(node.get("ref"), str)
    }
    expected_build_references = EXPECTED_BUILD_REFERENCES[platform]
    if observed_build_references != expected_build_references:
        raise AssertionError(
            f"{platform} build requirements changed: expected "
            f"{sorted(expected_build_references)!r}, got "
            f"{sorted(observed_build_references)!r}"
        )
    for build_node in build_nodes:
        build_settings = build_node.get("settings", {})
        if not isinstance(build_settings, dict):
            raise AssertionError("build requirement settings is not a mapping")
        if not build_settings:
            continue
        if (
            build_settings.get("os") != expected_settings["os"]
            or build_settings.get("arch") != expected_settings["arch"]
        ):
            raise AssertionError(
                f"{platform} build requirement uses a foreign build "
                f"profile: {build_node.get('ref')} {build_settings!r}"
            )
        for setting, value in build_settings.items():
            if setting in expected_settings and expected_settings[setting] != value:
                raise AssertionError(
                    f"{platform} build requirement setting changed: "
                    f"{build_node.get('ref')} {setting}={value!r}"
                )

    references = [
        reference
        for node in nodes
        if isinstance((reference := node.get("ref")), str)
    ]
    graph_payload = graph.get("graph", {})
    reference_metadata = list(references)
    for node in nodes:
        for dependency in dependency_edges(node):
            reference_metadata.extend(
                nested_strings(
                    {
                        "ref": dependency.get("ref"),
                        "require": dependency.get("require"),
                    }
                )
            )
    for metadata_key in (
        "overrides",
        "replaced_requires",
        "resolved_ranges",
    ):
        reference_metadata.extend(
            nested_strings(graph_payload.get(metadata_key, {}))
        )

    lowered = [reference.lower() for reference in reference_metadata]
    for fragment in FORBIDDEN_REFERENCE_FRAGMENTS:
        if any(fragment in reference for reference in lowered):
            raise AssertionError(
                f"legacy graph contains forbidden Cg reference: {fragment}"
            )
    unexpected_ogre_requirements = sorted(
        {
            reference
            for reference in reference_metadata
            if reference.lower().startswith("ogre3d/")
            and reference not in ALLOWED_OGRE_REQUIREMENTS
        }
    )
    if unexpected_ogre_requirements:
        raise AssertionError(
            "legacy graph contains an unapproved Ogre requirement or "
            f"override: {unexpected_ogre_requirements!r}"
        )

    ogre_nodes = [node for node in nodes if node.get("ref") == OGRE_REFERENCE]
    if len(ogre_nodes) != 1:
        raise AssertionError(
            f"legacy graph does not contain exactly {OGRE_REFERENCE}"
        )
    ogre_node = ogre_nodes[0]
    if ogre_node.get("context") != "host" or ogre_node.get("recipe") != "Cache":
        raise AssertionError("legacy Ogre recipe is not the exported cache node")
    if ogre_node.get("settings") != expected_settings:
        raise AssertionError(
            f"legacy Ogre settings do not match {platform}: "
            f"{ogre_node.get('settings')!r}"
        )
    if ogre_node.get("options") != EXPECTED_OGRE_OPTIONS:
        raise AssertionError(
            f"legacy Ogre options changed: {ogre_node.get('options')!r}"
        )

    assert_exact_ogre_dependency(root_nodes[0], "root", force=True)
    caelum_nodes = [
        node for node in nodes if node.get("ref") == CAELUM_REFERENCE
    ]
    if len(caelum_nodes) != 1:
        raise AssertionError(
            f"legacy graph does not contain exactly {CAELUM_REFERENCE}"
        )
    assert_exact_ogre_dependency(
        caelum_nodes[0], "Caelum", force=False
    )

    for package_prefix, expected_reference in EXACT_LEGACY_REFERENCES.items():
        package_references = [
            reference
            for reference in references
            if reference.lower().startswith(package_prefix)
        ]
        if package_references != [expected_reference]:
            raise AssertionError(
                f"legacy graph changed {package_prefix}: "
                f"expected {[expected_reference]!r}, "
                f"got {package_references!r}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("graph", type=Path)
    parser.add_argument(
        "--platform",
        required=True,
        choices=tuple(EXPECTED_PLATFORM_SETTINGS),
    )
    arguments = parser.parse_args()
    payload = json.loads(arguments.graph.read_text(encoding="utf-8"))
    assert_cg_free_legacy_graph(payload, arguments.platform)
    print(
        json.dumps(
            {
                "cg_requirement_present": False,
                "directx9_requirement_present": False,
                "legacy_caelum_reference": CAELUM_REFERENCE,
                "legacy_ogre_reference": OGRE_REFERENCE,
                "legacy_ois_reference": OIS_REFERENCE,
                "legacy_mygui_reference": MYGUI_REFERENCE,
                "legacy_paged_geometry_reference": PAGED_GEOMETRY_REFERENCE,
                "ogre14": False,
                "platform": arguments.platform,
                "settings": EXPECTED_PLATFORM_SETTINGS[arguments.platform],
                "status": "passed",
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
