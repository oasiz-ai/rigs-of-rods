#!/usr/bin/env python3
"""Offline contract tests for the pinned OGRE-Next integration probe."""

from __future__ import annotations

import base64
import copy
import functools
import gzip
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import threading
import unittest
from unittest import mock
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
FRAME_TOOL_PATH = (
    REPOSITORY_ROOT / "tools" / "validate_ogre_next_frame_probe.py"
)
PROBE_DIR = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
CMAKE_PATH = PROBE_DIR / "CMakeLists.txt"
PINNED_CMAKE_PATH = PROBE_DIR / "cmake" / "PinnedOgreNext.cmake"
FREETYPE_ARCHIVE_POLICY_PATH = (
    PROBE_DIR / "cmake" / "FreeTypeArchivePolicy.cmake"
)
LOCK_PATH = PROBE_DIR / "ogre-next.lock.json"
EVIDENCE_PATH = (
    REPOSITORY_ROOT
    / "doc"
    / "nextgen"
    / "evidence"
    / "OGRE_NEXT_METAL_PROBE_M5_2026-07-31.json"
)

SPEC = importlib.util.spec_from_file_location("run_ogre_next_probe", TOOL_PATH)
assert SPEC and SPEC.loader
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)

FRAME_SPEC = importlib.util.spec_from_file_location(
    "validate_checked_in_ogre_next_frame", FRAME_TOOL_PATH
)
assert FRAME_SPEC and FRAME_SPEC.loader
FRAME = importlib.util.module_from_spec(FRAME_SPEC)
FRAME_SPEC.loader.exec_module(FRAME)


def select_freetype_archive_urls(
    *,
    local_archive: Path | None,
    expected_sha256: str,
    primary_url: str,
    fallback_url: str,
) -> subprocess.CompletedProcess:
    """Execute the production CMake selector and return its ordered result."""

    with tempfile.TemporaryDirectory(prefix="ror-freetype-policy-") as temp:
        temporary_root = Path(temp)
        output_path = temporary_root / "selected.txt"
        script_path = temporary_root / "select.cmake"
        local_text = "" if local_archive is None else local_archive.as_posix()
        script_path.write_text(
            f'include("{FREETYPE_ARCHIVE_POLICY_PATH.as_posix()}")\n'
            "ror_select_freetype_archive_urls(\n"
            f'    selected "{local_text}" "{expected_sha256}"\n'
            f'    "{primary_url}" "{fallback_url}")\n'
            "list(LENGTH selected selected_count)\n"
            'string(REPLACE ";" "\\n" selected_lines "${selected}")\n'
            f'file(WRITE "{output_path.as_posix()}"\n'
            '    "${selected_count}\\n${selected_lines}\\n")\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script_path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if result.returncode == 0:
            lines = output_path.read_text(encoding="utf-8").splitlines()
            result.selected_count = int(lines[0])
            result.selected_urls = lines[1:]
        return result


class OgreNextProbeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock = PROBE.load_lock()
        cls.policy = PROBE.detect_policy("Darwin", "arm64")

    def make_report(self) -> dict:
        return {
            "schema_version": 2,
            "status": "pass",
            "provenance": {
                "repository": self.lock["repository"],
                "branch": self.lock["branch"],
                "commit": self.lock["commit"],
                "archive_sha256": self.lock["archive_sha256"],
                "license_spdx": self.lock["license"]["spdx"],
                "license_sha256": self.lock["license"]["sha256"],
                "rapidjson_tag": self.lock["dependencies"]["rapidjson"][
                    "tag"
                ],
                "rapidjson_archive_sha256": self.lock["dependencies"][
                    "rapidjson"
                ]["archive_sha256"],
                "rapidjson_source_archive_license_spdx": self.lock[
                    "dependencies"
                ]["rapidjson"]["license_spdx"],
                "rapidjson_compiled_headers_license_spdx": self.lock[
                    "dependencies"
                ]["rapidjson"]["compiled_headers_spdx"],
                "rapidjson_license_sha256": self.lock["dependencies"][
                    "rapidjson"
                ]["license_sha256"],
                "shader_media_root": self.lock["shader_media"]["root"],
                "shader_media_license_expression": self.lock[
                    "shader_media"
                ]["license_expression"],
                "shader_media_third_party_source_path": self.lock[
                    "shader_media"
                ]["third_party_notice"]["source_path"],
                "shader_media_third_party_source_sha256": self.lock[
                    "shader_media"
                ]["third_party_notice"]["source_sha256"],
                "shader_media_notice_path": self.lock["shader_media"][
                    "third_party_notice"
                ]["notice_path"],
                "shader_media_notice_sha256": self.lock["shader_media"][
                    "third_party_notice"
                ]["notice_sha256"],
                "shader_media_upstream_source": self.lock["shader_media"][
                    "third_party_notice"
                ]["upstream_source"],
                "shader_media_paper_reference": self.lock["shader_media"][
                    "third_party_notice"
                ]["paper_reference"],
                "shader_media_source_and_binary_notice_required": True,
                "shader_media_paper_reference_required": True,
            },
            "build": {
                "ogre_version": "3.0.0",
                "platform_policy": self.policy["name"],
                "cxx_standard": 17,
                "pointer_bits": 64,
                "static_link": True,
                "abi_cookie": "0123456789abcdeffedcba9876543210",
                "debug_mode": 0,
                "double_precision": False,
                "memory_allocator": 0,
                "container_custom_allocator": False,
                "string_custom_allocator": False,
                "thread_support": 0,
                "thread_provider": 0,
                "id_string_bits": 32,
                "id_string_size": 4,
                "flexibility_level": 0,
                "simd_alignment": 16,
                "use_simd": 1,
                "restrict_aliasing": 1,
                "assert_mode": 0,
            },
            "capabilities": {
                "renderer": {
                    "target": self.policy["renderer_target"],
                    "name": self.policy["renderer_name"],
                    "registered": True,
                    "registered_renderer_count": 1,
                    "configuration_option_count": 3,
                    "device_option_name": self.policy["device_option_name"],
                    "reported_device_count": 1,
                    "first_reported_device": "Reviewed GPU",
                },
                "hlms_pbs": {
                    "compiled_and_linked": True,
                    "shader_data_path": self.policy["shader_data_path"],
                    "shader_path_matches_policy": True,
                    "library_path_count": 4,
                },
                "compositor2": {
                    "compiled_and_linked": True,
                    "runtime_initialization": "deferred_until_real_window",
                    "deferred_contract_observed": True,
                },
                "native_ray_tracing": "not_evaluated",
            },
        }

    def test_exact_upstream_and_dependency_pins(self) -> None:
        self.assertEqual(self.lock["schema_version"], 6)
        self.assertEqual(
            self.lock["commit"],
            "37149a802de747f6806996fa3067b0748ecc1084",
        )
        self.assertEqual(
            self.lock["archive_sha256"],
            "1c0be064474da512606d02543be2630b36cdf99f359a9f23edc97eeb410e25b2",
        )
        self.assertEqual(self.lock["license"]["spdx"], "MIT")
        self.assertEqual(
            self.lock["dependencies"]["rapidjson"]["tag"], "v1.1.0"
        )
        self.assertEqual(
            self.lock["dependencies"]["rapidjson"]["archive_sha256"],
            "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e",
        )
        self.assertEqual(
            self.lock["dependencies"]["rapidjson"]["license_spdx"],
            "MIT AND BSD-3-Clause AND JSON",
        )
        self.assertEqual(
            self.lock["dependencies"]["rapidjson"]["compiled_headers_spdx"],
            "MIT",
        )
        freetype = self.lock["dependencies"]["freetype"]
        self.assertEqual(freetype["version"], "2.14.3")
        self.assertEqual(
            freetype["archive_url"],
            "https://download.savannah.gnu.org/releases/freetype/"
            "freetype-2.14.3.tar.xz",
        )
        self.assertEqual(
            freetype["archive_fallback_url"],
            "https://downloads.sourceforge.net/project/freetype/freetype2/"
            "2.14.3/freetype-2.14.3.tar.xz",
        )
        self.assertEqual(
            freetype["archive_sha256"],
            "36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f",
        )
        self.assertEqual(freetype["selected_license_spdx"], "GPL-2.0-or-later")
        self.assertEqual(
            freetype["package_license_path"],
            "licenses/FreeType-GPLv2.txt",
        )
        self.assertEqual(
            freetype["package_overview_path"],
            "licenses/FreeType-LICENSE.txt",
        )
        self.assertTrue(freetype["static_link"])
        self.assertEqual(
            freetype["disabled_optional_dependencies"],
            ["BZip2", "Brotli", "HarfBuzz", "PNG", "ZLIB"],
        )
        shader_media = self.lock["shader_media"]
        self.assertEqual(
            shader_media["license_expression"],
            "MIT AND LicenseRef-Heitz-LTC-Paper-Notice",
        )
        notice = shader_media["third_party_notice"]
        self.assertTrue(notice["source_and_binary_notice_required"])
        self.assertTrue(notice["paper_reference_required"])
        notice_path = PROBE_DIR / notice["notice_path"]
        self.assertEqual(
            hashlib.sha256(notice_path.read_bytes()).hexdigest(),
            notice["notice_sha256"],
        )

    def test_every_adaptation_patch_matches_lock(self) -> None:
        for patch in self.lock["patches"]:
            data = (PROBE_DIR / patch["path"]).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), patch["sha256"])

    def test_metal_ibl_patch_is_exact_and_backend_scoped(self) -> None:
        patch = self.lock["patches"][1]
        self.assertEqual(
            patch["path"],
            "patches/0005-metal-typed-ibl-uav-conversions.patch",
        )
        self.assertEqual(
            patch["source_sha256"],
            "68884256ab318116833bf2efe19518833459cc461fb8dd4f8e2c253f8c352165",
        )
        self.assertEqual(
            patch["patched_sha256"],
            "3ebebc1132c720ee8b741226d41e8638f747a0d5700222d7cb4c8f4e0663fa41",
        )
        source = (PROBE_DIR / patch["path"]).read_text(encoding="utf-8")
        self.assertEqual(source.count("@property( syntax == metal )"), 4)
        self.assertEqual(
            source.count("float4( OGRE_imageLoad2D"),
            2,
        )
        self.assertEqual(source.count("(@insertpiece(uav0_pf_type)4)"), 2)
        self.assertIn(
            "OGRE_imageWrite2DArray4( lastResult, "
            "gl_GlobalInvocationID.xyz, outputValue );",
            source,
        )
        self.assertIn(
            "OGRE_imageWrite2D4( lastResult, "
            "gl_GlobalInvocationID.xy, outputValue );",
            source,
        )

    def test_metal_anisotropy_limit_patch_is_exact_and_capability_scoped(self) -> None:
        patch = self.lock["patches"][2]
        self.assertEqual(
            patch["path"],
            "patches/0008-metal-report-anisotropy-limit.patch",
        )
        self.assertEqual(
            patch["source_path"],
            "RenderSystems/Metal/src/OgreMetalRenderSystem.mm",
        )
        self.assertEqual(
            patch["source_sha256"],
            "bebe97dd2cb318d6aa2331eaaf0f8b181e18ac66660b02d4160802e0ed8ed0eb",
        )
        self.assertEqual(
            patch["patched_sha256"],
            "56bb59e7e8d7be5b9efe10e724e5385583618a12e2bb49482e0472d273dc1222",
        )
        source = (PROBE_DIR / patch["path"]).read_text(encoding="utf-8")
        self.assertEqual(source.count("setMaxSupportedAnisotropy( 16.0f )"), 1)
        self.assertIn("RenderSystems/Metal/src/OgreMetalRenderSystem.mm", source)

    def test_ibl_notice_and_patched_shader_are_fail_closed_in_cmake(self) -> None:
        cmake = PINNED_CMAKE_PATH.read_text(encoding="utf-8")
        reflection_media = self.lock["reflection_shader_media"]
        self.assertEqual(
            reflection_media["license_expression"], "LicenseRef-IBLBaker"
        )
        self.assertTrue(
            reflection_media["third_party_notice"][
                "source_and_binary_notice_required"
            ]
        )
        for token in (
            "ROR_OGRE_NEXT_PATCH_COUNT EQUAL 3",
            "ROR_OGRE_NEXT_IBL_PATCHED_SHA256",
            "ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCHED_SHA256",
            "_ror_extracted_ibl_shader_sha256",
            "_ror_extracted_iblbaker_license_sha256",
            "ROR_OGRE_NEXT_PACKAGE_IBLBAKER_LICENSE_SOURCE",
            '"${GIT_EXECUTABLE}" -c core.autocrlf=false apply',
        ):
            self.assertIn(token, cmake)

    def test_ibl_patch_command_overrides_windows_autocrlf(self) -> None:
        source_path = Path(
            "Samples/Media/Compute/Algorithms/IBL/"
            "SpecularIblIntegrator_piece_cs.any"
        )
        lines = [
            f"// deterministic fixture line {index}\n"
            for index in range(1, 301)
        ]
        lines[218] = (
            "\t\t\t\tfloat4 lastResultVal = OGRE_imageLoad2DArray( "
            "lastResult, loadCoords.xyz );\n"
        )
        lines[220] = (
            "\t\t\t\tfloat4 lastResultVal = OGRE_imageLoad2D( "
            "lastResult, loadCoords.xy );\n"
        )
        lines[271] = (
            "\t\tOGRE_imageWrite2DArray4( lastResult, "
            "gl_GlobalInvocationID.xyz, outputValue );\n"
        )
        lines[273] = (
            "\t\tOGRE_imageWrite2D4( lastResult, "
            "gl_GlobalInvocationID.xy, outputValue );\n"
        )

        with tempfile.TemporaryDirectory(
            prefix="ror-ogre-next-autocrlf-"
        ) as temp:
            fixture = Path(temp) / source_path
            fixture.parent.mkdir(parents=True)
            fixture.write_bytes("".join(lines).encode("utf-8"))
            result = subprocess.run(
                [
                    "git",
                    "-c",
                    "core.autocrlf=true",
                    "-c",
                    "core.autocrlf=false",
                    "apply",
                    "--unidiff-zero",
                    "--whitespace=nowarn",
                    str(PROBE_DIR / self.lock["patches"][1]["path"]),
                ],
                cwd=temp,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout.decode())
            patched = fixture.read_bytes()
            self.assertNotIn(b"\r\n", patched)
            self.assertEqual(patched.count(b"@property( syntax == metal )"), 4)

    def test_reviewed_platform_matrix(self) -> None:
        self.assertEqual(
            PROBE.detect_policy("Darwin", "arm64")["name"],
            "macos-arm64-metal",
        )
        self.assertEqual(
            PROBE.detect_policy("Windows", "AMD64")["name"],
            "windows-x64-d3d11",
        )
        self.assertEqual(
            PROBE.detect_policy("Linux", "x86_64")["name"],
            "linux-x86_64-vulkan",
        )
        self.assertEqual(
            PROBE.detect_policy("Linux", "x86_64")["device_option_name"],
            "Device",
        )
        for system, machine in (
            ("Darwin", "x86_64"),
            ("Linux", "aarch64"),
            ("Windows", "x86"),
            ("FreeBSD", "x86_64"),
        ):
            with self.subTest(system=system, machine=machine):
                with self.assertRaises(PROBE.ProbeError):
                    PROBE.detect_policy(system, machine)

    def test_archive_verification_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-next-hash-") as temp:
            archive = Path(temp) / "archive.tar.gz"
            archive.write_bytes(b"pinned fixture")
            digest = hashlib.sha256(b"pinned fixture").hexdigest()
            self.assertEqual(
                PROBE.verify_archive(archive, digest, "fixture"),
                archive.resolve(),
            )
            with self.assertRaises(PROBE.ProbeError):
                PROBE.verify_archive(archive, "0" * 64, "fixture")

    def test_source_identity_drift_fails_closed(self) -> None:
        identity = PROBE.ror_source_identity()
        PROBE.require_source_identity_unchanged(identity)
        drifted = copy.deepcopy(identity)
        drifted["relevant_manifest_sha256"] = "0" * 64
        with self.assertRaisesRegex(PROBE.ProbeError, "changed during"):
            PROBE.require_source_identity_unchanged(drifted)

    def test_expected_source_labels_cannot_spoof_checkout_identity(self) -> None:
        with mock.patch.dict(
            os.environ,
            {"ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT": "0" * 40},
            clear=False,
        ), self.assertRaisesRegex(PROBE.ProbeError, "differs from checked-out"):
            PROBE.ror_source_identity()
        with mock.patch.dict(
            os.environ,
            {"ROR_OGRE_NEXT_EXPECTED_ROR_REPOSITORY": "https://invalid.test/repo"},
            clear=False,
        ), self.assertRaisesRegex(PROBE.ProbeError, "not canonical"):
            PROBE.ror_source_identity()
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        self.assertIn("_ror_checked_out_source_commit", cmake)
        self.assertIn(
            "Expected RoR repository differs from the reviewed source identity",
            cmake,
        )
        self.assertIn(
            'NOT "${ROR_SOURCE_COMMIT}" STREQUAL',
            cmake,
        )

    def test_mutated_reused_build_is_rejected_and_explicitly_recovered(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-next-reuse-") as temp:
            build_dir = Path(temp) / "build"
            prepared = PROBE.prepare_build_dir(build_dir, clean=False)
            mutated_source = prepared / "_deps" / "ogre_next-src" / "mutated.cpp"
            mutated_source.parent.mkdir(parents=True)
            mutated_source.write_text("unreviewed source\n", encoding="utf-8")

            with self.assertRaisesRegex(PROBE.ProbeError, "not empty"):
                PROBE.prepare_build_dir(build_dir, clean=False)

            recovered = PROBE.prepare_build_dir(build_dir, clean=True)
            self.assertEqual(recovered, build_dir.resolve())
            self.assertFalse(mutated_source.exists())
            self.assertEqual(
                sorted(path.name for path in recovered.iterdir()),
                [PROBE.BUILD_SENTINEL_NAME],
            )

    def test_clean_rejects_directory_without_probe_sentinel(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-next-owned-") as temp:
            build_dir = Path(temp) / "not-owned"
            build_dir.mkdir()
            (build_dir / "user-data").write_text("keep\n", encoding="utf-8")
            with self.assertRaisesRegex(PROBE.ProbeError, "not owned"):
                PROBE.prepare_build_dir(build_dir, clean=True)
            self.assertTrue((build_dir / "user-data").is_file())

    def test_reuse_requires_owned_exact_configured_probe(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-next-resume-") as temp:
            build_dir = Path(temp) / "build"
            prepared = PROBE.prepare_build_dir(build_dir, clean=False)
            with self.assertRaisesRegex(PROBE.ProbeError, "configured probe"):
                PROBE.prepare_build_dir(build_dir, clean=False, reuse=True)
            (prepared / "CMakeCache.txt").write_text(
                f"CMAKE_HOME_DIRECTORY:INTERNAL={PROBE.PROBE_SOURCE.resolve()}\n",
                encoding="utf-8",
            )
            self.assertEqual(
                PROBE.prepare_build_dir(build_dir, clean=False, reuse=True),
                build_dir.resolve(),
            )
            with self.assertRaisesRegex(PROBE.ProbeError, "conflict"):
                PROBE.prepare_build_dir(build_dir, clean=True, reuse=True)

    def test_cmake_source_path_normalization_accepts_windows_separators(
        self,
    ) -> None:
        self.assertEqual(
            PROBE._normalize_cmake_source_path(
                r"D:\a\rigs-of-rods\tools\ogre_next_probe",
                windows=True,
            ),
            PROBE._normalize_cmake_source_path(
                "D:/a/rigs-of-rods/tools/ogre_next_probe",
                windows=True,
            ),
        )
        self.assertNotEqual(
            PROBE._normalize_cmake_source_path(
                r"D:\a\rigs-of-rods\tools\ogre_next_probe-other",
                windows=True,
            ),
            PROBE._normalize_cmake_source_path(
                "D:/a/rigs-of-rods/tools/ogre_next_probe",
                windows=True,
            ),
        )

    def test_report_requires_every_capability_and_no_rt_claim(self) -> None:
        report = self.make_report()
        PROBE.validate_report(report, self.lock, self.policy)

        mutations = (
            (
                "renderer",
                lambda value: value["capabilities"]["renderer"].update(
                    registered=False
                ),
            ),
            (
                "pbs",
                lambda value: value["capabilities"]["hlms_pbs"].update(
                    compiled_and_linked=False
                ),
            ),
            (
                "compositor",
                lambda value: value["capabilities"]["compositor2"].update(
                    runtime_initialization="ready"
                ),
            ),
            (
                "rt",
                lambda value: value["capabilities"].update(
                    native_ray_tracing="supported"
                ),
            ),
            (
                "commit",
                lambda value: value["provenance"].update(commit="0" * 40),
            ),
            (
                "debug_configuration",
                lambda value: value["build"].update(debug_mode=3),
            ),
            (
                "shader_media_license",
                lambda value: value["provenance"].update(
                    shader_media_license_expression="MIT"
                ),
            ),
            (
                "shader_media_notice",
                lambda value: value["provenance"].update(
                    shader_media_source_and_binary_notice_required=False
                ),
            ),
        )
        for name, mutate in mutations:
            with self.subTest(name=name):
                invalid = copy.deepcopy(report)
                mutate(invalid)
                with self.assertRaises(PROBE.ProbeError):
                    PROBE.validate_report(invalid, self.lock, self.policy)

    def test_build_contract_rejects_abi_and_license_drift(self) -> None:
        evidence = json.loads(EVIDENCE_PATH.read_text(encoding="utf-8"))
        contract_path = REPOSITORY_ROOT / evidence["provenance"][
            "build_contract_path"
        ]
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        contract["schema_version"] = 3
        contract["patches"] = copy.deepcopy(self.lock["patches"])
        contract["reflection_shader_media"] = copy.deepcopy(
            self.lock["reflection_shader_media"]
        )
        contract["shader_media"] = PROBE.expected_build_shader_media(
            self.lock
        )
        PROBE.validate_build_contract(contract, self.lock, self.policy)
        current_contract = copy.deepcopy(contract)
        current_contract["schema_version"] = 4
        current_contract["components"].update(
            {
                "hlms_unlit": True,
                "overlay": True,
                "hdr_temporal_contract_version": 2,
                "hdr_history_validation_mode": (
                    "native_authoritative_conditioning_plus_one_r16_ulp_v2"
                ),
                "hdr_workspace": "RoRHdrWorkspaceUiFreeV2",
                "hdr_visual_evidence_version": 1,
            }
        )
        PROBE.validate_build_contract(
            current_contract, self.lock, self.policy
        )
        current_contract["schema_version"] = 5
        current_contract["components"].update(
            {
                "headless_child_bootstrap": True,
                "headless_child_output_name": "RoR-OgreNext",
                "headless_child_packaged": False,
                "headless_child_production_admitted": False,
            }
        )
        freetype = self.lock["dependencies"]["freetype"]
        current_contract["dependencies"]["freetype"] = {
            "repository": freetype["repository"],
            "version": freetype["version"],
            "archive_url": freetype["archive_url"],
            "archive_sha256": freetype["archive_sha256"],
            "license_expression": freetype["license_expression"],
            "selected_license_spdx": freetype["selected_license_spdx"],
            "license_path": freetype["license_path"],
            "license_sha256": freetype["license_sha256"],
            "package_license_path": freetype["package_license_path"],
            "overview_path": freetype["overview_path"],
            "overview_sha256": freetype["overview_sha256"],
            "package_overview_path": freetype["package_overview_path"],
            "target": "freetype",
            "target_type": "STATIC_LIBRARY",
            "static_link": True,
            "overlay_link_target": True,
            "disabled_optional_dependencies": copy.deepcopy(
                freetype["disabled_optional_dependencies"]
            ),
        }
        PROBE.validate_build_contract(
            current_contract, self.lock, self.policy
        )
        current_contract["schema_version"] = 6
        current_contract["components"].update(
            {
                "headless_child_execution_receipt_schema": (
                    "ror.ogre_next_child_runtime_execution_receipt.v1"
                ),
                "headless_child_execution_receipt_required": True,
                "headless_child_binary_retained": True,
                "headless_child_logs_retained": True,
                "headless_child_process_model": (
                    "single-process-reviewed-source-closure-v1"
                ),
            }
        )
        PROBE.validate_build_contract(
            current_contract, self.lock, self.policy
        )
        current_contract["schema_version"] = 7
        embedded = self.lock["embedded_namespace"]
        current_contract["embedded_namespace"] = {
            "enabled": False,
            "namespace": embedded["namespace"],
            "cmake_option": embedded["cmake_option"],
            "default_enabled": embedded["default_enabled"],
            "patch": {**embedded["patch"], "applied": False},
            "remap_header": {
                **embedded["remap_header"],
                "forced_include": False,
            },
            "full_n1_link_evidence": "not_evaluated",
        }
        PROBE.validate_build_contract(
            current_contract, self.lock, self.policy
        )
        for name, mutate in (
            (
                "simd",
                lambda value: value["abi"].update(simd_family="sse2"),
            ),
            (
                "license",
                lambda value: value["dependencies"]["rapidjson"].update(
                    source_archive_license_spdx="MIT"
                ),
            ),
            (
                "freetype_license",
                lambda value: value["dependencies"]["freetype"].update(
                    selected_license_spdx="FTL"
                ),
            ),
            (
                "configuration",
                lambda value: value["compiler"].update(build_type="Debug"),
            ),
            (
                "shader_media",
                lambda value: value["shader_media"].update(
                    license_expression="MIT"
                ),
            ),
        ):
            with self.subTest(name=name):
                invalid = copy.deepcopy(current_contract)
                mutate(invalid)
                with self.assertRaises(PROBE.ProbeError):
                    PROBE.validate_build_contract(invalid, self.lock, self.policy)

    def test_cmake_is_opt_in_and_forbids_unverified_source_overrides(self) -> None:
        entry_cmake = CMAKE_PATH.read_text(encoding="utf-8")
        pinned_cmake = PINNED_CMAKE_PATH.read_text(encoding="utf-8")
        cmake = entry_cmake + "\n" + pinned_cmake
        self.assertIn("ROR_OGRE_NEXT_PROBE", cmake)
        self.assertIn("cmake/PinnedOgreNext.cmake", entry_cmake)
        self.assertIn("FreeTypeArchivePolicy.cmake", pinned_cmake)
        self.assertIn(
            "if (TARGET OgreMain AND NOT ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)",
            pinned_cmake,
        )
        self.assertIn("URL_HASH \"SHA256=${ROR_OGRE_NEXT_ARCHIVE_SHA256}\"", cmake)
        self.assertIn("URL_HASH \"SHA256=${ROR_RAPIDJSON_ARCHIVE_SHA256}\"", cmake)
        self.assertIn("URL_HASH \"SHA256=${ROR_FREETYPE_ARCHIVE_SHA256}\"", cmake)
        self.assertIn("URL ${_ror_freetype_urls}", pinned_cmake)
        self.assertIn("FETCHCONTENT_SOURCE_DIR_OGRE_NEXT", cmake)
        self.assertIn("FETCHCONTENT_SOURCE_DIR_RAPIDJSON", cmake)
        self.assertIn("FETCHCONTENT_SOURCE_DIR_ROR_FREETYPE", cmake)
        self.assertIn("FetchContent_MakeAvailable(ror_freetype)", cmake)
        self.assertIn(
            'set(FREETYPE_LIBRARIES "${ROR_OGRE_NEXT_FREETYPE_TARGET}"',
            pinned_cmake,
        )
        self.assertIn("get_target_property(ROR_FREETYPE_TARGET_TYPE", cmake)
        self.assertIn(
            "OgreNextOverlay does not link the pinned FreeType", cmake
        )
        self.assertIn("OGRE-Next selected a host FreeType", cmake)
        for option in (
            "FT_DISABLE_ZLIB",
            "FT_DISABLE_BZIP2",
            "FT_DISABLE_PNG",
            "FT_DISABLE_HARFBUZZ",
            "FT_DISABLE_BROTLI",
        ):
            with self.subTest(freetype_option=option):
                self.assertIn(f"set({option} ON", cmake)
        self.assertIn("SHADERC ROR_GLSLANG_SOURCE", cmake)
        self.assertNotIn("ROR_SHADERC_SOURCE ROR_GLSLANG_SOURCE", cmake)
        self.assertLess(
            cmake.index("_ror_fresh_configure_guard"),
            cmake.index("FetchContent_Declare(\n    rapidjson"),
        )
        self.assertLess(
            cmake.index("_ror_fresh_configure_guard"),
            cmake.index("FetchContent_Declare(\n        ror_freetype"),
        )
        self.assertNotIn(PROBE.BUILD_SENTINEL_NAME, cmake)
        self.assertIn("OgreNextHlmsPbs", cmake)
        self.assertIn("OgreNextHlmsUnlit", cmake)
        self.assertIn("OgreNextOverlay", cmake)
        required_targets = pinned_cmake[
            pinned_cmake.index("foreach (_ror_required_target IN ITEMS") :
            pinned_cmake.index(
                "endforeach ()",
                pinned_cmake.index("foreach (_ror_required_target IN ITEMS"),
            )
        ]
        for target in (
            "OgreNextMain",
            "OgreNextHlmsPbs",
            "OgreNextHlmsUnlit",
            "OgreNextOverlay",
            "${ROR_OGRE_NEXT_RENDERER_TARGET}",
        ):
            with self.subTest(required_target=target):
                self.assertIn(target, required_targets)
        self.assertIn('CMAKE_BUILD_TYPE STREQUAL "Release"', cmake)
        self.assertIn("_ror_extracted_shader_media_source_sha256", cmake)
        self.assertNotEqual(
            self._cmake_value(cmake, "ROR_OGRE_NEXT_FRAME_IMAGE"),
            self._cmake_value(cmake, "ROR_OGRE_NEXT_CTEST_FRAME_IMAGE"),
        )
        self.assertNotEqual(
            self._cmake_value(cmake, "ROR_OGRE_NEXT_FRAME_REPORT"),
            self._cmake_value(cmake, "ROR_OGRE_NEXT_CTEST_FRAME_REPORT"),
        )
        build_contract = (
            PROBE_DIR / "ogre_next_build_contract.json.in"
        ).read_text(encoding="utf-8")
        self.assertIn('"schema_version": 7', build_contract)
        self.assertIn(
            '"target_type": "@ROR_FREETYPE_TARGET_TYPE@"',
            build_contract,
        )
        self.assertNotIn("archive_fallback_url", build_contract)
        self.assertIn(
            '"overlay_link_target": '
            "@ROR_FREETYPE_OVERLAY_LINK_TARGET_JSON@",
            build_contract,
        )
        self.assertIn("native_ray_tracing\": \"not_evaluated", build_contract)
        self.assertIn('"headless_child_bootstrap": true', build_contract)
        self.assertIn(
            '"headless_child_packaged": @ROR_OGRE_NEXT_CHILD_PACKAGED_JSON@',
            build_contract,
        )
        self.assertIn(
            'set(ROR_OGRE_NEXT_CHILD_PACKAGED_JSON "false")', cmake
        )
        self.assertIn(
            '"headless_child_execution_receipt_required": true',
            build_contract,
        )
        self.assertIn('"headless_child_binary_retained": true', build_contract)
        self.assertIn('"headless_child_logs_retained": true', build_contract)
        self.assertIn(
            '"headless_child_process_model": '
            '"single-process-reviewed-source-closure-v1"',
            build_contract,
        )
        self.assertIn(
            '"headless_child_production_admitted": false', build_contract
        )
        self.assertNotIn(
            "ogre_next_probe",
            (REPOSITORY_ROOT / "CMakeLists.txt").read_text(encoding="utf-8"),
        )

    @staticmethod
    def _cmake_value(cmake: str, name: str) -> str:
        match = re.search(
            rf"set\({name}\s+\n?\s*\"([^\"]+)\"\)", cmake
        )
        if match is None:
            raise AssertionError(f"missing CMake variable {name}")
        return match.group(1)

    def test_only_release_configuration_is_accepted(self) -> None:
        parser = PROBE.build_parser()
        self.assertEqual(parser.parse_args([]).config, PROBE.REQUIRED_CONFIG)
        self.assertEqual(parser.parse_args([]).checkpoint, "all")
        self.assertEqual(
            parser.parse_args(["--checkpoint", "n1"]).checkpoint, "n1"
        )
        self.assertEqual(
            parser.parse_args(
                ["--freetype-archive", "/tmp/freetype.tar.xz"]
            ).freetype_archive,
            Path("/tmp/freetype.tar.xz"),
        )
        for config in ("", "Debug", "RelWithDebInfo", "Arbitrary"):
            with self.subTest(config=config):
                with self.assertRaises(SystemExit):
                    parser.parse_args(["--config", config])

    def test_configure_without_opt_in_fails_before_fetch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-next-opt-in-") as temp:
            result = subprocess.run(
                ["cmake", "-S", str(PROBE_DIR), "-B", temp],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("probe is opt-in", result.stdout)

    def test_cmake_rejects_freetype_source_override_before_fetch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-freetype-override-") as temp:
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(PROBE_DIR),
                    "-B",
                    temp,
                    "-DROR_OGRE_NEXT_PROBE=ON",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DFETCHCONTENT_SOURCE_DIR_ROR_FREETYPE=untrusted-source",
                ],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FETCHCONTENT_SOURCE_DIR_ROR_FREETYPE", result.stdout)
        self.assertIn("bypasses archive verification", result.stdout)

    def test_cmake_selects_primary_then_fallback_freetype_urls(self) -> None:
        freetype = self.lock["dependencies"]["freetype"]
        result = select_freetype_archive_urls(
            local_archive=None,
            expected_sha256=freetype["archive_sha256"],
            primary_url=freetype["archive_url"],
            fallback_url=freetype["archive_fallback_url"],
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(result.selected_count, 2)
        self.assertEqual(
            result.selected_urls,
            [freetype["archive_url"], freetype["archive_fallback_url"]],
        )

    def test_cmake_fetchcontent_uses_hash_verified_fallback_url(self) -> None:
        class QuietHandler(SimpleHTTPRequestHandler):
            requested_paths: list[str] = []

            def do_GET(self) -> None:
                self.requested_paths.append(self.path)
                super().do_GET()

            def log_message(self, format: str, *args: object) -> None:
                pass

        with tempfile.TemporaryDirectory(
            prefix="ror-freetype-fetch-fallback-"
        ) as temp:
            root = Path(temp).resolve()
            archive = root / "fallback.zip"
            payload = b"hash-verified fallback archive payload\n"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr(
                    "CMakeLists.txt",
                    "add_library(ror_freetype_fallback_fixture INTERFACE)\n",
                )
                bundle.writestr("payload.txt", payload)
            archive_sha256 = hashlib.sha256(archive.read_bytes()).hexdigest()
            payload_sha256 = hashlib.sha256(payload).hexdigest()
            source = root / "source"
            source.mkdir()
            marker = root / "fallback-result.txt"
            server = ThreadingHTTPServer(
                ("127.0.0.1", 0),
                functools.partial(QuietHandler, directory=str(root)),
            )
            server_thread = threading.Thread(
                target=server.serve_forever, daemon=True
            )
            server_thread.start()
            base_url = f"http://127.0.0.1:{server.server_port}"
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.24)\n"
                "project(ror_freetype_fallback_fixture NONE)\n"
                "include(FetchContent)\n"
                "FetchContent_Declare(\n"
                "    ror_freetype_fallback_fixture\n"
                f'    URL "{base_url}/missing.zip"\n'
                f'        "{base_url}/{archive.name}"\n'
                f'    URL_HASH "SHA256={archive_sha256}"\n'
                "    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)\n"
                "FetchContent_MakeAvailable(ror_freetype_fallback_fixture)\n"
                "file(SHA256\n"
                "    \"${ror_freetype_fallback_fixture_SOURCE_DIR}/payload.txt\"\n"
                "    observed_payload_sha256)\n"
                f'file(WRITE "{marker.as_posix()}" '
                '"${observed_payload_sha256}\\n")\n',
                encoding="utf-8",
            )
            try:
                result = subprocess.run(
                    ["cmake", "-S", str(source), "-B", str(root / "build")],
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                )
            finally:
                server.shutdown()
                server.server_close()
                server_thread.join(timeout=5.0)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(
                marker.read_text(encoding="utf-8"), payload_sha256 + "\n"
            )
            self.assertEqual(
                QuietHandler.requested_paths,
                ["/missing.zip", "/fallback.zip"],
            )

    def test_cmake_local_freetype_archive_is_one_verified_path(self) -> None:
        freetype = self.lock["dependencies"]["freetype"]
        with tempfile.TemporaryDirectory(prefix="ror-freetype-local-") as temp:
            archive = Path(temp) / "freetype.tar.xz"
            archive.write_bytes(b"deterministic local FreeType fixture")
            archive_sha256 = hashlib.sha256(archive.read_bytes()).hexdigest()
            result = select_freetype_archive_urls(
                local_archive=archive,
                expected_sha256=archive_sha256,
                primary_url=freetype["archive_url"],
                fallback_url=freetype["archive_fallback_url"],
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(result.selected_count, 1)
            self.assertEqual(result.selected_urls, [archive.as_posix()])

            tampered = select_freetype_archive_urls(
                local_archive=archive,
                expected_sha256="0" * 64,
                primary_url=freetype["archive_url"],
                fallback_url=freetype["archive_fallback_url"],
            )
        self.assertNotEqual(tampered.returncode, 0)
        self.assertIn("Pinned FreeType SHA-256 mismatch", tampered.stdout)

    def test_cmake_rejects_tampered_freetype_fallback_lock(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-freetype-lock-") as temp:
            temporary_root = Path(temp)
            copied_probe = temporary_root / "ogre_next_probe"
            shutil.copytree(
                PROBE_DIR,
                copied_probe,
                ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
            )
            copied_lock_path = copied_probe / LOCK_PATH.name
            copied_lock = json.loads(
                copied_lock_path.read_text(encoding="utf-8")
            )
            copied_lock["dependencies"]["freetype"][
                "archive_fallback_url"
            ] = "https://example.invalid/freetype-2.14.3.tar.xz"
            copied_lock_path.write_text(
                json.dumps(copied_lock, indent=2) + "\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(copied_probe),
                    "-B",
                    str(temporary_root / "build"),
                    "-DROR_OGRE_NEXT_PROBE=ON",
                    "-DCMAKE_BUILD_TYPE=Release",
                ],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "reviewed FreeType pin or license contract changed",
            result.stdout,
        )

    def test_lock_is_canonical_json(self) -> None:
        text = LOCK_PATH.read_text(encoding="utf-8")
        self.assertEqual(text, json.dumps(json.loads(text), indent=2) + "\n")

    def test_checked_in_metal_evidence_matches_source_contract(self) -> None:
        evidence = json.loads(EVIDENCE_PATH.read_text(encoding="utf-8"))
        provenance = evidence["provenance"]
        rapidjson = self.lock["dependencies"]["rapidjson"]
        self.assertEqual(evidence["schema"], "ror.ogre_next_probe_evidence.v2")
        self.assertEqual(evidence["result"], "pass")
        self.assertEqual(
            provenance["repository"],
            "https://github.com/oasiz-ai/rigs-of-rods",
        )
        self.assertRegex(provenance["base_commit"], r"^[0-9a-f]{40}$")
        self.assertRegex(
            provenance["integration_target_commit"], r"^[0-9a-f]{40}$"
        )
        self.assertEqual(provenance["ogre_next_commit"], self.lock["commit"])
        self.assertEqual(
            provenance["ogre_next_archive_sha256"],
            self.lock["archive_sha256"],
        )
        self.assertEqual(
            provenance["rapidjson_source_archive_license_spdx"],
            rapidjson["license_spdx"],
        )
        self.assertEqual(
            provenance["rapidjson_compiled_headers_license_spdx"],
            rapidjson["compiled_headers_spdx"],
        )
        self.assertEqual(
            provenance["rapidjson_license_sha256"],
            rapidjson["license_sha256"],
        )
        shader_media = self.lock["shader_media"]
        shader_notice = shader_media["third_party_notice"]
        self.assertEqual(
            provenance["shader_media_license_expression"],
            shader_media["license_expression"],
        )
        self.assertEqual(
            provenance["shader_media_third_party_source_path"],
            shader_notice["source_path"],
        )
        self.assertEqual(
            provenance["shader_media_third_party_source_sha256"],
            shader_notice["source_sha256"],
        )
        self.assertTrue(
            provenance["shader_media_source_and_binary_notice_required"]
        )
        self.assertTrue(provenance["shader_media_paper_reference_required"])
        for path_key, hash_key in (
            ("source_path", "source_sha256"),
            ("frame_source_path", "frame_source_sha256"),
            ("frame_validator_path", "frame_validator_sha256"),
            ("probe_config_template_path", "probe_config_template_sha256"),
            (
                "frame_config_template_path",
                "frame_config_template_sha256",
            ),
            (
                "shader_media_notice_repository_path",
                "shader_media_notice_sha256",
            ),
            ("adaptation_patch_path", "adaptation_patch_sha256"),
            ("build_contract_path", "build_contract_sha256"),
            ("runtime_report_path", "runtime_report_sha256"),
            (
                "frame_runtime_report_path",
                "frame_runtime_report_sha256",
            ),
            ("frame_image_path", "frame_image_encoded_sha256"),
        ):
            source_path = REPOSITORY_ROOT / provenance[path_key]
            self.assertTrue(source_path.is_file(), source_path)
            self.assertEqual(
                hashlib.sha256(source_path.read_bytes()).hexdigest(),
                provenance[hash_key],
            )
        # The evidence record is an immutable historical capture, not a lock
        # that prevents the standalone CMake entrypoint or wrapper from
        # evolving. Their exact hashes remain recorded for provenance while
        # the live files are checked by the contract tests above.
        for historical_hash in (
            "cmake_sha256",
            "wrapper_sha256",
            "build_contract_template_sha256",
            "lock_sha256",
        ):
            self.assertRegex(provenance[historical_hash], r"^[0-9a-f]{64}$")
        build_contract = json.loads(
            (REPOSITORY_ROOT / provenance["build_contract_path"]).read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(build_contract["shader_media"], shader_media)
        runtime_report = json.loads(
            (REPOSITORY_ROOT / provenance["runtime_report_path"]).read_text(
                encoding="utf-8"
            )
        )
        frame_runtime_report = json.loads(
            (
                REPOSITORY_ROOT
                / provenance["frame_runtime_report_path"]
            ).read_text(encoding="utf-8")
        )
        build_contract["schema_version"] = 3
        build_contract["patches"] = copy.deepcopy(self.lock["patches"])
        build_contract["reflection_shader_media"] = copy.deepcopy(
            self.lock["reflection_shader_media"]
        )
        build_contract["shader_media"] = PROBE.expected_build_shader_media(
            self.lock
        )
        PROBE.validate_build_contract(build_contract, self.lock, self.policy)
        PROBE.validate_report(runtime_report, self.lock, self.policy)
        self.assertEqual(
            evidence["abi"]["cookie"], runtime_report["build"]["abi_cookie"]
        )
        self.assertEqual(
            evidence["capabilities"]["renderer_target"],
            runtime_report["capabilities"]["renderer"]["target"],
        )
        self.assertEqual(frame_runtime_report["status"], "pass")
        self.assertEqual(
            frame_runtime_report["platform_policy"],
            runtime_report["build"]["platform_policy"],
        )
        for field in FRAME.SHADER_MEDIA_PROVENANCE:
            with self.subTest(shader_media_field=field):
                self.assertEqual(
                    frame_runtime_report["provenance"][field],
                    runtime_report["provenance"][field],
                )
        self.assertEqual(
            evidence["frame"], frame_runtime_report["frame"]
        )
        self.assertEqual(
            frame_runtime_report["native_ray_tracing"], "not_evaluated"
        )
        self.assertTrue(provenance["frame_image_retained"])
        self.assertEqual(
            provenance["frame_image_encoding"], "base64-gzip-p6-ppm"
        )
        self.assertRegex(provenance["frame_image_sha256"], r"^[0-9a-f]{64}$")
        self.assertRegex(
            provenance["frame_build_artifact_sha256"], r"^[0-9a-f]{64}$"
        )
        encoded_image = (
            REPOSITORY_ROOT / provenance["frame_image_path"]
        ).read_bytes()
        compressed_image = base64.b64decode(
            b"".join(encoded_image.split()), validate=True
        )
        ppm_image = gzip.decompress(compressed_image)
        self.assertEqual(
            hashlib.sha256(ppm_image).hexdigest(),
            provenance["frame_image_sha256"],
        )
        with tempfile.TemporaryDirectory(
            prefix="ror-checked-in-ogre-next-frame-"
        ) as temp:
            ppm_path = Path(temp) / provenance["frame_image_name"]
            ppm_path.write_bytes(ppm_image)
            observed = FRAME.validate(
                frame_runtime_report,
                FRAME.read_ppm(ppm_path),
                runtime_report["build"]["platform_policy"],
                runtime_report,
            )
        self.assertEqual(observed["rgb8_fnv1a64"], "47f35fe4bdec9207")
        self.assertNotIn("build_artifact_path", provenance)
        self.assertFalse(provenance["build_artifact_retained"])
        self.assertRegex(
            provenance["build_artifact_sha256"], r"^[0-9a-f]{64}$"
        )
        self.assertTrue(
            all(
                not argument.startswith("/")
                for argument in provenance["build_invocation"]
            )
        )
        self.assertEqual(
            evidence["capabilities"]["native_ray_tracing"],
            "not_evaluated",
        )


if __name__ == "__main__":
    unittest.main()
