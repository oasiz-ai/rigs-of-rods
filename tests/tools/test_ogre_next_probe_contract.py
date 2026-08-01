#!/usr/bin/env python3
"""Offline contract tests for the pinned OGRE-Next integration probe."""

from __future__ import annotations

import base64
import copy
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
FRAME_TOOL_PATH = (
    REPOSITORY_ROOT / "tools" / "validate_ogre_next_frame_probe.py"
)
PROBE_DIR = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
CMAKE_PATH = PROBE_DIR / "CMakeLists.txt"
PINNED_CMAKE_PATH = PROBE_DIR / "cmake" / "PinnedOgreNext.cmake"
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
        self.assertEqual(self.lock["schema_version"], 2)
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
        PROBE.validate_build_contract(contract, self.lock, self.policy)
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
                invalid = copy.deepcopy(contract)
                mutate(invalid)
                with self.assertRaises(PROBE.ProbeError):
                    PROBE.validate_build_contract(invalid, self.lock, self.policy)

    def test_cmake_is_opt_in_and_forbids_unverified_source_overrides(self) -> None:
        entry_cmake = CMAKE_PATH.read_text(encoding="utf-8")
        pinned_cmake = PINNED_CMAKE_PATH.read_text(encoding="utf-8")
        cmake = entry_cmake + "\n" + pinned_cmake
        self.assertIn("ROR_OGRE_NEXT_PROBE", cmake)
        self.assertIn("cmake/PinnedOgreNext.cmake", entry_cmake)
        self.assertIn("if (TARGET OgreMain)", pinned_cmake)
        self.assertIn("URL_HASH \"SHA256=${ROR_OGRE_NEXT_ARCHIVE_SHA256}\"", cmake)
        self.assertIn("URL_HASH \"SHA256=${ROR_RAPIDJSON_ARCHIVE_SHA256}\"", cmake)
        self.assertIn("FETCHCONTENT_SOURCE_DIR_OGRE_NEXT", cmake)
        self.assertIn("FETCHCONTENT_SOURCE_DIR_RAPIDJSON", cmake)
        self.assertIn("SHADERC ROR_GLSLANG_SOURCE", cmake)
        self.assertNotIn("ROR_SHADERC_SOURCE ROR_GLSLANG_SOURCE", cmake)
        self.assertLess(
            cmake.index("_ror_fresh_configure_guard"),
            cmake.index("FetchContent_Declare(\n    rapidjson"),
        )
        self.assertNotIn(PROBE.BUILD_SENTINEL_NAME, cmake)
        self.assertIn("OgreNextHlmsPbs", cmake)
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
        self.assertIn("native_ray_tracing\": \"not_evaluated", build_contract)
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
            ("lock_path", "lock_sha256"),
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
        ):
            self.assertRegex(provenance[historical_hash], r"^[0-9a-f]{64}$")
        build_contract = json.loads(
            (REPOSITORY_ROOT / provenance["build_contract_path"]).read_text(
                encoding="utf-8"
            )
        )
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
        PROBE.validate_build_contract(build_contract, self.lock, self.policy)
        PROBE.validate_report(runtime_report, self.lock, self.policy)
        self.assertEqual(build_contract["shader_media"], shader_media)
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
