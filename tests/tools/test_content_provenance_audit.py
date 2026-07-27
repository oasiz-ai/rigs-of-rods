#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/content_provenance_audit.py"
FIXTURE_ROOT = REPOSITORY_ROOT / "tests/fixtures/content_provenance"
SIZE_MAX = (1 << (8 * struct.calcsize("P"))) - 1

SPEC = importlib.util.spec_from_file_location("content_provenance_audit", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load content provenance auditor")
AUDITOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDITOR)


def load_fixture(name: str) -> object:
    return json.loads((FIXTURE_ROOT / name).read_text(encoding="utf-8"))


def diagnostic_codes(report: dict[str, object]) -> list[str]:
    diagnostics = report["diagnostics"]
    assert isinstance(diagnostics, list)
    return [item["code"] for item in diagnostics]


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def canonical_json(value: object) -> str:
    return json.dumps(
        value, ensure_ascii=True, sort_keys=True, separators=(",", ":")
    )


def single_file_documents(
    path: str, payload: bytes
) -> tuple[dict[str, object], dict[str, object]]:
    digest = sha256(payload)
    manifest = {
        "format": "ror-content-provenance-v1",
        "spdx_list_version": "3.28.0",
        "assets": [
            {
                "path": path,
                "sha256": digest,
                "author": "Test author",
                "license": "CC0-1.0",
                "modified": False,
                "classification": "project-authored",
                "source": {
                    "kind": "repository",
                    "uri": "https://example.invalid/repository",
                    "revision": "0123456789abcdef0123456789abcdef01234567",
                },
                "editable_source": {
                    "path": "sources/asset.source",
                    "sha256": "0" * 64,
                },
                "redistribution": {
                    "allowed": True,
                    "evidence": "https://example.invalid/license",
                },
            }
        ],
    }
    inventory = {
        "format": "ror-distributable-inventory-v1",
        "files": [
            {
                "path": path,
                "sha256": digest,
                "size": len(payload),
                "type": "file",
            }
        ],
    }
    return manifest, inventory


class ContentProvenanceAuditTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = load_fixture("valid.manifest.json")
        self.inventory = load_fixture("valid.inventory.json")

    def audit(
        self,
        manifest: object | None = None,
        inventory: object | None = None,
        **kwargs: object,
    ) -> dict[str, object]:
        return AUDITOR.audit(
            self.manifest if manifest is None else manifest,
            self.inventory if inventory is None else inventory,
            **kwargs,
        )

    def test_valid_fixture_has_exact_coverage(self) -> None:
        report = self.audit()
        self.assertTrue(report["ok"])
        self.assertEqual(report["diagnostics"], [])
        self.assertEqual(
            report["summary"],
            {
                "checksum_matched_files": 2,
                "errors": 0,
                "inventory_files": 2,
                "manifest_assets": 2,
                "path_matched_files": 2,
            },
        )

    def test_report_is_invariant_to_entry_order(self) -> None:
        baseline = canonical_json(self.audit())
        reversed_manifest = copy.deepcopy(self.manifest)
        reversed_inventory = copy.deepcopy(self.inventory)
        reversed_manifest["assets"].reverse()
        reversed_inventory["files"].reverse()
        self.assertEqual(
            baseline,
            canonical_json(self.audit(reversed_manifest, reversed_inventory)),
        )

    def test_summary_separates_paths_from_valid_checksum_matches(self) -> None:
        inventory = copy.deepcopy(self.inventory)
        inventory["files"][0]["sha256"] = "f" * 64
        report = self.audit(self.manifest, inventory)
        self.assertFalse(report["ok"])
        self.assertEqual(report["summary"]["path_matched_files"], 2)
        self.assertEqual(report["summary"]["checksum_matched_files"], 1)
        self.assertIn(
            "PROVENANCE_CHECKSUM_MISMATCH", diagnostic_codes(report)
        )

        inventory["files"][0]["sha256"] = "F" * 64
        report = self.audit(self.manifest, inventory)
        self.assertFalse(report["ok"])
        self.assertEqual(report["summary"]["path_matched_files"], 2)
        self.assertEqual(report["summary"]["checksum_matched_files"], 1)
        self.assertIn("SHA256_INVALID", diagnostic_codes(report))

    def test_missing_provenance_and_stale_records_fail_closed(self) -> None:
        inventory = copy.deepcopy(self.inventory)
        inventory["files"].append(
            {
                "path": "imports/user-vehicle.zip",
                "sha256": "1" * 64,
                "size": 1,
                "type": "file",
            }
        )
        inventory["files"].append(
            {
                "path": "generated/cache/vehicle.tmp",
                "sha256": "2" * 64,
                "size": 1,
                "type": "file",
            }
        )
        manifest = copy.deepcopy(self.manifest)
        manifest["assets"].append(
            {
                **copy.deepcopy(manifest["assets"][0]),
                "path": "content/no-longer-shipped.png",
                "sha256": "3" * 64,
            }
        )
        report = self.audit(manifest, inventory)
        self.assertFalse(report["ok"])
        self.assertEqual(
            diagnostic_codes(report).count("UNTRACKED_IMPORT_ARTIFACT"), 2
        )
        self.assertIn("PROVENANCE_STALE", diagnostic_codes(report))
        self.assertEqual(report["summary"]["path_matched_files"], 2)
        self.assertEqual(report["summary"]["checksum_matched_files"], 2)

    def test_archive_and_cache_paths_cannot_evade_import_metadata(self) -> None:
        for path, classification in [
            ("imports/user-vehicle.zip", "project-authored"),
            ("generated/cache/vehicle.tmp", "third-party"),
        ]:
            with self.subTest(path=path):
                manifest = copy.deepcopy(self.manifest)
                inventory = copy.deepcopy(self.inventory)
                manifest["assets"][0]["path"] = path
                manifest["assets"][0]["classification"] = classification
                inventory["files"][0]["path"] = path
                report = self.audit(manifest, inventory)
                self.assertFalse(report["ok"])
                self.assertIn(
                    "IMPORT_CLASSIFICATION_REQUIRED",
                    diagnostic_codes(report),
                )

    def test_required_rights_and_provenance_fields_are_validated(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        asset = manifest["assets"][0]
        asset["sha256"] = "A" * 64
        asset["author"] = ""
        asset["license"] = "Definitely-Not-SPDX"
        asset["modified"] = "yes"
        asset["source"] = {
            "kind": "repository",
            "uri": "http://example.invalid/source#fragment",
        }
        asset["editable_source"] = {
            "path": "../source.blend",
            "sha256": "short",
        }
        asset["redistribution"] = {
            "allowed": False,
            "evidence": "file:///permission.txt",
        }
        codes = diagnostic_codes(self.audit(manifest, self.inventory))
        for expected in (
            "SHA256_INVALID",
            "TEXT_EMPTY",
            "SPDX_EXPRESSION_INVALID",
            "FIELD_TYPE",
            "SOURCE_NOT_PINNED",
            "SOURCE_URI_INVALID",
            "PATH_UNSAFE",
            "REDISTRIBUTION_NOT_ALLOWED",
        ):
            self.assertIn(expected, codes)

    def test_whitespace_text_is_a_diagnostic_not_an_exception(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        manifest["assets"][0]["author"] = " Author with padding "
        report = self.audit(manifest, self.inventory)
        self.assertFalse(report["ok"])
        self.assertIn("TEXT_NOT_CANONICAL", diagnostic_codes(report))

    def test_invalid_unicode_and_windows_device_paths_fail_closed(self) -> None:
        inventory = copy.deepcopy(self.inventory)
        inventory["files"].extend(
            [
                {
                    "path": "content/\ud800.png",
                    "sha256": "4" * 64,
                    "size": 0,
                    "type": "file",
                },
                {
                    "path": "content/CON.txt",
                    "sha256": "5" * 64,
                    "size": 0,
                    "type": "file",
                },
            ]
        )
        report = self.audit(self.manifest, inventory)
        self.assertFalse(report["ok"])
        self.assertEqual(diagnostic_codes(report).count("PATH_UNSAFE"), 2)

    def test_paths_reject_traversal_duplicates_and_case_collisions(self) -> None:
        inventory = copy.deepcopy(self.inventory)
        inventory["files"].extend(
            [
                {
                    "path": "../escape.png",
                    "sha256": "1" * 64,
                    "size": 0,
                    "type": "file",
                },
                copy.deepcopy(inventory["files"][0]),
                {
                    **copy.deepcopy(inventory["files"][0]),
                    "path": "Content/DAFSEMI/THUMBNAIL.PNG",
                },
            ]
        )
        codes = diagnostic_codes(self.audit(self.manifest, inventory))
        self.assertIn("PATH_UNSAFE", codes)
        self.assertIn("PATH_DUPLICATE", codes)
        self.assertIn("PATH_CASE_COLLISION", codes)

    def test_paths_reject_windows_forbidden_and_overlong_components(self) -> None:
        invalid_paths = [
            'content/bad"name.bin',
            "content/bad<name.bin",
            "content/bad>name.bin",
            "content/bad|name.bin",
            "content/bad?name.bin",
            "content/bad*name.bin",
            "content/directory./name.bin",
            "content/" + ("a" * 256),
            "content/CONIN$.txt",
            "content/LPT¹.txt",
        ]
        for invalid_path in invalid_paths:
            with self.subTest(path=invalid_path):
                self.assertIsNotNone(AUDITOR.path_problem(invalid_path))
        self.assertIsNone(AUDITOR.path_problem("content/portable-name.bin"))
        self.assertIsNone(AUDITOR.path_problem("a" * 255))

    def test_https_urls_require_canonical_valid_dns_or_ip_authority(self) -> None:
        invalid_urls = [
            "https://.",
            "https://exa_mple.invalid/source",
            "https://%65xample.invalid/source",
            "https://127.1/source",
            "https://example.invalid:/source",
            "https://example.invalid/%zz",
            "https://example.invalid/source#",
            "HTTPS://example.invalid/source",
            "https://Example.invalid/source",
            "https://例.example/source",
        ]
        for invalid_url in invalid_urls:
            with self.subTest(url=invalid_url):
                diagnostics = AUDITOR.Diagnostics()
                self.assertIsNone(
                    AUDITOR.checked_https_uri(
                        invalid_url,
                        pointer="/url",
                        diagnostics=diagnostics,
                    )
                )
                self.assertIn(
                    "SOURCE_URI_INVALID",
                    diagnostic_codes({"diagnostics": diagnostics.items}),
                )

        for valid_url in [
            "https://example.invalid/source",
            "https://127.0.0.1/source",
            "https://[2001:db8::1]/source",
            "https://xn--fsq.example/source",
        ]:
            with self.subTest(url=valid_url):
                diagnostics = AUDITOR.Diagnostics()
                self.assertEqual(
                    AUDITOR.checked_https_uri(
                        valid_url,
                        pointer="/url",
                        diagnostics=diagnostics,
                    ),
                    valid_url,
                )
                self.assertEqual(diagnostics.items, [])

        manifest = copy.deepcopy(self.manifest)
        for asset in manifest["assets"]:
            asset["source"]["uri"] = "https://."
            asset["redistribution"]["evidence"] = "https://."
        report = self.audit(manifest, self.inventory)
        self.assertFalse(report["ok"])
        self.assertEqual(
            diagnostic_codes(report).count("SOURCE_URI_INVALID"), 4
        )

    def test_mutable_source_revision_is_not_an_immutable_pin(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        for asset in manifest["assets"]:
            asset["source"].pop("sha256", None)
            asset["source"]["revision"] = "refs/heads/main"
        report = self.audit(manifest, self.inventory)
        self.assertFalse(report["ok"])
        self.assertEqual(
            diagnostic_codes(report).count("SOURCE_REVISION_INVALID"), 2
        )
        self.assertEqual(
            diagnostic_codes(report).count("SOURCE_NOT_PINNED"), 2
        )

    def test_import_and_cache_records_require_conversion_metadata(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        manifest["assets"][0]["classification"] = "import-archive"
        report = self.audit(manifest, self.inventory)
        self.assertIn("IMPORT_METADATA_MISSING", diagnostic_codes(report))

        manifest["assets"][0]["import"] = {
            "archive_sha256": manifest["assets"][0]["sha256"],
            "detected_version": "0.9.7",
            "importer_schema": "beamng-import-v1",
            "conversion_options": {"root_part": "FC-A7-01", "execute_lua": False},
        }
        self.assertNotIn(
            "IMPORT_METADATA_MISSING",
            diagnostic_codes(self.audit(manifest, self.inventory)),
        )
        manifest["assets"][0]["import"]["archive_sha256"] = "9" * 64
        self.assertIn(
            "IMPORT_ARCHIVE_CHECKSUM_MISMATCH",
            diagnostic_codes(self.audit(manifest, self.inventory)),
        )

    def test_generated_assets_pin_the_generator_and_modification(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        asset = manifest["assets"][0]
        asset["classification"] = "generated"
        asset["modified"] = False
        codes = diagnostic_codes(self.audit(manifest, self.inventory))
        self.assertIn("GENERATED_SOURCE_KIND_INVALID", codes)
        self.assertIn("GENERATED_MODIFICATION_INVALID", codes)
        asset["modified"] = True
        asset["source"]["kind"] = "generator"
        self.assertTrue(self.audit(manifest, self.inventory)["ok"])

    def test_spdx_expression_parser_accepts_known_compound_expression(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        manifest["assets"][0]["license"] = (
            "(GPL-3.0-or-later WITH Font-exception-2.0) OR CC-BY-SA-4.0"
        )
        self.assertTrue(self.audit(manifest, self.inventory)["ok"])
        manifest["assets"][0]["license"] = "GPL-3.0"
        self.assertIn(
            "SPDX_EXPRESSION_INVALID",
            diagnostic_codes(self.audit(manifest, self.inventory)),
        )

    def test_package_and_editable_roots_verify_real_bytes(self) -> None:
        package_payload = b"runtime asset\n"
        editable_payload = b"editable source\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package_root = root / "package"
            editable_root = root / "repository"
            package_file = package_root / "content/asset.bin"
            editable_file = editable_root / "sources/asset.blend"
            package_file.parent.mkdir(parents=True)
            editable_file.parent.mkdir(parents=True)
            package_file.write_bytes(package_payload)
            editable_file.write_bytes(editable_payload)
            manifest = {
                "format": "ror-content-provenance-v1",
                "spdx_list_version": "3.28.0",
                "assets": [
                    {
                        "path": "content/asset.bin",
                        "sha256": sha256(package_payload),
                        "author": "Test author",
                        "license": "CC0-1.0",
                        "modified": True,
                        "classification": "project-authored",
                        "source": {
                            "kind": "repository",
                            "uri": "https://example.invalid/repository",
                            "revision": (
                                "0123456789abcdef0123456789abcdef01234567"
                            ),
                        },
                        "editable_source": {
                            "path": "sources/asset.blend",
                            "sha256": sha256(editable_payload),
                        },
                        "redistribution": {
                            "allowed": True,
                            "evidence": "https://example.invalid/license",
                        },
                    }
                ],
            }
            inventory = {
                "format": "ror-distributable-inventory-v1",
                "files": [
                    {
                        "path": "content/asset.bin",
                        "sha256": sha256(package_payload),
                        "size": len(package_payload),
                        "type": "file",
                    }
                ],
            }
            report = self.audit(
                manifest,
                inventory,
                package_root=package_root,
                editable_root=editable_root,
                release_gate=True,
            )
            self.assertTrue(report["ok"], report)

            package_file.write_bytes(b"tampered\n")
            editable_file.write_bytes(b"tampered source\n")
            original_fstat = AUDITOR.os.fstat

            class DescriptorStat:
                """Model Windows' fstat change-time semantics on every host."""

                def __init__(self, wrapped: os.stat_result) -> None:
                    self._wrapped = wrapped
                    self.st_ctime = wrapped.st_ctime + 1
                    self.st_ctime_ns = wrapped.st_ctime_ns + 1_000_000_000

                def __getattr__(self, name: str) -> object:
                    return getattr(self._wrapped, name)

            def descriptor_stat(descriptor: int) -> DescriptorStat:
                return DescriptorStat(original_fstat(descriptor))

            with mock.patch.object(
                AUDITOR.os,
                "fstat",
                side_effect=descriptor_stat,
            ):
                report = self.audit(
                    manifest,
                    inventory,
                    package_root=package_root,
                    editable_root=editable_root,
                )
            codes = diagnostic_codes(report)
            self.assertIn("INVENTORY_CHECKSUM_MISMATCH", codes)
            self.assertIn("INVENTORY_SIZE_MISMATCH", codes)
            self.assertIn("EDITABLE_SOURCE_CHECKSUM_MISMATCH", codes)

    def test_package_root_detects_inventory_omissions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "unexpected").mkdir()
            (root / "unexpected/user-mod.zip").write_bytes(b"archive")
            report = self.audit(package_root=root)
            codes = diagnostic_codes(report)
            self.assertIn("UNTRACKED_IMPORT_ARTIFACT", codes)
            self.assertEqual(codes.count("INVENTORY_FILE_MISSING"), 2)

    def test_filesystem_hash_budget_is_bounded(self) -> None:
        payload = b"0123456789"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package_file = root / "content/dafsemi/thumbnail.png"
            package_file.parent.mkdir(parents=True)
            package_file.write_bytes(payload)
            inventory = copy.deepcopy(self.inventory)
            inventory["files"] = [inventory["files"][0]]
            inventory["files"][0]["size"] = len(payload)
            inventory["files"][0]["sha256"] = sha256(payload)
            manifest = copy.deepcopy(self.manifest)
            manifest["assets"] = [manifest["assets"][0]]
            manifest["assets"][0]["sha256"] = sha256(payload)
            report = self.audit(
                manifest,
                inventory,
                package_root=root,
                max_hashed_file_bytes=5,
            )
            self.assertIn("PACKAGE_FILE_TOO_LARGE", diagnostic_codes(report))

    def test_filesystem_entry_limit_stops_without_partial_comparisons(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for index in range(3):
                (root / f"file-{index}.bin").write_bytes(b"x")
            report = self.audit(
                package_root=root,
                max_filesystem_files=2,
            )
            codes = diagnostic_codes(report)
            self.assertEqual(codes.count("FILESYSTEM_FILE_LIMIT_EXCEEDED"), 1)
            self.assertNotIn("INVENTORY_FILE_MISSING", codes)
            self.assertNotIn("INVENTORY_MISSING_FILE", codes)

    def test_capped_package_diagnostics_ignore_scandir_order(self) -> None:
        class FakeEntry:
            def __init__(self, root: Path, name: str) -> None:
                self.name = name
                self.path = str(root / name)

            def stat(self, *, follow_symlinks: bool) -> os.stat_result:
                self.assert_no_follow(follow_symlinks)
                values = [stat.S_IFLNK | 0o777, 0, 0, 1, 0, 0, 0, 0, 0, 0]
                return os.stat_result(values)

            @staticmethod
            def assert_no_follow(follow_symlinks: bool) -> None:
                if follow_symlinks:
                    raise AssertionError("package scan followed a symlink")

        class FakeScandir:
            def __init__(self, entries: list[FakeEntry]) -> None:
                self.entries = entries

            def __enter__(self) -> "FakeScandir":
                return self

            def __exit__(self, *arguments: object) -> None:
                return None

            def __iter__(self) -> object:
                return iter(self.entries)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            entries = [
                FakeEntry(root, f"link-{index:05d}")
                for index in range(5_000)
            ]
            reports = []
            for ordered_entries in (entries, list(reversed(entries))):
                diagnostics = AUDITOR.Diagnostics()
                with mock.patch.object(
                    AUDITOR.os,
                    "scandir",
                    return_value=FakeScandir(ordered_entries),
                ):
                    found, complete = AUDITOR.scan_package_root(
                        root,
                        max_files=SIZE_MAX,
                        diagnostics=diagnostics,
                    )
                self.assertTrue(complete)
                self.assertEqual(found, set())
                reports.append(canonical_json(diagnostics.sorted()))
                self.assertEqual(diagnostics.total_count, 5_000)
            self.assertEqual(reports[0], reports[1])

    def test_directory_identity_ignores_mutable_windows_metadata(self) -> None:
        initial = os.stat_result(
            [stat.S_IFDIR | 0o755, 91, 7, 1, 0, 0, 0, 1, 2, 3]
        )
        refreshed = os.stat_result(
            [stat.S_IFDIR | 0o755, 91, 7, 1, 0, 0, 4096, 4, 5, 6]
        )
        replacement = os.stat_result(
            [stat.S_IFDIR | 0o755, 92, 7, 1, 0, 0, 4096, 4, 5, 6]
        )

        self.assertTrue(
            AUDITOR._same_directory_identity(initial, refreshed)
        )
        self.assertFalse(
            AUDITOR._same_directory_identity(initial, replacement)
        )

    def test_symlinked_roots_are_rejected(self) -> None:
        if not hasattr(Path, "symlink_to"):
            self.skipTest("platform has no symlink support")
        with tempfile.TemporaryDirectory() as temporary:
            container = Path(temporary)
            real_root = container / "real"
            real_root.mkdir()
            linked_root = container / "linked"
            try:
                linked_root.symlink_to(real_root, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symlink unavailable: {type(error).__name__}")
            package_codes = diagnostic_codes(
                self.audit(package_root=linked_root)
            )
            editable_codes = diagnostic_codes(
                self.audit(editable_root=linked_root)
            )
            self.assertIn("PACKAGE_ROOT_SYMLINK", package_codes)
            self.assertIn("EDITABLE_ROOT_SYMLINK", editable_codes)

    def test_nested_package_symlink_is_rejected(self) -> None:
        if not hasattr(Path, "symlink_to"):
            self.skipTest("platform has no symlink support")
        with tempfile.TemporaryDirectory() as temporary:
            container = Path(temporary)
            package_root = container / "package"
            outside_root = container / "outside"
            package_root.mkdir()
            outside_root.mkdir()
            payload = b"outside asset"
            (outside_root / "asset.bin").write_bytes(payload)
            linked = package_root / "linked"
            try:
                linked.symlink_to(outside_root, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symlink unavailable: {type(error).__name__}")
            manifest, inventory = single_file_documents(
                "linked/asset.bin", payload
            )
            report = self.audit(
                manifest,
                inventory,
                package_root=package_root,
            )
            self.assertFalse(report["ok"])
            self.assertIn("PACKAGE_SYMLINK", diagnostic_codes(report))

    def test_regular_file_to_symlink_hash_race_fails_closed(self) -> None:
        if not hasattr(Path, "symlink_to"):
            self.skipTest("platform has no symlink support")
        with tempfile.TemporaryDirectory() as temporary:
            container = Path(temporary)
            package_root = container / "package"
            package_root.mkdir()
            victim = package_root / "asset.bin"
            victim.write_bytes(b"initial package bytes")
            outside = container / "outside.bin"
            outside_payload = b"outside replacement"
            outside.write_bytes(outside_payload)
            manifest, inventory = single_file_documents(
                "asset.bin", outside_payload
            )
            original_open = AUDITOR.os.open
            swapped = False

            def replacing_open(
                path: object,
                flags: int,
                mode: int = 0o777,
                *,
                dir_fd: int | None = None,
            ) -> int:
                nonlocal swapped
                is_anchored_target = (
                    dir_fd is not None and os.fspath(path) == "asset.bin"
                )
                is_fallback_target = (
                    dir_fd is None and Path(path) == victim
                )
                if not swapped and (is_anchored_target or is_fallback_target):
                    swapped = True
                    victim.unlink()
                    try:
                        victim.symlink_to(outside)
                    except OSError as error:
                        self.skipTest(
                            f"symlink unavailable: {type(error).__name__}"
                        )
                if dir_fd is None:
                    return original_open(path, flags, mode)
                return original_open(path, flags, mode, dir_fd=dir_fd)

            with mock.patch.object(AUDITOR.os, "open", new=replacing_open):
                report = self.audit(
                    manifest,
                    inventory,
                    package_root=package_root,
                )
            self.assertTrue(swapped)
            self.assertFalse(report["ok"], report)
            self.assertTrue(victim.is_symlink())
            self.assertTrue(
                {
                    "PACKAGE_FILE_SYMLINK",
                    "PACKAGE_FILE_CHANGED",
                }
                & set(diagnostic_codes(report))
            )

    def test_path_identity_fallback_rejects_regular_file_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            victim = package_root / "asset.bin"
            victim.write_bytes(b"initial package bytes")
            replacement = package_root / "replacement.bin"
            replacement_payload = b"replacement bytes"
            replacement.write_bytes(replacement_payload)
            manifest, inventory = single_file_documents(
                "asset.bin", replacement_payload
            )
            original_open = AUDITOR.os.open
            swapped = False

            def replacing_open(
                path: object,
                flags: int,
                mode: int = 0o777,
                *,
                dir_fd: int | None = None,
            ) -> int:
                nonlocal swapped
                if not swapped and dir_fd is None and Path(path) == victim:
                    swapped = True
                    replacement.replace(victim)
                if dir_fd is None:
                    return original_open(path, flags, mode)
                return original_open(path, flags, mode, dir_fd=dir_fd)

            with (
                mock.patch.object(
                    AUDITOR,
                    "_supports_anchored_no_follow_open",
                    return_value=False,
                ),
                mock.patch.object(AUDITOR.os, "open", new=replacing_open),
            ):
                report = self.audit(
                    manifest,
                    inventory,
                    package_root=package_root,
                )
            self.assertTrue(swapped)
            self.assertFalse(report["ok"], report)
            self.assertIn("PACKAGE_FILE_CHANGED", diagnostic_codes(report))

    def test_path_identity_fallback_accepts_stable_regular_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            payload = b"stable fallback bytes"
            (package_root / "asset.bin").write_bytes(payload)
            manifest, inventory = single_file_documents(
                "asset.bin", payload
            )
            with mock.patch.object(
                AUDITOR,
                "_supports_anchored_no_follow_open",
                return_value=False,
            ):
                report = self.audit(
                    manifest,
                    inventory,
                    package_root=package_root,
                )
            self.assertTrue(report["ok"], report)

    def test_package_rescan_detects_late_untracked_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            tracked = package_root / "tracked/asset.bin"
            tracked.parent.mkdir()
            payload = b"tracked bytes"
            tracked.write_bytes(payload)
            unrelated = package_root / "unrelated"
            unrelated.mkdir()
            manifest, inventory = single_file_documents(
                "tracked/asset.bin", payload
            )
            original_read = AUDITOR.os.read
            injected = False

            def injecting_read(descriptor: int, size: int) -> bytes:
                nonlocal injected
                if not injected:
                    injected = True
                    (unrelated / "late.bin").write_bytes(b"late")
                return original_read(descriptor, size)

            with mock.patch.object(AUDITOR.os, "read", new=injecting_read):
                report = self.audit(
                    manifest,
                    inventory,
                    package_root=package_root,
                )
            self.assertTrue(injected)
            self.assertFalse(report["ok"], report)
            self.assertIn(
                "PACKAGE_SNAPSHOT_CHANGED", diagnostic_codes(report)
            )

    def test_entry_limit_is_fail_closed(self) -> None:
        report = self.audit(max_entries=1)
        self.assertFalse(report["ok"])
        self.assertEqual(
            diagnostic_codes(report).count("ENTRY_LIMIT_EXCEEDED"), 2
        )
        self.assertEqual(report["summary"]["path_matched_files"], 0)
        self.assertEqual(report["summary"]["checksum_matched_files"], 0)

    def test_hard_limits_allow_tightening_but_never_loosening(self) -> None:
        hard_limits = [
            AUDITOR.HARD_MAX_INPUT_BYTES,
            AUDITOR.HARD_MAX_ENTRIES,
            AUDITOR.HARD_MAX_FILESYSTEM_FILES,
            AUDITOR.HARD_MAX_HASHED_FILE_BYTES,
            AUDITOR.HARD_MAX_TOTAL_HASH_BYTES,
        ]
        for hard_limit in hard_limits:
            with self.subTest(hard_limit=hard_limit):
                self.assertEqual(
                    AUDITOR.tightened_limit(hard_limit - 1, hard_limit),
                    hard_limit - 1,
                )
                self.assertEqual(
                    AUDITOR.tightened_limit(hard_limit, hard_limit),
                    hard_limit,
                )
                self.assertEqual(
                    AUDITOR.tightened_limit(SIZE_MAX, hard_limit),
                    hard_limit,
                )
                self.assertEqual(AUDITOR.tightened_limit(0, hard_limit), 0)
                self.assertEqual(
                    AUDITOR.tightened_limit(True, hard_limit), 0
                )

        report = self.audit(
            max_entries=SIZE_MAX,
            max_filesystem_files=SIZE_MAX,
            max_hashed_file_bytes=SIZE_MAX,
            max_total_hash_bytes=SIZE_MAX,
        )
        self.assertTrue(report["ok"], report)

        invalid = self.audit(max_entries=True)
        self.assertFalse(invalid["ok"])
        self.assertIn("LIMIT_INVALID", diagnostic_codes(invalid))

    def test_malformed_records_have_bounded_deterministic_diagnostics(self) -> None:
        malformed_count = 10_000
        manifest = {
            "format": AUDITOR.MANIFEST_FORMAT,
            "spdx_list_version": AUDITOR.SPDX_LIST_VERSION,
            "assets": [{} for _ in range(malformed_count)],
        }
        inventory = {
            "format": AUDITOR.INVENTORY_FORMAT,
            "files": [],
        }
        first = self.audit(manifest, inventory)
        second = self.audit(manifest, inventory)
        self.assertFalse(first["ok"])
        self.assertEqual(first["summary"]["errors"], malformed_count * 18)
        self.assertEqual(canonical_json(first), canonical_json(second))
        self.assertLessEqual(
            len(first["diagnostics"]),
            AUDITOR.HARD_MAX_RETAINED_DIAGNOSTICS,
        )
        self.assertEqual(
            diagnostic_codes(first).count("DIAGNOSTIC_LIMIT_EXCEEDED"), 1
        )
        detail_bytes = sum(
            len(value.encode("utf-8"))
            for item in first["diagnostics"]
            for value in item.values()
        )
        self.assertLessEqual(
            detail_bytes, AUDITOR.HARD_MAX_DIAGNOSTIC_DETAIL_BYTES
        )

    def test_large_unknown_keys_are_bounded_in_report_fields(self) -> None:
        large_key = "K" * (1024 * 1024)
        manifest = {
            "format": AUDITOR.MANIFEST_FORMAT,
            "spdx_list_version": AUDITOR.SPDX_LIST_VERSION,
            "assets": [],
            large_key: 0,
        }
        report = self.audit(manifest, {"format": AUDITOR.INVENTORY_FORMAT, "files": []})
        self.assertFalse(report["ok"])
        self.assertEqual(report["summary"]["errors"], 1)
        self.assertEqual(len(report["diagnostics"]), 1)
        pointer = report["diagnostics"][0]["pointer"]
        self.assertLessEqual(
            len(pointer.encode("utf-8")),
            AUDITOR.HARD_MAX_DIAGNOSTIC_FIELD_BYTES,
        )
        self.assertTrue(pointer.endswith("...[truncated]"))

    def test_diagnostic_merge_sort_and_detail_budget_remain_bounded(
        self,
    ) -> None:
        first = AUDITOR.Diagnostics()
        second = AUDITOR.Diagnostics()
        large_detail = "D" * AUDITOR.HARD_MAX_DIAGNOSTIC_FIELD_BYTES
        for index in range(3_000):
            first.add(
                "LARGE_DETAIL",
                pointer=f"/first/{index}",
                path=large_detail,
                message=large_detail,
            )
            second.add(
                "LARGE_DETAIL",
                pointer=f"/second/{index}",
                path=large_detail,
                message=large_detail,
            )
        first.merge(second)
        retained = first.sorted()
        self.assertEqual(first.total_count, 6_000)
        self.assertLessEqual(
            len(retained), AUDITOR.HARD_MAX_RETAINED_DIAGNOSTICS
        )
        self.assertEqual(
            diagnostic_codes({"diagnostics": retained}).count(
                "DIAGNOSTIC_LIMIT_EXCEEDED"
            ),
            1,
        )
        detail_bytes = sum(
            len(value.encode("utf-8"))
            for item in retained
            for value in item.values()
        )
        self.assertLessEqual(
            detail_bytes, AUDITOR.HARD_MAX_DIAGNOSTIC_DETAIL_BYTES
        )

    def test_release_gate_requires_both_verification_roots(self) -> None:
        report = self.audit(release_gate=True)
        self.assertFalse(report["ok"])
        self.assertIn(
            "RELEASE_PACKAGE_ROOT_REQUIRED", diagnostic_codes(report)
        )
        self.assertIn(
            "RELEASE_EDITABLE_ROOT_REQUIRED", diagnostic_codes(report)
        )

    def test_input_growth_is_bounded_before_decode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            input_path = Path(temporary) / "input.json"
            input_path.write_bytes(b"{}")
            original_fstat = AUDITOR.os.fstat
            grew = False

            def growing_fstat(descriptor: int) -> os.stat_result:
                nonlocal grew
                info = original_fstat(descriptor)
                if not grew:
                    grew = True
                    with input_path.open("ab") as stream:
                        stream.write(b"x" * 128)
                return info

            with mock.patch.object(
                AUDITOR.os, "fstat", side_effect=growing_fstat
            ):
                with self.assertRaises(AUDITOR.InputFailure) as caught:
                    AUDITOR.read_json(
                        input_path,
                        label="manifest",
                        max_bytes=16,
                    )
            self.assertTrue(grew)
            self.assertEqual(caught.exception.code, "INPUT_TOO_LARGE")

    def test_input_reader_never_requests_beyond_the_remaining_bound(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            input_path = Path(temporary) / "input.json"
            input_path.write_bytes(b"{}")
            original_open = Path.open
            requests: list[int] = []

            class RecordingStream:
                def __init__(self, stream: object) -> None:
                    self.stream = stream

                def __enter__(self) -> "RecordingStream":
                    self.stream.__enter__()
                    return self

                def __exit__(self, *arguments: object) -> object:
                    return self.stream.__exit__(*arguments)

                def fileno(self) -> int:
                    return self.stream.fileno()

                def read(self, size: int = -1) -> bytes:
                    requests.append(size)
                    return self.stream.read(size)

            def recording_open(
                path: Path, *arguments: object, **keywords: object
            ) -> RecordingStream:
                return RecordingStream(
                    original_open(path, *arguments, **keywords)
                )

            with mock.patch.object(Path, "open", new=recording_open):
                parsed = AUDITOR.read_json(
                    input_path,
                    label="manifest",
                    max_bytes=4,
                )
            self.assertEqual(parsed, {})
            self.assertTrue(requests)
            self.assertTrue(all(1 <= request <= 5 for request in requests))

    def test_manifest_schema_tracks_runtime_security_constraints(self) -> None:
        schema = json.loads(
            (
                REPOSITORY_ROOT
                / "tools/content_provenance_manifest.schema.json"
            ).read_text(encoding="utf-8")
        )
        assets_schema = schema["properties"]["assets"]
        self.assertEqual(
            assets_schema["maxItems"], AUDITOR.HARD_MAX_ENTRIES
        )
        classification_description = assets_schema["items"]["properties"][
            "classification"
        ]["description"]
        self.assertIn("Archive-looking", classification_description)
        self.assertIn("cache-looking", classification_description)

        revision_pattern = assets_schema["items"][
            "properties"
        ]["source"]["properties"]["revision"]["pattern"]
        self.assertIsNotNone(
            re.fullmatch(
                revision_pattern,
                "0123456789abcdef0123456789abcdef01234567",
            )
        )
        self.assertIsNone(re.fullmatch(revision_pattern, "refs/heads/main"))

        safe_path = schema["$defs"]["safePath"]
        safe_path_pattern = re.compile(safe_path["pattern"])
        for invalid_path in [
            "../escape",
            "content//asset.bin",
            "content/ leading.bin",
            "content/directory./asset.bin",
            "content/bad?.bin",
            "content/trailing/",
        ]:
            with self.subTest(path=invalid_path):
                self.assertIsNone(safe_path_pattern.fullmatch(invalid_path))
                self.assertIsNotNone(AUDITOR.path_problem(invalid_path))
        self.assertIn("255 UTF-8 bytes", safe_path["description"])

        uri_pattern = assets_schema["items"]["properties"][
            "source"
        ]["properties"]["uri"]["pattern"]
        self.assertIsNone(re.match(uri_pattern, "HTTPS://example.invalid"))
        diagnostics = AUDITOR.Diagnostics()
        self.assertIsNone(
            AUDITOR.checked_https_uri(
                "HTTPS://example.invalid",
                pointer="/uri",
                diagnostics=diagnostics,
            )
        )

    def test_cli_emits_canonical_json_and_stable_exit_codes(self) -> None:
        command = [
            sys.executable,
            str(TOOL_PATH),
            "--manifest",
            str(FIXTURE_ROOT / "valid.manifest.json"),
            "--inventory",
            str(FIXTURE_ROOT / "valid.inventory.json"),
        ]
        first = subprocess.run(command, check=False, capture_output=True, text=True)
        second = subprocess.run(command, check=False, capture_output=True, text=True)
        self.assertEqual(first.returncode, 0)
        self.assertEqual(first.stderr, "")
        self.assertEqual(first.stdout, second.stdout)
        self.assertTrue(json.loads(first.stdout)["ok"])

    def test_cli_exact_hard_limits_and_size_max_are_equivalent(self) -> None:
        command = [
            sys.executable,
            str(TOOL_PATH),
            "--manifest",
            str(FIXTURE_ROOT / "valid.manifest.json"),
            "--inventory",
            str(FIXTURE_ROOT / "valid.inventory.json"),
        ]
        hard_limit_arguments = [
            "--max-input-bytes",
            str(AUDITOR.HARD_MAX_INPUT_BYTES),
            "--max-entries",
            str(AUDITOR.HARD_MAX_ENTRIES),
            "--max-filesystem-files",
            str(AUDITOR.HARD_MAX_FILESYSTEM_FILES),
            "--max-hashed-file-bytes",
            str(AUDITOR.HARD_MAX_HASHED_FILE_BYTES),
            "--max-total-hash-bytes",
            str(AUDITOR.HARD_MAX_TOTAL_HASH_BYTES),
        ]
        size_max_arguments = [
            argument if argument.startswith("--") else str(SIZE_MAX)
            for argument in hard_limit_arguments
        ]
        at_boundary = subprocess.run(
            command + hard_limit_arguments,
            check=False,
            capture_output=True,
            text=True,
        )
        above_boundary = subprocess.run(
            command + size_max_arguments,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(at_boundary.returncode, 0)
        self.assertEqual(above_boundary.returncode, 0)
        self.assertEqual(at_boundary.stderr, "")
        self.assertEqual(above_boundary.stderr, "")
        self.assertEqual(at_boundary.stdout, above_boundary.stdout)

    def test_cli_size_max_cannot_raise_input_byte_ceiling(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            oversized_manifest = Path(temporary) / "oversized.json"
            with oversized_manifest.open("wb") as stream:
                stream.truncate(AUDITOR.HARD_MAX_INPUT_BYTES + 1)
            result = subprocess.run(
                [
                    sys.executable,
                    str(TOOL_PATH),
                    "--manifest",
                    str(oversized_manifest),
                    "--inventory",
                    str(FIXTURE_ROOT / "valid.inventory.json"),
                    "--max-input-bytes",
                    str(SIZE_MAX),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stderr, "")
        report = json.loads(result.stdout)
        self.assertEqual(diagnostic_codes(report), ["INPUT_TOO_LARGE"])

    def test_cli_bounds_large_keys_and_ten_thousand_malformed_records(
        self,
    ) -> None:
        manifest = {
            "format": AUDITOR.MANIFEST_FORMAT,
            "spdx_list_version": AUDITOR.SPDX_LIST_VERSION,
            "assets": [{} for _ in range(10_000)],
            "K" * (1024 * 1024): 0,
        }
        inventory = {
            "format": AUDITOR.INVENTORY_FORMAT,
            "files": [],
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest_path = root / "manifest.json"
            inventory_path = root / "inventory.json"
            manifest_path.write_text(
                json.dumps(manifest, separators=(",", ":")),
                encoding="utf-8",
            )
            inventory_path.write_text(
                json.dumps(inventory, separators=(",", ":")),
                encoding="utf-8",
            )
            command = [
                sys.executable,
                str(TOOL_PATH),
                "--manifest",
                str(manifest_path),
                "--inventory",
                str(inventory_path),
                "--max-input-bytes",
                str(SIZE_MAX),
                "--max-entries",
                str(SIZE_MAX),
            ]
            first = subprocess.run(
                command, check=False, capture_output=True, text=True
            )
            second = subprocess.run(
                command, check=False, capture_output=True, text=True
            )
        self.assertEqual(first.returncode, 1)
        self.assertEqual(second.returncode, 1)
        self.assertEqual(first.stderr, "")
        self.assertEqual(second.stderr, "")
        self.assertEqual(first.stdout, second.stdout)
        report = json.loads(first.stdout)
        self.assertEqual(report["summary"]["errors"], 180_001)
        self.assertLessEqual(
            len(report["diagnostics"]),
            AUDITOR.HARD_MAX_RETAINED_DIAGNOSTICS,
        )
        self.assertEqual(
            diagnostic_codes(report).count("DIAGNOSTIC_LIMIT_EXCEEDED"), 1
        )
        for item in report["diagnostics"]:
            for value in item.values():
                self.assertLessEqual(
                    len(value.encode("utf-8")),
                    AUDITOR.HARD_MAX_DIAGNOSTIC_FIELD_BYTES,
                )

    def test_cli_release_gate_requires_both_roots(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                str(TOOL_PATH),
                "--manifest",
                str(FIXTURE_ROOT / "valid.manifest.json"),
                "--inventory",
                str(FIXTURE_ROOT / "valid.inventory.json"),
                "--release-gate",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, "")
        report = json.loads(result.stdout)
        self.assertIn(
            "RELEASE_PACKAGE_ROOT_REQUIRED", diagnostic_codes(report)
        )
        self.assertIn(
            "RELEASE_EDITABLE_ROOT_REQUIRED", diagnostic_codes(report)
        )

    def test_cli_rejects_duplicate_json_keys_without_traceback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            bad_manifest = Path(temporary) / "manifest.json"
            bad_manifest.write_text(
                '{"format":"a","format":"b","assets":[]}',
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(TOOL_PATH),
                    "--manifest",
                    str(bad_manifest),
                    "--inventory",
                    str(FIXTURE_ROOT / "valid.inventory.json"),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stderr, "")
        report = json.loads(result.stdout)
        self.assertEqual(
            diagnostic_codes(report),
            ["JSON_DUPLICATE_KEY"],
        )


if __name__ == "__main__":
    unittest.main()
