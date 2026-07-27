#!/usr/bin/env python3
"""Unit tests for OGRE native package path-boundary helpers."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

import assert_ogre_native_package as assertion


class OgreNativePackageAssertionTests(unittest.TestCase):
    def test_darwin_tmp_aliases_are_both_retained(self) -> None:
        private = assertion.path_spelling_variants(
            Path("/private/tmp/ror-ogre-cache")
        )
        public = assertion.path_spelling_variants(
            Path("/tmp/ror-ogre-cache")
        )
        self.assertIn("/private/tmp/ror-ogre-cache", private)
        self.assertIn("/tmp/ror-ogre-cache", private)
        self.assertIn("/private/tmp/ror-ogre-cache", public)
        self.assertIn("/tmp/ror-ogre-cache", public)

    def test_symlinked_conan_cache_retains_lexical_and_resolved_roots(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            real_home = root / "real-home"
            package = real_home / "p" / "b" / "hash" / "p"
            package.mkdir(parents=True)
            linked_home = root / "linked-home"
            try:
                linked_home.symlink_to(real_home, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"directory symlinks unavailable: {error}")
            linked_package = linked_home / "p" / "b" / "hash" / "p"
            prefixes = assertion.forbidden_cache_prefixes(linked_package)
            self.assertIn(str(linked_home), prefixes)
            self.assertIn(str(real_home.resolve()), prefixes)
            self.assertIn(str(linked_package), prefixes)
            self.assertIn(str(package.resolve()), prefixes)

    def test_prefixes_are_fatal_in_runtime_or_text_metadata(self) -> None:
        prefixes = frozenset(
            {
                "/tmp/ror-ogre-cache",
                "/private/tmp/ror-ogre-cache",
            }
        )
        for label, payload in (
            (
                "dependency",
                "/tmp/ror-ogre-cache/p/lib/libOgreMain.dylib",
            ),
            (
                "rpath",
                "path /private/tmp/ror-ogre-cache/p/lib",
            ),
            (
                "install name",
                "/tmp/ror-ogre-cache/p/libOgreMain.dylib",
            ),
            (
                "pkg-config",
                "prefix=/private/tmp/ror-ogre-cache/p",
            ),
            (
                "plugins.cfg",
                "PluginFolder=/tmp/ror-ogre-cache/p/lib/OGRE",
            ),
        ):
            with self.subTest(label=label):
                with self.assertRaises(assertion.VerificationError):
                    assertion.assert_no_forbidden_prefixes(
                        payload,
                        prefixes,
                        context=label,
                    )

    def test_nonruntime_source_strings_are_inventoried_not_hidden(self) -> None:
        prefixes = frozenset(
            {
                "/tmp/ror-ogre-cache",
                "/private/tmp/ror-ogre-cache",
            }
        )
        self.assertEqual(
            assertion.matching_forbidden_prefixes(
                "assertion at /tmp/ror-ogre-cache/b/source.mm",
                prefixes,
            ),
            ["/tmp/ror-ogre-cache"],
        )
        self.assertEqual(
            assertion.matching_forbidden_prefixes(
                "ordinary diagnostic text",
                prefixes,
            ),
            [],
        )

    def test_otool_input_header_is_not_treated_as_embedded_metadata(
        self,
    ) -> None:
        output = (
            "/tmp/ror-ogre-cache/p/bin/OgreMeshUpgrader:\n"
            "\t@rpath/libOgreMain.14.5.dylib "
            "(compatibility version 14.5.0, current version 14.5.2)\n"
        )
        payload = assertion.otool_payload(output)
        self.assertNotIn("/tmp/ror-ogre-cache", payload)
        self.assertIn("@rpath/libOgreMain.14.5.dylib", payload)

    def test_loader_references_reject_traversal_and_nested_paths(self) -> None:
        for reference in (
            "@loader_path/libOgreMain.dylib",
            "@executable_path/libOgreMain.dylib",
            "@rpath/../../../../tmp/evil.dylib",
            "@rpath/subdir/libOgreMain.dylib",
            r"@rpath\..\evil.dylib",
            "@rpath/",
            "@rpath/..",
        ):
            with self.subTest(reference=reference):
                self.assertFalse(assertion.is_safe_rpath_dylib(reference))
        self.assertTrue(
            assertion.is_safe_rpath_dylib(
                "@rpath/libOgreMain.14.5.dylib"
            )
        )

    def test_system_dependencies_are_absolute_and_normalized(self) -> None:
        for reference in (
            "/usr/lib/../tmp/evil.dylib",
            "/System/Library/../tmp/evil.framework/evil",
            "/usr/lib//libSystem.B.dylib",
            r"/usr/lib\..\tmp\evil.dylib",
            "/opt/local/lib/libevil.dylib",
        ):
            with self.subTest(reference=reference):
                self.assertFalse(
                    assertion.is_safe_system_dependency(reference)
                )
        self.assertTrue(
            assertion.is_safe_system_dependency(
                "/usr/lib/libSystem.B.dylib"
            )
        )
        self.assertTrue(
            assertion.is_safe_system_dependency(
                "/System/Library/Frameworks/Metal.framework/"
                "Versions/A/Metal"
            )
        )


if __name__ == "__main__":
    unittest.main()
