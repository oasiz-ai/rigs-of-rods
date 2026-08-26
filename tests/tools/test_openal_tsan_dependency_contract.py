#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_PATH = ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
CMAKE_PATH = ROOT / "CMakeLists.txt"
RECIPE_PATH = ROOT / "cmake/conan/recipes/openal-soft/conanfile.py"
RECIPE_DATA_PATH = ROOT / "cmake/conan/recipes/openal-soft/conandata.yml"
TSAN_LOCK_PATH = (
    ROOT / "cmake/conan/locks/ror-ogre14-linux-x86_64-tsan.lock"
)
NORMAL_LOCK_PATHS = (
    ROOT / "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
    ROOT / "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
    ROOT / "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
)
AUDIT_PATH = ROOT / "tools/audit_openal_tsan_package.py"

AUDIT_SPEC = importlib.util.spec_from_file_location(
    "audit_openal_tsan_package", AUDIT_PATH
)
if AUDIT_SPEC is None or AUDIT_SPEC.loader is None:
    raise RuntimeError("could not load OpenAL TSan package auditor")
AUDITOR = importlib.util.module_from_spec(AUDIT_SPEC)
AUDIT_SPEC.loader.exec_module(AUDITOR)

UPSTREAM_RECIPE_REVISION = "0f2e117218b8294276a3c56fa57d3bec"
TSAN_RECIPE_REVISION = "47d7f9d8acb249fbdab9d93428361ce0"
SOURCE_SHA256 = (
    "cb5e6197a1c0da0edcf2a81024953cc8fa8545c3b9474e48c852af709d587892"
)
PRODUCTION_PACKAGE_ID = "7b08ab0814dd037bac5a06c5ba689c48d12f5422"


class OpenALTsanDependencyContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        cls.cmake = CMAKE_PATH.read_text(encoding="utf-8")
        cls.recipe = RECIPE_PATH.read_text(encoding="utf-8")
        cls.recipe_data = RECIPE_DATA_PATH.read_text(encoding="utf-8")
        cls.tsan_lock = json.loads(TSAN_LOCK_PATH.read_text(encoding="utf-8"))
        cls.normal_locks = tuple(
            json.loads(path.read_text(encoding="utf-8"))
            for path in NORMAL_LOCK_PATHS
        )
        cls.audit = AUDIT_PATH.read_text(encoding="utf-8")

    def _audio_validator_blocks(self) -> tuple[str, ...]:
        blocks = re.findall(
            r"<<'PY' \|\| audio_status=\$\?\n(?P<body>.*?)\n          PY",
            self.workflow,
            re.DOTALL,
        )
        self.assertEqual(len(blocks), 2)
        return tuple(textwrap.dedent(block) for block in blocks)

    def _run_audio_validator(
        self,
        source: str,
        log: str,
        openal_log: str,
        *,
        symlink_openal_log: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            log_path = root / "RoR.log"
            openal_log_path = root / "openal-soft.log"
            dependency = root / "dependency.json"
            receipt = root / "audio.json"
            log_path.write_text(log, encoding="utf-8")
            if symlink_openal_log:
                target = root / "openal-soft-target.log"
                target.write_text(openal_log, encoding="utf-8")
                openal_log_path.symlink_to(target)
            else:
                openal_log_path.write_text(openal_log, encoding="utf-8")
            dependency.write_text(
                json.dumps(
                    {"schema": "ror.openal-soft-tsan-package-audit@1"}
                )
                + "\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    "-",
                    str(log_path),
                    str(openal_log_path),
                    str(dependency),
                    str(receipt),
                ],
                input=source,
                check=False,
                capture_output=True,
                text=True,
            )
            if result.returncode == 0:
                document = json.loads(receipt.read_text(encoding="utf-8"))
                self.assertEqual(
                    document["format"],
                    "ror-openal-tsan-audio-runtime-initialization-v1",
                )
                self.assertEqual(document["backend"], "null")
                self.assertEqual(
                    document["evidence_scope"],
                    "runtime-initialization-and-null-backend-device-creation",
                )
                self.assertIs(document["null_backend_added"], True)
                self.assertIs(document["null_device_created"], True)
                self.assertEqual(document["hardware_sources"], 32)
                self.assertEqual(document["default_wav"], "default_break.wav")
                self.assertRegex(document["openal_log_sha256"], r"^[0-9a-f]{64}$")
            else:
                self.assertFalse(receipt.exists())
            return result

    def test_workflow_triggers_on_every_tsan_dependency_contract_byte(self) -> None:
        for path in (
            "CMakeLists.txt",
            "cmake/conan/locks/ror-ogre14-linux-x86_64-tsan.lock",
            "cmake/conan/recipes/openal-soft/**",
            "tools/audit_openal_tsan_package.py",
            "tests/tools/test_openal_tsan_dependency_contract.py",
        ):
            with self.subTest(path=path):
                self.assertIn(f"- {path}", self.workflow)

        self.assertIn(
            "ogre-next-combined-linux-gcc11-tsan-openal-source-v1-"
            "conan-2.31.1-",
            self.workflow,
        )
        self.assertNotIn(
            "key: ogre-next-combined-linux-gcc11-release-conan-",
            self.workflow,
        )
        self.assertIn("'cmake/conan/recipes/**'", self.workflow)
        self.assertIn("'cmake/conan/locks/*.lock'", self.workflow)

    def test_only_explicit_tsan_mode_selects_the_local_recipe_and_lock(self) -> None:
        self.assertIn("option(\n    ROR_TSAN_INSTRUMENT_OPENAL", self.cmake)
        self.assertIn(
            '"cmake/conan/locks/ror-ogre14-linux-x86_64-tsan.lock"',
            self.cmake,
        )
        self.assertIn("list(APPEND _ror_local_recipes openal-soft)", self.cmake)
        self.assertIn(
            '"-o=openal-soft/*:thread_sanitizer=True"', self.cmake
        )
        self.assertIn(
            "ROR_TSAN_INSTRUMENT_OPENAL is restricted to the reviewed Linux",
            self.cmake,
        )
        self.assertIn(
            "ROR_TSAN_INSTRUMENT_OPENAL requires ThreadSanitizer", self.cmake
        )
        self.assertIn(
            "if (ROR_TSAN_INSTRUMENT_OPENAL AND NOT ROR_USE_OPENAL)",
            self.cmake,
        )

        self.assertIn(
            "-DROR_TSAN_INSTRUMENT_OPENAL=ON", self.workflow
        )
        self.assertEqual(
            self.workflow.count("-DROR_TSAN_INSTRUMENT_OPENAL=ON"), 1
        )
        self.assertNotIn("-DROR_USE_OPENAL=OFF", self.workflow)

    def test_recipe_is_exact_source_pinned_and_package_id_distinct(self) -> None:
        for token in (
            'name = "openal-soft"',
            '"thread_sanitizer": [True, False]',
            '"thread_sanitizer": True',
            '"-fsanitize=thread"',
            '"-fno-omit-frame-pointer"',
            '"-fno-optimize-sibling-calls"',
            "tc.extra_cflags.extend(sanitizer_flags)",
            "tc.extra_cxxflags.extend(sanitizer_flags)",
            "The pinned OpenAL ThreadSanitizer package is Linux x86_64 only",
            "The pinned OpenAL ThreadSanitizer package requires GCC",
            "The pinned OpenAL ThreadSanitizer package must remain static",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.recipe)
        self.assertNotIn("ignore_noninstrumented_modules", self.recipe)
        self.assertNotIn("suppressions", self.recipe)
        self.assertNotIn("-fno-sanitize=thread", self.recipe)
        self.assertIn(SOURCE_SHA256, self.recipe_data)

        tsan_requires = self.tsan_lock.get("requires", [])
        self.assertEqual(
            [ref for ref in tsan_requires if ref.startswith("openal-soft/")],
            [f"openal-soft/1.24.3#{TSAN_RECIPE_REVISION}"],
        )
        for lock in self.normal_locks:
            openal = [
                ref
                for ref in lock.get("requires", [])
                if ref.startswith("openal-soft/")
            ]
            self.assertEqual(len(openal), 1)
            self.assertIn(
                f"openal-soft/1.24.3#{UPSTREAM_RECIPE_REVISION}", openal[0]
            )
            self.assertNotIn(TSAN_RECIPE_REVISION, openal[0])

    def test_package_audit_is_fail_closed_before_any_runtime_execution(self) -> None:
        contract = "python tests/tools/test_openal_tsan_dependency_contract.py"
        optimized_contract = (
            "python -O tests/tools/test_openal_tsan_dependency_contract.py"
        )
        audit = "python3 tools/audit_openal_tsan_package.py"
        scene = (
            "Render packaged Simple2 and semi through four Ogre-Next workers "
            "under ThreadSanitizer"
        )
        for token in (
            contract,
            optimized_contract,
            audit,
            '--conan-home "$CONAN_HOME"',
            '--build-dir "$ROR_TSAN_BUILD_DIR"',
            '--artifact-dir "$ROR_TSAN_ARTIFACTS_DIR/openal-soft-tsan"',
            'test -s "$ROR_TSAN_ARTIFACTS_DIR/openal-soft-tsan/receipt.json"',
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.workflow)
        self.assertLess(self.workflow.index(contract), self.workflow.index(audit))
        self.assertLess(self.workflow.index(audit), self.workflow.index(scene))

        for token in (
            TSAN_RECIPE_REVISION,
            SOURCE_SHA256,
            PRODUCTION_PACKAGE_ID,
            "libopenal.a",
            "__tsan_func_entry",
            "__tsan_atomic",
            "--build-dir",
            "OpenAL-release-x86_64-data.cmake",
            "RoR-Combined.ninja-commands.txt",
            "receipt.json",
        ):
            with self.subTest(audit_token=token):
                self.assertIn(token, self.audit)

    def test_split_conan_queries_preserve_revision_and_package_info(self) -> None:
        package_revision = "ab" * 16

        def document(package: dict[str, object]) -> dict[str, object]:
            return {
                "Local Cache": {
                    f"{AUDITOR.PACKAGE_NAME}/{AUDITOR.PACKAGE_VERSION}": {
                        "revisions": {
                            AUDITOR.RECIPE_REVISION: {
                                "packages": {AUDITOR.PACKAGE_ID: package}
                            }
                        }
                    }
                }
            }

        revisions_document = document(
            {"revisions": {package_revision: {"timestamp": 1}}}
        )
        info_document = document(
            {
                "info": {
                    "settings": dict(AUDITOR.EXPECTED_SETTINGS),
                    "options": dict(AUDITOR.EXPECTED_OPTIONS),
                }
            }
        )

        self.assertEqual(
            AUDITOR._parse_conan_package_revision(revisions_document),
            package_revision,
        )
        info = AUDITOR._parse_conan_package_info(info_document)
        AUDITOR._validate_package_info(info)
        self.assertEqual(info["settings"], AUDITOR.EXPECTED_SETTINGS)
        self.assertEqual(info["options"], AUDITOR.EXPECTED_OPTIONS)

        with self.assertRaisesRegex(
            AUDITOR.AuditError, "expected exactly one OpenAL package revision"
        ):
            AUDITOR._parse_conan_package_revision(info_document)
        with self.assertRaisesRegex(
            AUDITOR.AuditError, "omitted settings/options information"
        ):
            AUDITOR._parse_conan_package_info(revisions_document)

    def test_duplicate_archive_members_use_exact_selector_and_fail_closed(
        self,
    ) -> None:
        members = AUDITOR._parse_members(
            "null.cpp.o\nnull.cpp.o\ncontext.cpp.o\n"
        )
        self.assertEqual(
            members, ["null.cpp.o", "null.cpp.o", "context.cpp.o"]
        )

        def run_selector(
            defined_symbols: tuple[str, str],
            undefined_symbols: tuple[tuple[str, ...], tuple[str, ...]],
        ) -> dict[str, object]:
            def fake_run(
                command: tuple[str, ...] | list[str],
                *,
                environment: dict[str, str],
                cwd: Path | None = None,
            ) -> str:
                del environment
                arguments = list(command)
                if arguments[:2] == ["ar", "xN"]:
                    self.assertIsNotNone(cwd)
                    if cwd is None:
                        raise AssertionError("GNU ar extraction omitted cwd")
                    (cwd / arguments[-1]).write_bytes(b"object")
                    return ""
                object_path = Path(arguments[-1])
                ordinal = int(object_path.parent.name) - 1
                if "--demangle" in arguments:
                    return (
                        f"{object_path}:00000000 T "
                        f"{defined_symbols[ordinal]}\n"
                    )
                if "--undefined-only" in arguments:
                    return "".join(
                        f"{object_path}:                 U {symbol}\n"
                        for symbol in undefined_symbols[ordinal]
                    )
                raise AssertionError(f"unexpected command: {arguments!r}")

            with mock.patch.object(AUDITOR, "_run_checked", fake_run):
                return AUDITOR._audit_selected_object(
                    "ar",
                    "nm",
                    Path("/tmp/libopenal.a"),
                    ("null.cpp.o", "null.cpp.o"),
                    {},
                    member_name="null.cpp.o",
                    entrypoint_pattern=r"NullBackend::mixerProc\(\)$",
                )

        instrumented = ("__tsan_func_entry", "__tsan_read8")
        evidence = run_selector(
            ("NullBackend::mixerProc()", "OtherBackend::mixerProc()"),
            (instrumented, instrumented),
        )
        self.assertEqual(evidence["archive_member_occurrences"], 2)
        self.assertEqual(evidence["archive_member_ordinal"], 1)
        self.assertIs(evidence["function_entry"], True)
        self.assertEqual(evidence["memory_symbols"], ["__tsan_read8"])

        with self.assertRaisesRegex(
            AUDITOR.AuditError, "expected exactly one null.cpp.o object"
        ):
            run_selector(
                (
                    "NullBackend::mixerProc()",
                    "NullBackend::mixerProc()",
                ),
                (instrumented, instrumented),
            )

        with self.assertRaisesRegex(
            AUDITOR.AuditError, "lacks TSan memory-access hooks"
        ):
            run_selector(
                ("NullBackend::mixerProc()", "OtherBackend::mixerProc()"),
                (("__tsan_func_entry",), instrumented),
            )

        with self.assertRaisesRegex(
            AUDITOR.AuditError, "lacks TSan entry hooks"
        ):
            run_selector(
                ("NullBackend::mixerProc()", "OtherBackend::mixerProc()"),
                (("__tsan_read8",), instrumented),
            )

    def test_scene_and_soak_require_openal_initialization_without_suppressions(
        self,
    ) -> None:
        for marker in (
            "SoundManager: OpenAL version is: 1.1 ALSOFT 1.24.3",
            "SoundManager: OpenAL renderer is: OpenAL Soft",
            "SoundScriptManager: Sound Manager started with ([0-9]+) sources",
            "Loading WAV file default_break.wav",
            "SoundScriptInstance: instance created: defaults.soundscript-",
            "ror-openal-tsan-audio-runtime-initialization-v1",
            'Added "null" for playback',
            r'Created device [^,\r\n]+, "No Output"',
            "runtime-initialization-and-null-backend-device-creation",
            '"openal_log_sha256"',
            "OpenAL TSan dependency receipt is absent",
        ):
            with self.subTest(marker=marker):
                self.assertEqual(self.workflow.count(marker), 2)
        for forbidden_marker in (
            "SoundScriptManager: Sound Manager is disabled",
            "SoundScriptManager: Failed to create the Sound Manager",
            "Failed to open configured audio device",
            "Failed to open default audio device. Sound disabled.",
            "Failed to make the OpenAL context current. Sound disabled.",
        ):
            with self.subTest(forbidden_marker=forbidden_marker):
                self.assertEqual(self.workflow.count(forbidden_marker), 2)

        self.assertEqual(self.workflow.count("ALSOFT_DRIVERS=null"), 2)
        self.assertEqual(self.workflow.count("ALSOFT_LOGLEVEL=3"), 2)
        self.assertEqual(
            self.workflow.count('ALSOFT_LOGFILE="$openal_log"'), 2
        )
        self.assertIn(
            'openal_log="$evidence/openal-soft-packaged-scene.log"',
            self.workflow,
        )
        self.assertIn(
            'openal_log="$evidence/openal-soft-full-soak.log"', self.workflow
        )
        self.assertEqual(self.workflow.count('test ! -e "$openal_log"'), 2)
        self.assertEqual(
            self.workflow.count(
                "did not retain a regular OpenAL trace"
            ),
            2,
        )
        self.assertEqual(
            self.workflow.count('"backend": "null"'), 2
        )
        self.assertEqual(
            self.workflow.count("did not initialize positive OpenAL sources"),
            2,
        )
        self.assertIn("scene_status=0", self.workflow)
        self.assertIn("soak_status=0", self.workflow)
        self.assertEqual(self.workflow.count("audio_status=0"), 2)
        self.assertNotIn("ror-openal-tsan-audio-activity-v1", self.workflow)
        self.assertNotIn("active OpenAL execution", self.workflow)
        self.assertNotIn("playback_progress", self.workflow)
        self.assertNotIn("mixer_progress", self.workflow)

        for evasion in (
            "suppressions=",
            "ignore_noninstrumented_modules",
            "ignore_interceptors_accesses",
            "disabledefaultsounds",
            "-DROR_USE_OPENAL=OFF",
            "audio_device_name=",
        ):
            with self.subTest(evasion=evasion):
                self.assertNotIn(evasion, self.workflow)

        options = re.findall(
            r'export TSAN_OPTIONS="([^"]+)"', self.workflow
        )
        self.assertEqual(len(options), 2)
        for option_map in options:
            with self.subTest(option_map=option_map):
                self.assertIn("halt_on_error=1", option_map)
                self.assertIn("exitcode=66", option_map)
                self.assertNotIn("suppress", option_map)

    def test_scene_and_soak_audio_validators_accept_only_initialization_and_null_device(
        self,
    ) -> None:
        valid_log = "\n".join(
            (
                "SoundManager: OpenAL version is: 1.1 ALSOFT 1.24.3",
                "SoundManager: OpenAL renderer is: OpenAL Soft",
                "SoundScriptManager: Sound Manager started with 32 sources",
                "Loading WAV file default_break.wav",
                "SoundScriptInstance: instance created: defaults.soundscript-1-0",
            )
        )
        valid_openal_log = "\n".join(
            (
                '[ALSOFT] (II) Added "null" for playback',
                '[ALSOFT] (II) Created device 0x1234, "No Output"',
            )
        )
        for source in self._audio_validator_blocks():
            with self.subTest(lane="valid"):
                result = self._run_audio_validator(
                    source, valid_log, valid_openal_log
                )
                self.assertEqual(result.returncode, 0, result.stderr)

            hostile_logs = (
                valid_log.replace("32 sources", "0 sources"),
                valid_log.replace("Loading WAV file default_break.wav", ""),
                valid_log
                + "\nFailed to open default audio device. Sound disabled.",
            )
            for hostile in hostile_logs:
                with self.subTest(lane="hostile", log=hostile[-80:]):
                    result = self._run_audio_validator(
                        source, hostile, valid_openal_log
                    )
                    self.assertNotEqual(result.returncode, 0)

            hostile_openal_logs = (
                valid_openal_log.replace('Added "null" for playback', ""),
                valid_openal_log.replace('"No Output"', '"Hardware Output"'),
                valid_openal_log
                + '\n[ALSOFT] (II) Added "null" for playback',
            )
            for hostile_openal in hostile_openal_logs:
                with self.subTest(
                    lane="hostile-openal", log=hostile_openal[-100:]
                ):
                    result = self._run_audio_validator(
                        source, valid_log, hostile_openal
                    )
                    self.assertNotEqual(result.returncode, 0)

            with self.subTest(lane="symlink-openal"):
                result = self._run_audio_validator(
                    source,
                    valid_log,
                    valid_openal_log,
                    symlink_openal_log=True,
                )
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
