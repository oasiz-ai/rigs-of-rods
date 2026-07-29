#!/usr/bin/env python3
"""Contract tests for authenticated terrain resource bundle dependencies."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEPENDENCY_CMAKE = REPOSITORY_ROOT / "cmake" / "DependenciesConfig.cmake"
MAIN_CMAKE = REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"


class TerrainBundleDependencyContractTests(unittest.TestCase):
    def test_openssl_is_a_mandatory_top_level_dependency(self) -> None:
        source = DEPENDENCY_CMAKE.read_text(encoding="utf-8")

        self.assertEqual(source.count("find_package(OpenSSL REQUIRED)"), 1)
        self.assertNotRegex(
            source,
            re.compile(r"find_package\(OpenSSL\s+QUIET\)"),
        )

    def test_ror_links_crypto_directly(self) -> None:
        source = MAIN_CMAKE.read_text(encoding="utf-8")
        link_blocks = re.findall(
            r"target_link_libraries\(\$\{BINNAME\} PRIVATE([^)]*)\)",
            source,
            re.DOTALL,
        )

        self.assertTrue(
            any("OpenSSL::Crypto" in block for block in link_blocks),
        )
        self.assertEqual(source.count("OpenSSL::Crypto"), 1)


if __name__ == "__main__":
    unittest.main()
