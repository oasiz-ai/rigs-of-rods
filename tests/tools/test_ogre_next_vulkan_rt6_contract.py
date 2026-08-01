#!/usr/bin/env python3
"""Offline fail-closed checks for the Vulkan KHR ray-dispatch RT6 proof."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source/main/gfx/render/ogrenext"
PROBE_ROOT = REPOSITORY_ROOT / "tools/ogre_next_probe"
RUNNER_PATH = PROBE_ROOT / "run_vulkan_rt6.py"
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_vulkan_rt6_for_tests", RUNNER_PATH
)
if RUNNER_SPEC is None or RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load Vulkan RT6 runner")
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


class OgreNextVulkanRt6ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.bootstrap = (
            RENDER_ROOT / "OgreNextVulkanRayTracingBootstrap.cpp"
        ).read_text(encoding="utf-8")
        cls.policy = (RENDER_ROOT / "OgreNextVulkanRt6Contract.cpp").read_text(
            encoding="utf-8"
        )
        cls.smoke = (PROBE_ROOT / "src/vulkan_rt6_smoke.cpp").read_text(
            encoding="utf-8"
        )
        cls.cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")
        cls.lock = RUNNER.MAIN_RUNNER.load_lock()
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
                "checkpoint": "rt6",
                "hardware_ray_dispatch_pass": True,
                "native_ray_tracing": "hardware_dispatch_pass",
                "ray_traced_image_produced": True,
                "ogre_native_image_composite": "not_evaluated",
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
                "ogre_next_repository": self.lock["repository"],
                "ogre_next_branch": self.lock["branch"],
                "ogre_next_commit": self.lock["commit"],
                "ogre_next_archive_sha256": self.lock["archive_sha256"],
                "ogre_next_license_spdx": self.lock["license"]["spdx"],
                "ogre_next_license_sha256": self.lock["license"]["sha256"],
            },
            "build": {
                "platform_policy": "linux-x86_64-vulkan",
                "system": "Linux",
                "processor": "x86_64",
                "compiler_id": "GNU",
                "compiler_version": "14.2",
                "ogre_version": "3.0.0",
                "pointer_bits": 64,
            },
            "vulkan": {
                "loader_api_version": "1.3.0",
                "requested_instance_api_version": "1.2.0",
                "physical_device_api_version": "1.3.0",
                "driver_version_packed": 42,
                "vendor_id": 4318,
                "device_id": 1,
                "device_name": "fixture RTX hardware",
                "device_uuid": "3" * 32,
                "device_class": "discrete_gpu",
                "known_software_adapter": False,
                "device_identity_available": True,
                "candidate_decision": "accept",
                "graphics_queue_family": 2,
                "graphics_queue_index": 0,
            },
            "required_extensions": {
                "deferred_host_operations": True,
                "buffer_device_address": True,
                "acceleration_structure": True,
                "ray_tracing_pipeline": True,
                "enabled_count": 4,
                "enabled_set_exact": True,
            },
            "features": {
                "graphics_queue": True,
                "compute_on_graphics_queue": True,
                "timeline_semaphore_supported": True,
                "timeline_semaphore_enabled": True,
                "buffer_device_address_supported": True,
                "buffer_device_address_enabled": True,
                "acceleration_structure_supported": True,
                "acceleration_structure_enabled": True,
                "ray_tracing_pipeline_supported": True,
                "ray_tracing_pipeline_enabled": True,
                "output_rgba32_uint_storage_image": True,
                "ray_tracing_properties_valid": True,
                "all_supported_core_features_enabled": True,
                "enabled_instance_extensions_exact": True,
            },
            "ray_properties": {
                "shader_group_handle_size": 32,
                "shader_group_handle_alignment": 32,
                "shader_group_base_alignment": 64,
                "max_ray_recursion_depth": 2,
                "acceleration_structure_scratch_alignment": 256,
            },
            "ogre_external_adoption": {
                "plugin_option": "external_instance",
                "first_window_option": "external_device",
                "window_type": "null",
                "instance_injected_exactly": True,
                "physical_device_injected_exactly": True,
                "logical_device_injected_exactly": True,
                "graphics_queue_injected_exactly": True,
                "ogre_external_ownership_observed": True,
            },
            "geometry_and_acceleration": {
                "mirror_vertex_count": 3,
                "geometry_buffer_created": True,
                "geometry_buffer_device_address": 0x1000,
                "instance_buffer_device_address": 0x2000,
                "scratch_buffer_device_address": 0x3000,
                "blas_device_address": 0x4000,
                "tlas_device_address": 0x5000,
                "blas_built": True,
                "tlas_built": True,
            },
            "pipeline_and_dispatch": {
                "shader_contract_compiled": True,
                "descriptor_set_bound": True,
                "ray_pipeline_created": True,
                "shader_binding_table_created": True,
                "shader_binding_table_device_address": 0x6000,
                "dispatch_dimensions": [1, 1, 1],
                "ray_dispatch_completed": True,
                "output_format": "VK_FORMAT_R32G32B32A32_UINT",
                "output_image_copied_to_host": True,
                "expected_primary_hit_words": RUNNER.EXPECTED_HIT_WORDS.copy(),
                "readback_words": RUNNER.EXPECTED_HIT_WORDS.copy(),
                "primary_hit_observed": True,
            },
            "timeline": {
                "value_before_ray_dispatch": 1,
                "value_at_ray_dispatch": 2,
                "value_after_ogre_shutdown": 3,
            },
            "lifecycle": {
                "ogre_shutdown_before_owner_teardown": True,
                "ray_resources_destroyed_before_device": True,
                "timeline_destroyed_before_device": True,
                "device_destroyed_before_instance": True,
                "shutdown_completed": True,
            },
        }

    def make_lavapipe_skip_report(self) -> dict[str, object]:
        report = copy.deepcopy(self.make_pass_report())
        report["status"] = "unsupported"
        report["reason"] = (
            "no attested RT6 hardware device: software_or_unattested_device"
        )
        report["scope"].update(
            hardware_ray_dispatch_pass=False,
            native_ray_tracing="not_executed",
            ray_traced_image_produced=False,
        )
        report["vulkan"].update(
            device_name="llvmpipe (LLVM 19.1.7, 256 bits)",
            device_class="cpu",
            known_software_adapter=True,
            device_identity_available=True,
            candidate_decision="software_or_unattested_device",
        )
        report["required_extensions"].update(
            deferred_host_operations=False,
            buffer_device_address=True,
            acceleration_structure=False,
            ray_tracing_pipeline=False,
            enabled_count=0,
            enabled_set_exact=False,
        )
        report["features"].update(
            timeline_semaphore_enabled=False,
            buffer_device_address_enabled=False,
            acceleration_structure_supported=False,
            acceleration_structure_enabled=False,
            ray_tracing_pipeline_supported=False,
            ray_tracing_pipeline_enabled=False,
            ray_tracing_properties_valid=False,
            all_supported_core_features_enabled=False,
        )
        for field in (
            "instance_injected_exactly",
            "physical_device_injected_exactly",
            "logical_device_injected_exactly",
            "graphics_queue_injected_exactly",
            "ogre_external_ownership_observed",
        ):
            report["ogre_external_adoption"][field] = False
        report["geometry_and_acceleration"].update(
            geometry_buffer_created=False,
            geometry_buffer_device_address=0,
            instance_buffer_device_address=0,
            scratch_buffer_device_address=0,
            blas_device_address=0,
            tlas_device_address=0,
            blas_built=False,
            tlas_built=False,
        )
        report["pipeline_and_dispatch"].update(
            descriptor_set_bound=False,
            ray_pipeline_created=False,
            shader_binding_table_created=False,
            shader_binding_table_device_address=0,
            ray_dispatch_completed=False,
            output_image_copied_to_host=False,
            readback_words=[0, 0, 0, 0],
            primary_hit_observed=False,
        )
        report["timeline"].update(
            value_before_ray_dispatch=0,
            value_at_ray_dispatch=0,
            value_after_ogre_shutdown=0,
        )
        report["lifecycle"].update(
            ogre_shutdown_before_owner_teardown=False,
            ray_resources_destroyed_before_device=False,
            timeline_destroyed_before_device=False,
            device_destroyed_before_instance=False,
        )
        return report

    def make_pre_candidate_skip_report(
        self, reason: str, loader_version: str, instance_created: bool
    ) -> dict[str, object]:
        report = self.make_lavapipe_skip_report()
        report["reason"] = reason
        report["vulkan"].update(
            loader_api_version=loader_version,
            physical_device_api_version="0.0.0",
            driver_version_packed=0,
            vendor_id=0,
            device_id=0,
            device_name="",
            device_uuid="",
            device_class="other",
            known_software_adapter=False,
            device_identity_available=False,
            candidate_decision="software_or_unattested_device",
            graphics_queue_family=0,
            graphics_queue_index=0,
        )
        report["required_extensions"].update(
            deferred_host_operations=False,
            buffer_device_address=False,
            acceleration_structure=False,
            ray_tracing_pipeline=False,
        )
        report["features"].update(
            graphics_queue=False,
            compute_on_graphics_queue=False,
            timeline_semaphore_supported=False,
            buffer_device_address_supported=False,
            output_rgba32_uint_storage_image=False,
            enabled_instance_extensions_exact=instance_created,
        )
        report["ray_properties"].update(
            shader_group_handle_size=0,
            shader_group_handle_alignment=0,
            shader_group_base_alignment=0,
            max_ray_recursion_depth=0,
            acceleration_structure_scratch_alignment=0,
        )
        return report

    def make_candidate_skip_report(self, decision: str) -> dict[str, object]:
        report = self.make_lavapipe_skip_report()
        report["reason"] = f"no attested RT6 hardware device: {decision}"
        report["vulkan"].update(
            physical_device_api_version="1.3.0",
            device_name="fixture RTX hardware",
            device_uuid="3" * 32,
            device_class="discrete_gpu",
            known_software_adapter=False,
            device_identity_available=True,
            candidate_decision=decision,
        )
        report["required_extensions"].update(
            deferred_host_operations=True,
            buffer_device_address=True,
            acceleration_structure=True,
            ray_tracing_pipeline=True,
        )
        report["features"].update(
            graphics_queue=True,
            compute_on_graphics_queue=True,
            timeline_semaphore_supported=True,
            buffer_device_address_supported=True,
            acceleration_structure_supported=True,
            ray_tracing_pipeline_supported=True,
            output_rgba32_uint_storage_image=True,
            ray_tracing_properties_valid=True,
        )
        return report

    def validate(self, report: dict[str, object], exit_code: int) -> None:
        RUNNER.validate_report(report, exit_code, self.lock, self.source_identity)

    def test_real_khr_dispatch_operations_are_statically_bound(self) -> None:
        for token in (
            "vkCmdBuildAccelerationStructuresKHR",
            "vkCreateRayTracingPipelinesKHR",
            "vkGetRayTracingShaderGroupHandlesKHR",
            "vkCmdTraceRaysKHR",
            "VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR",
            "VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR",
            "vkCmdCopyImageToBuffer",
            "readback_words == kExpectedPrimaryHit",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.bootstrap)

    def test_exact_extensions_features_and_hardware_gate_are_statically_bound(self) -> None:
        for token in (
            "VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME",
            "VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME",
            "VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME",
            "VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME",
            "bufferDeviceAddress = VK_TRUE",
            "accelerationStructure = VK_TRUE",
            "rayTracingPipeline = VK_TRUE",
            "lavapipe",
            "llvmpipe",
            "VulkanRt5DeviceClass::CPU",
            "VulkanRt5DeviceClass::VIRTUAL_GPU",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.bootstrap + self.policy)

    def test_scratch_sbt_and_build_dependencies_are_explicit(self) -> None:
        for token in (
            "minAccelerationStructureScratchOffsetAlignment",
            "AlignUp(scratch_base, scratch_alignment)",
            "scratch_address + required_scratch_size",
            "AlignUp(sbt_base, base_alignment)",
            "VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |",
            "VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.bootstrap)

    def test_validator_accepts_only_real_pass_or_explicit_lavapipe_skip(self) -> None:
        self.validate(self.make_pass_report(), 0)
        self.validate(self.make_lavapipe_skip_report(), 77)
        loader_skip = self.make_pre_candidate_skip_report(
            "Vulkan loader does not expose Vulkan 1.2", "1.1.0", False
        )
        no_device_skip = self.make_pre_candidate_skip_report(
            "Vulkan reported no physical devices", "1.3.0", True
        )
        self.validate(loader_skip, 77)
        self.validate(no_device_skip, 77)

    def test_pass_rejects_forged_or_partial_dispatch_evidence(self) -> None:
        mutations = (
            lambda report: report["pipeline_and_dispatch"].update(
                ray_dispatch_completed=False
            ),
            lambda report: report["pipeline_and_dispatch"].update(
                shader_contract_compiled=False
            ),
            lambda report: report["pipeline_and_dispatch"].update(
                readback_words=[0, 0, 0, 0]
            ),
            lambda report: report["pipeline_and_dispatch"].update(
                readback_words=[*RUNNER.EXPECTED_HIT_WORDS[:3], 0]
            ),
            lambda report: report["geometry_and_acceleration"].update(
                tlas_device_address=0
            ),
            lambda report: report["required_extensions"].update(
                ray_tracing_pipeline=False
            ),
            lambda report: report["features"].update(
                acceleration_structure_enabled=False
            ),
            lambda report: report["timeline"].update(value_at_ray_dispatch=3),
            lambda report: report["vulkan"].update(known_software_adapter=True),
            lambda report: report["ray_properties"].update(
                acceleration_structure_scratch_alignment=96
            ),
            lambda report: report["geometry_and_acceleration"].update(
                scratch_buffer_device_address=0x3080
            ),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                report = self.make_pass_report()
                mutate(report)
                with self.assertRaises(RUNNER.Rt6Error):
                    self.validate(report, 0)

    def test_skip_cannot_carry_ray_dispatch_or_output_claims(self) -> None:
        mutations = (
            lambda report: report["scope"].update(
                native_ray_tracing="hardware_dispatch_pass"
            ),
            lambda report: report["geometry_and_acceleration"].update(
                blas_built=True
            ),
            lambda report: report["pipeline_and_dispatch"].update(
                ray_dispatch_completed=True
            ),
            lambda report: report["pipeline_and_dispatch"].update(
                readback_words=RUNNER.EXPECTED_HIT_WORDS.copy()
            ),
            lambda report: report["timeline"].update(value_at_ray_dispatch=2),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                report = self.make_lavapipe_skip_report()
                mutate(report)
                with self.assertRaises(RUNNER.Rt6Error):
                    self.validate(report, 77)

    def test_skip_decision_must_follow_contract_precedence(self) -> None:
        report = self.make_lavapipe_skip_report()
        report["vulkan"].update(
            physical_device_api_version="1.1.0",
            candidate_decision="software_or_unattested_device",
        )
        with self.assertRaises(RUNNER.Rt6Error):
            self.validate(report, 77)

    def test_every_runtime_candidate_skip_decision_is_validated_in_order(self) -> None:
        cases = (
            (
                "api_too_old",
                lambda report: report["vulkan"].update(
                    physical_device_api_version="1.1.0"
                ),
            ),
            (
                "software_or_unattested_device",
                lambda report: report["vulkan"].update(
                    device_class="cpu", known_software_adapter=True
                ),
            ),
            (
                "device_identity_unavailable",
                lambda report: report["vulkan"].update(
                    device_name="",
                    device_uuid="0" * 32,
                    device_identity_available=False,
                ),
            ),
            (
                "graphics_queue_unavailable",
                lambda report: report["features"].update(graphics_queue=False),
            ),
            (
                "compute_queue_unavailable",
                lambda report: report["features"].update(
                    compute_on_graphics_queue=False
                ),
            ),
            (
                "timeline_semaphore_unavailable",
                lambda report: report["features"].update(
                    timeline_semaphore_supported=False
                ),
            ),
            (
                "deferred_host_operations_extension_unavailable",
                lambda report: report["required_extensions"].update(
                    deferred_host_operations=False
                ),
            ),
            (
                "buffer_device_address_extension_unavailable",
                lambda report: report["required_extensions"].update(
                    buffer_device_address=False
                ),
            ),
            (
                "acceleration_structure_extension_unavailable",
                lambda report: report["required_extensions"].update(
                    acceleration_structure=False
                ),
            ),
            (
                "ray_tracing_pipeline_extension_unavailable",
                lambda report: report["required_extensions"].update(
                    ray_tracing_pipeline=False
                ),
            ),
            (
                "buffer_device_address_feature_unavailable",
                lambda report: report["features"].update(
                    buffer_device_address_supported=False
                ),
            ),
            (
                "acceleration_structure_feature_unavailable",
                lambda report: report["features"].update(
                    acceleration_structure_supported=False
                ),
            ),
            (
                "ray_tracing_pipeline_feature_unavailable",
                lambda report: report["features"].update(
                    ray_tracing_pipeline_supported=False
                ),
            ),
            (
                "output_storage_image_format_unavailable",
                lambda report: report["features"].update(
                    output_rgba32_uint_storage_image=False
                ),
            ),
            (
                "ray_tracing_properties_invalid",
                lambda report: report["features"].update(
                    ray_tracing_properties_valid=False
                ),
            ),
        )
        for decision, mutate in cases:
            with self.subTest(decision=decision):
                report = self.make_candidate_skip_report(decision)
                mutate(report)
                self.validate(report, 77)
        report = self.make_lavapipe_skip_report()
        report["reason"] = "arbitrary"
        with self.assertRaises(RUNNER.Rt6Error):
            self.validate(report, 77)

    def test_attestation_hashes_exact_report_executable_and_source(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-vulkan-rt6-") as temporary:
            root = Path(temporary)
            report_path = root / RUNNER.REPORT_NAME
            executable = root / "ror_ogre_next_vulkan_rt6_smoke"
            report = self.make_pass_report()
            report_path.write_text(json.dumps(report), encoding="utf-8")
            executable.write_bytes(b"exact rt6 executable\n")
            attestation = RUNNER.make_attestation(
                report_path, executable, report, 0, self.source_identity
            )
            RUNNER.validate_attestation(
                attestation, report_path, executable, report, self.source_identity
            )
            report_path.write_text(json.dumps({"tampered": True}), encoding="utf-8")
            with self.assertRaises(RUNNER.Rt6Error):
                RUNNER.validate_attestation(
                    attestation,
                    report_path,
                    executable,
                    report,
                    self.source_identity,
                )

    def test_linux_target_ctest_and_ci_require_explicit_skip(self) -> None:
        for token in (
            "ROR_OGRE_NEXT_VULKAN_RT6",
            'ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan"',
            "ror_ogre_next_vulkan_rt6_smoke",
            "shaderc_combined",
            "SKIP_RETURN_CODE 77",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cmake)
        self.assertIn("run_vulkan_rt6.py", self.workflow)
        self.assertIn("hosted lavapipe runner must not report an RT6 hardware pass", self.workflow)
        self.assertIn("OGRE-Next-vulkan-rt6-${{ github.sha }}", self.workflow)

    def test_contract_only_path_is_network_free_and_optimization_safe(self) -> None:
        for optimized in (False, True):
            command = [sys.executable]
            if optimized:
                command.append("-O")
            command.extend([str(RUNNER_PATH), "--validate-contract-only"])
            with self.subTest(optimized=optimized):
                result = subprocess.run(
                    command,
                    cwd=REPOSITORY_ROOT,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                report = json.loads(result.stdout)
                self.assertEqual(report["status"], "pass")
                self.assertFalse(report["network_used"])
                self.assertEqual(report["native_ray_tracing"], "requires_actual_dispatch")


if __name__ == "__main__":
    unittest.main()
