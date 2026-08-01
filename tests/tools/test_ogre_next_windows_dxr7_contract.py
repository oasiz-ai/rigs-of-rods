#!/usr/bin/env python3
"""Offline fail-closed tests for the Windows D3D11On12/DXR RT7 proof."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = (
    REPOSITORY_ROOT / "tools/ogre_next_probe/run_windows_dxr7.py"
)
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

    def make_pass_report(self) -> dict[str, object]:
        return {
            "schema": RUNNER.SCHEMA,
            "status": "pass",
            "reason": "",
            "scope": {
                "external_d3d11on12_foundation": True,
                "hardware_dxr_pass": True,
                "native_ray_tracing": "dispatch_rays",
                "acceleration_structure_built": True,
                "ray_traced_probe_readback": True,
                "ray_traced_image_produced": False,
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
                "ogre_next_archive_sha256": self.ogre_lock[
                    "archive_sha256"
                ],
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
                "dxc_executable_sha256": "3" * 64,
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
        scope["hardware_dxr_pass"] = False
        scope["native_ray_tracing"] = "unsupported"
        scope["acceleration_structure_built"] = False
        scope["ray_traced_probe_readback"] = False
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
            report["ray_tracing"][key] = False if isinstance(
                report["ray_tracing"][key], bool
            ) else 0
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
        RUNNER.validate_report(
            report,
            exit_code,
            self.ogre_lock,
            self.dxr7_lock,
            self.source_identity,
        )

    def test_static_contract_and_locked_sources(self) -> None:
        RUNNER.validate_static_contract()
        self.assertEqual(
            RUNNER.sha256_file(RUNNER.PROBE_SOURCE / RUNNER.LOCK_NAME),
            RUNNER.LOCK_SHA256,
        )

    def test_complete_real_dispatch_report_passes(self) -> None:
        self.validate(self.make_pass_report())

    def test_every_material_pass_claim_is_fail_closed(self) -> None:
        mutations = (
            ("scope", "hardware_dxr_pass", False),
            ("adapter", "software_adapter", True),
            ("adapter", "raytracing_tier", 10),
            ("ownership", "app_owned_direct_queue", False),
            (
                "ownership",
                "d3d11on12_created_with_exact_direct_queue",
                False,
            ),
            (
                "ownership",
                "d3d11on12_underlying_d3d12_device_exact",
                False,
            ),
            ("ownership", "ogre_d3d11_device_exact", False),
            ("ray_tracing", "blas_built", False),
            ("ray_tracing", "tlas_built", False),
            ("ray_tracing", "dispatch_rays_called", False),
            ("ray_tracing", "readback_value", 0x0BADCAFE),
            ("synchronization", "fence_after_ogre", 2),
            ("lifecycle", "d3d12_queue_released_before_device", False),
        )
        for section, field, value in mutations:
            with self.subTest(section=section, field=field):
                report = self.make_pass_report()
                report[section][field] = value
                with self.assertRaises(RUNNER.Dxr7Error):
                    self.validate(report)

    def test_report_rejects_extra_or_wrong_typed_evidence(self) -> None:
        report = self.make_pass_report()
        report["ray_tracing"]["forged"] = True
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report)
        report = self.make_pass_report()
        report["ownership"]["app_owned_fence"] = 1
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report)

    def test_honest_no_hardware_skip_passes(self) -> None:
        self.validate(self.make_unsupported_report(), RUNNER.UNSUPPORTED_EXIT_CODE)

    def test_unsupported_reason_cannot_hide_partial_rt_claims(self) -> None:
        report = self.make_unsupported_report()
        report["ray_tracing"]["dispatch_rays_called"] = True
        with self.assertRaises(RUNNER.Dxr7Error):
            self.validate(report, RUNNER.UNSUPPORTED_EXIT_CODE)
        report = self.make_unsupported_report()
        report["reason"] = "driver did not work"
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

    def test_attestation_binds_report_executable_dxil_and_source(self) -> None:
        report = self.make_pass_report()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report_path = root / RUNNER.REPORT_NAME
            executable = root / "ror_ogre_next_windows_dxr7_smoke.exe"
            dxil = root / "generated/ror_ogre_next_windows_dxr7_probe.dxil"
            dxil.parent.mkdir()
            report_path.write_text(json.dumps(report), encoding="utf-8")
            executable.write_bytes(b"exe")
            dxil.write_bytes(b"dxil")
            attestation = RUNNER.make_attestation(
                report_path,
                executable,
                dxil,
                report,
                0,
                self.source_identity,
            )
            RUNNER.validate_attestation(
                attestation,
                report_path,
                executable,
                dxil,
                report,
                self.source_identity,
            )
            mutated = copy.deepcopy(attestation)
            mutated["claims"]["real_dispatch_rays"] = False
            with self.assertRaises(RUNNER.Dxr7Error):
                RUNNER.validate_attestation(
                    mutated,
                    report_path,
                    executable,
                    dxil,
                    report,
                    self.source_identity,
                )

    def test_parser_exposes_contract_and_dxc_controls(self) -> None:
        parser = RUNNER.build_parser()
        parsed = parser.parse_args(
            ["--validate-contract-only", "--dxc", "C:/SDK/dxc.exe"]
        )
        self.assertTrue(parsed.validate_contract_only)
        self.assertEqual(parsed.dxc, Path("C:/SDK/dxc.exe"))

    def test_windows_ci_builds_verifies_and_uploads_attested_evidence(self) -> None:
        for token in (
            "Locate and attest the Windows SDK DXC compiler",
            "test_ogre_next_windows_dxr7_contract.py",
            "run_windows_dxr7.py --validate-contract-only",
            "Build and attest app-owned D3D12/D3D11On12/DXR RT7",
            "--verify-existing",
            "Upload attested Windows D3D12/D3D11On12/DXR RT7 evidence",
            "ror-ogre-next-windows-dxr7-attestation.json",
            "ror_ogre_next_windows_dxr7_probe.dxil",
        ):
            self.assertIn(token, self.workflow)
        self.assertLess(
            self.workflow.index("Locate and attest the Windows SDK DXC compiler"),
            self.workflow.index("Build, render, and validate the independent N1 frontend"),
        )


if __name__ == "__main__":
    unittest.main()
