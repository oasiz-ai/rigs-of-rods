#!/usr/bin/env python3
"""Provenance and source-closure gate for the parallax-probe oracle."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
ORACLE_HEADER = (
    REPOSITORY_ROOT
    / "source"
    / "main"
    / "gfx"
    / "render"
    / "ParallaxProbeReference.h"
)
REFERENCE_LOCK = PROBE_ROOT / "ogre-next-parallax-probe-reference.lock.json"
OGRE_LOCK = PROBE_ROOT / "ogre-next.lock.json"
SOURCE_ROOT_ENV = "ROR_OGRE_NEXT_REFERENCE_SOURCE_ROOT"


def _load_json(path: pathlib.Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if type(value) is not dict:
        raise AssertionError(f"{path} must contain a JSON object")
    return value


def _header_string(name: str, text: str) -> str:
    match = re.search(
        rf"constexpr const char {re.escape(name)}\[\]\s*=\s*\"([^\"]+)\";",
        text,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"missing exact header constant {name}")
    return match.group(1)


class ParallaxProbeReferenceContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.reference = _load_json(REFERENCE_LOCK)
        self.ogre = _load_json(OGRE_LOCK)
        self.header = ORACLE_HEADER.read_text(encoding="utf-8")

    def test_manifest_is_exact_and_bound_to_canonical_pin(self) -> None:
        self.assertEqual(
            set(self.reference),
            {"schema_version", "oracle", "ogre_next_commit", "sources"},
        )
        self.assertIs(type(self.reference["schema_version"]), int)
        self.assertEqual(self.reference["schema_version"], 1)
        self.assertIs(type(self.reference["oracle"]), str)
        self.assertEqual(self.reference["oracle"], "ParallaxProbeReference")
        self.assertIs(type(self.reference["ogre_next_commit"]), str)
        self.assertEqual(
            self.reference["ogre_next_commit"], self.ogre["commit"]
        )
        self.assertEqual(
            _header_string(
                "kParallaxProbeReferenceOgreNextCommit", self.header
            ),
            self.ogre["commit"],
        )

    def test_manifest_sources_are_path_bound_and_match_header(self) -> None:
        sources = self.reference["sources"]
        self.assertIs(type(sources), list)
        self.assertEqual(len(sources), 3)
        expected_constants = {
            "shader_equations": (
                "kParallaxProbeReferenceShaderPath",
                "kParallaxProbeReferenceShaderSha256",
            ),
            "constant_buffer_layout": (
                "kParallaxProbeReferenceBufferSourcePath",
                "kParallaxProbeReferenceBufferSourceSha256",
            ),
            "probe_configuration": (
                "kParallaxProbeReferenceProbeSourcePath",
                "kParallaxProbeReferenceProbeSourceSha256",
            ),
        }
        roles: list[str] = []
        paths: list[str] = []
        for source in sources:
            self.assertIs(type(source), dict)
            self.assertEqual(set(source), {"role", "path", "sha256"})
            role = source["role"]
            path = source["path"]
            digest = source["sha256"]
            self.assertIs(type(role), str)
            self.assertIs(type(path), str)
            self.assertIs(type(digest), str)
            self.assertRegex(digest, r"^[0-9a-f]{64}$")
            self.assertNotIn("..", pathlib.PurePosixPath(path).parts)
            path_constant, digest_constant = expected_constants[role]
            self.assertEqual(_header_string(path_constant, self.header), path)
            self.assertEqual(
                _header_string(digest_constant, self.header), digest
            )
            roles.append(role)
            paths.append(path)
        self.assertEqual(set(roles), set(expected_constants))
        self.assertEqual(len(paths), len(set(paths)))

    def test_exact_pinned_sources_when_source_root_is_supplied(self) -> None:
        configured = os.environ.get(SOURCE_ROOT_ENV)
        if configured is None:
            self.skipTest(f"{SOURCE_ROOT_ENV} is not configured")
        source_root = pathlib.Path(configured).resolve()
        self.assertTrue(source_root.is_dir())
        for source in self.reference["sources"]:
            path = source_root / source["path"]
            self.assertTrue(path.is_file(), f"missing pinned source {path}")
            self.assertEqual(
                hashlib.sha256(path.read_bytes()).hexdigest(),
                source["sha256"],
                f"pinned source hash mismatch: {source['path']}",
            )


if __name__ == "__main__":
    unittest.main()
