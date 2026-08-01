#!/usr/bin/env python3
"""Offline fail-closed checks for Ogre-Next Vulkan external-device RT5."""

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
RUNNER_PATH = PROBE_ROOT / "run_vulkan_rt5.py"
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_vulkan_rt5_for_tests", RUNNER_PATH
)
if RUNNER_SPEC is None or RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load Vulkan RT5 runner")
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


class OgreNextVulkanRt5ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.bootstrap = (
            RENDER_ROOT / "OgreNextVulkanExternalDeviceBootstrap.cpp"
        ).read_text(encoding="utf-8")
        cls.header = (
            RENDER_ROOT / "OgreNextVulkanExternalDeviceBootstrap.h"
        ).read_text(encoding="utf-8")
        cls.policy = (
            RENDER_ROOT / "OgreNextVulkanRt5Contract.cpp"
        ).read_text(encoding="utf-8")
        cls.smoke = (PROBE_ROOT / "src/vulkan_rt5_smoke.cpp").read_text(
            encoding="utf-8"
        )
        cls.cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.workflow = (
            REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")
        cls.lock = RUNNER.MAIN_RUNNER.load_lock()
        cls.source_identity = {
            "repository": "https://github.com/oasiz-ai/rigs-of-rods",
            "ref": "codex/fixture",
            "commit": "1" * 40,
            "relevant_manifest_sha256": "2" * 64,
            "relevant_manifest_file_count": 17,
        }

    def make_pass_report(self) -> dict[str, object]:
        return {
            "schema": RUNNER.SCHEMA,
            "status": "pass",
            "reason": "",
            "scope": {
                "external_instance_device_foundation": True,
                "hardware_bootstrap_pass": True,
                "native_ray_tracing": "not_evaluated",
                "ray_traced_image_produced": False,
                "acceleration_structure_built": False,
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
                "device_name": "fixture hardware",
                "device_uuid": "3" * 32,
                "device_class": "discrete_gpu",
                "known_software_adapter": False,
                "candidate_decision": "accept",
                "graphics_queue_family": 2,
                "graphics_queue_index": 0,
            },
            "enabled_state_contract": {
                "graphics_queue_available": True,
                "timeline_semaphore_supported": True,
                "timeline_semaphore_enabled": True,
                "all_supported_core_features_enabled": True,
                "claimed_instance_extension_count": 0,
                "claimed_device_extension_count": 0,
                "enabled_instance_extensions_exact": True,
                "enabled_device_extensions_exact": True,
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
            "timeline": {
                "queue_submit_and_host_wait": True,
                "value_before_ogre": 1,
                "value_after_ogre_shutdown": 2,
            },
            "lifecycle": {
                "ogre_shutdown_before_owner_teardown": True,
                "timeline_destroyed_before_device": True,
                "device_destroyed_before_instance": True,
                "shutdown_completed": True,
            },
        }

    def make_skip_report(self) -> dict[str, object]:
        report = copy.deepcopy(self.make_pass_report())
        report["status"] = "unsupported"
        report["reason"] = (
            "no attested RT5 hardware device: software_or_unattested_device"
        )
        report["scope"]["hardware_bootstrap_pass"] = False
        report["vulkan"]["device_name"] = "llvmpipe (LLVM 19)"
        report["vulkan"]["device_class"] = "cpu"
        report["vulkan"]["known_software_adapter"] = True
        report["vulkan"]["candidate_decision"] = (
            "software_or_unattested_device"
        )
        report["enabled_state_contract"]["timeline_semaphore_enabled"] = False
        report["enabled_state_contract"][
            "all_supported_core_features_enabled"
        ] = False
        report["enabled_state_contract"][
            "enabled_device_extensions_exact"
        ] = False
        for key in (
            "instance_injected_exactly",
            "physical_device_injected_exactly",
            "logical_device_injected_exactly",
            "graphics_queue_injected_exactly",
            "ogre_external_ownership_observed",
        ):
            report["ogre_external_adoption"][key] = False
        report["timeline"] = {
            "queue_submit_and_host_wait": False,
            "value_before_ogre": 0,
            "value_after_ogre_shutdown": 0,
        }
        report["lifecycle"]["ogre_shutdown_before_owner_teardown"] = False
        report["lifecycle"]["timeline_destroyed_before_device"] = False
        report["lifecycle"]["device_destroyed_before_instance"] = False
        return report

    def make_pre_candidate_skip_report(
        self, reason: str, loader_version: str
    ) -> dict[str, object]:
        report = self.make_skip_report()
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
            candidate_decision="software_or_unattested_device",
            graphics_queue_family=0,
            graphics_queue_index=0,
        )
        report["enabled_state_contract"].update(
            graphics_queue_available=False,
            timeline_semaphore_supported=False,
            timeline_semaphore_enabled=False,
            all_supported_core_features_enabled=False,
            enabled_instance_extensions_exact=False,
            enabled_device_extensions_exact=False,
        )
        return report

    def test_exact_ogre_external_instance_and_device_seams_are_used(self) -> None:
        for token in (
            'plugin_options["external_instance"]',
            'window_parameters["external_device"]',
            "Ogre::VulkanExternalInstance",
            "Ogre::VulkanExternalDevice",
            "getVkInstance() == impl_->instance",
            "mPhysicalDevice == impl_->physical_device",
            "mDevice == impl_->device",
            "mGraphicsQueue.mQueue == impl_->graphics_queue",
            "ogre_device->mIsExternal",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.bootstrap + self.smoke)

    def test_enabled_feature_state_is_not_inferred_from_support(self) -> None:
        for token in (
            "VkPhysicalDeviceFeatures2",
            "VkPhysicalDeviceVulkan12Features",
            "timelineSemaphore = VK_TRUE",
            "device_info.pEnabledFeatures = &selected.features2.features",
            "every_ogre_observed_core_feature_enabled",
            "ENABLED_FEATURE_STATE_AMBIGUOUS",
            "claimed_extension_set_is_exact",
            "ENABLED_EXTENSION_STATE_AMBIGUOUS",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.bootstrap + self.header + self.policy)
        self.assertIn("external_instance.instanceExtensions.clear()", self.bootstrap)
        self.assertIn("external_device.deviceExtensions.clear()", self.bootstrap)

    def test_timeline_queue_and_teardown_order_are_real(self) -> None:
        for token in (
            "vkCreateSemaphore",
            "VK_SEMAPHORE_TYPE_TIMELINE",
            "vkQueueSubmit",
            "VkTimelineSemaphoreSubmitInfo",
            "vkWaitSemaphores",
            "vkGetSemaphoreCounterValue",
            "ProveTimelineQueue(1U)",
            "ProveTimelineQueue(2U)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.bootstrap + self.smoke)
        shutdown = self.bootstrap[self.bootstrap.index(
            "OgreNextVulkanExternalDeviceBootstrap::Shutdown"
        ) :]
        self.assertLess(shutdown.index("vkDestroySemaphore"), shutdown.index("vkDestroyDevice"))
        self.assertLess(shutdown.index("vkDestroyDevice"), shutdown.index("vkDestroyInstance"))
        self.assertLess(
            self.smoke.index("RunOgreAdoption(bootstrap)"),
            self.smoke.index("bootstrap.Shutdown()"),
        )

    def test_software_and_unattested_devices_cannot_pass(self) -> None:
        for token in (
            "lavapipe",
            "llvmpipe",
            "swiftshader",
            "VulkanRt5DeviceClass::CPU",
            "VulkanRt5DeviceClass::VIRTUAL_GPU",
            "SOFTWARE_OR_UNATTESTED_DEVICE",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.bootstrap + self.policy)
        self.assertIn("kUnsupportedExitCode = 77", self.smoke)
        self.assertIn("hosted lavapipe runner must not report", self.workflow)
        self.assertIn('report.get("status") != "unsupported"', self.workflow)

    def test_unsupported_loader_completes_empty_teardown(self) -> None:
        guard_start = self.bootstrap.index(
            "if (loader_version < VK_API_VERSION_1_2)"
        )
        guard_end = self.bootstrap.index("VkApplicationInfo application_info")
        loader_guard = self.bootstrap[guard_start:guard_end]
        self.assertLess(
            loader_guard.index("impl_->DestroyPartial()"),
            loader_guard.index("return Unsupported"),
        )

    def test_report_validator_accepts_only_scoped_pass_or_skip(self) -> None:
        RUNNER.validate_report(
            self.make_pass_report(), 0, self.lock, self.source_identity
        )
        RUNNER.validate_report(
            self.make_skip_report(), 77, self.lock, self.source_identity
        )
        for label, mutate in (
            (
                "software hardware pass",
                lambda report: report["vulkan"].update(
                    known_software_adapter=True
                ),
            ),
            (
                "native RT claim",
                lambda report: report["scope"].update(native_ray_tracing="pass"),
            ),
            (
                "wrong queue",
                lambda report: report["ogre_external_adoption"].update(
                    graphics_queue_injected_exactly=False
                ),
            ),
            (
                "ambiguous extensions",
                lambda report: report["enabled_state_contract"].update(
                    enabled_device_extensions_exact=False
                ),
            ),
        ):
            with self.subTest(label=label):
                report = self.make_pass_report()
                mutate(report)
                with self.assertRaises(RUNNER.Rt5Error):
                    RUNNER.validate_report(
                        report, 0, self.lock, self.source_identity
                    )

    def test_report_accepts_every_runtime_unsupported_stage(self) -> None:
        loader_skip = self.make_pre_candidate_skip_report(
            "Vulkan loader does not expose Vulkan 1.2", "1.1.0"
        )
        no_device_skip = self.make_pre_candidate_skip_report(
            "Vulkan reported no physical devices", "1.3.0"
        )
        candidate_skips = []
        for decision, updates in (
            (
                "api_too_old",
                {
                    "physical_device_api_version": "1.1.0",
                    "device_class": "discrete_gpu",
                    "known_software_adapter": False,
                },
            ),
            (
                "graphics_queue_unavailable",
                {
                    "device_class": "discrete_gpu",
                    "known_software_adapter": False,
                    "graphics_queue_available": False,
                },
            ),
            (
                "timeline_semaphore_unavailable",
                {
                    "device_class": "discrete_gpu",
                    "known_software_adapter": False,
                    "timeline_semaphore_supported": False,
                },
            ),
        ):
            report = self.make_skip_report()
            report["reason"] = f"no attested RT5 hardware device: {decision}"
            report["vulkan"]["candidate_decision"] = decision
            for key, value in updates.items():
                if key in report["enabled_state_contract"]:
                    report["enabled_state_contract"][key] = value
                else:
                    report["vulkan"][key] = value
            candidate_skips.append(report)

        for report in (loader_skip, no_device_skip, *candidate_skips):
            with self.subTest(reason=report["reason"]):
                RUNNER.validate_report(
                    report, 77, self.lock, self.source_identity
                )

    def test_attestation_hashes_exact_report_and_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-vulkan-rt5-") as temp:
            root = Path(temp)
            report_path = root / RUNNER.REPORT_NAME
            executable = root / "ror_ogre_next_vulkan_rt5_smoke"
            report = self.make_skip_report()
            report_path.write_text(json.dumps(report), encoding="utf-8")
            executable.write_bytes(b"exact rt5 executable\n")
            attestation = RUNNER.make_attestation(
                report_path,
                executable,
                report,
                77,
                self.source_identity,
            )
            RUNNER.validate_attestation(
                attestation,
                report_path,
                executable,
                report,
                self.source_identity,
            )
            report_path.write_text(json.dumps({"tampered": True}), encoding="utf-8")
            with self.assertRaises(RUNNER.Rt5Error):
                RUNNER.validate_attestation(
                    attestation,
                    report_path,
                    executable,
                    report,
                    self.source_identity,
                )

    def test_report_rejects_impossible_versions_ids_and_skip_decisions(self) -> None:
        for label, mutate in (
            (
                "old loader pass",
                lambda report: report["vulkan"].update(
                    loader_api_version="0.0.0"
                ),
            ),
            (
                "old physical API pass",
                lambda report: report["vulkan"].update(
                    physical_device_api_version="1.0.0"
                ),
            ),
            (
                "non-numeric vendor",
                lambda report: report["vulkan"].update(vendor_id="bad"),
            ),
            (
                "negative queue family",
                lambda report: report["vulkan"].update(
                    graphics_queue_family=-1
                ),
            ),
            (
                "zero device UUID",
                lambda report: report["vulkan"].update(device_uuid="0" * 32),
            ),
        ):
            with self.subTest(label=label):
                report = self.make_pass_report()
                mutate(report)
                with self.assertRaises(RUNNER.Rt5Error):
                    RUNNER.validate_report(
                        report, 0, self.lock, self.source_identity
                    )

        skip = self.make_skip_report()
        skip["reason"] = "arbitrary"
        skip["vulkan"].update(
            candidate_decision="accept",
            device_class="discrete_gpu",
            known_software_adapter=False,
        )
        with self.assertRaises(RUNNER.Rt5Error):
            RUNNER.validate_report(skip, 77, self.lock, self.source_identity)

        wrong_precedence = self.make_skip_report()
        wrong_precedence["vulkan"].update(
            physical_device_api_version="1.0.0",
            candidate_decision="software_or_unattested_device",
        )
        with self.assertRaises(RUNNER.Rt5Error):
            RUNNER.validate_report(
                wrong_precedence, 77, self.lock, self.source_identity
            )

    def test_attestation_is_bound_to_exact_ror_source(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-vulkan-rt5-source-") as temp:
            root = Path(temp)
            report_path = root / RUNNER.REPORT_NAME
            executable = root / "ror_ogre_next_vulkan_rt5_smoke"
            report = self.make_skip_report()
            report_path.write_text(json.dumps(report), encoding="utf-8")
            executable.write_bytes(b"exact rt5 executable\n")
            attestation = RUNNER.make_attestation(
                report_path,
                executable,
                report,
                77,
                self.source_identity,
            )
            attestation["ror_source"] = {"commit": "forged"}
            with self.assertRaises(RUNNER.Rt5Error):
                RUNNER.validate_attestation(
                    attestation,
                    report_path,
                    executable,
                    report,
                    self.source_identity,
                )

    def test_target_is_linux_only_opt_in_and_ctest_skips_unsupported(self) -> None:
        for token in (
            "ROR_OGRE_NEXT_VULKAN_RT5",
            'ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan"',
            "ror_ogre_next_vulkan_rt5_smoke",
            "ROR_OGRE_NEXT_N1_VULKAN=1",
            "SKIP_RETURN_CODE 77",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cmake)
        self.assertIn("if: runner.os == 'Linux'", self.workflow)
        self.assertIn("--verify-existing", self.workflow)
        self.assertIn("OGRE-Next-vulkan-rt5-${{ github.sha }}", self.workflow)

    def test_contract_only_path_is_network_free_and_optimization_safe(self) -> None:
        for optimization in (False, True):
            command = [sys.executable]
            if optimization:
                command.append("-O")
            command.extend([str(RUNNER_PATH), "--validate-contract-only"])
            with self.subTest(optimization=optimization):
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
                self.assertEqual(report["native_ray_tracing"], "not_evaluated")


if __name__ == "__main__":
    unittest.main()
