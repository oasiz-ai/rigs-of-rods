#!/usr/bin/env python3
"""Unit tests for the cross-platform OGRE 14 graph auditor."""

from __future__ import annotations

import unittest

import assert_ogre14_app_graph as assertion


OGRE_REFERENCE = "ogre3d/14.5.2#ogre-revision"
MYGUI_REFERENCE = "mygui/3.4.0#mygui-revision"


def local_node(reference: str, platform: assertion.PlatformGraph) -> dict:
    return {
        "ref": reference,
        "context": "host",
        "recipe": "Cache",
        "binary": "Build",
        "settings": {
            "os": platform.conan_os,
            "os.version": platform.os_version,
            "arch": platform.conan_arch,
        },
        "options": (
            dict(assertion.EXPECTED_OGRE_OPTIONS)
            if reference == OGRE_REFERENCE
            else {}
        ),
    }


def graph_for(platform: assertion.PlatformGraph) -> dict:
    nodes = {
        "0": local_node(OGRE_REFERENCE, platform),
        "1": local_node(MYGUI_REFERENCE, platform),
    }
    if platform.key == "linux-x86_64":
        nodes["2"] = local_node("opengl/system#revision", platform)
        nodes["3"] = local_node("xorg/system#revision", platform)
    return {"graph": {"nodes": nodes}}


class Ogre14AppGraphAssertionTests(unittest.TestCase):
    def test_exact_local_graph_is_accepted_for_each_platform(self) -> None:
        for platform in assertion.PLATFORMS.values():
            with self.subTest(platform=platform.key):
                graph = graph_for(platform)
                assertion.assert_host_target(graph, platform)
                assertion.assert_local_node(
                    graph,
                    exact_reference=OGRE_REFERENCE,
                    platform=platform,
                    expected_options=assertion.EXPECTED_OGRE_OPTIONS,
                )
                assertion.assert_local_node(
                    graph,
                    exact_reference=MYGUI_REFERENCE,
                    platform=platform,
                )
                assertion.assert_platform_reference_boundary(graph, platform)

    def test_wrong_host_arch_is_rejected(self) -> None:
        platform = assertion.PLATFORMS["windows-x86_64"]
        graph = graph_for(platform)
        graph["graph"]["nodes"]["1"]["settings"]["arch"] = "armv8"
        with self.assertRaises(AssertionError):
            assertion.assert_host_target(graph, platform)

    def test_wrong_recipe_revision_is_rejected(self) -> None:
        platform = assertion.PLATFORMS["macos-arm64"]
        with self.assertRaises(AssertionError):
            assertion.assert_local_node(
                graph_for(platform),
                exact_reference="mygui/3.4.0#different",
            )

    def test_platform_specific_dependencies_fail_closed(self) -> None:
        platform = assertion.PLATFORMS["windows-x86_64"]
        graph = graph_for(platform)
        graph["graph"]["nodes"]["2"] = local_node(
            "xorg/system#revision",
            platform,
        )
        with self.assertRaises(AssertionError):
            assertion.assert_platform_reference_boundary(graph, platform)

    def test_lock_requires_both_local_recipe_revisions(self) -> None:
        lockfile = {
            "version": "0.5",
            "requires": [
                f"{OGRE_REFERENCE}%123",
                f"{MYGUI_REFERENCE}%456",
            ],
            "build_requires": [],
            "python_requires": [],
            "config_requires": [],
        }
        assertion.assert_lock_contract(
            lockfile,
            ogre_reference=OGRE_REFERENCE,
            mygui_reference=MYGUI_REFERENCE,
        )
        lockfile["requires"].pop()
        with self.assertRaises(AssertionError):
            assertion.assert_lock_contract(
                lockfile,
                ogre_reference=OGRE_REFERENCE,
                mygui_reference=MYGUI_REFERENCE,
            )

    def test_isolated_ogre_lock_rejects_mygui(self) -> None:
        lockfile = {
            "version": "0.5",
            "requires": [OGRE_REFERENCE],
            "build_requires": [],
            "python_requires": [],
            "config_requires": [],
        }
        assertion.assert_isolated_ogre_lock_contract(
            lockfile,
            ogre_reference=OGRE_REFERENCE,
        )
        lockfile["requires"].append(MYGUI_REFERENCE)
        with self.assertRaises(AssertionError):
            assertion.assert_isolated_ogre_lock_contract(
                lockfile,
                ogre_reference=OGRE_REFERENCE,
            )


if __name__ == "__main__":
    unittest.main()
