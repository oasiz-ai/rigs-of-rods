#!/usr/bin/env python3
"""Fail-closed contracts for the pinned Linux shader static closure."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
LOCK_PATH = PROBE_ROOT / "linux-shader-toolchain.lock.json"
PINNED_CMAKE_PATH = PROBE_ROOT / "cmake" / "PinnedOgreNext.cmake"
MANIFEST_SCRIPT = PROBE_ROOT / "cmake" / "WriteLinuxStaticClosureManifest.cmake"
ENTRY_CMAKE_PATH = PROBE_ROOT / "CMakeLists.txt"
PATCH_PATH = PROBE_ROOT / "patches" / "0002-vulkan-use-glslang-spv-options.patch"
SHADERC_PATCH_PATH = (
    PROBE_ROOT / "patches" / "0003-shaderc-disable-glslang-install.patch"
)
RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_ogre_next_probe_for_linux_closure_tests", RUNNER_PATH
)
if RUNNER_SPEC is None or RUNNER_SPEC.loader is None:
    raise RuntimeError("could not import OGRE-Next probe runner")
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


class OgreNextLinuxStaticClosureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock_text = LOCK_PATH.read_text(encoding="utf-8")
        cls.lock = RUNNER.load_linux_shader_toolchain_lock()
        cls.pinned_cmake = PINNED_CMAKE_PATH.read_text(encoding="utf-8")
        cls.entry_cmake = ENTRY_CMAKE_PATH.read_text(encoding="utf-8")

    def test_lock_is_canonical_whole_file_pinned_and_source_owned(self) -> None:
        self.assertEqual(
            self.lock_text, json.dumps(json.loads(self.lock_text), indent=2) + "\n"
        )
        self.assertEqual(
            RUNNER.sha256_file(LOCK_PATH),
            RUNNER.LINUX_SHADER_TOOLCHAIN_LOCK_SHA256,
        )
        self.assertEqual(self.lock["provider"], "pinned-source")
        self.assertEqual(
            self.lock["host_dynamic_boundary"]["component"], "Vulkan-Loader"
        )

    def test_exact_compatible_source_family_and_patch_are_locked(self) -> None:
        expected = {
            "shaderc": (
                self.lock["shaderc_release"],
                "8c2e602ce440b7739c95ff3d69cecb1adf6becda",
            ),
            "glslang": (
                self.lock["dependencies"]["glslang"],
                "efd24d75bcbc55620e759f6bf42c45a32abac5f8",
            ),
            "spirv-tools": (
                self.lock["dependencies"]["spirv_tools"],
                "33e02568181e3312f49a3cf33df470bf96ef293a",
            ),
            "spirv-headers": (
                self.lock["dependencies"]["spirv_headers"],
                "2a611a970fdbc41ac2e3e328802aed9985352dca",
            ),
        }
        for component, (record, commit) in expected.items():
            with self.subTest(component=component):
                self.assertEqual(record["commit"], commit)
                self.assertRegex(record["archive_sha256"], r"^[0-9a-f]{64}$")
                self.assertEqual(
                    record["license_sha256"], record["package_notice_sha256"]
                )
        self.assertEqual(
            RUNNER.sha256_file(PATCH_PATH),
            self.lock["ogre_compatibility_patch"]["sha256"],
        )
        patch = PATCH_PATH.read_text(encoding="utf-8")
        self.assertIn("glslang/SPIRV/GlslangToSpv.h", patch)
        self.assertIn("-    struct SpvOptions", patch)
        shaderc_patch = self.lock["shaderc_release"]["compatibility_patch"]
        self.assertEqual(
            RUNNER.sha256_file(SHADERC_PATCH_PATH), shaderc_patch["sha256"]
        )
        self.assertIn(
            "GLSLANG_ENABLE_INSTALL OFF",
            SHADERC_PATCH_PATH.read_text(encoding="utf-8"),
        )

    def test_cmake_rejects_distro_cpp_abi_and_builds_one_static_closure(self) -> None:
        for token in (
            "SHADERC ROR_GLSLANG_SOURCE ROR_SPIRV_TOOLS_SOURCE",
            "FETCHCONTENT_SOURCE_DIR_${_ror_content_name}",
            'URL_HASH "SHA256=${ROR_LINUX_SHADERC_ARCHIVE_SHA256}"',
            'URL_HASH "SHA256=${ROR_LINUX_GLSLANG_ARCHIVE_SHA256}"',
            'URL_HASH "SHA256=${ROR_LINUX_SPIRV_TOOLS_ARCHIVE_SHA256}"',
            'URL_HASH "SHA256=${ROR_LINUX_SPIRV_HEADERS_ARCHIVE_SHA256}"',
            "ROR_LINUX_SHADERC_PATCH_PATH",
            "shaderc_combined no longer owns",
            "set(Vulkan_SHADERC_LIB_REL shaderc_combined",
            "ror_ogre_next_linux_static_closure_verify",
            "SPIRV-Tools-static",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.pinned_cmake)
        guard_start = self.pinned_cmake.index(
            "foreach (_ror_content_name IN ITEMS"
        )
        guard_end = self.pinned_cmake.index("endforeach ()", guard_start)
        override_guard = self.pinned_cmake[guard_start:guard_end]
        for content_name in (
            "SHADERC",
            "ROR_GLSLANG_SOURCE",
            "ROR_SPIRV_TOOLS_SOURCE",
            "ROR_SPIRV_HEADERS_SOURCE",
        ):
            with self.subTest(content_name=content_name):
                self.assertIn(content_name, override_guard)
        self.assertIn(
            "FETCHCONTENT_SOURCE_DIR_${_ror_content_name}", override_guard
        )
        self.assertNotIn(
            "ROR_SHADERC_SOURCE ROR_GLSLANG_SOURCE",
            self.pinned_cmake,
        )
        for prohibited in (
            "find_package(glslang",
            "find_library(ROR_OGRE_NEXT_SHADERC",
            "ROR_OGRE_NEXT_SHADERC_SHARED_LIBRARY",
        ):
            with self.subTest(prohibited=prohibited):
                self.assertNotIn(prohibited, self.pinned_cmake)

    def test_cmake_rejects_actual_shaderc_fetchcontent_override(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-shaderc-source-override-"
        ) as temp:
            build_dir = Path(temp) / "build"
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(PROBE_ROOT),
                    "-B",
                    str(build_dir),
                    "-DROR_OGRE_NEXT_PROBE=ON",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DFETCHCONTENT_SOURCE_DIR_SHADERC=untrusted-source",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            diagnostics = result.stdout + result.stderr
            self.assertIn("FETCHCONTENT_SOURCE_DIR_SHADERC", diagnostics)
            self.assertIn("bypasses the Linux shader source lock", diagnostics)
            self.assertIn("is prohibited", diagnostics)

    def test_package_stages_and_byte_compares_every_notice_and_manifest(self) -> None:
        for relative_path in (
            "licenses/Rigs-of-Rods-GPL-3.0.txt",
            "licenses/Ogre-Next-MIT.txt",
            "licenses/RapidJSON-license.txt",
            "${ROR_LINUX_APACHE_NOTICE_PATH}",
            "${ROR_LINUX_GLSLANG_NOTICE_PATH}",
            "${ROR_LINUX_SPIRV_TOOLS_NOTICE_PATH}",
            "${ROR_LINUX_SPIRV_HEADERS_NOTICE_PATH}",
            "provenance/ogre-next-linux-shader-toolchain.lock.json",
            "provenance/ogre-next-linux-static-closure.json",
        ):
            with self.subTest(relative_path=relative_path):
                self.assertIn(relative_path, self.entry_cmake)
        self.assertGreaterEqual(self.entry_cmake.count("-E compare_files"), 10)
        self.assertIn(".stage-v3", self.entry_cmake)
        self.assertIn(
            "ror_ogre_next_linux_static_closure_manifest",
            self.entry_cmake,
        )

    def test_manifest_hashes_exact_archives_and_detects_post_build_mutation(self) -> None:
        target_files = (
            ("shaderc_combined", "libshaderc_combined.a"),
            ("shaderc", "libshaderc.a"),
            ("shaderc_util", "libshaderc_util.a"),
            ("glslang", "libglslang.a"),
            ("SPIRV", "libSPIRV.a"),
            ("SPIRV-Tools-opt", "libSPIRV-Tools-opt.a"),
            ("SPIRV-Tools-static", "libSPIRV-Tools.a"),
        )
        with tempfile.TemporaryDirectory(prefix="ror-linux-static-closure-") as temp:
            root = Path(temp)
            manifest_path = root / "closure.json"
            command = [
                "cmake",
                f"-DOUTPUT={manifest_path}",
                f"-DLOCK_PATH={LOCK_PATH}",
                (
                    "-DEXPECTED_LOCK_SHA256="
                    f"{RUNNER.LINUX_SHADER_TOOLCHAIN_LOCK_SHA256}"
                ),
                "-DCOMPILER_ID=GNU",
                "-DCOMPILER_VERSION=14.2.0",
                "-DSYSTEM_NAME=Linux",
                "-DSYSTEM_PROCESSOR=x86_64",
                "-DBUILD_TYPE=Release",
                "-DARTIFACT_COUNT=7",
            ]
            for index, (target, filename) in enumerate(target_files):
                artifact = root / filename
                artifact.write_bytes(f"archive-{index}\n".encode("ascii"))
                command.extend(
                    [
                        f"-DARTIFACT_{index}_NAME={target}",
                        f"-DARTIFACT_{index}_PATH={artifact}",
                    ]
                )
            command.extend(["-P", str(MANIFEST_SCRIPT)])
            subprocess.run(command, check=True, stdout=subprocess.PIPE, text=True)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            RUNNER.validate_linux_static_closure_manifest(manifest, self.lock)

            verify_command = command[:-2] + ["-DVERIFY_EXISTING=ON"] + command[-2:]
            subprocess.run(
                verify_command, check=True, stdout=subprocess.PIPE, text=True
            )
            (root / "libglslang.a").write_bytes(b"mutated\n")
            result = subprocess.run(
                verify_command,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("no longer matches", result.stdout)


if __name__ == "__main__":
    unittest.main()
