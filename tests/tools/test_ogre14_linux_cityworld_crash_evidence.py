#!/usr/bin/env python3
"""Tests for OGRE 14 Linux CityWorld crash evidence collection."""

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
    / "collect_linux_cityworld_crash_evidence.py"
)
SPEC = importlib.util.spec_from_file_location(
    "collect_linux_cityworld_crash_evidence",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load Linux crash evidence collector")
COLLECTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COLLECTOR)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class LinuxCityWorldCrashEvidenceTests(unittest.TestCase):
    def fixture(
        self,
        root: Path,
        returncode: int | None,
    ) -> tuple[Path, Path, Path, Path, Path]:
        executable = root / "runtime" / "RoR-Ogre14"
        diagnostic = root / "scene" / "diagnostics" / "runtime-process.json"
        core_dir = root / "evidence" / "cores" / "sync"
        backtrace = root / "evidence" / "backtraces" / "sync.txt"
        output = root / "evidence" / "sync-cores.json"
        executable.parent.mkdir(parents=True)
        diagnostic.parent.mkdir(parents=True)
        core_dir.mkdir(parents=True)
        executable.write_bytes(b"exact Linux RoR executable")
        if returncode is not None:
            termination: dict[str, object] = {
                "kind": "signal" if returncode < 0 else "success",
                "returncode": returncode,
            }
            if returncode < 0:
                termination["signal"] = -returncode
            diagnostic.write_text(
                json.dumps(
                    {
                        "format": "ror-cityworld-runtime-process-diagnostic-v1",
                        "target_platform": "linux",
                        "termination": termination,
                    }
                ),
                encoding="utf-8",
            )
        return executable, diagnostic, core_dir, backtrace, output

    def collect(
        self,
        root: Path,
        returncode: int | None,
        *,
        driver_exit_code: int,
    ) -> tuple[dict[str, object], Path]:
        executable, diagnostic, core_dir, backtrace, output = self.fixture(
            root, returncode
        )
        document = COLLECTOR.collect(
            mode="sync",
            driver_exit_code=driver_exit_code,
            process_diagnostic=diagnostic,
            core_dir=core_dir,
            executable=executable,
            backtrace=backtrace,
            output=output,
        )
        return document, output

    def test_success_hashes_binary_and_requires_no_crash_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            document, output = self.collect(
                Path(directory), 0, driver_exit_code=0
            )
            self.assertEqual(document["format"], COLLECTOR.FORMAT)
            self.assertEqual(document["mode"], "sync")
            self.assertEqual(document["native_exit"], {
                "kind": "success", "returncode": 0
            })
            self.assertEqual(document["core_capture"], {
                "required": False, "status": "not_required"
            })
            self.assertEqual(document["backtrace_capture"], {
                "required": False, "status": "not_required"
            })
            self.assertEqual(
                document["binaries"]["executable"]["sha256"],
                sha256(b"exact Linux RoR executable"),
            )
            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8")), document
            )

    def test_signal_requires_exact_core_and_backtrace(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable, diagnostic, core_dir, backtrace, output = self.fixture(
                root, -11
            )
            core = core_dir / "core.RoR-Ogre14.4242"
            core.write_bytes(b"exact ELF core")
            backtrace.parent.mkdir(parents=True)
            backtrace.write_bytes(b"thread apply all bt full\n#0 crash\n")

            document = COLLECTOR.collect(
                mode="sync",
                driver_exit_code=1,
                process_diagnostic=diagnostic,
                core_dir=core_dir,
                executable=executable,
                backtrace=backtrace,
                output=output,
            )
            self.assertEqual(document["native_exit"], {
                "kind": "signal", "returncode": -11, "signal": 11
            })
            self.assertEqual(document["core_capture"], {
                "required": True, "status": "captured"
            })
            self.assertEqual(document["backtrace_capture"], {
                "required": True, "status": "captured"
            })
            self.assertEqual(document["cores"][0]["sha256"], sha256(b"exact ELF core"))
            self.assertEqual(
                document["backtrace"]["sha256"],
                sha256(b"thread apply all bt full\n#0 crash\n"),
            )

    def test_missing_core_writes_manifest_then_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable, diagnostic, core_dir, backtrace, output = self.fixture(
                root, -11
            )
            with self.assertRaisesRegex(
                COLLECTOR.EvidenceFailure,
                "requires exactly one nonempty core",
            ):
                COLLECTOR.collect(
                    mode="sync",
                    driver_exit_code=1,
                    process_diagnostic=diagnostic,
                    core_dir=core_dir,
                    executable=executable,
                    backtrace=backtrace,
                    output=output,
                )
            document = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(document["core_capture"]["status"], "required_missing")
            self.assertEqual(
                document["backtrace_capture"]["status"],
                "required_missing",
            )

    def test_preflight_failure_records_no_native_exit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            document, _output = self.collect(
                Path(directory), None, driver_exit_code=1
            )
            self.assertIsNone(document["native_exit"])
            self.assertIsNone(document["process_diagnostic"])

    def test_driver_and_native_success_must_agree(self) -> None:
        for returncode, driver_exit_code in ((0, 1), (-11, 0), (None, 0)):
            with self.subTest(
                returncode=returncode,
                driver_exit_code=driver_exit_code,
            ):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    executable, diagnostic, core_dir, backtrace, output = (
                        self.fixture(root, returncode)
                    )
                    with self.assertRaisesRegex(
                        COLLECTOR.EvidenceFailure,
                        "driver|diagnostic",
                    ):
                        COLLECTOR.collect(
                            mode="sync",
                            driver_exit_code=driver_exit_code,
                            process_diagnostic=diagnostic,
                            core_dir=core_dir,
                            executable=executable,
                            backtrace=backtrace,
                            output=output,
                        )


if __name__ == "__main__":
    unittest.main()
