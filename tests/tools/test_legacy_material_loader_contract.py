#!/usr/bin/env python3
"""Source-level trust-boundary checks for legacy generated textures."""

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CONTENT_MANAGER = (
    REPOSITORY_ROOT / "source" / "main" / "resources" / "ContentManager.cpp"
)


def authorized_texture_loading_block() -> str:
    source = CONTENT_MANAGER.read_text(encoding="utf-8")
    start = source.index(
        "    if (!resolution_archive_sha256.empty())",
        source.index("ContentManager::resourceLoading"),
    )
    end = source.index(
        "\n#if OGRE_VERSION_MAJOR >= 14\n"
        "    // OGRE 14 documents ZipArchive::open() as non-thread-safe",
        start,
    )
    return source[start:end]


class LegacyMaterialLoaderContractTests(unittest.TestCase):
    def test_authorized_generated_name_never_falls_through(self) -> None:
        block = authorized_texture_loading_block()

        self.assertNotIn("resourceExists", block)
        self.assertNotRegex(
            block,
            re.compile(r"return\s+Ogre::DataStreamPtr\(\s*\);"),
        )
        self.assertIn("return replacement;", block)

    def test_invalidated_authorization_aborts_the_load(self) -> None:
        block = authorized_texture_loading_block()

        self.assertGreaterEqual(block.count("OGRE_EXCEPT("), 2)
        self.assertIn("ERR_INVALID_STATE", block)
        self.assertIn("was revoked while the resource was loading", block)
        self.assertIn("changed while the resource was loading", block)


if __name__ == "__main__":
    unittest.main()
