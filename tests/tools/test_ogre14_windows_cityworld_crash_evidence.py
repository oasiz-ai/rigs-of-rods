#!/usr/bin/env python3
"""Tests for OGRE 14 Windows CityWorld crash evidence collection."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = (
    REPOSITORY_ROOT
    / "tools"
    / "collect_windows_cityworld_crash_evidence.py"
)
SPEC = importlib.util.spec_from_file_location(
    "collect_windows_cityworld_crash_evidence",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load Windows crash evidence collector")
COLLECTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COLLECTOR)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class WindowsCityWorldCrashEvidenceTests(unittest.TestCase):
    def fixture(
        self,
        root: Path,
        returncode: int | None,
    ) -> tuple[Path, Path, Path, Path, Path]:
        executable = root / "runtime" / "RoR-Ogre14.exe"
        pdb = root / "evidence" / "symbols" / "RoR-Ogre14.pdb"
        diagnostic = root / "scene" / "diagnostics" / "runtime-process.json"
        dump_dir = root / "evidence" / "dumps" / "async"
        output = root / "evidence" / "async-dumps.json"
        executable.parent.mkdir(parents=True)
        pdb.parent.mkdir(parents=True)
        diagnostic.parent.mkdir(parents=True)
        dump_dir.mkdir(parents=True)
        executable.write_bytes(b"exact RoR executable")
        pdb.write_bytes(b"exact RoR program database")
        if returncode is not None:
            unsigned = returncode & 0xFFFFFFFF
            termination: dict[str, object] = {
                "kind": (
                    "windows_ntstatus"
                    if unsigned >= 0x80000000
                    else "success"
                ),
                "returncode": returncode,
            }
            if unsigned >= 0x80000000:
                termination.update(
                    {
                        "meaning": "access_violation",
                        "ntstatus_hex": f"0x{unsigned:08X}",
                        "unsigned_returncode": unsigned,
                    }
                )
            diagnostic.write_text(
                json.dumps(
                    {
                        "format": "ror-cityworld-runtime-process-diagnostic-v1",
                        "termination": termination,
                    }
                ),
                encoding="utf-8",
            )
        return executable, pdb, diagnostic, dump_dir, output

    def collect(
        self,
        root: Path,
        returncode: int | None,
        *,
        driver_exit_code: int,
    ) -> tuple[dict[str, object], Path]:
        executable, pdb, diagnostic, dump_dir, output = self.fixture(
            root,
            returncode,
        )
        document = COLLECTOR.collect(
            mode="async",
            driver_exit_code=driver_exit_code,
            process_diagnostic=diagnostic,
            dump_dir=dump_dir,
            executable=executable,
            pdb=pdb,
            output=output,
            poll_attempts=1,
            poll_interval_ms=0,
        )
        return document, output

    def test_success_records_signed_exit_and_exact_symbols_without_dump(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document, output = self.collect(
                root,
                0,
                driver_exit_code=0,
            )

            self.assertEqual(document["format"], COLLECTOR.FORMAT)
            self.assertEqual(document["mode"], "async")
            self.assertEqual(document["driver_exit_code"], 0)
            self.assertEqual(
                document["native_exit_code"],
                {
                    "hex": "0x00000000",
                    "kind": "success",
                    "raw": 0,
                    "signed": 0,
                    "unsigned": 0,
                },
            )
            self.assertEqual(
                document["dump_capture"],
                {"required": False, "status": "not_required"},
            )
            self.assertEqual(document["dumps"], [])
            self.assertEqual(
                document["binaries"]["executable"]["sha256"],
                sha256(b"exact RoR executable"),
            )
            self.assertEqual(
                document["binaries"]["executable"]["artifact"],
                "runtime/RoR-Ogre14.exe",
            )
            self.assertEqual(
                document["binaries"]["pdb"]["sha256"],
                sha256(b"exact RoR program database"),
            )
            self.assertEqual(
                document["binaries"]["pdb"]["artifact"],
                "symbols/RoR-Ogre14.pdb",
            )
            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8")),
                document,
            )
            self.assertFalse(output.with_name(output.name + ".tmp").exists())

    def test_access_violation_requires_and_hashes_nonempty_dump(self) -> None:
        for returncode in (0xC0000005, -1073741819):
            with self.subTest(returncode=returncode):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    executable, pdb, diagnostic, dump_dir, output = (
                        self.fixture(root, returncode)
                    )
                    dump = dump_dir / "RoR-Ogre14.exe.4242.dmp"
                    dump.write_bytes(b"full WER minidump")

                    document = COLLECTOR.collect(
                        mode="async",
                        driver_exit_code=1,
                        process_diagnostic=diagnostic,
                        dump_dir=dump_dir,
                        executable=executable,
                        pdb=pdb,
                        output=output,
                        poll_attempts=1,
                        poll_interval_ms=0,
                    )

                    self.assertEqual(
                        document["native_exit_code"]["hex"],
                        "0xC0000005",
                    )
                    self.assertEqual(
                        document["native_exit_code"]["signed"],
                        -1073741819,
                    )
                    self.assertEqual(
                        document["native_exit_code"]["unsigned"],
                        0xC0000005,
                    )
                    self.assertEqual(
                        document["dump_capture"],
                        {"required": True, "status": "captured"},
                    )
                    self.assertEqual(
                        document["dumps"],
                        [
                            {
                                "artifact": "dumps/async/RoR-Ogre14.exe.4242.dmp",
                                "bytes": len(b"full WER minidump"),
                                "sha256": sha256(b"full WER minidump"),
                            }
                        ],
                    )

    def test_missing_access_violation_dump_writes_manifest_then_fails(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable, pdb, diagnostic, dump_dir, output = self.fixture(
                root,
                0xC0000005,
            )
            (dump_dir / "empty.dmp").write_bytes(b"")

            with self.assertRaisesRegex(
                COLLECTOR.EvidenceFailure,
                r"WER evidence error.*0xC0000005.*no nonempty \.dmp",
            ):
                COLLECTOR.collect(
                    mode="async",
                    driver_exit_code=1,
                    process_diagnostic=diagnostic,
                    dump_dir=dump_dir,
                    executable=executable,
                    pdb=pdb,
                    output=output,
                    poll_attempts=1,
                    poll_interval_ms=0,
                )

            document = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                document["dump_capture"],
                {"required": True, "status": "required_missing"},
            )
            self.assertEqual(document["dumps"], [])
            self.assertEqual(
                document["native_exit_code"]["signed"],
                -1073741819,
            )

    def test_driver_preflight_failure_still_writes_no_crash_manifest(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document, output = self.collect(
                root,
                None,
                driver_exit_code=1,
            )
            self.assertIsNone(document["native_exit_code"])
            self.assertIsNone(document["process_diagnostic"])
            self.assertEqual(
                document["dump_capture"],
                {"required": False, "status": "not_required"},
            )
            self.assertTrue(output.is_file())


if __name__ == "__main__":
    unittest.main()
