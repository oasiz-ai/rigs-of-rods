#!/usr/bin/env python3
"""Offline fail-closed tests for the Windows D3D11On12/DXR RT7 proof."""

from __future__ import annotations

from collections import Counter
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = REPOSITORY_ROOT / "tools/ogre_next_probe/run_windows_dxr7.py"
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_windows_dxr7_for_tests", RUNNER_PATH
)
if RUNNER_SPEC is None or RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load Windows DXR7 runner")
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


class OgreNextWindowsDxr7ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ogre_lock = RUNNER.MAIN_RUNNER.load_lock()
        cls.dxr7_lock = RUNNER.load_dxr7_lock()
        cls.workflow = (
            REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")
        cls.source_identity = {
            "repository": "https://github.com/oasiz-ai/rigs-of-rods",
            "ref": "codex/fixture",
            "commit": "1" * 40,
            "relevant_manifest_sha256": "2" * 64,
            "relevant_manifest_file_count": 23,
        }
        cls.nonce = "a" * 64
        cls.dxc_closure = {
            "provider": "Windows SDK",
            "sdk_version": "10.0.26100.0",
            "sdk_bin_root": "c:/program files (x86)/windows kits/10/bin",
            "x64_directory": (
                "c:/program files (x86)/windows kits/10/bin/"
                "10.0.26100.0/x64"
            ),
            "dxc_path": (
                "c:/program files (x86)/windows kits/10/bin/"
                "10.0.26100.0/x64/dxc.exe"
            ),
            "dxc_version": (
                "dxcompiler.dll: 1.8 - 1.8.2502.11 (239921522); "
                "dxil.dll: 1.8(1.8.2502.11)"
            ),
            "components": {
                "dxc.exe": {
                    "path": "dxc.exe",
                    "bytes": 100,
                    "sha256": "3" * 64,
                },
                "dxcompiler.dll": {
                    "path": "dxcompiler.dll",
                    "bytes": 200,
                    "sha256": "4" * 64,
                },
                "dxil.dll": {
                    "path": "dxil.dll",
                    "bytes": 300,
                    "sha256": "5" * 64,
                },
            },
        }
        cls.frame_pixels = cls.make_frame_pixels()
        colours = [
            cls.frame_pixels[offset : offset + 3]
            for offset in range(0, len(cls.frame_pixels), 3)
        ]
        counts = Counter(colours)
        cls.frame_metrics = {
            "width": 192,
            "height": 128,
            "distinct_rgb8_values": len(counts),
            "non_background_pixels": len(colours) - max(counts.values()),
            "rgb8_fnv1a64": f"{RUNNER._fnv1a64(cls.frame_pixels):016x}",
        }

    @staticmethod
    def make_frame_pixels() -> bytes:
        payload = bytearray()
        for y in range(128):
            for x in range(192):
                if 32 <= x < 160 and 24 <= y < 104:
                    payload.extend((x & 0xFF, (y * 2) & 0xFF, (x + y) & 0xFF))
                else:
                    payload.extend((2, 3, 5))
        return bytes(payload)

    def write_frame(self, path: Path) -> None:
        path.write_bytes(b"P6\n192 128\n255\n" + self.frame_pixels)

    def make_pass_report(self) -> dict[str, object]:
        return {
            "schema": RUNNER.SCHEMA,
            "status": "pass",
            "reason": "",
            "execution": {
                "challenge_nonce": self.nonce,
                "probe_binary_marker": RUNNER.expected_binary_marker(
                    self.source_identity
                ),
            },
            "scope": {
                "external_d3d11on12_foundation": True,
                "hardware_dxr_pass": True,
                "native_ray_tracing": "dispatch_rays",
                "acceleration_structure_built": True,
                "ray_traced_probe_readback": True,
                "ray_traced_image_produced": False,
                "ogre_raster_image_produced": True,
                "hybrid_ogre_image_composite": False,
                "limitation": RUNNER.SCOPE_LIMITATION,
            },
            "provenance": {
                "ror_repository": self.source_identity["repository"],
                "ror_ref": self.source_identity["ref"],
                "ror_commit": self.source_identity["commit"],
                "ror_relevant_source_manifest_sha256": self.source_identity[
                    "relevant_manifest_sha256"
                ],
                "ror_relevant_source_manifest_file_count": self.source_identity[
                    "relevant_manifest_file_count"
                ],
                "ogre_next_repository": self.ogre_lock["repository"],
                "ogre_next_branch": self.ogre_lock["branch"],
                "ogre_next_commit": self.ogre_lock["commit"],
                "ogre_next_archive_sha256": self.ogre_lock["archive_sha256"],
                "ogre_next_license_spdx": self.ogre_lock["license"]["spdx"],
                "ogre_next_license_sha256": self.ogre_lock["license"][
                    "sha256"
                ],
                "dxr7_toolchain_lock_sha256": RUNNER.LOCK_SHA256,
                "ogre_adaptation_patch_path": self.dxr7_lock[
                    "adaptation_patch"
                ]["path"],
                "ogre_adaptation_patch_sha256": self.dxr7_lock[
                    "adaptation_patch"
                ]["sha256"],
                "hlsl_source_sha256": self.dxr7_lock["shader"]["sha256"],
                "dxc_executable_sha256": self.dxc_closure["components"][
                    "dxc.exe"
                ]["sha256"],
                "dxcompiler_dll_sha256": self.dxc_closure["components"][
                    "dxcompiler.dll"
                ]["sha256"],
                "dxil_dll_sha256": self.dxc_closure["components"]["dxil.dll"][
                    "sha256"
                ],
                "dxc_sdk_version": self.dxc_closure["sdk_version"],
                "dxc_version": self.dxc_closure["dxc_version"],
                "dxc_path": self.dxc_closure["dxc_path"],
                "dxc_x64_directory": self.dxc_closure["x64_directory"],
            },
            "build": {
                "platform_policy": "windows-x64-d3d11on12-dxr",
                "system": "Windows",
                "processor": "AMD64",
                "compiler_id": "MSVC",
                "compiler_version": "19.44",
                "ogre_version": "3.0.0",
                "pointer_bits": 64,
            },
            "adapter": {
                "name": "fixture hardware",
                "luid": "0000000000000042",
                "vendor_id": 4318,
                "device_id": 9860,
                "software_adapter": False,
                "d3d12_feature_level": 0xC000,
                "d3d11_feature_level": 0xB100,
                "raytracing_tier": 11,
                "candidate_decision": "accept",
            },
            "ownership": {
                "app_owned_d3d12_device": True,
                "app_owned_direct_queue": True,
                "app_owned_fence": True,
                "d3d11on12_device_created": True,
                "d3d11on12_created_with_exact_direct_queue": True,
                "d3d11on12_underlying_d3d12_device_exact": True,
                "d3d11on12_adapter_luid_exact": True,
                "ogre_plugin_option": "external_device",
                "ogre_external_device_option_used": True,
                "ogre_d3d11_device_exact": True,
                "ogre_external_device_active": True,
            },
            "ray_tracing": {
                "blas_built": True,
                "tlas_built": True,
                "state_object_created": True,
                "shader_identifiers_resolved": True,
                "dispatch_rays_called": True,
                "dispatch_width": 1,
                "dispatch_height": 1,
                "dispatch_depth": 1,
                "readback_value": 0xD1CEB00B,
                "closest_hit_readback_exact": True,
            },
            "ogre_frame": {
                "native_hidden_window_created": True,
                "pbs_material_created": True,
                "compositor_workspace_created": True,
                "frame_submitted": True,
                "gpu_readback_completed": True,
                "nonblank": True,
                "ui_included": False,
                "resources_destroyed_before_ogre_shutdown": True,
                "workspace_removed": True,
                "workspace_definition_removed": True,
                "render_target_destroyed": True,
                "scene_destroyed": True,
                "pbs_datablock_destroyed": True,
                "pbs_hlms_unregistered": True,
                "native_window_destroyed": True,
                "root_shutdown_completed": True,
                **self.frame_metrics,
            },
            "synchronization": {
                "fence_before_dispatch": 1,
                "fence_after_dispatch": 2,
                "fence_after_ogre": 3,
            },
            "lifecycle": {
                "ogre_shutdown_before_d3d11_release": True,
                "d3d11_context_flushed_before_release": True,
                "d3d11_released_before_d3d12_queue": True,
                "d3d12_queue_released_before_device": True,
                "shutdown_completed": True,
            },
        }

    def make_unsupported_report(self) -> dict[str, object]:
        report = self.make_pass_report()
        report["status"] = "unsupported"
        report["reason"] = "no attested DXR7 adapter: no_hardware_adapter"
        scope = report["scope"]
        scope["external_d3d11on12_foundation"] = False
        scope["hardware_dxr_pass"] = False
        scope["native_ray_tracing"] = "unsupported"
        scope["acceleration_structure_built"] = False
        scope["ray_traced_probe_readback"] = False
        scope["ogre_raster_image_produced"] = False
        report["adapter"] = {
            "name": "",
            "luid": "",
            "vendor_id": 0,
            "device_id": 0,
            "software_adapter": False,
            "d3d12_feature_level": 0,
            "d3d11_feature_level": 0,
            "raytracing_tier": 0,
            "candidate_decision": "no_hardware_adapter",
        }
        for key in report["ownership"]:
            if key != "ogre_plugin_option":
                report["ownership"][key] = False
        for key in report["ray_tracing"]:
            report["ray_tracing"][key] = (
                False if isinstance(report["ray_tracing"][key], bool) else 0
            )
        report["ogre_frame"] = {
            "native_hidden_window_created": False,
            "pbs_material_created": False,
            "compositor_workspace_created": False,
            "frame_submitted": False,
            "gpu_readback_completed": False,
            "nonblank": False,
            "ui_included": False,
            "resources_destroyed_before_ogre_shutdown": False,
            "workspace_removed": False,
            "workspace_definition_removed": False,
            "render_target_destroyed": False,
            "scene_destroyed": False,
            "pbs_datablock_destroyed": False,
            "pbs_hlms_unregistered": False,
            "native_window_destroyed": False,
            "root_shutdown_completed": False,
            "width": 0,
            "height": 0,
            "distinct_rgb8_values": 0,
            "non_background_pixels": 0,
            "rgb8_fnv1a64": "0000000000000000",
        }
        report["synchronization"] = {
            "fence_before_dispatch": 0,
            "fence_after_dispatch": 0,
            "fence_after_ogre": 0,
        }
        report["lifecycle"] = {
            "ogre_shutdown_before_d3d11_release": False,
            "d3d11_context_flushed_before_release": False,
            "d3d11_released_before_d3d12_queue": False,
            "d3d12_queue_released_before_device": False,
            "shutdown_completed": True,
        }
        return report

    def validate(self, report: dict[str, object], exit_code: int = 0) -> None:
        with tempfile.TemporaryDirectory() as directory:
            frame = Path(directory) / RUNNER.OGRE_FRAME_NAME
            if report["status"] == "pass":
                self.write_frame(frame)
            RUNNER.validate_report(
                report,
                exit_code,
                self.ogre_lock,
                self.dxr7_lock,
                self.source_identity,
                self.dxc_closure,
                self.nonce,
                frame,
            )

    def verify_fixture(
        self,
        root: Path,
        *,
        integrity_only: bool = False,
        trusted_attestation_bundle: Path | None = None,
        expected_source_ref: str | None = None,
    ) -> dict[str, object]:
        executable = root / "bin/ror_ogre_next_windows_dxr7_smoke.exe"
        executable.parent.mkdir(exist_ok=True)
        executable.write_bytes(b"fixture executable")
        dxil = root / RUNNER.DXIL_RELATIVE
        dxil.parent.mkdir(exist_ok=True)
        dxil.write_bytes(b"fixture dxil")
        report = self.make_pass_report()
        (root / RUNNER.REPORT_NAME).write_text(
            json.dumps(report), encoding="utf-8"
        )
        (root / RUNNER.ATTESTATION_NAME).write_text(
            json.dumps(
                {
                    "execution": {
                        "observed_process_exit_code": 0,
                        "challenge_nonce": self.nonce,
                    }
                }
            ),
            encoding="utf-8",
        )
        (root / RUNNER.EXECUTION_RECEIPT_NAME).write_text(
            "{}\n", encoding="utf-8"
        )
        build_context = {"fixture": True}
        with mock.patch.object(
            RUNNER.MAIN_RUNNER, "require_relevant_source_clean"
        ), mock.patch.object(
            RUNNER.MAIN_RUNNER,
            "ror_source_identity",
            return_value=self.source_identity,
        ), mock.patch.object(
            RUNNER, "validate_build_context", return_value=build_context
        ), mock.patch.object(
            RUNNER, "recorded_dxc_closure", return_value=self.dxc_closure
        ), mock.patch.object(
            RUNNER, "executable_path", return_value=executable
        ), mock.patch.object(
            RUNNER, "validate_pe_executable", return_value={"format": "PE32+"}
        ), mock.patch.object(
            RUNNER,
            "validate_dxil_container",
            return_value={"format": "DXBC/DXIL"},
        ), mock.patch.object(
            RUNNER, "validate_report"
        ), mock.patch.object(
            RUNNER, "validate_execution_receipt"
        ), mock.patch.object(
            RUNNER, "validate_attestation"
        ), mock.patch.object(
            RUNNER, "require_dxc_closure_unchanged"
        ), mock.patch.object(
            RUNNER.MAIN_RUNNER, "require_source_identity_unchanged"
        ):
            return RUNNER.verify_existing(
                root,
                integrity_only=integrity_only,
                trusted_attestation_bundle=trusted_attestation_bundle,
                expected_source_ref=expected_source_ref,
            )

    def test_static_contract_and_locked_sources(self) -> None:
        RUNNER.validate_static_contract()
        cmake_source = (RUNNER.PROBE_SOURCE / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertEqual(
            RUNNER.sha256_file(RUNNER.PROBE_SOURCE / RUNNER.LOCK_NAME),
            RUNNER.LOCK_SHA256,
        )
        self.assertEqual(
            RUNNER.expected_binary_marker(self.source_identity),
            "ror-ogre-next-dxr7-pe-v2:"
            + "1" * 40
            + ":"
            + "2" * 64,
        )
        self.assertIn(
            '"ror-ogre-next-dxr7-pe-v2:${ROR_SOURCE_COMMIT}:'
            '${ROR_SOURCE_MANIFEST_SHA256}"',
            cmake_source,
        )
        # Parentheses are legal in the Windows environment-variable name but
        # must be escaped inside CMake's $ENV{} grammar. An unescaped spelling
        # passes every non-Windows static test and fails during configure.
        self.assertIn(r"$ENV{ProgramFiles\(x86\)}", cmake_source)
        self.assertNotIn("$ENV{ProgramFiles(x86)}", cmake_source)
        self.assertIn(
            "ROR_WINDOWS_DXR7_DXC_VERSION_C_LITERAL", cmake_source
        )
        config_template = (
            RUNNER.PROBE_SOURCE / "windows_dxr7_config.h.in"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "@ROR_WINDOWS_DXR7_DXC_VERSION_C_LITERAL@", config_template
        )
        self.assertNotIn(
            'MATCHES "[\\\\\\\";]"', cmake_source
        )

    def test_complete_dispatch_and_real_ogre_frame_report_passes(self) -> None:
        self.validate(self.make_pass_report())

    def test_every_material_pass_claim_is_fail_closed(self) -> None:
        mutations = (
            ("scope", "hardware_dxr_pass", False),
            ("adapter", "software_adapter", True),
            ("adapter", "raytracing_tier", 10),
            ("ownership", "app_owned_direct_queue", False),
            ("ownership", "ogre_d3d11_device_exact", False),
            ("ray_tracing", "blas_built", False),
            ("ray_tracing", "dispatch_rays_called", False),
            ("ray_tracing", "readback_value", 0x0BADCAFE),
            ("ogre_frame", "native_hidden_window_created", False),
            ("ogre_frame", "gpu_readback_completed", False),
            ("ogre_frame", "pbs_datablock_destroyed", False),
            ("ogre_frame", "root_shutdown_completed", False),
            ("ogre_frame", "non_background_pixels", 1),
            ("synchronization", "fence_after_ogre", 2),
            ("lifecycle", "d3d12_queue_released_before_device", False),
        )
        for section, field, value in mutations:
            with self.subTest(section=section, field=field):
                report = self.make_pass_report()
                report[section][field] = value
                with self.assertRaises(RUNNER.Dxr7Error):
                    self.validate(report)

    def test_report_rejects_wrong_exit_nonce_and_toolchain(self) -> None:
        report = self.make_pass_report()
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report, RUNNER.UNSUPPORTED_EXIT_CODE)
        report = self.make_pass_report()
        report["execution"]["challenge_nonce"] = "b" * 64
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report)
        report = self.make_pass_report()
        report["provenance"]["dxcompiler_dll_sha256"] = "0" * 64
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report)

    def test_honest_no_hardware_skip_passes_without_frame(self) -> None:
        self.validate(self.make_unsupported_report(), RUNNER.UNSUPPORTED_EXIT_CODE)

    def test_unsupported_reason_cannot_hide_partial_claims(self) -> None:
        report = self.make_unsupported_report()
        report["ray_tracing"]["dispatch_rays_called"] = True
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report, RUNNER.UNSUPPORTED_EXIT_CODE)
        report = self.make_unsupported_report()
        report["scope"]["external_d3d11on12_foundation"] = True
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report, RUNNER.UNSUPPORTED_EXIT_CODE)

    def test_tier_1_0_skip_requires_exact_consistent_identity(self) -> None:
        report = self.make_unsupported_report()
        report["reason"] = "no attested DXR7 adapter: dxr_tier_below_1_1"
        report["adapter"] = {
            "name": "fixture tier 1.0 hardware",
            "luid": "0000000000000042",
            "vendor_id": 4318,
            "device_id": 9860,
            "software_adapter": False,
            "d3d12_feature_level": 0xC000,
            "d3d11_feature_level": 0,
            "raytracing_tier": 10,
            "candidate_decision": "dxr_tier_below_1_1",
        }
        self.validate(report, RUNNER.UNSUPPORTED_EXIT_CODE)
        report["adapter"]["raytracing_tier"] = 11
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report, RUNNER.UNSUPPORTED_EXIT_CODE)

    @staticmethod
    def write_valid_pe(path: Path, marker: str) -> None:
        contents = bytearray(2048)
        contents[:2] = b"MZ"
        struct.pack_into("<I", contents, 0x3C, 0x80)
        contents[0x80:0x84] = b"PE\0\0"
        coff = 0x84
        struct.pack_into("<HHIIIHH", contents, coff, 0x8664, 1, 0, 0, 0, 0xF0, 0x0002)
        optional = coff + 20
        struct.pack_into("<H", contents, optional, 0x20B)
        struct.pack_into("<I", contents, optional + 16, 0x1000)
        struct.pack_into("<I", contents, optional + 56, 0x2000)
        struct.pack_into("<I", contents, optional + 60, 0x200)
        struct.pack_into("<H", contents, optional + 68, 3)
        section = optional + 0xF0
        contents[section : section + 8] = b".text\0\0\0"
        struct.pack_into("<I", contents, section + 8, 0x400)
        struct.pack_into("<I", contents, section + 16, 0x600)
        struct.pack_into("<I", contents, section + 20, 0x200)
        struct.pack_into("<I", contents, section + 36, 0x60000020)
        contents[0x200:0x800] = b"\x90" * 0x600
        marker_bytes = marker.encode("utf-8")
        contents[0x300 : 0x300 + len(marker_bytes)] = marker_bytes
        path.write_bytes(contents)

    @staticmethod
    def write_valid_dxil(path: Path) -> None:
        bitcode = b"BC\xc0\xde" + b"fixture" + b"RayGen\0Miss\0ClosestHit\0"
        program_size = 24 + len(bitcode)
        program = bytearray((program_size + 3) & ~3)
        struct.pack_into("<II4sIII", program, 0, 0x00060065,
                         len(program) // 4, b"DXIL", 0x00000108, 16,
                         len(bitcode))
        program[24 : 24 + len(bitcode)] = bitcode
        parts = [(b"DXIL", bytes(program)), (b"SFI0", b"\0" * 8)]
        header_size = 32 + len(parts) * 4
        offsets = []
        body = bytearray()
        for fourcc, payload in parts:
            while (header_size + len(body)) % 4:
                body.append(0)
            offsets.append(header_size + len(body))
            body.extend(fourcc)
            body.extend(struct.pack("<I", len(payload)))
            body.extend(payload)
        container_size = header_size + len(body)
        header = bytearray()
        header.extend(b"DXBC")
        header.extend(b"\x11" * 16)
        header.extend(struct.pack("<III", 1, container_size, len(parts)))
        header.extend(struct.pack(f"<{len(offsets)}I", *offsets))
        path.write_bytes(bytes(header + body))

    def test_pe_validator_rejects_dummy_pe_and_accepts_structured_x64(self) -> None:
        marker = RUNNER.expected_binary_marker(self.source_identity)
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "probe.exe"
            executable.write_bytes(b"MZ" + b"not a PE" * 200)
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.validate_pe_executable(executable, marker)
            self.write_valid_pe(executable, marker)
            semantics = RUNNER.validate_pe_executable(executable, marker)
            self.assertEqual(semantics["machine"], "x86_64")
            self.assertGreater(semantics["text_bytes"], 0)

    def test_dxil_validator_rejects_dummy_dxil_and_requires_exports(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            dxil = Path(directory) / "probe.dxil"
            dxil.write_bytes(b"DXBC" + b"not DXIL" * 20)
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.validate_dxil_container(dxil, ["RayGen", "Miss", "ClosestHit"])
            self.write_valid_dxil(dxil)
            semantics = RUNNER.validate_dxil_container(
                dxil, ["RayGen", "Miss", "ClosestHit"]
            )
            self.assertIn("DXIL", semantics["part_fourccs"])
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.validate_dxil_container(dxil, ["ForgedExport"])

    def test_dxc_closure_rejects_path_and_missing_dlls(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            program_files_x86 = Path(directory) / "Program Files (x86)"
            root = program_files_x86 / "Windows Kits/10/bin"
            x64 = root / "10.0.26100.0/x64"
            x64.mkdir(parents=True)
            for name in ("dxc.exe", "dxcompiler.dll", "dxil.dll"):
                (x64 / name).write_bytes(name.encode("ascii"))
            with mock.patch.object(
                RUNNER.platform, "system", return_value="Windows"
            ), mock.patch.dict(
                os.environ,
                {"ProgramFiles(x86)": str(program_files_x86)},
            ):
                closure = RUNNER.validate_dxc_closure(
                    x64 / "dxc.exe", root, execute_version=False
                )
                self.assertEqual(closure["sdk_version"], "10.0.26100.0")
                fake_root = Path(directory) / "unreviewed-sdk"
                with self.assertRaises(RUNNER.Dxr7Error):
                    RUNNER.validate_dxc_closure(
                        x64 / "dxc.exe", fake_root, execute_version=False
                    )
                (x64 / "dxil.dll").unlink()
                with self.assertRaises(RUNNER.Dxr7Error):
                    RUNNER.validate_dxc_closure(
                        x64 / "dxc.exe", root, execute_version=False
                    )
                arbitrary = root / "dxc.exe"
                arbitrary.write_bytes(b"dxc")
                with self.assertRaises(RUNNER.Dxr7Error):
                    RUNNER.validate_dxc_closure(
                        arbitrary, root, execute_version=False
                    )

    def test_recorded_dxc_closure_is_offline_and_cache_bound(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeCache.txt").write_text(
                "ROR_OGRE_NEXT_DXC_EXECUTABLE:FILEPATH="
                + self.dxc_closure["dxc_path"]
                + "\nROR_OGRE_NEXT_DXC_SDK_BIN_ROOT:PATH="
                + self.dxc_closure["sdk_bin_root"]
                + "\n",
                encoding="utf-8",
            )
            self.assertEqual(
                RUNNER.recorded_dxc_closure(root, self.dxc_closure),
                self.dxc_closure,
            )
            mutated = copy.deepcopy(self.dxc_closure)
            mutated["components"]["dxc.exe"]["bytes"] = True
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.recorded_dxc_closure(root, mutated)
            mutated = copy.deepcopy(self.dxc_closure)
            mutated["dxc_path"] = (
                "c:/program files (x86)/windows kits/10/bin/"
                "10.0.26100.0/x64/stale-dxc.exe"
            )
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.recorded_dxc_closure(root, mutated)

    def test_attestation_binds_exit_build_receipt_and_honest_claim_scope(self) -> None:
        report = self.make_pass_report()
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict(
            os.environ, {"GITHUB_ACTIONS": "false"}
        ):
            root = Path(directory)
            report_path = root / RUNNER.REPORT_NAME
            executable = root / "ror_ogre_next_windows_dxr7_smoke.exe"
            dxil = root / RUNNER.DXIL_RELATIVE
            frame = root / RUNNER.OGRE_FRAME_NAME
            receipt_path = root / RUNNER.EXECUTION_RECEIPT_NAME
            dxil.parent.mkdir()
            report_path.write_text(json.dumps(report), encoding="utf-8")
            executable.write_bytes(b"structured fixture PE")
            dxil.write_bytes(b"structured fixture DXIL")
            self.write_frame(frame)
            build_context = {
                "sentinel": {"path": "sentinel", "bytes": 1, "sha256": "6" * 64},
                "build_contract": {"path": "contract", "bytes": 2, "sha256": "7" * 64},
                "cmake_cache": {"path": "cache", "bytes": 3, "sha256": "8" * 64},
            }
            receipt = RUNNER.make_execution_receipt(
                report_path, executable, dxil, frame, report, 0,
                self.source_identity, build_context, self.dxc_closure, False,
            )
            RUNNER.write_json_atomically(receipt_path, receipt)
            RUNNER.validate_execution_receipt(
                receipt,
                report_path,
                executable,
                dxil,
                frame,
                report,
                self.source_identity,
                build_context,
                self.dxc_closure,
            )
            mutated_receipt = copy.deepcopy(receipt)
            mutated_receipt["subjects"]["report"]["bytes"] = True
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.validate_execution_receipt(
                    mutated_receipt,
                    report_path,
                    executable,
                    dxil,
                    frame,
                    report,
                    self.source_identity,
                    build_context,
                    self.dxc_closure,
                )
            pe_semantics = {"format": "PE32+"}
            dxil_semantics = {"format": "DXBC/DXIL"}
            attestation = RUNNER.make_attestation(
                report_path, executable, dxil, frame, receipt_path, report, 0,
                self.source_identity, build_context, self.dxc_closure,
                pe_semantics, dxil_semantics,
            )
            RUNNER.validate_attestation(
                attestation, report_path, executable, dxil, frame,
                receipt_path, report, self.source_identity, build_context,
                self.dxc_closure, pe_semantics, dxil_semantics,
            )
            self.assertFalse(
                attestation["execution"]["offline_artifact_proves_execution"]
            )
            mutated = copy.deepcopy(attestation)
            mutated["execution"]["observed_process_exit_code"] = 77
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.validate_attestation(
                    mutated, report_path, executable, dxil, frame,
                    receipt_path, report, self.source_identity, build_context,
                    self.dxc_closure, pe_semantics, dxil_semantics,
                )
            mutated = copy.deepcopy(attestation)
            mutated["build_context"]["sentinel"]["bytes"] = True
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.validate_attestation(
                    mutated, report_path, executable, dxil, frame,
                    receipt_path, report, self.source_identity, build_context,
                    self.dxc_closure, pe_semantics, dxil_semantics,
                )

    def test_verify_existing_rejects_fully_fabricated_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / RUNNER.REPORT_NAME).write_text(
                json.dumps(self.make_pass_report()), encoding="utf-8"
            )
            (root / RUNNER.ATTESTATION_NAME).write_text("{}", encoding="utf-8")
            (root / RUNNER.EXECUTION_RECEIPT_NAME).write_text("{}", encoding="utf-8")
            executable = root / "bin/ror_ogre_next_windows_dxr7_smoke.exe"
            executable.parent.mkdir()
            executable.write_bytes(b"MZ" + b"fabricated" * 100)
            dxil = root / RUNNER.DXIL_RELATIVE
            dxil.parent.mkdir(parents=True)
            dxil.write_bytes(b"DXBC" + b"fabricated" * 100)
            with mock.patch.object(
                RUNNER.MAIN_RUNNER, "require_relevant_source_clean"
            ), mock.patch.object(
                RUNNER.MAIN_RUNNER,
                "ror_source_identity",
                return_value=self.source_identity,
            ), self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.verify_existing(root, integrity_only=True)

    def test_unsigned_verification_requires_explicit_untrusted_scope(self) -> None:
        with self.assertRaisesRegex(RUNNER.Dxr7Error, "integrity-only"):
            RUNNER.verify_existing(Path("synthetic-unsigned-pass"))

    def test_integrity_only_pass_is_never_reported_as_trusted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = self.verify_fixture(
                Path(directory), integrity_only=True
            )
        self.assertEqual(result["artifact_status"], "pass")
        self.assertEqual(result["verification_scope"], "integrity_only")
        self.assertFalse(result["trusted_hardware_dxr_pass"])
        self.assertFalse(result["dsse_bundle"]["present"])
        self.assertNotIn("status", result)

    def test_trusted_pass_requires_dsse_verifier_success(self) -> None:
        source_ref = "refs/heads/codex/dxr7-fixture"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = root / RUNNER.DSSE_BUNDLE_NAME
            bundle.write_text("{}\n", encoding="utf-8")
            dsse_record = {
                "present": True,
                **RUNNER.artifact_record(bundle, RUNNER.DSSE_BUNDLE_NAME),
                "verified_attestations": 1,
            }
            with mock.patch.object(
                RUNNER,
                "validate_trusted_dsse_bundle",
                return_value=dsse_record,
            ) as verify_dsse:
                result = self.verify_fixture(
                    root,
                    trusted_attestation_bundle=bundle,
                    expected_source_ref=source_ref,
                )
        verify_dsse.assert_called_once()
        self.assertEqual(result["verification_scope"], "github_sigstore_dsse")
        self.assertTrue(result["trusted_hardware_dxr_pass"])
        self.assertEqual(result["signer"]["source_ref"], source_ref)

    def test_trusted_dsse_binds_repository_workflow_ref_and_commit(self) -> None:
        source_ref = "refs/heads/codex/dxr7-fixture"
        receipt = {
            "ci": {
                "provider": "github-actions",
                "repository": RUNNER.TRUSTED_REPOSITORY,
                "workflow_ref": (
                    RUNNER.TRUSTED_SIGNER_WORKFLOW + "@" + source_ref
                ),
                "ref": source_ref,
                "sha": self.source_identity["commit"],
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            receipt_path = root / RUNNER.EXECUTION_RECEIPT_NAME
            receipt_path.write_text("{}\n", encoding="utf-8")
            bundle = root / RUNNER.DSSE_BUNDLE_NAME
            bundle.write_text(
                '{"mediaType":"application/vnd.dev.sigstore.bundle.v0.3+json"}\n',
                encoding="utf-8",
            )
            completed = mock.Mock(
                returncode=0,
                stdout='[{"verificationResult":{}}]',
                stderr="",
            )
            with mock.patch.object(
                RUNNER.shutil,
                "which",
                return_value="C:/Program Files/GitHub CLI/gh.exe",
            ), mock.patch.object(
                RUNNER.subprocess, "run", return_value=completed
            ) as run:
                result = RUNNER.validate_trusted_dsse_bundle(
                    bundle,
                    receipt_path,
                    receipt,
                    self.source_identity,
                    source_ref,
                )
            self.assertTrue(result["present"])
            self.assertEqual(result["verified_attestations"], 1)
            command = run.call_args.args[0]
            self.assertIn(RUNNER.TRUSTED_REPOSITORY, command)
            self.assertIn(RUNNER.TRUSTED_SIGNER_WORKFLOW, command)
            self.assertIn(source_ref, command)
            self.assertIn(self.source_identity["commit"], command)
            self.assertIn("--deny-self-hosted-runners", command)

            for field, value in (
                ("repository", "attacker/fork"),
                (
                    "workflow_ref",
                    "attacker/fork/.github/workflows/fake.yml@" + source_ref,
                ),
                ("ref", "refs/heads/stale"),
                ("sha", "f" * 40),
            ):
                with self.subTest(field=field):
                    mutated = copy.deepcopy(receipt)
                    mutated["ci"][field] = value
                    with self.assertRaises(RUNNER.Dxr7Error):
                        RUNNER.validate_trusted_dsse_bundle(
                            bundle,
                            receipt_path,
                            mutated,
                            self.source_identity,
                            source_ref,
                        )

    def test_trusted_dsse_fails_closed_on_cli_or_bundle_faults(self) -> None:
        source_ref = "refs/tags/dxr7-fixture"
        receipt = {
            "ci": {
                "provider": "github-actions",
                "repository": RUNNER.TRUSTED_REPOSITORY,
                "workflow_ref": (
                    RUNNER.TRUSTED_SIGNER_WORKFLOW + "@" + source_ref
                ),
                "ref": source_ref,
                "sha": self.source_identity["commit"],
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            receipt_path = root / RUNNER.EXECUTION_RECEIPT_NAME
            receipt_path.write_text("{}\n", encoding="utf-8")
            bundle = root / RUNNER.DSSE_BUNDLE_NAME
            bundle.write_text("not-json\n", encoding="utf-8")
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.validate_trusted_dsse_bundle(
                    bundle,
                    receipt_path,
                    receipt,
                    self.source_identity,
                    source_ref,
                )
            bundle.write_text("{}\n", encoding="utf-8")
            failed = mock.Mock(
                returncode=1, stdout="", stderr="invalid signature"
            )
            with mock.patch.object(
                RUNNER.shutil, "which", return_value="gh"
            ), mock.patch.object(RUNNER.subprocess, "run", return_value=failed):
                with self.assertRaisesRegex(RUNNER.Dxr7Error, "invalid signature"):
                    RUNNER.validate_trusted_dsse_bundle(
                        bundle,
                        receipt_path,
                        receipt,
                        self.source_identity,
                        source_ref,
                    )

    def test_exact_json_comparison_rejects_boolean_integer_aliases(self) -> None:
        self.assertFalse(RUNNER.exact_json_equal({"bytes": True}, {"bytes": 1}))
        self.assertFalse(
            RUNNER.exact_json_equal({"complete": 1}, {"complete": True})
        )

    def test_atomic_json_publication_replaces_existing_without_temp_leak(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            with mock.patch.object(
                RUNNER,
                "fsync_parent_directory",
                wraps=RUNNER.fsync_parent_directory,
            ) as parent_fsync:
                RUNNER.write_json_atomically(path, {"generation": 1})
                RUNNER.write_json_atomically(path, {"generation": 2})
            if os.name != "nt":
                self.assertEqual(parent_fsync.call_count, 2)
            self.assertEqual(json.loads(path.read_text()), {"generation": 2})
            self.assertEqual(list(path.parent.glob(path.name + ".tmp-*")), [])

    def test_parser_requires_explicit_sdk_closure_controls(self) -> None:
        parser = RUNNER.build_parser()
        parsed = parser.parse_args(
            [
                "--validate-contract-only",
                "--dxc",
                "C:/SDK/x64/dxc.exe",
                "--windows-sdk-bin-root",
                "C:/SDK",
            ]
        )
        self.assertTrue(parsed.validate_contract_only)
        self.assertEqual(parsed.dxc, Path("C:/SDK/x64/dxc.exe"))

    def test_dxc_closure_mutation_fails_closed(self) -> None:
        mutated = copy.deepcopy(self.dxc_closure)
        mutated["components"]["dxc.exe"]["sha256"] = "f" * 64
        with mock.patch.object(
            RUNNER, "configured_dxc_closure", return_value=mutated
        ), self.assertRaises(RUNNER.Dxr7Error):
            RUNNER.require_dxc_closure_unchanged(
                Path("fixture-build"), self.dxc_closure
            )

    def test_windows_ci_builds_reruns_signs_and_uploads_evidence(self) -> None:
        for token in (
            "A complete versioned Windows SDK x64 DXC closure is unavailable",
            "dxcompiler.dll",
            "dxil.dll",
            "test_ogre_next_windows_dxr7_contract.py",
            "run_windows_dxr7.py --validate-contract-only",
            "Build and attest app-owned D3D12/D3D11On12/DXR RT7",
            "--require-ci-context",
            "--verify-existing",
            "--integrity-only",
            "--trusted-attestation-bundle",
            "--expected-source-ref $env:GITHUB_REF",
            "actions/attest-build-provenance@",
            "attest_windows_dxr7_receipt.outputs.bundle-path",
            "ror-ogre-next-windows-dxr7-execution-receipt.json",
            "ror-ogre-next-windows-dxr7-execution-receipt.sigstore.jsonl",
            "ror-ogre-next-windows-dxr7-ogre-frame.ppm",
            "CMakeCache.txt",
            "Upload attested Windows D3D12/D3D11On12/DXR RT7 evidence",
        ):
            self.assertIn(token, self.workflow)
        self.assertNotIn("Get-Command dxc.exe", self.workflow)
        signing_block = self.workflow[
            self.workflow.index(
                "Cryptographically attest the Windows DXR RT7 execution receipt"
            ) : self.workflow.index(
                "Upload exact reports and UI-free frame"
            )
        ]
        self.assertIn("success() && runner.os == 'Windows'", signing_block)
        self.assertNotIn("always()", signing_block)
        self.assertLess(
            self.workflow.index("Locate and attest the Windows SDK DXC compiler"),
            self.workflow.index("Build, render, and validate the independent N1 frontend"),
        )


if __name__ == "__main__":
    unittest.main()
