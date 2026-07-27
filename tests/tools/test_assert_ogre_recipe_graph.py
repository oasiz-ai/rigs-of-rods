#!/usr/bin/env python3
"""Unit tests for the OGRE recipe graph assertion's fail-closed helpers."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

import assert_ogre_recipe_graph as assertion


EXPORTED_REFERENCE = "ogre3d/14.5.2#0123456789abcdef"


def ogre_node() -> dict[str, object]:
    return {
        "ref": EXPORTED_REFERENCE,
        "context": "host",
        "recipe": "Cache",
        "binary": "Build",
        "settings": {
            "arch": "armv8",
            "build_type": "Release",
            "compiler": "apple-clang",
            "compiler.cppstd": "17",
            "compiler.libcxx": "libc++",
            "compiler.version": "15",
            "os": "Macos",
            "os.version": "11.0",
        },
        "options": dict(assertion.EXPECTED_OGRE_OPTIONS),
    }


def graph_with(node: dict[str, object]) -> dict[str, object]:
    return {"graph": {"nodes": {"0": {}, "1": node}}}


class OgreGraphAssertionTests(unittest.TestCase):
    def test_checked_in_patch_set_is_exact(self) -> None:
        repository_root = Path(assertion.__file__).resolve().parents[2]
        assertion.assert_exact_patch_set(
            repository_root / "cmake/conan/recipes/ogre3d"
        )

    def test_registered_patch_paths_are_read_from_conandata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            conandata = Path(directory) / "conandata.yml"
            conandata.write_text(
                "patches:\n"
                "  14.5.2:\n"
                "    - patch_file: patches/first.patch\n"
                "    - patch_file: patches/second.patch\n"
                "\n"
                "sources:\n"
                "  14.5.2:\n"
                "    url: https://example.invalid/ogre.tar.gz\n",
                encoding="utf-8",
            )
            self.assertEqual(
                assertion.registered_patch_paths(conandata),
                {
                    "patches/first.patch",
                    "patches/second.patch",
                },
            )

    def test_exact_local_build_node_is_accepted(self) -> None:
        graph = graph_with(ogre_node())
        assertion.assert_macos_arm64_ogre_node(
            graph,
            EXPORTED_REFERENCE,
        )
        assertion.assert_host_graph_target_consistency(graph)

    def test_missing_binary_is_rejected(self) -> None:
        node = ogre_node()
        node["binary"] = "Missing"
        with self.assertRaises(AssertionError):
            assertion.assert_macos_arm64_ogre_node(
                graph_with(node),
                EXPORTED_REFERENCE,
            )

    def test_different_recipe_revision_is_rejected(self) -> None:
        node = ogre_node()
        node["ref"] = "ogre3d/14.5.2#different"
        with self.assertRaises(AssertionError):
            assertion.assert_macos_arm64_ogre_node(
                graph_with(node),
                EXPORTED_REFERENCE,
            )

    def test_option_drift_is_rejected(self) -> None:
        node = ogre_node()
        options = dict(assertion.EXPECTED_OGRE_OPTIONS)
        options["with_vulkan"] = "True"
        node["options"] = options
        with self.assertRaises(AssertionError):
            assertion.assert_macos_arm64_ogre_node(
                graph_with(node),
                EXPORTED_REFERENCE,
            )

    def test_dependency_target_drift_is_rejected(self) -> None:
        dependency = {
            "ref": "zlib/1.3.2#revision",
            "context": "host",
            "settings": {
                "arch": "armv8",
                "os": "Macos",
                "os.version": "26.0",
            },
        }
        graph = {
            "graph": {
                "nodes": {
                    "0": {},
                    "1": ogre_node(),
                    "2": dependency,
                }
            }
        }
        with self.assertRaises(AssertionError):
            assertion.assert_host_graph_target_consistency(graph)

    def test_dependency_wrong_os_is_rejected(self) -> None:
        dependency = {
            "ref": "zlib/1.3.2#revision",
            "context": "host",
            "settings": {
                "arch": "x86_64",
                "os": "Linux",
            },
        }
        graph = {
            "graph": {
                "nodes": {
                    "0": {},
                    "1": ogre_node(),
                    "2": dependency,
                }
            }
        }
        with self.assertRaises(AssertionError):
            assertion.assert_host_graph_target_consistency(graph)

    def test_dependency_missing_target_setting_is_rejected(self) -> None:
        dependency = {
            "ref": "zlib/1.3.2#revision",
            "context": "host",
            "settings": {
                "arch": "armv8",
                "os": "Macos",
            },
        }
        graph = {
            "graph": {
                "nodes": {
                    "0": {},
                    "1": ogre_node(),
                    "2": dependency,
                }
            }
        }
        with self.assertRaises(AssertionError):
            assertion.assert_host_graph_target_consistency(graph)

    def test_exact_dependency_revision_set_is_accepted(self) -> None:
        references = sorted(
            assertion.EXPECTED_DEPENDENCY_REVISIONS
            | {EXPORTED_REFERENCE, "conanfile"}
        )
        assertion.assert_exact_dependency_revisions(
            references,
            EXPORTED_REFERENCE,
        )

    def test_dependency_revision_drift_is_rejected(self) -> None:
        references = sorted(
            (
                assertion.EXPECTED_DEPENDENCY_REVISIONS
                - {
                    "zlib/1.3.2"
                    "#1cb806da49011867778ffb6ac7190fcb"
                }
            )
            | {
                "zlib/1.3.2#different",
                EXPORTED_REFERENCE,
                "conanfile",
            }
        )
        with self.assertRaises(AssertionError):
            assertion.assert_exact_dependency_revisions(
                references,
                EXPORTED_REFERENCE,
            )

    def test_exact_lockfile_is_accepted(self) -> None:
        host_references = sorted(
            (
                assertion.EXPECTED_DEPENDENCY_REVISIONS
                - assertion.EXPECTED_BUILD_DEPENDENCY_REVISIONS
            )
            | {EXPORTED_REFERENCE}
        )
        lockfile = {
            "version": "0.5",
            "requires": [
                f"{reference}%123.456"
                for reference in host_references
            ],
            "build_requires": sorted(
                assertion.EXPECTED_BUILD_DEPENDENCY_REVISIONS
            ),
            "python_requires": [],
            "config_requires": [],
        }
        assertion.assert_exact_lockfile(lockfile, EXPORTED_REFERENCE)

    def test_stale_local_recipe_lock_is_rejected(self) -> None:
        host_references = (
            assertion.EXPECTED_DEPENDENCY_REVISIONS
            - assertion.EXPECTED_BUILD_DEPENDENCY_REVISIONS
        ) | {"ogre3d/14.5.2#stale"}
        lockfile = {
            "version": "0.5",
            "requires": sorted(host_references),
            "build_requires": sorted(
                assertion.EXPECTED_BUILD_DEPENDENCY_REVISIONS
            ),
            "python_requires": [],
            "config_requires": [],
        }
        with self.assertRaises(AssertionError):
            assertion.assert_exact_lockfile(
                lockfile,
                EXPORTED_REFERENCE,
            )

    def test_profile_pins_complete_macos_target(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            host_profile = Path(directory) / "host"
            build_profile = Path(directory) / "build"
            assertion.write_macos_arm64_profile(
                host_profile,
                host_target=True,
            )
            assertion.write_macos_arm64_profile(
                build_profile,
                host_target=False,
            )
            host_text = host_profile.read_text(encoding="utf-8")
            build_text = build_profile.read_text(encoding="utf-8")
        self.assertIn("os=Macos\n", host_text)
        self.assertIn("os.version=11.0\n", host_text)
        self.assertIn("arch=armv8\n", host_text)
        self.assertNotIn("os.version=", build_text)


if __name__ == "__main__":
    unittest.main()
