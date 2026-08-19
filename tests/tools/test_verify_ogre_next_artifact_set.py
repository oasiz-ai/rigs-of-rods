#!/usr/bin/env python3
"""Unit tests for the exact OGRE-Next CI artifact-set gate."""

from __future__ import annotations

import importlib.util
import hashlib
import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
SPEC = importlib.util.spec_from_file_location("verify_ogre_next_artifacts", SCRIPT_PATH)
assert SPEC and SPEC.loader
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)
RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_ogre_next_probe", RUNNER_PATH
)
assert RUNNER_SPEC and RUNNER_SPEC.loader
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


class OgreNextArtifactSetTests(unittest.TestCase):
    def setUp(self) -> None:
        source = VERIFY._current_source_identity()
        lock = VERIFY._read_pinned_lock()
        self.lock = lock
        self.ror_repository = source["repository"]
        self.ror_ref = source["ref"]
        self.ror_commit = source["commit"]
        self.ror_manifest = source["relevant_manifest_sha256"]
        self.ror_manifest_count = source["relevant_manifest_file_count"]
        self.ogre_repository = lock["repository"]
        self.ogre_branch = lock["branch"]
        self.ogre_commit = lock["commit"]
        self.ogre_archive = lock["archive_sha256"]
        self.ogre_license = lock["license"]["sha256"]
        notice = lock["shader_media"]["third_party_notice"]
        self.shader_source = notice["source_sha256"]
        self.shader_notice = notice["notice_sha256"]
        self.production_freetype_license_contract = copy.deepcopy(
            VERIFY.FREETYPE_PACKAGE_LICENSE_CONTRACT
        )
        self.freetype_license_payloads = {
            "licenses/FreeType-GPLv2.txt": b"FreeType GPL fixture\n",
            "licenses/FreeType-LICENSE.txt": b"FreeType overview fixture\n",
        }
        VERIFY.FREETYPE_PACKAGE_LICENSE_CONTRACT = tuple(
            (path, hashlib.sha256(payload).hexdigest())
            for path, payload in self.freetype_license_payloads.items()
        )
        self.addCleanup(
            setattr,
            VERIFY,
            "FREETYPE_PACKAGE_LICENSE_CONTRACT",
            self.production_freetype_license_contract,
        )

    def test_executable_mode_policy_is_host_metadata_aware(self) -> None:
        self.assertTrue(
            VERIFY._requires_posix_executable_permission("mach-o-64", "posix")
        )
        self.assertTrue(
            VERIFY._requires_posix_executable_permission("elf64", "posix")
        )
        self.assertFalse(
            VERIFY._requires_posix_executable_permission("pe32+", "posix")
        )
        for binary_format in ("mach-o-64", "elf64", "pe32+"):
            with self.subTest(binary_format=binary_format):
                self.assertFalse(
                    VERIFY._requires_posix_executable_permission(
                        binary_format, "nt"
                    )
                )

    def test_pssm_executable_uses_host_metadata_mode_policy(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-pssm-mode-policy-") as temp:
            root = Path(temp)
            contract = self.build_contract()
            (root / VERIFY.REQUIRED_ARTIFACTS[0]).write_text(
                json.dumps(contract) + "\n", encoding="utf-8"
            )
            self.write_pssm(root, contract)
            report = json.loads(
                (root / VERIFY.PSSM_REPORT_ARTIFACT).read_text(encoding="utf-8")
            )
            executable = root / "bin" / VERIFY.PSSM_EXECUTABLE_STEM
            executable.chmod(0o644)
            if VERIFY.os.name == "posix":
                with self.assertRaisesRegex(
                    VERIFY.ArtifactSetError, "has no execute permission"
                ):
                    VERIFY._verify_pssm_executable(executable, contract, report)
            with mock.patch.object(
                VERIFY,
                "_requires_posix_executable_permission",
                return_value=False,
            ) as mode_policy:
                VERIFY._verify_pssm_executable(executable, contract, report)
            mode_policy.assert_called_once_with("mach-o-64")

    def build_contract(self) -> dict[str, object]:
        rapidjson = self.lock["dependencies"]["rapidjson"]
        freetype = self.lock["dependencies"]["freetype"]
        lock_abi = self.lock["abi_contract"]
        expected_abi = {
            key: copy.deepcopy(value)
            for key, value in lock_abi.items()
            if key != "simd"
        }
        expected_abi.update(
            {
                "simd_enabled": lock_abi["simd"]["enabled"],
                "simd_alignment": lock_abi["simd"]["alignment"],
                "simd_family": "neon",
                "simd_neon": True,
                "simd_sse2": False,
            }
        )
        return {
            "schema_version": 5,
            "ror_source": {
                "repository": self.ror_repository,
                "ref": self.ror_ref,
                "commit": self.ror_commit,
                "relevant_manifest_sha256": self.ror_manifest,
                "relevant_manifest_file_count": self.ror_manifest_count,
            },
            "provenance": {
                "repository": self.ogre_repository,
                "branch": self.ogre_branch,
                "commit": self.ogre_commit,
                "archive_sha256": self.ogre_archive,
                "license_spdx": self.lock["license"]["spdx"],
                "license_sha256": self.ogre_license,
            },
            "patches": copy.deepcopy(self.lock["patches"]),
            "dependencies": {
                "freetype": {
                    "repository": freetype["repository"],
                    "version": freetype["version"],
                    "archive_url": freetype["archive_url"],
                    "archive_sha256": freetype["archive_sha256"],
                    "license_expression": freetype["license_expression"],
                    "selected_license_spdx": freetype[
                        "selected_license_spdx"
                    ],
                    "license_path": freetype["license_path"],
                    "license_sha256": freetype["license_sha256"],
                    "package_license_path": freetype[
                        "package_license_path"
                    ],
                    "overview_path": freetype["overview_path"],
                    "overview_sha256": freetype["overview_sha256"],
                    "package_overview_path": freetype[
                        "package_overview_path"
                    ],
                    "target": "freetype",
                    "target_type": "STATIC_LIBRARY",
                    "static_link": True,
                    "overlay_link_target": True,
                    "disabled_optional_dependencies": copy.deepcopy(
                        freetype["disabled_optional_dependencies"]
                    ),
                },
                "rapidjson": {
                    "tag": rapidjson["tag"],
                    "archive_sha256": rapidjson["archive_sha256"],
                    "source_archive_license_spdx": rapidjson["license_spdx"],
                    "compiled_headers_license_spdx": rapidjson[
                        "compiled_headers_spdx"
                    ],
                    "license_sha256": rapidjson["license_sha256"],
                }
            },
            "shader_media": VERIFY._expected_build_shader_media(self.lock),
            "reflection_shader_media": copy.deepcopy(
                self.lock["reflection_shader_media"]
            ),
            "platform": {
                "policy": "macos-arm64-metal",
                "system": "Darwin",
                "processor": "arm64",
                "renderer_target": "RenderSystem_Metal",
                "device_option_name": "Rendering Device",
            },
            "abi": expected_abi,
            "components": {
                "hlms_pbs": True,
                "hlms_unlit": True,
                "overlay": True,
                "compositor2_core": True,
                "json_materials": True,
                "mesh_lod": True,
                "dds_codec": True,
                "hdr_temporal_contract_version": 2,
                "hdr_history_validation_mode": (
                    "native_authoritative_conditioning_plus_one_r16_ulp_v2"
                ),
                "hdr_workspace": "RoRHdrWorkspaceHudV1",
                "hdr_visual_evidence_version": 1,
                "headless_child_bootstrap": True,
                "headless_child_output_name": "RoR-OgreNext",
                "headless_child_packaged": False,
                "headless_child_production_admitted": False,
                "native_ray_tracing": "not_evaluated",
            },
            "compiler": {
                "id": "AppleClang",
                "version": "21.0.0.21000101",
                "build_type": "Release",
            },
        }

    def write_baseline(self, root: Path) -> None:
        root.mkdir(parents=True, exist_ok=True)
        contract = self.build_contract()
        contract_path = root / VERIFY.REQUIRED_ARTIFACTS[0]
        contract_path.write_text(json.dumps(contract) + "\n", encoding="utf-8")
        rt4_names = {
            VERIFY.RT4_REPORT_ARTIFACT,
            VERIFY.RT4_PPM_ARTIFACT,
            VERIFY.RT4_ISOLATION_ARTIFACT,
            VERIFY.RT4_REFLECTION_ARTIFACT,
            VERIFY.RT4_COMPOSITOR_ARTIFACT,
            VERIFY.RT4_ANALYTIC_SKY_PPM_ARTIFACT,
            VERIFY.RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT,
            VERIFY.RT4_REPEAT_REPORT_ARTIFACT,
            VERIFY.RT4_REPEAT_PPM_ARTIFACT,
            VERIFY.RT4_REPEAT_ISOLATION_ARTIFACT,
            VERIFY.RT4_REPEAT_REFLECTION_ARTIFACT,
            VERIFY.RT4_REPEAT_COMPOSITOR_ARTIFACT,
            VERIFY.RT4_REPEAT_ANALYTIC_SKY_PPM_ARTIFACT,
            VERIFY.RT4_REPEAT_ANALYTIC_SKY_EVIDENCE_ARTIFACT,
            VERIFY.RT4_ATTESTATION_ARTIFACT,
        }
        for name in VERIFY.REQUIRED_ARTIFACTS[1:]:
            if name in rt4_names:
                continue
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"baseline")
        self.write_pssm(root, contract)
        self.write_rt4(root, contract)

    def write_pssm(self, root: Path, contract: dict[str, object]) -> None:
        width = 192
        height = 128

        def pair(
            *, hdr: bool, first_changed_x: int, changed_y: int
        ) -> tuple[bytes, bytes]:
            if hdr:
                clear_pixel = struct.pack("<4e", 1.0, 1.0, 1.0, 1.0)
                shadow_pixel = struct.pack("<4e", 0.5, 0.5, 0.5, 1.0)
            else:
                clear_pixel = bytes((200, 200, 200, 255))
                shadow_pixel = bytes((100, 100, 100, 255))
            clear = clear_pixel * (width * height)
            shadow = bytearray(clear)
            for x in range(first_changed_x, first_changed_x + 16):
                offset = (changed_y * width + x) * len(clear_pixel)
                shadow[offset : offset + len(clear_pixel)] = shadow_pixel
            return clear, bytes(shadow)

        hdr = pair(hdr=True, first_changed_x=34, changed_y=18)
        sdr = pair(hdr=False, first_changed_x=34, changed_y=18)
        cascade_2 = pair(hdr=False, first_changed_x=68, changed_y=18)
        cascade_3 = pair(hdr=False, first_changed_x=68, changed_y=18)
        off_center_tight = pair(hdr=False, first_changed_x=20, changed_y=30)
        slices = (*hdr, *sdr, *cascade_2, *cascade_3, *off_center_tight)
        payload = b"".join(slices)
        evidence_path = root / VERIFY.PSSM_EVIDENCE_ARTIFACT
        evidence_path.write_bytes(payload)
        shader_manifest = "c" * 64
        report: dict[str, object] = {
            "schema": VERIFY.PSSM_REPORT_SCHEMA,
            "status": "pass",
            "execution": {
                "schema": VERIFY.PSSM_EXECUTION_SCHEMA,
                "challenge_nonce": "d" * 64,
            },
            "provenance": {
                "ror_repository": self.ror_repository,
                "ror_ref": self.ror_ref,
                "ror_commit": self.ror_commit,
                "ror_relevant_source_manifest_sha256": self.ror_manifest,
                "ogre_next_commit": self.ogre_commit,
                "ogre_next_archive_sha256": self.ogre_archive,
                "shader_media_manifest_sha256": shader_manifest,
            },
            "platform_policy": "macos-arm64-metal",
            "renderer": "Metal Rendering Subsystem",
            "shadow_contract": {
                "version": 1,
                "mode": "PSSM_3_CASCADE_V1",
                "cascade_count": 3,
                "split_points_m": [0.5, 7.81633186, 45.2411156, 350],
                "blend_points_m": [6.90179062, 40.5630188],
                "fade_point_m": 254.610474,
                "atlas": {
                    "format": "D32_FLOAT",
                    "width": 2048,
                    "height": 3072,
                },
                "filter": "PCF_4x4",
                "programmatic_compositor2": True,
                "ui_included": False,
                "backend_substitution": False,
                "split_stable_tangent_projection": True,
                "native_definition_split_and_runtime_bias_readback": True,
                "native_d32_probe_attempted": True,
                "native_d32_atlas_allocation_use_readback_verified": True,
                "native_d32_atlas_cleanup_verified": True,
                "native_d32_atlas_cleanup_absence_checks": 1,
                "runtime_normal_offset_bias": [168.0, 168.0, 168.0],
            },
            "isolation": {
                "controlled_visual_change": "occluder_instance_casts_shadow",
                "nonvisual_snapshot_identity_changed": True,
                "changed_pixels_outside_reviewed_receiver_region": 0,
                "changed_pixels_inside_reviewed_occluder_region": 0,
                "hdr_changed_receiver_pixels": 16,
                "hdr_darkened_receiver_pixels": 16,
                "sdr_changed_receiver_pixels": 16,
                "sdr_darkened_receiver_pixels": 16,
                "normalized_visibility_mask_0x1_verified": True,
                "shadow_disabled_default_equals_explicit": True,
                "shadow_disabled_exact_fnv1a64": "0123456789abcdef",
            },
            "distant_cascade_proof": [
                {
                    "cascade_index": 1,
                    "receiver_depth_m": 20,
                    "occluder_depth_m": 12.5,
                    "off_axis": True,
                    "sdr_changed_receiver_pixels": 16,
                    "sdr_darkened_receiver_pixels": 16,
                },
                {
                    "cascade_index": 2,
                    "receiver_depth_m": 100,
                    "occluder_depth_m": 62.5,
                    "off_axis": True,
                    "sdr_changed_receiver_pixels": 16,
                    "sdr_darkened_receiver_pixels": 16,
                },
            ],
            "projection_and_bounds_fixture": {
                "horizontal_lens_offset": 0.25,
                "vertical_lens_offset": -0.125,
                "expected_tangent_extents": [-0.75, 1.25, 0.875 / 1.5, -0.75],
                "off_center_projection_verified": True,
                "receiver_bounds_min_z": 0,
                "receiver_bounds_max_z": 0,
                "caster_bounds_min_z": 0,
                "caster_bounds_max_z": 0,
                "tight_caster_bounds_verified": True,
                "native_bounds_readback_verified": True,
                "native_aabb_observations": [
                    {
                        "instance_id": 1,
                        "casts_shadow": True,
                        "receives_shadow": True,
                        "expected_local": {
                            "minimum": [-2.5, -1.8, 0],
                            "maximum": [2.5, 1.8, 0],
                        },
                        "ogre_mesh_local": {
                            "minimum": [-2.5, -1.8, 0],
                            "maximum": [2.5, 1.8, 0],
                        },
                        "ogre_item_local": {
                            "minimum": [-2.5, -1.8, 0],
                            "maximum": [2.5, 1.8, 0],
                        },
                        "expected_world": {
                            "minimum": [-2.5, -1.8, 0],
                            "maximum": [2.5, 1.8, 0],
                        },
                        "ogre_item_world": {
                            "minimum": [-2.5, -1.8, 0],
                            "maximum": [2.5, 1.8, 0],
                        },
                    },
                    {
                        "instance_id": 2,
                        "casts_shadow": True,
                        "receives_shadow": False,
                        "expected_local": {
                            "minimum": [-0.45, -0.45, 0],
                            "maximum": [0.45, 0.45, 0],
                        },
                        "ogre_mesh_local": {
                            "minimum": [-0.45, -0.45, 0],
                            "maximum": [0.45, 0.45, 0],
                        },
                        "ogre_item_local": {
                            "minimum": [-0.45, -0.45, 0],
                            "maximum": [0.45, 0.45, 0],
                        },
                        "expected_world": {
                            "minimum": [-0.45, -0.45, 1.5],
                            "maximum": [0.45, 0.45, 1.5],
                        },
                        "ogre_item_world": {
                            "minimum": [-0.45, -0.45, 1.5],
                            "maximum": [0.45, 0.45, 1.5],
                        },
                    },
                ],
                "sdr_changed_pixels": 16,
                "sdr_darkened_pixels": 16,
            },
            "lifecycle": {
                "shadow_frames_completed": 10,
                "shadow_node_creates": 10,
                "shadow_node_destroys": 10,
                "workspace_node_definition_creates": 10,
                "workspace_node_definition_destroys": 10,
                "receiver_datablock_creates": 10,
                "receiver_datablock_destroys": 10,
                "d32_atlas_cleanup_absence_checks": 1,
                "workspace_definition_cleanup_absence_checks": 10,
                "workspace_node_cleanup_absence_checks": 10,
                "shadow_node_cleanup_absence_checks": 10,
                "receiver_datablock_cleanup_absence_checks": 10,
                "target_texture_cleanup_absence_checks": 10,
                "d32_post_create_same_instance_retry_verified": True,
                "d32_cleanup_lookup_failure_closed_retry_verified": True,
                "receiver_clone_same_frame_retry_verified": True,
                "workspace_node_same_frame_retry_verified": True,
                "receiver_cleanup_lookup_failure_closed_retry_verified": True,
                "workspace_definition_cleanup_lookup_failure_closed_retry_verified": True,
                "workspace_cleanup_lookup_failure_closed_retry_verified": True,
                "shadow_cleanup_lookup_failure_closed_retry_verified": True,
                "target_cleanup_lookup_failure_closed_retry_verified": True,
            },
            "evidence": {
                "file": VERIFY.PSSM_EVIDENCE_ARTIFACT,
                "bytes": len(payload),
                "hdr_no_occluder_fnv1a64": VERIFY._fnv1a64(slices[0]),
                "hdr_occluder_fnv1a64": VERIFY._fnv1a64(slices[1]),
                "sdr_no_occluder_fnv1a64": VERIFY._fnv1a64(slices[2]),
                "sdr_occluder_fnv1a64": VERIFY._fnv1a64(slices[3]),
                "cascade_2_sdr_no_occluder_fnv1a64": VERIFY._fnv1a64(
                    slices[4]
                ),
                "cascade_2_sdr_occluder_fnv1a64": VERIFY._fnv1a64(
                    slices[5]
                ),
                "cascade_3_sdr_no_occluder_fnv1a64": VERIFY._fnv1a64(
                    slices[6]
                ),
                "cascade_3_sdr_occluder_fnv1a64": VERIFY._fnv1a64(
                    slices[7]
                ),
                "off_center_tight_bounds_sdr_no_occluder_fnv1a64": VERIFY._fnv1a64(
                    slices[8]
                ),
                "off_center_tight_bounds_sdr_occluder_fnv1a64": VERIFY._fnv1a64(
                    slices[9]
                ),
            },
        }
        identity = VERIFY._expected_base_build_identity(contract, report)
        report["provenance"]["executable_build_identity"] = identity
        (root / VERIFY.PSSM_REPORT_ARTIFACT).write_text(
            json.dumps(report) + "\n", encoding="utf-8"
        )
        executable_path = root / "bin" / VERIFY.PSSM_EXECUTABLE_STEM
        executable_path.parent.mkdir(parents=True, exist_ok=True)
        executable_size = 64 * 1024
        header = struct.pack(
            "<IiiIIIII", 0xFEEDFACF, 0x0100000C, 0, 2, 2, 96, 0, 0
        )
        segment = struct.pack(
            "<II16sQQQQiiII",
            0x19,
            72,
            b"__TEXT" + b"\0" * 10,
            0,
            executable_size,
            0,
            executable_size,
            5,
            5,
            0,
            0,
        )
        entry = struct.pack("<IIQQ", 0x80000028, 24, 128, 0)
        binary_tokens = b"\0".join(
            token.encode("utf-8")
            for token in (
                identity,
                VERIFY.PSSM_REPORT_SCHEMA,
                "--media-root",
                "--execution-challenge",
                "PSSM_3_CASCADE_V1",
                VERIFY.PSSM_UNSUPPORTED_DETAIL,
                "Metal Rendering Subsystem",
            )
        )
        executable = header + segment + entry + b"\xc0\x03\x5f\xd6" + binary_tokens
        executable += b"\0" * (executable_size - len(executable))
        executable_path.write_bytes(executable)
        executable_path.chmod(0o755)

        def record(path: Path, relative: str) -> dict[str, object]:
            return {
                "path": relative,
                "bytes": path.stat().st_size,
                "sha256": VERIFY.sha256_file(path),
            }

        executable_relative = "bin/ror_ogre_next_pssm_shadow_smoke"
        local_workflow = {
            "provider": "local",
            "repository": "",
            "workflow_ref": "",
            "run_id": "",
            "run_attempt": "",
            "sha": "",
            "ref": "",
            "job": "",
            "external_dsse_required": False,
        }
        subjects = {
            "build_contract": record(
                root / VERIFY.REQUIRED_ARTIFACTS[0], VERIFY.REQUIRED_ARTIFACTS[0]
            ),
            "executable": record(executable_path, executable_relative),
            "report": record(
                root / VERIFY.PSSM_REPORT_ARTIFACT, VERIFY.PSSM_REPORT_ARTIFACT
            ),
            "evidence": record(evidence_path, VERIFY.PSSM_EVIDENCE_ARTIFACT),
        }
        receipt = {
            "schema": VERIFY.PSSM_EXECUTION_RECEIPT_SCHEMA,
            "status": "pass",
            "observation": {
                "mode": "fresh_child_process_challenge",
                "challenge_nonce": "d" * 64,
                "observed_process_exit_code": 0,
                "offline_cryptographic_execution_proof": False,
                "limitation": VERIFY.PSSM_OFFLINE_EXECUTION_LIMITATION,
            },
            "subjects": subjects,
            "build_identity": identity,
            "source": contract["ror_source"],
            "workflow": local_workflow,
            "complete": True,
        }
        receipt_path = root / VERIFY.PSSM_EXECUTION_RECEIPT_ARTIFACT
        receipt_path.write_text(json.dumps(receipt) + "\n", encoding="utf-8")
        receipt_record = record(receipt_path, VERIFY.PSSM_EXECUTION_RECEIPT_ARTIFACT)
        attestation = {
            "schema": VERIFY.PSSM_ATTESTATION_SCHEMA,
            "status": "pass",
            "integrity_model": (
                "atomic-self-contained-sha256-plus-challenged-execution-receipt; "
                "external-github-dsse-required-in-ci"
            ),
            "source": contract["ror_source"],
            "workflow": local_workflow,
            "build_identity": identity,
            "files": {**subjects, "execution_receipt": receipt_record},
            "complete": True,
        }
        attestation_path = root / VERIFY.PSSM_ATTESTATION_ARTIFACT
        attestation_path.write_text(
            json.dumps(attestation) + "\n", encoding="utf-8"
        )
        artifacts = sorted(
            [
                *subjects.values(),
                receipt_record,
                record(attestation_path, VERIFY.PSSM_ATTESTATION_ARTIFACT),
            ],
            key=lambda item: item["path"],
        )
        artifact_manifest = {
            "schema": VERIFY.PSSM_ARTIFACT_MANIFEST_SCHEMA,
            "status": "pass",
            "source": contract["ror_source"],
            "workflow": local_workflow,
            "build_identity": identity,
            "artifacts": artifacts,
            "complete": True,
        }
        (root / VERIFY.PSSM_ARTIFACT_MANIFEST_ARTIFACT).write_text(
            json.dumps(artifact_manifest) + "\n", encoding="utf-8"
        )

    def refresh_pssm_integrity(self, root: Path) -> None:
        contract = json.loads(
            (root / VERIFY.REQUIRED_ARTIFACTS[0]).read_text(encoding="utf-8")
        )
        report = json.loads(
            (root / VERIFY.PSSM_REPORT_ARTIFACT).read_text(encoding="utf-8")
        )
        executable_path = root / "bin" / VERIFY.PSSM_EXECUTABLE_STEM

        def record(path: Path, relative: str) -> dict[str, object]:
            return {
                "path": relative,
                "bytes": path.stat().st_size,
                "sha256": VERIFY.sha256_file(path),
            }

        workflow = {
            "provider": "local",
            "repository": "",
            "workflow_ref": "",
            "run_id": "",
            "run_attempt": "",
            "sha": "",
            "ref": "",
            "job": "",
            "external_dsse_required": False,
        }
        status = report["status"]
        evidence_path = root / VERIFY.PSSM_EVIDENCE_ARTIFACT
        subjects = {
            "build_contract": record(
                root / VERIFY.REQUIRED_ARTIFACTS[0], VERIFY.REQUIRED_ARTIFACTS[0]
            ),
            "executable": record(
                executable_path, "bin/" + executable_path.name
            ),
            "report": record(
                root / VERIFY.PSSM_REPORT_ARTIFACT, VERIFY.PSSM_REPORT_ARTIFACT
            ),
            "evidence": (
                record(evidence_path, VERIFY.PSSM_EVIDENCE_ARTIFACT)
                if status == "pass"
                else None
            ),
        }
        identity = report["provenance"]["executable_build_identity"]
        receipt = {
            "schema": VERIFY.PSSM_EXECUTION_RECEIPT_SCHEMA,
            "status": status,
            "observation": {
                "mode": "fresh_child_process_challenge",
                "challenge_nonce": report["execution"]["challenge_nonce"],
                "observed_process_exit_code": 0 if status == "pass" else 77,
                "offline_cryptographic_execution_proof": False,
                "limitation": VERIFY.PSSM_OFFLINE_EXECUTION_LIMITATION,
            },
            "subjects": subjects,
            "build_identity": identity,
            "source": contract["ror_source"],
            "workflow": workflow,
            "complete": True,
        }
        receipt_path = root / VERIFY.PSSM_EXECUTION_RECEIPT_ARTIFACT
        receipt_path.write_text(json.dumps(receipt) + "\n", encoding="utf-8")
        receipt_record = record(receipt_path, receipt_path.name)
        attestation = {
            "schema": VERIFY.PSSM_ATTESTATION_SCHEMA,
            "status": status,
            "integrity_model": (
                "atomic-self-contained-sha256-plus-challenged-execution-receipt; "
                "external-github-dsse-required-in-ci"
            ),
            "source": contract["ror_source"],
            "workflow": workflow,
            "build_identity": identity,
            "files": {**subjects, "execution_receipt": receipt_record},
            "complete": True,
        }
        attestation_path = root / VERIFY.PSSM_ATTESTATION_ARTIFACT
        attestation_path.write_text(
            json.dumps(attestation) + "\n", encoding="utf-8"
        )
        artifact_manifest = {
            "schema": VERIFY.PSSM_ARTIFACT_MANIFEST_SCHEMA,
            "status": status,
            "source": contract["ror_source"],
            "workflow": workflow,
            "build_identity": identity,
            "artifacts": sorted(
                [
                    *[item for item in subjects.values() if item is not None],
                    receipt_record,
                    record(attestation_path, attestation_path.name),
                ],
                key=lambda item: item["path"],
            ),
            "complete": True,
        }
        (root / VERIFY.PSSM_ARTIFACT_MANIFEST_ARTIFACT).write_text(
            json.dumps(artifact_manifest) + "\n", encoding="utf-8"
        )

    def write_rt4(self, root: Path, contract: dict[str, object]) -> None:
        width = 192
        height = 128
        pixel_count = width * height
        background = bytes((2, 3, 5, 255))
        foreground = bytes((100, 120, 140, 255))
        baseline_sdr = foreground * 1024 + background * (pixel_count - 1024)
        foreground_hdr = struct.pack("<4e", 2.0, 1.0, 0.5, 1.0)
        background_hdr = struct.pack("<4e", 0.01, 0.02, 0.03, 1.0)
        baseline_hdr = (
            foreground_hdr * 1024
            + background_hdr * (pixel_count - 1024)
        )
        evidence = bytearray()
        variants: list[dict[str, object]] = []
        slices: list[dict[str, object]] = []
        offset = 0
        for index, (name, changed_input) in enumerate(
            VERIFY.RT4_EXPECTED_VARIANTS
        ):
            transformed = name == "uv0_affine"
            if index == 0:
                hdr = baseline_hdr
                sdr = baseline_sdr
            else:
                changed_hdr = struct.pack(
                    "<4e", 2.0 + index * 0.25, 1.0, 0.5, 1.0
                )
                hdr = changed_hdr * 128 + baseline_hdr[128 * 8 :]
                changed_sdr = bytes((100 + index, 120, 140, 255))
                sdr = changed_sdr * 128 + baseline_sdr[128 * 4 :]
            attachments: dict[str, object] = {}
            for attachment, payload, bytes_per_pixel in (
                ("hdr", hdr, 8),
                ("sdr", sdr, 4),
            ):
                changed = (
                    0
                    if index == 0
                    else VERIFY._changed_pixels(
                        baseline_hdr if attachment == "hdr" else baseline_sdr,
                        payload,
                        bytes_per_pixel,
                    )
                )
                attachments[attachment] = {
                    "offset": offset,
                    "bytes": len(payload),
                    "exact_fnv1a64": VERIFY._fnv1a64(payload),
                    "changed_pixels_from_baseline": changed,
                }
                slices.append(
                    {
                        "variant": name,
                        "attachment": attachment,
                        "offset": offset,
                        "bytes": len(payload),
                        "sha256": hashlib.sha256(payload).hexdigest(),
                    }
                )
                evidence.extend(payload)
                offset += len(payload)
            variants.append(
                {
                    "name": name,
                    "changed_input": changed_input,
                    "asset_sequence": index + 1,
                    "uv0_affine": {
                        "version": 1,
                        "scale": [2, 4] if transformed else [1, 1],
                        "offset": (
                            [0.125, -0.25] if transformed else [0, 0]
                        ),
                        "portable_binding_count": 4,
                        "native_slot_count": 5,
                        "native_slot_readbacks": 5,
                        "native_user_value_readbacks": 3,
                        "transformed": transformed,
                        "uv0_only": True,
                        "positive_scale": True,
                        "rotation_zero": True,
                        "shared_across_bound_slots": True,
                        "shader_piece_selected": True,
                        "exact_native_state": True,
                    },
                    **attachments,
                }
            )

        texture_evidence_bytes = offset
        positive_hdr = baseline_hdr
        positive_sdr = baseline_sdr
        negative_hdr_pixel = struct.pack("<4e", 2.0, 0.5, 0.25, 1.0)
        negative_hdr = negative_hdr_pixel * 128 + baseline_hdr[128 * 8 :]
        negative_sdr_pixel = bytes((100, 80, 140, 255))
        negative_sdr = negative_sdr_pixel * 128 + baseline_sdr[128 * 4 :]
        handedness_offset = offset
        handedness_phases: dict[str, dict[str, object]] = {}
        for sign, hdr_payload, sdr_payload in (
            ("positive", positive_hdr, positive_sdr),
            ("negative", negative_hdr, negative_sdr),
        ):
            phase: dict[str, object] = {}
            for attachment, payload in (
                ("hdr", hdr_payload),
                ("sdr", sdr_payload),
            ):
                phase[attachment] = {
                    "offset": offset,
                    "bytes": len(payload),
                    "exact_fnv1a64": VERIFY._fnv1a64(payload),
                }
                slices.append(
                    {
                        "variant": f"tangent_{sign}_w",
                        "attachment": attachment,
                        "offset": offset,
                        "bytes": len(payload),
                        "sha256": hashlib.sha256(payload).hexdigest(),
                    }
                )
                evidence.extend(payload)
                offset += len(payload)
            handedness_phases[sign] = phase
        tangent_handedness = {
            "schema": "ror.ogre_next_rt4_tangent_handedness.v1",
            "evidence_file": VERIFY.RT4_ISOLATION_ARTIFACT,
            "evidence_offset": handedness_offset,
            "evidence_bytes": offset - handedness_offset,
            "authored_tangent_format": "FLOAT4",
            "positive_tangent_w": 1,
            "negative_tangent_w": -1,
            "position_normal_tangent_xyz_uv0_identical": True,
            "material_camera_lights_identical": True,
            "ui_included": False,
            "positive": handedness_phases["positive"],
            "negative": handedness_phases["negative"],
            "hdr_changed_pixels": VERIFY._changed_pixels(
                positive_hdr, negative_hdr, 8
            ),
            "sdr_changed_pixels": VERIFY._changed_pixels(
                positive_sdr, negative_sdr, 4
            ),
        }

        raw_pixels = VERIFY.RT4_REFLECTION_RAW_BYTES // 8
        filtered_pixels = VERIFY.RT4_REFLECTION_FILTERED_BYTES // 8
        reflection_raw = (
            struct.pack("<4e", 1.0, 0.5, 0.25, 0.75) * (raw_pixels - 1)
            + struct.pack("<4e", 0.25, 1.0, 0.5, 0.25)
        )
        reflection_filtered = (
            struct.pack("<4e", 0.75, 0.375, 0.125, 1.0)
            * (filtered_pixels - 1)
            + struct.pack("<4e", 0.125, 0.75, 0.375, 1.0)
        )
        reflection_payload = reflection_raw + reflection_filtered
        reflection_slices: list[dict[str, object]] = []
        reflection_offset = 0
        for texture, dimensions in (
            ("raw", (VERIFY.RT4_REFLECTION_RESOLUTION,)),
            ("filtered", VERIFY.RT4_REFLECTION_FILTERED_DIMENSIONS),
        ):
            for mip, dimension in enumerate(dimensions):
                slice_bytes = dimension * dimension * 8
                for face in range(VERIFY.RT4_REFLECTION_FACE_COUNT):
                    payload = reflection_payload[
                        reflection_offset : reflection_offset + slice_bytes
                    ]
                    reflection_slices.append(
                        {
                            "texture": texture,
                            "mip": mip,
                            "face": face,
                            "offset": reflection_offset,
                            "bytes": slice_bytes,
                            "sha256": hashlib.sha256(payload).hexdigest(),
                        }
                    )
                    reflection_offset += slice_bytes

        def reflection_section(
            payload: bytes, section_offset: int, dimensions: list[int]
        ) -> dict[str, object]:
            metrics = VERIFY._reflection_half_metrics(payload, "fixture")
            return {
                "offset": section_offset,
                "bytes": len(payload),
                "face_count": VERIFY.RT4_REFLECTION_FACE_COUNT,
                "mip_dimensions": dimensions,
                "exact_fnv1a64": VERIFY._fnv1a64(payload),
                **metrics,
            }

        reflection_report = {
            "schema": VERIFY.RT4_REFLECTION_SCHEMA,
            "evidence_file": VERIFY.RT4_REFLECTION_ARTIFACT,
            "evidence_bytes": VERIFY.RT4_REFLECTION_EVIDENCE_BYTES,
            "backend": "OGRE_NEXT_METAL",
            "render_system": "Metal Rendering Subsystem",
            "device_name": "Synthetic GPU",
            "driver_version": "1.2.3",
            "pixel_format": "RGBA16_FLOAT",
            "byte_order": "little_endian",
            "row_padding_included": False,
            "subresource_order": (
                "raw_face_major_then_filtered_mip_major_face_major"
            ),
            "ui_included": False,
            "same_device_exact_replay": True,
            "capture": {
                "render_frame_id": 1,
                "simulation_tick": 1,
                "probe_id": 1,
                "content_revision": 1,
                "candidate_generation": 1,
                "deterministic_seed": "0123456789abcdef",
                "resolution": VERIFY.RT4_REFLECTION_RESOLUTION,
            },
            "runtime_audit": {
                "version": 3,
                "successful_capture_count": 1,
                "failed_capture_count": 0,
                "live_probe_count": 1,
                "probe_resolution": VERIFY.RT4_REFLECTION_RESOLUTION,
                "blend_resolution": 2048,
                "blend_texture_ready": True,
                "committed_state_digest": "1111111111111111",
                "native_execution_evidence": "2222222222222222",
                "capture_digest": "3333333333333333",
                "canonical_filtered_payload_bytes": (
                    VERIFY.RT4_REFLECTION_FILTERED_BYTES
                ),
                "completed_face_count": VERIFY.RT4_REFLECTION_FACE_COUNT,
                "completed_mip_count": len(
                    VERIFY.RT4_REFLECTION_FILTERED_DIMENSIONS
                ),
                "ui_free_capture": True,
                "reserved_render_queue_excluded": True,
            },
            "raw": reflection_section(
                reflection_raw, 0, [VERIFY.RT4_REFLECTION_RESOLUTION]
            ),
            "filtered": reflection_section(
                reflection_filtered,
                VERIFY.RT4_REFLECTION_RAW_BYTES,
                list(VERIFY.RT4_REFLECTION_FILTERED_DIMENSIONS),
            ),
        }

        isolation_path = root / VERIFY.RT4_ISOLATION_ARTIFACT
        isolation_path.write_bytes(evidence)
        reflection_path = root / VERIFY.RT4_REFLECTION_ARTIFACT
        reflection_path.write_bytes(reflection_payload)
        compositor_first = baseline_sdr
        final_foreground = bytes((110, 130, 150, 255))
        compositor_final = (
            final_foreground * 1024
            + background * (pixel_count - 1024)
        )
        compositor_overlay = bytes((255, 0, 255, 255)) * pixel_count
        split_base = struct.pack("<4e", 0.25, 0.125, 0.0625, 1.0) * pixel_count
        split_sun_full = (
            struct.pack("<4e", 0.5, 0.25, 0.125, 1.0) * pixel_count
        )
        split_sun_direct = (
            struct.pack("<4e", 0.25, 0.125, 0.0625, 0.0) * pixel_count
        )
        split_raster_lit = split_sun_full
        compositor_payload = (
            compositor_first
            + compositor_final
            + compositor_overlay
            + split_base
            + split_sun_full
            + split_sun_direct
            + split_raster_lit
        )
        compositor_path = root / VERIFY.RT4_COMPOSITOR_ARTIFACT
        compositor_path.write_bytes(compositor_payload)
        compositor_attachments = []
        compositor_slices = []
        for index, (name, payload) in enumerate(
            (
                ("first_ui_free", compositor_first),
                ("final_ui_free", compositor_final),
                ("ui_overlay_control", compositor_overlay),
            )
        ):
            attachment_offset = index * len(payload)
            changed = VERIFY._changed_pixels(
                compositor_first, payload, 4
            )
            compositor_attachments.append(
                {
                    "name": name,
                    "offset": attachment_offset,
                    "bytes": len(payload),
                    "exact_fnv1a64": VERIFY._fnv1a64(payload),
                    "changed_pixels_from_first": changed,
                }
            )
            compositor_slices.append(
                {
                    "attachment": name,
                    "offset": attachment_offset,
                    "bytes": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            )
        compositor_split_attachments = []
        split_offset = len(compositor_first) * 3
        for index, (name, payload) in enumerate(
            (
                ("base_hdr", split_base),
                ("sun_full_unoccluded_hdr", split_sun_full),
                ("sun_direct_hdr", split_sun_direct),
                ("raster_lit_hdr", split_raster_lit),
            )
        ):
            attachment_offset = split_offset + index * len(payload)
            exact_hash = VERIFY._fnv1a64(payload)
            compositor_split_attachments.append(
                {
                    "name": name,
                    "offset": attachment_offset,
                    "bytes": len(payload),
                    "format": "RGBA16_FLOAT",
                    "exact_fnv1a64": exact_hash,
                }
            )
            compositor_slices.append(
                {
                    "attachment": name,
                    "offset": attachment_offset,
                    "bytes": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            )
        ppm_pixels = bytes(
            channel
            for pixel_offset in range(0, len(compositor_final), 4)
            for channel in compositor_final[pixel_offset : pixel_offset + 3]
        )
        ppm_path = root / VERIFY.RT4_PPM_ARTIFACT
        ppm_path.write_bytes(b"P6\n192 128\n255\n" + ppm_pixels)
        sky_width = 768
        sky_height = 512
        sky_pixel_count = sky_width * sky_height
        sky_sunless_rows = []
        sky_rgb_rows = []
        for row in range(sky_height):
            fraction = row / (sky_height - 1)
            red = 0.02 + fraction * 0.06
            green = 0.04 + fraction * 0.05
            blue = 0.10 - fraction * 0.04
            sky_sunless_rows.append(
                struct.pack("<4e", red, green, blue, 1.0) * sky_width
            )
            sky_rgb_rows.append(
                bytes(
                    (
                        round(red * 255.0),
                        round(green * 255.0),
                        round(blue * 255.0),
                    )
                )
                * sky_width
            )
        sky_sunless = b"".join(sky_sunless_rows)
        sky_sun = bytearray(sky_sunless)
        sky_sun_pixel = sky_pixel_count // 2 + sky_width // 2
        sky_sun_offset = sky_sun_pixel * 8
        sky_sun[sky_sun_offset : sky_sun_offset + 8] = struct.pack(
            "<4e", 16.0, 14.0, 12.0, 1.0
        )
        sky_sun = bytes(sky_sun)
        sky_rgb = bytearray(b"".join(sky_rgb_rows))
        sky_rgb_offset = sky_sun_pixel * 3
        sky_rgb[sky_rgb_offset : sky_rgb_offset + 3] = bytes((255, 255, 255))
        sky_rgb = bytes(sky_rgb)
        sky_evidence_path = root / VERIFY.RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT
        sky_evidence_path.write_bytes(sky_sunless + sky_sun)
        sky_ppm_path = root / VERIFY.RT4_ANALYTIC_SKY_PPM_ARTIFACT
        sky_ppm_path.write_bytes(
            f"P6\n{sky_width} {sky_height}\n255\n".encode("ascii")
            + sky_rgb
        )
        sky_slices = [
            {
                "attachment": "camera_facing_sunless_hdr",
                "offset": 0,
                "bytes": len(sky_sunless),
                "sha256": hashlib.sha256(sky_sunless).hexdigest(),
            },
            {
                "attachment": "camera_facing_sun_hdr",
                "offset": len(sky_sunless),
                "bytes": len(sky_sun),
                "sha256": hashlib.sha256(sky_sun).hexdigest(),
            },
            {
                "attachment": "camera_facing_sun_sdr",
                "offset": 0,
                "bytes": len(sky_rgb),
                "sha256": hashlib.sha256(sky_rgb).hexdigest(),
            },
        ]
        colours = {
            ppm_pixels[pixel_offset : pixel_offset + 3]
            for pixel_offset in range(0, len(ppm_pixels), 3)
        }
        hdr_metrics = VERIFY._attachment_metrics(baseline_hdr, True)
        sdr_metrics = VERIFY._attachment_metrics(baseline_sdr, False)
        package_media_root = (
            root
            / "ror-ogre-next-n1-package"
            / "share"
            / "rigsofrods"
            / "ogre-next"
            / "Samples"
            / "Media"
        )
        for relative, payload in self.freetype_license_payloads.items():
            license_path = root / "ror-ogre-next-n1-package" / relative
            license_path.parent.mkdir(parents=True, exist_ok=True)
            license_path.write_bytes(payload)
        media_files = {
            "Hlms/Pbs/Any/Main_piece.any": b"hlms",
            (
                "Hlms/RoR/UvAffinePbs/UvAffinePbs_piece_ps.any"
            ): b"uv-affine-pbs",
            "2.0/scripts/Compositors/HDR.compositor": b"compositor",
            "2.0/scripts/materials/Common/Metal/Quad_vs.metal": b"common",
            "2.0/scripts/materials/HDR/HLSL/ToneMap.hlsl": b"hdr",
            (
                "2.0/scripts/materials/LocalCubemaps/"
                "BlendProjectCubemap.material"
            ): b"reflection-local-cubemap",
            (
                "Compute/Algorithms/IBL/"
                "SpecularIblIntegrator_piece_cs.any"
            ): b"reflection-ibl",
            "Compute/Tools/Any/sRGB.any": b"reflection-compute-tool",
        }
        for relative, payload in media_files.items():
            path = package_media_root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        hlms_manifest = VERIFY._packaged_media_manifest(
            package_media_root / "Hlms", (Path("."),), "fixture HLMS"
        )
        hdr_media_manifest = VERIFY._packaged_media_manifest(
            package_media_root,
            (
                Path("2.0/scripts/Compositors"),
                Path("2.0/scripts/materials/Common"),
                Path("2.0/scripts/materials/HDR"),
            ),
            "fixture HDR",
        )
        shader_media = contract["shader_media"]
        notice = shader_media["third_party_notice"]
        history_inputs = {
            "history_ogre_exposure": 0.0,
            "history_minimum_auto_exposure": -2.5,
            "history_maximum_auto_exposure": 2.5,
            "history_average_log_luminance": 7.5,
            "history_previous_inverse_luminance_r16_bits": int.from_bytes(
                struct.pack("<e", 0.02), "little"
            ),
            "history_delta_seconds": struct.unpack(
                "<f", struct.pack("<f", 1.0 / 48.0)
            )[0],
        }
        history_oracle = VERIFY._recompute_hdr_history_oracle(history_inputs)
        report = {
            "schema": VERIFY.RT4_REPORT_SCHEMA,
            "status": "pass",
            "provenance": {
                "ror_repository": self.ror_repository,
                "ror_ref": self.ror_ref,
                "ror_commit": self.ror_commit,
                "ror_relevant_source_manifest_sha256": self.ror_manifest,
                "ror_relevant_source_manifest_file_count": self.ror_manifest_count,
                "ogre_next_commit": self.ogre_commit,
                "ogre_next_archive_sha256": self.ogre_archive,
                "normal_map_source_lock_sha256": (
                    VERIFY.NORMAL_MAP_SOURCE_LOCK_SHA256
                ),
                "shader_media_root": shader_media["root"],
                "shader_media_license_expression": shader_media[
                    "license_expression"
                ],
                "shader_media_notice_path": notice["notice_path"],
                "shader_media_notice_sha256": self.shader_notice,
                "shader_media_manifest_sha256": hlms_manifest["sha256"],
                "shader_media_manifest_file_count": hlms_manifest["file_count"],
                "hdr_media_manifest_sha256": hdr_media_manifest["sha256"],
                "hdr_media_manifest_file_count": hdr_media_manifest[
                    "file_count"
                ],
            },
            "platform_policy": "macos-arm64-metal",
            "renderer": "Metal Rendering Subsystem",
            "adapter": {
                "frontend_version": "n1-ogre-3.0-" + self.ogre_commit,
                "native_mesh_path": (
                    "Ogre v2 Mesh plus immutable VertexArrayObject"
                ),
                "material_path": "HLMS PBS metallic-roughness",
                "brdf": "PbsBrdf::Default height-correlated GGX",
                "pbr_datablock_readback_verified": True,
                "raster_feature_tier": "MODERN_PBR_RT4_V1",
                "vertex_layout": "position_normal_tangent_uv0",
                "base_color_upload": "RGBA8_UNORM_SRGB",
                "metallic_roughness_upload": (
                    "linear_G_to_R8_roughness_B_to_R8_metallic"
                ),
                "emissive_upload": "RGBA8_UNORM_SRGB",
                "normal_upload": "linear_RGBA8_positive_Z_to_RG8_UNORM",
                "padded_source_rows_verified": True,
                "portable_sampler_mapping_verified": True,
                "normal_texture_admitted": True,
                "normal_slot": "PBSM_NORMAL",
                "normal_uv_source": 0,
                "normal_scale": 1,
                "normal_map_weight": 1,
                "normal_positive_z_tolerance_decoded": "1/255",
                "occlusion_texture_admitted": False,
                "occlusion_blocker": (
                    "pinned_HLMS_PBS_has_no_ambient_only_AO_slot"
                ),
                "runtime_media_root": "explicit_absolute",
                "package_media_relative_path": (
                    "share/rigsofrods/ogre-next/Samples/Media"
                ),
                "relocated_executable": True,
                "compositor2": True,
                "ui_included": False,
                "cpu_readback_completed": True,
                "analytic_lights_calibrated": True,
                "directional_lux_to_native_power_scale": 1.0 / 1024.0,
                "maximum_directional_lights": 1,
                "analytic_sky_capture_policy_version": 1,
                "analytic_sky_native_render_policy_version": 1,
                "analytic_sky_path": (
                    "camera_centered_gradient_ground_additive_sun"
                ),
                "analytic_sky_exact_skyx_pixel_capture": False,
                "constant_environment_only": False,
                "native_interop": False,
                "ray_tracing": False,
            },
            "catalog": {
                "registry_id": 0x4E315F534D4F4B45,
                "sequence": 7,
                "baseline_sequence": 1,
                "live_replacement_count": 6,
                "referenced_texture_count": 4,
                "referenced_sampler_count": 1,
                "unreferenced_assets_not_uploaded": True,
                "transactional_replay_after_restart": True,
            },
            "dynamic_meshes": {
                "schema": "ror.ogre_next_dynamic_mesh.v1",
                "base_deformation_revision": 1,
                "deformed_deformation_revision": 2,
                "full_update_owned": True,
                "solver_memory_aliased": False,
                "changed_pixels": 512,
                "base_attachment_fnv1a64": "0123456789abcdef",
                "deformed_attachment_fnv1a64": "fedcba9876543210",
                "base_exact_replay": True,
                "deformed_exact_replay": True,
            },
            "analytic_sky": {
                "schema": "ror.ogre_next_analytic_sky.v2",
                "evidence_file": VERIFY.RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT,
                "visual_file": VERIFY.RT4_ANALYTIC_SKY_PPM_ARTIFACT,
                "capture_policy_version": 1,
                "native_render_policy_version": 1,
                "authoritative_inputs": (
                    "joined_live_ambient_and_exact_converted_main_light"
                ),
                "exact_skyx_pixel_capture": False,
                "skyx_capture_boundary": (
                    "SkyX_shader_is_azimuth_dependent_and_may_apply_LDR_exposure"
                ),
                "sun_light_id": 1,
                "descriptor": {
                    "zenith_radiance": [
                        0.022859251126646996,
                        0.047378916293382645,
                        0.09662292897701263,
                    ],
                    "horizon_radiance": [
                        0.06477569788694382,
                        0.07451573759317398,
                        0.08912292867898941,
                    ],
                    "ground_radiance": [
                        0.001500000013038516,
                        0.0017999999690800905,
                        0.0022499999031424522,
                    ],
                    "sun_disk_radiance": [
                        25.812335968017578,
                        23.74734878540039,
                        21.166114807128906,
                    ],
                    "sun_angular_radius_radians": 0.00465047,
                },
                "native_geometry": {
                    "resource_model": "frontend_owned_v2_mesh_item",
                    "background_vertex_count": 2082,
                    "background_index_count": 11904,
                    "sun_vertex_count": 34,
                    "sun_index_count": 96,
                    "native_content_bytes": 107248,
                    "cpu_geometry_fnv1a64": 123456789,
                    "native_geometry_metadata_verified": True,
                    "production_default_gpu_content_readbacks_zero": True,
                    "exact_gpu_buffer_content_readback": True,
                    "camera_centered": True,
                    "rendered_first": True,
                    "depth_check_disabled": True,
                    "depth_write_disabled": True,
                    "additive_sun_disk": True,
                    "separate_sun_alpha_replace": True,
                    "casts_shadows": False,
                    "portable_scene_identity_absent": True,
                },
                "runtime_audit": {
                    "version": 2,
                    "completed_frames": 16,
                    "native_mesh_creates": 32,
                    "native_mesh_destroys": 32,
                    "native_vertex_buffer_creates": 32,
                    "native_vertex_buffer_destroys": 32,
                    "native_index_buffer_creates": 32,
                    "native_index_buffer_destroys": 32,
                    "native_vao_creates": 32,
                    "native_vao_destroys": 32,
                    "native_item_creates": 32,
                    "native_item_destroys": 32,
                    "native_scene_node_creates": 16,
                    "native_scene_node_destroys": 16,
                    "native_datablock_creates": 32,
                    "native_datablock_destroys": 32,
                    "native_mesh_absence_checks": 32,
                    "native_item_absence_checks": 32,
                    "native_scene_node_absence_checks": 16,
                    "native_datablock_absence_checks": 32,
                    "native_gpu_content_readbacks": 64,
                    "native_state_verifications": 16,
                },
                "visual_proof": {
                    "sky_only": True,
                    "camera_facing_sun": True,
                    "width": sky_width,
                    "height": sky_height,
                    "hdr_pixel_format": "RGBA16_FLOAT",
                    "evidence_bytes": len(sky_sunless) + len(sky_sun),
                    "sunless_hdr_offset": 0,
                    "sunless_hdr_bytes": len(sky_sunless),
                    "sun_hdr_offset": len(sky_sunless),
                    "sun_hdr_bytes": len(sky_sun),
                    "sunless_hdr_fnv1a64": VERIFY._fnv1a64(sky_sunless),
                    "sun_hdr_fnv1a64": VERIFY._fnv1a64(sky_sun),
                    "visual_rgb_fnv1a64": VERIFY._fnv1a64(sky_rgb),
                    "hemisphere_covered_pixels": sky_pixel_count,
                    "hemisphere_gradient_rows": sum(
                        abs(
                            sum(
                                coefficient * value
                                for coefficient, value in zip(
                                    (0.2126, 0.7152, 0.0722),
                                    struct.unpack_from(
                                        "<3e", sky_sunless_rows[row], 0
                                    ),
                                    strict=True,
                                )
                            )
                            - sum(
                                coefficient * value
                                for coefficient, value in zip(
                                    (0.2126, 0.7152, 0.0722),
                                    struct.unpack_from(
                                        "<3e", sky_sunless_rows[row - 1], 0
                                    ),
                                    strict=True,
                                )
                            )
                        )
                        > 1.0e-6
                        for row in range(1, sky_height)
                    ),
                    "broad_hemisphere_coverage": True,
                    "sun_changed_pixels": 1,
                    "sun_changed_pixels_alpha_exact_one": 1,
                    "sun_hdr_opaque_alpha_pixels": sky_pixel_count,
                    "visible_sun_effect": True,
                    "visible_sun_alpha_exact_one": True,
                },
                "transactional_rollback": {
                    "injected_stage_count": 20,
                    "publication_unchanged_on_failure": True,
                    "native_lifetimes_balanced_on_failure": True,
                    "clean_retry": True,
                },
            },
            "display_domain_unlit": {
                "schema": "ror.ogre_next_rt4_display_domain_unlit.v1",
                "base_color_transfer": (
                    "SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE"
                ),
                "upload_format": "RGBA8_UNORM",
                "mip_policy": "complete_base_to_1x1_nearest_mip",
                "sampler": "linear_min_mag_clamp_edge",
                "shader_precision": "PrecisionFull32",
                "encoded_filtered": [0.5, 128.0 / 255.0, 128.0 / 255.0],
                "filter_then_eotf": [0.21404114, 0.21586053, 0.21586053],
                "decode_before_filter": [0.5, 0.309789, 0.289193],
                "matching_foreground_pixels": 4096,
                "decode_before_filter_pixels": 0,
                "complete_unorm_mips_uploaded": True,
                "full32_after_filter_shader_executed": True,
                "alpha_untouched_opaque": True,
                "no_cast_or_receive_shadow_flags": True,
                "usage_transition_rollback_exact": True,
                "usage_transition_commit_exact": True,
            },
            "texture_allocations": copy.deepcopy(
                VERIFY.RT4_EXPECTED_TEXTURE_ALLOCATIONS
            ),
            "texture_upload_rollback": copy.deepcopy(
                VERIFY.RT4_EXPECTED_TEXTURE_UPLOAD_ROLLBACK
            ),
            "hdr_compositor": {
                "schema": "ror.ogre_next_hdr_compositor.v6",
                "workspace": "RoRHdrWorkspaceHudV1",
                "persistent_workspace": True,
                "scene_format": "RGBA16_FLOAT",
                "history_format": "R16_FLOAT",
                "output_format": "RGBA8_SRGB",
                "ui_included": True,
                "hud_workspace_verified": True,
                "deterministic_simulation_delta": True,
                "history_validation_mode": (
                    "native_authoritative_conditioning_plus_one_r16_ulp_v2"
                ),
                "native_r16_history_validated": True,
                "exact_current_to_old_copy_verified": True,
                "warmup_frames": 2,
                "committed_frames": 2,
                "split_lighting": {
                    "base_hdr_rgba16": True,
                    "sun_full_unoccluded_rgba16": True,
                    "sun_direct_rgba16": True,
                    "gpu_max_full_minus_base": True,
                    "transactional_sun_toggle": True,
                    "raster_lit_rgba16": True,
                    "scene_evaluations": 3,
                    "single_history_step": True,
                },
                "split_content": {
                    "rgb_channels_verified": pixel_count * 3,
                    "positive_sun_direct_pixels": pixel_count,
                    "canonical_base_full_raster_alpha_one_direct_alpha_zero": True,
                    "base_fnv1a64": VERIFY._fnv1a64(split_base),
                    "sun_full_fnv1a64": VERIFY._fnv1a64(split_sun_full),
                    "sun_direct_fnv1a64": VERIFY._fnv1a64(split_sun_direct),
                    "raster_lit_fnv1a64": VERIFY._fnv1a64(split_raster_lit),
                },
                "native_lighting_state_verifications": 6,
                "lighting_test_content_readbacks": 13,
                "lighting_production_content_readbacks": 0,
                "lighting_production_framebuffer_readbacks": 0,
                "ogre14_lighting_passes": 0,
                "initial_inverse_luminance_r16_bits": 8479,
                "final_inverse_luminance_r16_bits": history_oracle[
                    "reference_bits"
                ],
                "reference_inverse_luminance_r16_bits": history_oracle[
                    "reference_bits"
                ],
                **history_inputs,
                "history_absolute_error": 0.0,
                "history_allowed_error": history_oracle["allowed_error"],
                "history_conditioning_bound": history_oracle[
                    "conditioning_bound"
                ],
                "history_binary32_rounding_bound": history_oracle[
                    "rounding_bound"
                ],
                "history_storage_ulp": history_oracle["storage_ulp"],
                "history_r16_ulp_distance": 0,
                "history_changed_from_initial": True,
                "exposure_changed_pixels": 1024,
                "ui_overlay_control_node": "HdrRenderUi",
                "ui_overlay_control_kind": "Ogre::v1::Overlay",
                "ui_overlay_control_changed_pixels": pixel_count,
                "ui_overlay_control_magenta_pixels": pixel_count,
                "ui_overlay_control_fnv1a64": VERIFY._fnv1a64(
                    compositor_overlay
                ),
                "initialization_failure_stages_verified": 10,
                "same_object_reinitialize_verified": True,
                "frame_commit_prepare_failure_verified": True,
                "aborted_hdr_audit_unchanged": True,
                "aborted_reflection_audit_unchanged": True,
                "aborted_submission_uncommitted": True,
                "aborted_output_unchanged": True,
                "post_render_failure_fault_latched": True,
                "suspend_restore_preserved_graph": True,
                "invalid_resize_rollback_verified": True,
                "resize_rebuild_verified": True,
                "resized_frame_verified": True,
                "first_attachment_fnv1a64": VERIFY._fnv1a64(
                    compositor_first
                ),
                "final_attachment_fnv1a64": VERIFY._fnv1a64(
                    compositor_final
                ),
                "clean_shutdown": True,
            },
            "hdr_compositor_visual": {
                "schema": "ror.ogre_next_hdr_compositor_visual.v2",
                "evidence_file": VERIFY.RT4_COMPOSITOR_ARTIFACT,
                "ppm_attachment": "final_ui_free",
                "width": width,
                "height": height,
                "bytes_per_pixel": 4,
                "attachments": compositor_attachments,
                "linear_split_attachments": compositor_split_attachments,
                "evidence_bytes": len(compositor_payload),
            },
            "hdr": {
                "format": "RGBA16_FLOAT",
                "width": width,
                "height": height,
                **{
                    key: value
                    for key, value in hdr_metrics.items()
                    if key != "rgb"
                },
            },
            "sdr": {
                "format": "RGBA8_SRGB",
                "width": width,
                "height": height,
                **{
                    key: value
                    for key, value in sdr_metrics.items()
                    if key != "rgb"
                },
            },
            "texture_isolation": {
                "schema": "ror.ogre_next_rt4_texture_isolation.v2",
                "evidence_file": VERIFY.RT4_ISOLATION_ARTIFACT,
                "width": width,
                "height": height,
                "geometry_identical": True,
                "material_factors_constants_identical": True,
                "camera_identical": True,
                "lights_identical": True,
                "ui_included": False,
                "variants": variants,
                "evidence_bytes": texture_evidence_bytes,
            },
            "tangent_handedness": tangent_handedness,
            "reflection_probes": reflection_report,
            "texture_retirement": VERIFY.RT4_EXPECTED_RETIREMENT,
            "lifecycle": copy.deepcopy(VERIFY.RT4_EXPECTED_LIFECYCLE),
        }
        report["executable_build_identity"] = (
            VERIFY._expected_rt4_build_identity(contract, report)
        )
        report_path = root / VERIFY.RT4_REPORT_ARTIFACT
        report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
        repeat_paths = {
            VERIFY.RT4_REPEAT_REPORT_ARTIFACT: report_path,
            VERIFY.RT4_REPEAT_PPM_ARTIFACT: ppm_path,
            VERIFY.RT4_REPEAT_ISOLATION_ARTIFACT: isolation_path,
            VERIFY.RT4_REPEAT_REFLECTION_ARTIFACT: reflection_path,
            VERIFY.RT4_REPEAT_COMPOSITOR_ARTIFACT: compositor_path,
            VERIFY.RT4_REPEAT_ANALYTIC_SKY_PPM_ARTIFACT: sky_ppm_path,
            VERIFY.RT4_REPEAT_ANALYTIC_SKY_EVIDENCE_ARTIFACT: sky_evidence_path,
        }
        for relative, primary in repeat_paths.items():
            repeat = root / relative
            repeat.parent.mkdir(parents=True, exist_ok=True)
            repeat.write_bytes(primary.read_bytes())
        executable_path = (
            root
            / "ror-ogre-next-n1-package"
            / "bin"
            / VERIFY.RT4_PACKAGE_EXECUTABLE_STEM
        )
        executable_path.parent.mkdir(parents=True, exist_ok=True)
        executable_size = 64 * 1024
        command_bytes = 96
        header = struct.pack(
            "<IiiIIIII",
            0xFEEDFACF,
            0x0100000C,
            0,
            2,
            2,
            command_bytes,
            0,
            0,
        )
        segment = struct.pack(
            "<II16sQQQQiiII",
            0x19,
            72,
            b"__TEXT" + b"\0" * 10,
            0,
            executable_size,
            0,
            executable_size,
            5,
            5,
            0,
            0,
        )
        entry = struct.pack("<IIQQ", 0x80000028, 24, 128, 0)
        binary_tokens = b"\0".join(
            token.encode("utf-8")
            for token in (
                report["executable_build_identity"],
                VERIFY.RT4_REPORT_SCHEMA,
                "--modern-pbr",
                "Metal Rendering Subsystem",
                '\"raster_feature_tier\": \"MODERN_PBR_RT4_V1\"',
                "linear_RGBA8_positive_Z_to_RG8_UNORM",
            )
        )
        executable = header + segment + entry + binary_tokens
        executable += b"\0" * (executable_size - len(executable))
        executable_path.write_bytes(executable)
        executable_path.chmod(0o755)

        def file_entry(path: Path) -> dict[str, object]:
            return {
                "path": path.relative_to(root).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": VERIFY.sha256_file(path),
            }

        attestation = {
            "schema": VERIFY.RT4_ATTESTATION_SCHEMA,
            "status": "pass",
            "integrity_model": VERIFY.RT4_INTEGRITY_MODEL,
            "source": {
                "repository": self.ror_repository,
                "ref": self.ror_ref,
                "commit": self.ror_commit,
                "relevant_manifest_sha256": self.ror_manifest,
                "relevant_manifest_file_count": self.ror_manifest_count,
            },
            "ogre_next": {
                "repository": self.ogre_repository,
                "branch": self.ogre_branch,
                "commit": self.ogre_commit,
                "archive_sha256": self.ogre_archive,
                "license_spdx": self.lock["license"]["spdx"],
                "license_sha256": self.ogre_license,
                "normal_map_source_lock_sha256": (
                    VERIFY.NORMAL_MAP_SOURCE_LOCK_SHA256
                ),
            },
            "shader_media": {
                "root": shader_media["root"],
                "license_expression": shader_media["license_expression"],
                "source_path": notice["source_path"],
                "source_sha256": self.shader_source,
                "notice_path": notice["notice_path"],
                "notice_sha256": self.shader_notice,
                "manifest_sha256": hlms_manifest["sha256"],
                "manifest_file_count": hlms_manifest["file_count"],
                "hdr_manifest_sha256": hdr_media_manifest["sha256"],
                "hdr_manifest_file_count": hdr_media_manifest["file_count"],
            },
            "files": {
                "build_contract": file_entry(root / VERIFY.REQUIRED_ARTIFACTS[0]),
                "report": file_entry(report_path),
                "ppm": file_entry(ppm_path),
                "isolation": file_entry(isolation_path),
                "reflection": file_entry(reflection_path),
                "compositor": file_entry(compositor_path),
                "analytic_sky_evidence": file_entry(sky_evidence_path),
                "analytic_sky_ppm": file_entry(sky_ppm_path),
                "repeat_report": file_entry(
                    root / VERIFY.RT4_REPEAT_REPORT_ARTIFACT
                ),
                "repeat_ppm": file_entry(
                    root / VERIFY.RT4_REPEAT_PPM_ARTIFACT
                ),
                "repeat_isolation": file_entry(
                    root / VERIFY.RT4_REPEAT_ISOLATION_ARTIFACT
                ),
                "repeat_reflection": file_entry(
                    root / VERIFY.RT4_REPEAT_REFLECTION_ARTIFACT
                ),
                "repeat_compositor": file_entry(
                    root / VERIFY.RT4_REPEAT_COMPOSITOR_ARTIFACT
                ),
                "repeat_analytic_sky_evidence": file_entry(
                    root / VERIFY.RT4_REPEAT_ANALYTIC_SKY_EVIDENCE_ARTIFACT
                ),
                "repeat_analytic_sky_ppm": file_entry(
                    root / VERIFY.RT4_REPEAT_ANALYTIC_SKY_PPM_ARTIFACT
                ),
                "executable": file_entry(executable_path),
            },
            "isolation_slices": slices,
            "reflection_slices": reflection_slices,
            "compositor_slices": compositor_slices,
            "analytic_sky_slices": sky_slices,
        }
        (root / VERIFY.RT4_ATTESTATION_ARTIFACT).write_text(
            json.dumps(attestation) + "\n", encoding="utf-8"
        )

    def refresh_rt4_attestation(
        self,
        root: Path,
        file_keys: tuple[str, ...] = (),
        refresh_slices: bool = False,
        refresh_compositor_slices: bool = False,
        refresh_analytic_sky_slices: bool = False,
    ) -> None:
        path = root / VERIFY.RT4_ATTESTATION_ARTIFACT
        attestation = json.loads(path.read_text(encoding="utf-8"))
        for key in file_keys:
            entry = attestation["files"][key]
            artifact = root / entry["path"]
            attestation["files"][key] = {
                "path": entry["path"],
                "bytes": artifact.stat().st_size,
                "sha256": VERIFY.sha256_file(artifact),
            }
        if refresh_slices:
            payload = (root / VERIFY.RT4_ISOLATION_ARTIFACT).read_bytes()
            for entry in attestation["isolation_slices"]:
                start = entry["offset"]
                end = start + entry["bytes"]
                entry["sha256"] = hashlib.sha256(payload[start:end]).hexdigest()
            reflection = (root / VERIFY.RT4_REFLECTION_ARTIFACT).read_bytes()
            for entry in attestation["reflection_slices"]:
                start = entry["offset"]
                end = start + entry["bytes"]
                entry["sha256"] = hashlib.sha256(
                    reflection[start:end]
                ).hexdigest()
        if refresh_compositor_slices:
            payload = (root / VERIFY.RT4_COMPOSITOR_ARTIFACT).read_bytes()
            for entry in attestation["compositor_slices"]:
                start = entry["offset"]
                end = start + entry["bytes"]
                entry["sha256"] = hashlib.sha256(payload[start:end]).hexdigest()
        if refresh_analytic_sky_slices:
            payload = (
                root / VERIFY.RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT
            ).read_bytes()
            ppm = (root / VERIFY.RT4_ANALYTIC_SKY_PPM_ARTIFACT).read_bytes()
            ppm_header = b"P6\n768 512\n255\n"
            for entry in attestation["analytic_sky_slices"]:
                if entry["attachment"] == "camera_facing_sun_sdr":
                    block = ppm[len(ppm_header) :]
                else:
                    start = entry["offset"]
                    end = start + entry["bytes"]
                    block = payload[start:end]
                entry["sha256"] = hashlib.sha256(block).hexdigest()
        path.write_text(json.dumps(attestation) + "\n", encoding="utf-8")

    def write_metal_n2(self, root: Path, status: str) -> None:
        executable_path = root / VERIFY.METAL_N2_REQUIRED_ARTIFACTS[2]
        executable_path.parent.mkdir(parents=True, exist_ok=True)
        executable_path.write_bytes(b"attested-metal-n2")
        source = {
            "ror_commit": self.ror_commit,
            "ror_ref": self.ror_ref,
            "relevant_source_clean": True,
            "relevant_source_manifest_sha256": self.ror_manifest,
        }
        report = {
            "schema": "ror.ogre_next_metal_rt_n2.v3",
            "status": status,
            "provenance": {
                **source,
                "ror_repository": self.ror_repository,
                "ogre_next_repository": self.ogre_repository,
                "ogre_next_commit": self.ogre_commit,
                "ogre_next_archive_sha256": self.ogre_archive,
                "build_artifact": executable_path.name,
                "build_artifact_bytes": executable_path.stat().st_size,
                "build_artifact_sha256": VERIFY.sha256_file(executable_path),
            },
        }
        report_path = root / VERIFY.METAL_N2_REQUIRED_ARTIFACTS[0]
        report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
        probe_path = root / VERIFY.METAL_N2_PROBE_ARTIFACT
        if status == "pass":
            probe_path.write_bytes(b"12345678")

        def entry(path: Path) -> dict[str, object]:
            return {
                "path": path.name,
                "bytes": path.stat().st_size,
                "sha256": VERIFY.sha256_file(path),
            }

        attestation = {
            "schema": "ror.ogre_next_metal_rt_n2.attestation.v2",
            "status": status,
            "source": source,
            "executable": entry(executable_path),
            "report": entry(report_path),
            "probe": entry(probe_path) if status == "pass" else None,
        }
        (root / VERIFY.METAL_N2_REQUIRED_ARTIFACTS[1]).write_text(
            json.dumps(attestation) + "\n", encoding="utf-8"
        )

    def write_metal_n3(self, root: Path, status: str) -> None:
        executable_path = root / VERIFY.METAL_N3_REQUIRED_ARTIFACTS[2]
        executable_path.parent.mkdir(parents=True, exist_ok=True)
        executable_path.write_bytes(b"attested-metal-n3")
        source = {
            "ror_commit": self.ror_commit,
            "ror_ref": self.ror_ref,
            "relevant_source_clean": True,
            "relevant_source_manifest_sha256": self.ror_manifest,
        }
        report = {
            "schema": "ror.ogre_next_metal_rt_n3.v3",
            "status": status,
            "provenance": {
                **source,
                "ror_repository": self.ror_repository,
                "ogre_next_commit": self.ogre_commit,
                "build_artifact": executable_path.name,
                "build_artifact_bytes": executable_path.stat().st_size,
                "build_artifact_sha256": VERIFY.sha256_file(executable_path),
            },
        }
        if status == "skip":
            report["reason"] = "unsupported test device"
        if status == "pass":
            report.update(
                {
                    "scope": VERIFY.METAL_N3_SCOPE,
                    "device": {
                        "name": "Test Metal Device",
                        "same_ogre_device": True,
                        "same_ogre_queue": True,
                        "apple_family_9": True,
                    },
                    "contract": {
                        "image_version": 2,
                        "image_generation": 1,
                        "usage": "COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE",
                        "release_state": "GENERAL_READ_WRITE",
                        "return_state": "GENERAL_READ_WRITE",
                    },
                    "raster_contract": {
                        "raster_feature_tier": "MODERN_PBR_RT4_V1",
                        "native_feature_tier": "METAL_RAY_TRACING_N3",
                        "vertex_layout": (
                            "POSITION_NORMAL_TANGENT_UV0_FLOAT32_48"
                        ),
                        "vertex_stride_bytes": 48,
                        "authored_tangent_uv0": True,
                        "base_color_texture": "RGBA8_UNORM_SRGB",
                        "directional_light_lux": 1024,
                        "ray_material_parity_claimed": False,
                        "texture_allocations": {
                            "live": {
                                "version": 2,
                                "source_textures": 1,
                                "sampled_rgba": 1,
                                "linear_rgba": 0,
                                "roughness_r8": 0,
                                "metallic_r8": 0,
                                "normal_rg8": 0,
                                "creates": 1,
                                "destroys": 0,
                                "live": 1,
                                "exact_usage": True,
                            },
                            "after_shutdown": {
                                "version": 2,
                                "creates": 1,
                                "destroys": 1,
                                "live": 0,
                                "retired_name_lookups": 1,
                                "retired_name_rejections": 1,
                            },
                        },
                    },
                    "second_view_contribution": {
                        "width": 96,
                        "height": 64,
                        "format": "RGBA16_FLOAT",
                        "bytes": 96 * 64 * 8,
                        "sha256": "5" * 64,
                        "mean_luminance": 0.1,
                        "nontrivial_pixels": 2,
                    },
                    "resized_hybrid": {
                        "width": 80,
                        "height": 48,
                        "format": "RGBA16_FLOAT",
                        "bytes": 80 * 48 * 8,
                        "sha256": "6" * 64,
                        "mean_luminance": 0.2,
                        "nontrivial_pixels": 3,
                    },
                    "proof": {
                        **{
                            field: True
                            for field in VERIFY.METAL_N3_REQUIRED_PROOF_BOOLEANS
                        },
                        "contribution_pixels": 1,
                        "off_axis_far_plane_contribution_pixels": 1,
                    },
                }
            )

        def entry(path: Path) -> dict[str, object]:
            return {
                "path": path.name,
                "bytes": path.stat().st_size,
                "sha256": VERIFY.sha256_file(path),
            }

        raster_pixel = struct.pack("<4e", 0.5, 0.25, 0.125, 1.0)
        contribution_hit = struct.pack("<4e", 0.25, 0.125, 0.0625, 0.0)
        contribution_zero = struct.pack("<4e", 0.0, 0.0, 0.0, 0.0)
        hybrid_hit = struct.pack("<4e", 0.75, 0.375, 0.1875, 1.0)
        pixel_count = 96 * 64
        payloads = {
            "raster_only_hdr": raster_pixel * pixel_count,
            "rt_contribution": contribution_hit
            + contribution_zero * (pixel_count - 1),
            "hybrid_hdr": hybrid_hit + raster_pixel * (pixel_count - 1),
        }
        images: dict[str, dict[str, object] | None] = {}
        for key, name in VERIFY.METAL_N3_IMAGE_ARTIFACTS:
            path = root / name
            if status == "pass":
                path.write_bytes(payloads[key])
                images[key] = entry(path)
                report[key] = VERIFY._metal_n3_image_metrics(payloads[key])
            else:
                images[key] = None
        report_path = root / VERIFY.METAL_N3_REQUIRED_ARTIFACTS[0]
        report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
        attestation = {
            "schema": "ror.ogre_next_metal_rt_n3.attestation.v1",
            "status": status,
            "source": source,
            "executable": entry(executable_path),
            "report": entry(report_path),
            **images,
        }
        (root / VERIFY.METAL_N3_REQUIRED_ARTIFACTS[1]).write_text(
            json.dumps(attestation) + "\n", encoding="utf-8"
        )

    def update_metal_n3_attestation(self, root: Path) -> None:
        report_path = root / VERIFY.METAL_N3_REQUIRED_ARTIFACTS[0]
        attestation_path = root / VERIFY.METAL_N3_REQUIRED_ARTIFACTS[1]
        attestation = json.loads(attestation_path.read_text(encoding="utf-8"))
        attestation["report"] = {
            "path": report_path.name,
            "bytes": report_path.stat().st_size,
            "sha256": VERIFY.sha256_file(report_path),
        }
        for key, name in VERIFY.METAL_N3_IMAGE_ARTIFACTS:
            path = root / name
            if path.exists():
                attestation[key] = {
                    "path": path.name,
                    "bytes": path.stat().st_size,
                    "sha256": VERIFY.sha256_file(path),
                }
        attestation_path.write_text(json.dumps(attestation) + "\n", encoding="utf-8")

    def mutate_metal_n3_report(self, root: Path, mutation) -> None:
        report_path = root / VERIFY.METAL_N3_REQUIRED_ARTIFACTS[0]
        report = json.loads(report_path.read_text(encoding="utf-8"))
        mutation(report)
        report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
        self.update_metal_n3_attestation(root)

    def assert_metal_n3_report_rejected(self, mutation, expected: str) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n3-invalid-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n3(root, "pass")
            self.mutate_metal_n3_report(root, mutation)
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, expected):
                VERIFY.verify_artifact_set(root, verify_metal_n3_evidence=True)

    def write_metal_n4(self, root: Path, status: str) -> None:
        executable_path = root / VERIFY.METAL_N4_REQUIRED_ARTIFACTS[1]
        executable_path.parent.mkdir(parents=True, exist_ok=True)
        executable_path.write_bytes(b"attested-metal-n4-directional-shadow")
        provenance = {
            "ror_repository": self.ror_repository,
            "ror_ref": self.ror_ref,
            "ror_commit": self.ror_commit,
            "relevant_source_clean": True,
            "relevant_source_manifest_sha256": self.ror_manifest,
            "ogre_next_commit": self.ogre_commit,
            "build_artifact": executable_path.name,
            "build_artifact_bytes": executable_path.stat().st_size,
            "build_artifact_sha256": VERIFY.sha256_file(executable_path),
        }
        report: dict[str, object] = {
            "schema": "ror.ogre_next_metal_rt_n4_directional_shadow.v2",
            "status": status,
            "scope": (
                VERIFY.METAL_N4_PASS_SCOPE
                if status == "pass"
                else VERIFY.METAL_N4_SKIP_SCOPE
            ),
            "provenance": provenance,
        }
        if status == "skip":
            report.update(
                {
                    "reason": "test device does not expose Apple GPU family 9",
                    "device_name": "Test Paravirtual Metal Device",
                    "required_apple_gpu_family": 9,
                    "required_metal_ray_tracing": True,
                    "required_visibility_format": "R16_FLOAT",
                }
            )
        else:
            width = 96
            height = 64
            pixel_count = width * height
            raster_pixel = struct.pack("<4e", 0.5, 0.25, 0.125, 1.0)
            raster = raster_pixel * pixel_count
            occluded_start = 100
            occluded_end = 200
            visibility = (
                struct.pack("<H", 0x3C00) * occluded_start
                + struct.pack("<H", 0x0000)
                * (occluded_end - occluded_start)
                + struct.pack("<H", 0x3C00) * (pixel_count - occluded_end)
            )
            lineage = (
                struct.pack("<I", 1) * occluded_start
                + struct.pack("<I", 3) * (occluded_end - occluded_start)
                + struct.pack("<I", 1) * (pixel_count - occluded_end)
            )
            shadow_pixel = struct.pack("<4H", 0, 0, 0, 0x3C00)
            hybrid = (
                raster_pixel * occluded_start
                + shadow_pixel * (occluded_end - occluded_start)
                + raster_pixel * (pixel_count - occluded_end)
            )
            payloads = {
                "raster": raster,
                "visibility": visibility,
                "ray_lineage": lineage,
                "hybrid": hybrid,
            }
            artifacts: dict[str, dict[str, object]] = {}
            for key, name, pixel_format, _ in VERIFY.METAL_N4_IMAGE_ARTIFACTS:
                path = root / name
                path.write_bytes(payloads[key])
                artifacts[key] = {
                    "format": pixel_format,
                    "bytes": path.stat().st_size,
                    "sha256": VERIFY.sha256_file(path),
                }
            artifacts["visibility"].update(
                {
                    "visible_r16_bits": "0x3c00",
                    "occluded_r16_bits": "0x0000",
                }
            )

            def sample(pixel: int, label: str, blocker: int) -> dict[str, object]:
                offset = pixel * 8
                visibility_bits = struct.unpack_from(
                    "<H", visibility, pixel * 2
                )[0]
                return {
                    "x": pixel % width,
                    "y": pixel // width,
                    "visibility": label,
                    "visibility_r16_bits": f"0x{visibility_bits:04x}",
                    "secondary_blocker_instance_id": blocker,
                    "raster_rgba16_bits": VERIFY._metal_n4_rgba16_strings(
                        raster, offset
                    ),
                    "hybrid_rgba16_bits": VERIFY._metal_n4_rgba16_strings(
                        hybrid, offset
                    ),
                    "portable_contract_validated": True,
                }

            report.update(
                {
                    "device": {
                        "name": "Test Apple Family 9 Device",
                        "same_ogre_device": True,
                        "same_ogre_queue": True,
                        "apple_family_9": True,
                    },
                    "raster_contract": {
                        "raster_feature_tier": "MODERN_PBR_RT4_V1",
                        "native_feature_tier": (
                            "METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW"
                        ),
                        "directional_shadow_mode": "DISABLED",
                        "pssm_enabled": False,
                        "vertex_layout": (
                            "POSITION_NORMAL_TANGENT_UV0_FLOAT32_48"
                        ),
                        "vertex_stride_bytes": 48,
                    },
                    "native_contract": {
                        "version": 1,
                        "backend": "METAL",
                        "tier": "NATIVE_DIRECTIONAL_HARD_SHADOW_V1",
                        "blas_count": 2,
                        "tlas_instance_count": 2,
                        "primary_camera_rays_per_sample": 1,
                        "secondary_directional_visibility_rays_per_sample": 1,
                        "receiver_instance_id": 1,
                        "occluder_instance_id": 2,
                    },
                    "artifacts": artifacts,
                    "coverage": {
                        "width": width,
                        "height": height,
                        "pixels": pixel_count,
                        "receiver_visible_pixels": pixel_count - 100,
                        "occluded_pixels": 100,
                        "primary_miss_pixels": 0,
                    },
                    "samples": [
                        sample(0, "VISIBLE", 0),
                        sample(occluded_start, "OCCLUDED", 2),
                    ],
                    "runtime_sequence": {
                        "exact_repeat": {
                            "frame_id": 2,
                            "width": width,
                            "height": height,
                            "pixels": pixel_count,
                            "receiver_visible_pixels": pixel_count - 100,
                            "occluded_pixels": 100,
                            "primary_miss_pixels": 0,
                            "raster_sha256": artifacts["raster"]["sha256"],
                            "visibility_sha256": artifacts["visibility"][
                                "sha256"
                            ],
                            "ray_lineage_sha256": artifacts["ray_lineage"][
                                "sha256"
                            ],
                            "hybrid_sha256": artifacts["hybrid"]["sha256"],
                        },
                        "moved_occluder": {
                            "frame_id": 3,
                            "width": width,
                            "height": height,
                            "pixels": pixel_count,
                            "receiver_visible_pixels": pixel_count - 120,
                            "occluded_pixels": 120,
                            "primary_miss_pixels": 0,
                            "raster_sha256": artifacts["raster"]["sha256"],
                            "visibility_sha256": "1" * 64,
                            "ray_lineage_sha256": "2" * 64,
                            "hybrid_sha256": "3" * 64,
                        },
                        "resized_extent": {
                            "frame_id": 4,
                            "width": 80,
                            "height": 48,
                            "pixels": 80 * 48,
                            "receiver_visible_pixels": 80 * 48 - 80,
                            "occluded_pixels": 80,
                            "primary_miss_pixels": 0,
                            "raster_sha256": "4" * 64,
                            "visibility_sha256": "5" * 64,
                            "ray_lineage_sha256": "6" * 64,
                            "hybrid_sha256": "7" * 64,
                        },
                    },
                    "proof": {
                        field: True
                        for field in VERIFY.METAL_N4_REQUIRED_PROOF_BOOLEANS
                    },
                }
            )
        report_path = root / VERIFY.METAL_N4_REQUIRED_ARTIFACTS[0]
        report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")

    def mutate_metal_n4_report(self, root: Path, mutation) -> None:
        report_path = root / VERIFY.METAL_N4_REQUIRED_ARTIFACTS[0]
        report = json.loads(report_path.read_text(encoding="utf-8"))
        mutation(report)
        report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")

    def assert_metal_n4_report_rejected(self, mutation, expected: str) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n4-invalid-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n4(root, "pass")
            self.mutate_metal_n4_report(root, mutation)
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, expected):
                VERIFY.verify_artifact_set(root, verify_metal_n4_evidence=True)

    def test_requires_every_exact_nonempty_regular_file(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-artifacts-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            manifest = VERIFY.verify_artifact_set(root)
            manifest_paths = [entry["path"] for entry in manifest]
            package_media_root = (
                root
                / "ror-ogre-next-n1-package"
                / "share"
                / "rigsofrods"
                / "ogre-next"
                / "Samples"
                / "Media"
            )
            packaged_media_paths = sorted(
                path.relative_to(root).as_posix()
                for path in package_media_root.rglob("*")
                if path.is_file()
            )
            self.assertEqual(
                manifest_paths,
                [
                    *VERIFY.REQUIRED_ARTIFACTS,
                    "bin/ror_ogre_next_pssm_shadow_smoke",
                    VERIFY.PSSM_EVIDENCE_ARTIFACT,
                    VERIFY.PSSM_EXECUTION_RECEIPT_ARTIFACT,
                    VERIFY.PSSM_ATTESTATION_ARTIFACT,
                    VERIFY.PSSM_ARTIFACT_MANIFEST_ARTIFACT,
                    *packaged_media_paths,
                    "ror-ogre-next-n1-package/bin/ror_ogre_next_frontend_n1_smoke",
                    "ror-ogre-next-n1-package/licenses/FreeType-GPLv2.txt",
                    "ror-ogre-next-n1-package/licenses/FreeType-LICENSE.txt",
                ],
            )
            self.assertTrue(
                any("n1-package/share/" in path for path in manifest_paths)
            )
            missing = root / VERIFY.REQUIRED_ARTIFACTS[-1]
            missing.unlink()
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "missing"):
                VERIFY.verify_artifact_set(root)

    def test_freetype_license_contract_matches_the_canonical_lock(self) -> None:
        freetype = self.lock["dependencies"]["freetype"]
        self.assertEqual(
            dict(self.production_freetype_license_contract),
            {
                freetype["package_license_path"]: freetype[
                    "license_sha256"
                ],
                freetype["package_overview_path"]: freetype[
                    "overview_sha256"
                ],
            },
        )

    def test_rejects_tampered_freetype_package_license(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-freetype-license-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            license_path = (
                root
                / "ror-ogre-next-n1-package"
                / "licenses"
                / "FreeType-GPLv2.txt"
            )
            license_path.write_bytes(license_path.read_bytes() + b"tampered")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "FreeType package license hash mismatch",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rejects_missing_or_indirect_freetype_package_license(self) -> None:
        for mode in ("missing", "symlink"):
            with self.subTest(mode=mode):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-freetype-license-layout-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    license_path = (
                        root
                        / "ror-ogre-next-n1-package"
                        / "licenses"
                        / "FreeType-LICENSE.txt"
                    )
                    license_path.unlink()
                    if mode == "symlink":
                        license_path.write_bytes(
                            self.freetype_license_payloads[
                                "licenses/FreeType-LICENSE.txt"
                            ]
                        )
                        license_path = license_path.resolve()
                        original_is_symlink = Path.is_symlink
                        with mock.patch.object(
                            Path,
                            "is_symlink",
                            autospec=True,
                            side_effect=lambda candidate: (
                                candidate == license_path
                                or original_is_symlink(candidate)
                            ),
                        ):
                            with self.assertRaisesRegex(
                                VERIFY.ArtifactSetError,
                                "FreeType package license is missing or indirect",
                            ):
                                VERIFY.verify_artifact_set(root)
                    else:
                        with self.assertRaisesRegex(
                            VERIFY.ArtifactSetError,
                            "FreeType package license is missing or indirect",
                        ):
                            VERIFY.verify_artifact_set(root)

    def test_rejects_empty_artifact(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-artifacts-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            (root / VERIFY.REQUIRED_ARTIFACTS[0]).write_bytes(b"")
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "empty"):
                VERIFY.verify_artifact_set(root)

    def test_rejects_duplicate_json_object_keys(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-duplicate-json-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            report_path = root / VERIFY.PSSM_REPORT_ARTIFACT
            payload = report_path.read_text(encoding="utf-8").replace(
                '"status": "pass",',
                '"status": "pass", "status": "pass",',
                1,
            )
            report_path.write_text(payload, encoding="utf-8")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "duplicate JSON object key"
            ):
                VERIFY.verify_artifact_set(root)

    def test_pssm_gate_binds_distant_evidence_bytes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-pssm-evidence-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            evidence_path = root / VERIFY.PSSM_EVIDENCE_ARTIFACT
            payload = bytearray(evidence_path.read_bytes())
            payload[-1] ^= 1
            evidence_path.write_bytes(payload)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "PSSM evidence slice mismatch"
            ):
                VERIFY.verify_artifact_set(root)

    def test_pssm_gate_rejects_mutated_native_aabb_observations(self) -> None:
        mutations = (
            (
                "mesh local",
                lambda observations: observations[0]["ogre_mesh_local"][
                    "minimum"
                ].__setitem__(0, -2.25),
            ),
            (
                "item local",
                lambda observations: observations[1]["ogre_item_local"][
                    "maximum"
                ].__setitem__(1, 0.5),
            ),
            (
                "item world",
                lambda observations: observations[1]["ogre_item_world"][
                    "minimum"
                ].__setitem__(2, 1.25),
            ),
            (
                "expected world",
                lambda observations: observations[0]["expected_world"][
                    "maximum"
                ].__setitem__(2, 0.25),
            ),
            (
                "caster role",
                lambda observations: observations[1].__setitem__(
                    "casts_shadow", False
                ),
            ),
            (
                "missing receiver",
                lambda observations: observations.pop(0),
            ),
        )
        for label, mutate in mutations:
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix="ror-ogre-pssm-aabb-"
            ) as temp:
                root = Path(temp)
                self.write_baseline(root)
                report_path = root / VERIFY.PSSM_REPORT_ARTIFACT
                report = json.loads(report_path.read_text(encoding="utf-8"))
                mutate(
                    report["projection_and_bounds_fixture"][
                        "native_aabb_observations"
                    ]
                )
                report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
                self.refresh_pssm_integrity(root)
                with self.assertRaisesRegex(
                    VERIFY.ArtifactSetError, "PSSM native AABB"
                ):
                    VERIFY.verify_artifact_set(root)

    def test_pssm_gate_requires_challenged_execution_receipt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-pssm-receipt-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            (root / VERIFY.PSSM_EXECUTION_RECEIPT_ARTIFACT).unlink()
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "execution receipt.*missing"
            ):
                VERIFY.verify_artifact_set(root)

    def test_pssm_gate_rejects_token_only_entrypoint(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-pssm-token-exe-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            executable = root / "bin" / VERIFY.PSSM_EXECUTABLE_STEM
            payload = executable.read_bytes()
            # Remove the synthetic arm64 RET at LC_MAIN.entryoff. The embedded
            # identity and contract tokens remain, reproducing the old
            # token-only executable that passed superficial verification.
            executable.write_bytes(payload[:128] + payload[132:] + b"\0" * 4)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "no plausible machine code"
            ):
                VERIFY.verify_artifact_set(root)

    def test_pssm_unsupported_requires_exact_capability_evidence(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-pssm-unsupported-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            report_path = root / VERIFY.PSSM_REPORT_ARTIFACT
            report = json.loads(report_path.read_text(encoding="utf-8"))
            unsupported = {
                "schema": VERIFY.PSSM_REPORT_SCHEMA,
                "status": "unsupported",
                "execution": report["execution"],
                "provenance": report["provenance"],
                "platform_policy": report["platform_policy"],
                "renderer": report["renderer"],
                "capability_evidence": {
                    "code": "PSSM_REQUIRED_NATIVE_CAPABILITY_MISSING",
                    "reason": VERIFY.PSSM_UNSUPPORTED_DETAIL,
                    "required_atlas_width": 2048,
                    "required_atlas_height": 3072,
                    "required_format": "D32_FLOAT",
                    "required_filter": "PCF_4x4_TEXTURE_GATHER",
                    "observed_maximum_texture_dimension": 16384,
                    "atlas_dimensions_supported": True,
                    "texture_gather_supported": False,
                    "d32_probe_attempted": False,
                    "d32_render_target_supported": False,
                    "d32_atlas_allocation_verified": False,
                    "d32_atlas_readback_verified": False,
                    "d32_atlas_cleanup_verified": True,
                    "d32_atlas_cleanup_absence_checks": 1,
                },
                "backend_substitution": False,
            }
            report_path.write_text(
                json.dumps(unsupported) + "\n", encoding="utf-8"
            )
            (root / VERIFY.PSSM_EVIDENCE_ARTIFACT).unlink()
            self.refresh_pssm_integrity(root)
            VERIFY.verify_artifact_set(root)
            contract = json.loads(
                (root / VERIFY.REQUIRED_ARTIFACTS[0]).read_text(encoding="utf-8")
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "requires an actual PSSM native pass"
            ):
                VERIFY._verify_pssm(
                    root, [], contract, require_pass=True
                )
            unsupported["capability_evidence"]["reason"] = "arbitrary skip"
            report_path.write_text(
                json.dumps(unsupported) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "unsupported capability evidence is not exact",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_package_media_is_byte_exact_and_required(self) -> None:
        relatives = (
            "Hlms/Pbs/Any/Main_piece.any",
            "Hlms/RoR/UvAffinePbs/UvAffinePbs_piece_ps.any",
            "2.0/scripts/materials/HDR/HLSL/ToneMap.hlsl",
        )
        for relative in relatives:
            with self.subTest(relative=relative):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-rt4-package-media-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    path = (
                        root
                        / "ror-ogre-next-n1-package"
                        / "share"
                        / "rigsofrods"
                        / "ogre-next"
                        / "Samples"
                        / "Media"
                        / relative
                    )
                    path.write_bytes(path.read_bytes() + b"tamper")
                    with self.assertRaisesRegex(
                        VERIFY.ArtifactSetError,
                        "packaged shader-media manifest mismatch",
                    ):
                        VERIFY.verify_artifact_set(root)

    def test_rt4_gate_recomputes_report_semantics_after_reattestation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-report-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            report_path = root / VERIFY.RT4_REPORT_ARTIFACT
            report = json.loads(report_path.read_text(encoding="utf-8"))
            report["texture_isolation"]["geometry_identical"] = False
            report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
            self.refresh_rt4_attestation(root, ("report",))
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "RT4 isolation controls failed"
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_rejects_tampered_native_uv0_affine_receipt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-uv0-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            report_path = root / VERIFY.RT4_REPORT_ARTIFACT
            report = json.loads(report_path.read_text(encoding="utf-8"))
            report["texture_isolation"]["variants"][6]["uv0_affine"][
                "native_user_value_readbacks"
            ] = 2
            report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
            self.refresh_rt4_attestation(root, ("report",))
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "native UV0 affine receipt"
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_recomputes_ppm_semantics_after_reattestation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-ppm-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            ppm_path = root / VERIFY.RT4_PPM_ARTIFACT
            ppm = bytearray(ppm_path.read_bytes())
            ppm[-1] ^= 1
            ppm_path.write_bytes(ppm)
            self.refresh_rt4_attestation(root, ("ppm",))
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "RT4 HDR compositor visual evidence failed",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_recomputes_isolation_after_file_and_slice_reattestation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-isolation-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            isolation_path = root / VERIFY.RT4_ISOLATION_ARTIFACT
            payload = bytearray(isolation_path.read_bytes())
            payload[0] ^= 1
            isolation_path.write_bytes(payload)
            self.refresh_rt4_attestation(
                root, ("isolation",), refresh_slices=True
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "exact hash mismatch"
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_reflection_semantics_are_cross_platform_and_tamper_closed(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-reflection-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            report = json.loads(
                (root / VERIFY.RT4_REPORT_ARTIFACT).read_text(encoding="utf-8")
            )
            contract = self.build_contract()
            reflection_path = root / VERIFY.RT4_REFLECTION_ARTIFACT
            renderers = {
                "macos-arm64-metal": "Metal Rendering Subsystem",
                "windows-x64-d3d11": "Direct3D11 Rendering Subsystem",
                "linux-x86_64-vulkan": "Vulkan Rendering Subsystem",
            }
            for policy_name, backend in VERIFY.RT4_REFLECTION_BACKENDS.items():
                with self.subTest(policy=policy_name):
                    policy_report = copy.deepcopy(report)
                    policy_contract = copy.deepcopy(contract)
                    policy_contract["platform"]["policy"] = policy_name
                    policy_report["renderer"] = renderers[policy_name]
                    reflection = policy_report["reflection_probes"]
                    reflection["backend"] = backend
                    reflection["render_system"] = renderers[policy_name]
                    slices = VERIFY._verify_rt4_reflection_semantics(
                        policy_report, reflection_path, policy_contract
                    )
                    self.assertEqual(len(slices), 18)

            wrong_resolution = copy.deepcopy(report)
            wrong_resolution["reflection_probes"]["runtime_audit"][
                "probe_resolution"
            ] = VERIFY.RT4_REFLECTION_RESOLUTION * 2
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "RT4 reflection runtime audit failed: probe_resolution",
            ):
                VERIFY._verify_rt4_reflection_semantics(
                    wrong_resolution, reflection_path, contract
                )

            valid_payload = reflection_path.read_bytes()
            changed = bytearray(valid_payload)
            changed[0] ^= 1
            reflection_path.write_bytes(changed)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "reflection raw evidence failed"
            ):
                VERIFY._verify_rt4_reflection_semantics(
                    report, reflection_path, contract
                )

            nonfinite = bytearray(valid_payload)
            nonfinite[:8] = struct.pack(
                "<4e", float("nan"), 1.0, 1.0, 1.0
            )
            reflection_path.write_bytes(nonfinite)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "non-finite"
            ):
                VERIFY._verify_rt4_reflection_semantics(
                    report, reflection_path, contract
                )

            reflection_path.write_bytes(valid_payload + b"\x00")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "trailing bytes"
            ):
                VERIFY._verify_rt4_reflection_semantics(
                    report, reflection_path, contract
                )

            mip_one_start = (
                VERIFY.RT4_REFLECTION_RAW_BYTES
                + VERIFY.RT4_REFLECTION_RESOLUTION
                * VERIFY.RT4_REFLECTION_RESOLUTION
                * VERIFY.RT4_REFLECTION_FACE_COUNT
                * 8
            )
            no_mip_one = valid_payload[:mip_one_start] + bytes(
                len(valid_payload) - mip_one_start
            )
            no_mip_report = copy.deepcopy(report)
            filtered = no_mip_one[VERIFY.RT4_REFLECTION_RAW_BYTES :]
            metrics = VERIFY._reflection_half_metrics(filtered, "filtered")
            filtered_report = no_mip_report["reflection_probes"]["filtered"]
            filtered_report["exact_fnv1a64"] = VERIFY._fnv1a64(filtered)
            for key, value in metrics.items():
                filtered_report[key] = value
            reflection_path.write_bytes(no_mip_one)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "subresource coverage"
            ):
                VERIFY._verify_rt4_reflection_semantics(
                    no_mip_report, reflection_path, contract
                )

    def test_rt4_gate_recomputes_reflection_and_requires_slice_attestation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-reflection-gate-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            reflection_path = root / VERIFY.RT4_REFLECTION_ARTIFACT
            payload = bytearray(reflection_path.read_bytes())
            payload[0] ^= 1
            reflection_path.write_bytes(payload)
            self.refresh_rt4_attestation(
                root, ("reflection",), refresh_slices=True
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "reflection raw evidence failed"
            ):
                VERIFY.verify_artifact_set(root)

            self.write_baseline(root)
            attestation_path = root / VERIFY.RT4_ATTESTATION_ARTIFACT
            attestation = json.loads(
                attestation_path.read_text(encoding="utf-8")
            )
            attestation["reflection_slices"][7]["sha256"] = "0" * 64
            attestation_path.write_text(
                json.dumps(attestation) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "reflection SHA-256 slice attestation mismatch",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_requires_exact_sha256_slice_attestation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-slice-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            attestation_path = root / VERIFY.RT4_ATTESTATION_ARTIFACT
            attestation = json.loads(
                attestation_path.read_text(encoding="utf-8")
            )
            attestation["isolation_slices"][3]["sha256"] = "0" * 64
            attestation_path.write_text(
                json.dumps(attestation) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "SHA-256 slice attestation mismatch"
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_binds_packaged_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-executable-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            executable = (
                root
                / "ror-ogre-next-n1-package"
                / "bin"
                / VERIFY.RT4_PACKAGE_EXECUTABLE_STEM
            )
            executable.write_bytes(b"tampered")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "structurally implausible"
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_rejects_refreshed_arbitrary_executable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-forged-exe-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            executable = (
                root
                / "ror-ogre-next-n1-package"
                / "bin"
                / VERIFY.RT4_PACKAGE_EXECUTABLE_STEM
            )
            executable.write_bytes(b"arbitrary executable bytes!!")
            executable.chmod(0o755)
            self.refresh_rt4_attestation(root, ("executable",))
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "structurally implausible"
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_rejects_exact_refreshed_hash_forgery(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-forgery-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            report_path = root / VERIFY.RT4_REPORT_ARTIFACT
            report = json.loads(report_path.read_text(encoding="utf-8"))
            for field in (
                "adapter",
                "catalog",
                "texture_allocations",
                "lifecycle",
                "platform_policy",
                "renderer",
            ):
                report.pop(field)
            report["hdr"]["maximum_luminance"] = -999
            report["hdr"]["non_background_pixels"] = 0
            report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
            executable = (
                root
                / "ror-ogre-next-n1-package"
                / "bin"
                / VERIFY.RT4_PACKAGE_EXECUTABLE_STEM
            )
            executable.write_bytes(b"arbitrary executable bytes!!")
            executable.chmod(0o755)
            self.refresh_rt4_attestation(root, ("report", "executable"))
            with self.assertRaises(VERIFY.ArtifactSetError):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_recomputes_hdr_energy_and_geometry(self) -> None:
        for field, value in (
            ("maximum_luminance", -999),
            ("non_background_pixels", 0),
        ):
            with self.subTest(field=field):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-rt4-hdr-forgery-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    report_path = root / VERIFY.RT4_REPORT_ARTIFACT
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    report["hdr"][field] = value
                    report_path.write_text(
                        json.dumps(report) + "\n", encoding="utf-8"
                    )
                    self.refresh_rt4_attestation(root, ("report",))
                    with self.assertRaisesRegex(
                        VERIFY.ArtifactSetError,
                        "RT4 PPM/isolation report mismatch",
                    ):
                        VERIFY.verify_artifact_set(root)

    def test_rt4_gate_requires_exact_report_sections(self) -> None:
        for section in (
            "adapter",
            "catalog",
            "dynamic_meshes",
            "analytic_sky",
            "texture_allocations",
            "texture_upload_rollback",
            "texture_retirement",
            "texture_isolation",
            "tangent_handedness",
            "hdr_compositor",
            "lifecycle",
            "platform_policy",
            "renderer",
        ):
            with self.subTest(section=section):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-rt4-section-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    report_path = root / VERIFY.RT4_REPORT_ARTIFACT
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    report.pop(section)
                    report_path.write_text(
                        json.dumps(report) + "\n", encoding="utf-8"
                    )
                    self.refresh_rt4_attestation(root, ("report",))
                    with self.assertRaises(VERIFY.ArtifactSetError):
                        VERIFY.verify_artifact_set(root)

    def test_rt4_analytic_sky_is_fail_closed_after_reattestation(self) -> None:
        mutations = (
            lambda report: report["analytic_sky"].__setitem__(
                "capture_policy_version", True
            ),
            lambda report: report["analytic_sky"].__setitem__(
                "exact_skyx_pixel_capture", True
            ),
            lambda report: report["analytic_sky"]["descriptor"].__setitem__(
                "zenith_radiance", [0.0, 0.0, 0.0]
            ),
            lambda report: report["analytic_sky"]["native_geometry"].__setitem__(
                "depth_write_disabled", False
            ),
            lambda report: report["analytic_sky"]["runtime_audit"].__setitem__(
                "native_datablock_destroys", 31
            ),
            lambda report: report["analytic_sky"][
                "transactional_rollback"
            ].__setitem__("clean_retry", False),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-rt4-analytic-sky-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    report_path = root / VERIFY.RT4_REPORT_ARTIFACT
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    mutation(report)
                    report_path.write_text(
                        json.dumps(report) + "\n", encoding="utf-8"
                    )
                    self.refresh_rt4_attestation(root, ("report",))
                    with self.assertRaisesRegex(
                        VERIFY.ArtifactSetError,
                        "RT4 analytic-sky controls failed",
                    ):
                        VERIFY.verify_artifact_set(root)

    def test_rt4_probe_analytic_sky_validator_is_exact(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre-rt4-probe-analytic-sky-"
        ) as temp:
            root = Path(temp)
            self.write_baseline(root)
            report = json.loads(
                (root / VERIFY.RT4_REPORT_ARTIFACT).read_text(encoding="utf-8")
            )
            sky_evidence = root / VERIFY.RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT
            sky_ppm = root / VERIFY.RT4_ANALYTIC_SKY_PPM_ARTIFACT
            RUNNER.validate_analytic_sky_report(
                report, sky_evidence, sky_ppm
            )
            for mutation in (
                lambda value: value["analytic_sky"].__setitem__(
                    "capture_policy_version", True
                ),
                lambda value: value["analytic_sky"]["descriptor"].__setitem__(
                    "sun_disk_radiance", [0.0, 0.0, 0.0]
                ),
                lambda value: value["analytic_sky"]["runtime_audit"].__setitem__(
                    "native_mesh_creates", 32.0
                ),
            ):
                tampered = copy.deepcopy(report)
                mutation(tampered)
                with self.assertRaisesRegex(
                    RUNNER.ProbeError,
                    "analytic-sky controls failed closed",
                ):
                    RUNNER.validate_analytic_sky_report(
                        tampered, sky_evidence, sky_ppm
                    )

            tampered_report = copy.deepcopy(report)
            payload = bytearray(sky_evidence.read_bytes())
            attachment_bytes = 768 * 512 * 8
            sun_pixel = (768 * 512) // 2 + 768 // 2
            alpha_offset = attachment_bytes + sun_pixel * 8 + 6
            payload[alpha_offset : alpha_offset + 2] = b"\x00\x40"
            sky_evidence.write_bytes(payload)
            tampered_report["analytic_sky"]["visual_proof"][
                "sun_hdr_fnv1a64"
            ] = RUNNER._fnv1a64(bytes(payload[attachment_bytes:]))
            with self.assertRaisesRegex(
                RUNNER.ProbeError, "analytic-sky visual proof failed closed"
            ):
                RUNNER.validate_analytic_sky_report(
                    tampered_report, sky_evidence, sky_ppm
                )

    def test_rt4_analytic_sky_hdr_alpha_is_recomputed_after_reattestation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre-rt4-analytic-sky-alpha-"
        ) as temp:
            root = Path(temp)
            self.write_baseline(root)
            attachment_bytes = 768 * 512 * 8
            sun_pixel = (768 * 512) // 2 + 768 // 2
            alpha_offset = attachment_bytes + sun_pixel * 8 + 6
            evidence_paths = (
                root / VERIFY.RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT,
                root / VERIFY.RT4_REPEAT_ANALYTIC_SKY_EVIDENCE_ARTIFACT,
            )
            payload = bytearray(evidence_paths[0].read_bytes())
            payload[alpha_offset : alpha_offset + 2] = b"\x00\x40"
            for evidence_path in evidence_paths:
                evidence_path.write_bytes(payload)
            sun_hash = VERIFY._fnv1a64(bytes(payload[attachment_bytes:]))
            for report_relative in (
                VERIFY.RT4_REPORT_ARTIFACT,
                VERIFY.RT4_REPEAT_REPORT_ARTIFACT,
            ):
                report_path = root / report_relative
                report = json.loads(report_path.read_text(encoding="utf-8"))
                report["analytic_sky"]["visual_proof"][
                    "sun_hdr_fnv1a64"
                ] = sun_hash
                report_path.write_text(
                    json.dumps(report) + "\n", encoding="utf-8"
                )
            self.refresh_rt4_attestation(
                root,
                (
                    "report",
                    "repeat_report",
                    "analytic_sky_evidence",
                    "repeat_analytic_sky_evidence",
                ),
                refresh_analytic_sky_slices=True,
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "RT4 analytic-sky visual proof failed",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_dynamic_mesh_is_fail_closed_after_reattestation(self) -> None:
        mutations = (
            lambda report: report["dynamic_meshes"].__setitem__(
                "changed_pixels", True
            ),
            lambda report: report["dynamic_meshes"].__setitem__(
                "deformed_attachment_fnv1a64",
                report["dynamic_meshes"]["base_attachment_fnv1a64"],
            ),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-rt4-dynamic-mesh-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    report_path = root / VERIFY.RT4_REPORT_ARTIFACT
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    mutation(report)
                    report_path.write_text(
                        json.dumps(report) + "\n", encoding="utf-8"
                    )
                    self.refresh_rt4_attestation(root, ("report",))
                    with self.assertRaisesRegex(
                        VERIFY.ArtifactSetError,
                        "RT4 dynamic-mesh controls failed",
                    ):
                        VERIFY.verify_artifact_set(root)

    def test_rt4_retirement_and_rollback_reject_numeric_type_aliases(self) -> None:
        mutations = (
            lambda report: report["texture_retirement"]["audits"]["initial"].__setitem__(
                "creates", True
            ),
            lambda report: report["texture_retirement"]["transitions"][0].__setitem__(
                "width", 2.0
            ),
            lambda report: report["texture_upload_rollback"]["stages"][0][
                "audits"
            ]["after_failure"].__setitem__("destroys", True),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-rt4-type-alias-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    report_path = root / VERIFY.RT4_REPORT_ARTIFACT
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    mutation(report)
                    report_path.write_text(
                        json.dumps(report) + "\n", encoding="utf-8"
                    )
                    self.refresh_rt4_attestation(root, ("report",))
                    with self.assertRaisesRegex(
                        VERIFY.ArtifactSetError,
                        "RT4 PPM/isolation report mismatch",
                    ):
                        VERIFY.verify_artifact_set(root)

    def test_rt4_hdr_compositor_evidence_is_fail_closed(self) -> None:
        mutations = (
            lambda report: report["hdr_compositor"].__setitem__(
                "committed_frames", True
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "initial_inverse_luminance_r16_bits", 0
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "final_attachment_fnv1a64",
                report["hdr_compositor"]["first_attachment_fnv1a64"],
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "final_inverse_luminance_r16_bits",
                report["hdr_compositor"][
                    "initial_inverse_luminance_r16_bits"
                ],
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "history_absolute_error",
                report["hdr_compositor"]["history_allowed_error"] + 1.0,
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "history_storage_ulp",
                report["hdr_compositor"]["history_storage_ulp"] * 2.0,
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "history_r16_ulp_distance", 2
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "ui_overlay_control_magenta_pixels", 0
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "initialization_failure_stages_verified", 9
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "frame_commit_prepare_failure_verified", False
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "aborted_hdr_audit_unchanged", False
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "aborted_reflection_audit_unchanged", False
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "aborted_submission_uncommitted", False
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "aborted_output_unchanged", False
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "post_render_failure_fault_latched", False
            ),
            lambda report: report["hdr_compositor"]["split_lighting"].__setitem__(
                "scene_evaluations", 2
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "lighting_production_content_readbacks", 1
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "ogre14_lighting_passes", 1
            ),
            lambda report: report["hdr_compositor"].__setitem__(
                "resize_rebuild_verified", False
            ),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(mutation=index):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-rt4-hdr-compositor-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    report_path = root / VERIFY.RT4_REPORT_ARTIFACT
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    mutation(report)
                    report_path.write_text(
                        json.dumps(report) + "\n", encoding="utf-8"
                    )
                    self.refresh_rt4_attestation(root, ("report",))
                    with self.assertRaisesRegex(
                        VERIFY.ArtifactSetError,
                        "RT4 HDR compositor evidence failed",
                    ):
                        VERIFY.verify_artifact_set(root)

        with tempfile.TemporaryDirectory(
            prefix="ror-ogre-rt4-hdr-atomic-field-"
        ) as temp:
            root = Path(temp)
            self.write_baseline(root)
            report_path = root / VERIFY.RT4_REPORT_ARTIFACT
            report = json.loads(report_path.read_text(encoding="utf-8"))
            report["hdr_compositor"].pop(
                "frame_commit_prepare_failure_verified"
            )
            report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
            self.refresh_rt4_attestation(root, ("report",))
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "RT4 HDR compositor report fields are incomplete or unexpected",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_hdr_split_bytes_are_independently_recomputed(self) -> None:
        mutations = (
            ("directional radiance oracle", "sun_direct_hdr", 0, b"\x00\x00"),
            ("non-finite radiance", "base_hdr", 0, b"\x00\x7c"),
            ("negative-zero radiance", "sun_direct_hdr", 0, b"\x00\x80"),
            ("alpha.*mismatch", "sun_direct_hdr", 6, b"\x00\x80"),
        )
        for expected, attachment_name, relative_offset, replacement in mutations:
            with self.subTest(expected=expected):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-rt4-hdr-split-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    report_path = root / VERIFY.RT4_REPORT_ARTIFACT
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    visual = report["hdr_compositor_visual"]
                    entry = next(
                        item
                        for item in visual["linear_split_attachments"]
                        if item["name"] == attachment_name
                    )
                    compositor_path = root / VERIFY.RT4_COMPOSITOR_ARTIFACT
                    payload = bytearray(compositor_path.read_bytes())
                    offset = entry["offset"] + relative_offset
                    payload[offset : offset + len(replacement)] = replacement
                    compositor_path.write_bytes(payload)
                    block = bytes(
                        payload[entry["offset"] : entry["offset"] + entry["bytes"]]
                    )
                    exact_hash = VERIFY._fnv1a64(block)
                    entry["exact_fnv1a64"] = exact_hash
                    hash_field = {
                        "base_hdr": "base_fnv1a64",
                        "sun_direct_hdr": "sun_direct_fnv1a64",
                    }[attachment_name]
                    report["hdr_compositor"]["split_content"][hash_field] = exact_hash
                    report_path.write_text(
                        json.dumps(report) + "\n", encoding="utf-8"
                    )
                    ppm = (root / VERIFY.RT4_PPM_ARTIFACT).read_bytes()
                    ppm_pixels = ppm[len(b"P6\n192 128\n255\n") :]
                    with self.assertRaisesRegex(
                        VERIFY.ArtifactSetError, expected
                    ):
                        VERIFY._verify_hdr_compositor_visual(
                            report, ppm_pixels, compositor_path
                        )
                    with self.assertRaisesRegex(RUNNER.ProbeError, expected):
                        RUNNER.validate_hdr_compositor_visual_evidence(
                            report, compositor_path, ppm_pixels
                        )

    def test_rt4_hdr_split_rejects_co_mutated_negative_radiance(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre-rt4-hdr-negative-"
        ) as temp:
            root = Path(temp)
            self.write_baseline(root)
            report_path = root / VERIFY.RT4_REPORT_ARTIFACT
            report = json.loads(report_path.read_text(encoding="utf-8"))
            visual = report["hdr_compositor_visual"]
            compositor_path = root / VERIFY.RT4_COMPOSITOR_ARTIFACT
            payload = bytearray(compositor_path.read_bytes())
            replacements = {
                "base_hdr": struct.pack("<e", -1.0),
                "sun_full_unoccluded_hdr": struct.pack("<e", -0.5),
                "sun_direct_hdr": struct.pack("<e", 0.5),
                "raster_lit_hdr": struct.pack("<e", -0.5),
            }
            hash_fields = {
                "base_hdr": "base_fnv1a64",
                "sun_full_unoccluded_hdr": "sun_full_fnv1a64",
                "sun_direct_hdr": "sun_direct_fnv1a64",
                "raster_lit_hdr": "raster_lit_fnv1a64",
            }
            for entry in visual["linear_split_attachments"]:
                replacement = replacements[entry["name"]]
                offset = entry["offset"]
                payload[offset : offset + 2] = replacement
                block = bytes(payload[offset : offset + entry["bytes"]])
                exact_hash = VERIFY._fnv1a64(block)
                entry["exact_fnv1a64"] = exact_hash
                report["hdr_compositor"]["split_content"][
                    hash_fields[entry["name"]]
                ] = exact_hash
            compositor_path.write_bytes(payload)
            report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
            ppm = (root / VERIFY.RT4_PPM_ARTIFACT).read_bytes()
            ppm_pixels = ppm[len(b"P6\n192 128\n255\n") :]
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "negative or non-finite"
            ):
                VERIFY._verify_hdr_compositor_visual(
                    report, ppm_pixels, compositor_path
                )
            with self.assertRaisesRegex(
                RUNNER.ProbeError, "negative or non-finite"
            ):
                RUNNER.validate_hdr_compositor_visual_evidence(
                    report, compositor_path, ppm_pixels
                )

    def test_rt4_history_oracle_rejects_co_mutated_tolerance_claims(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre-rt4-history-oracle-"
        ) as temp:
            root = Path(temp)
            self.write_baseline(root)
            report_path = root / VERIFY.RT4_REPORT_ARTIFACT
            report = json.loads(report_path.read_text(encoding="utf-8"))
            compositor = report["hdr_compositor"]
            forged_bits = (
                compositor["reference_inverse_luminance_r16_bits"] + 2999
            )
            forged_history = VERIFY._decode_positive_r16(forged_bits)
            reference_history = VERIFY._decode_positive_r16(
                compositor["reference_inverse_luminance_r16_bits"]
            )
            self.assertIsNotNone(forged_history)
            self.assertIsNotNone(reference_history)
            forged_error = abs(forged_history - reference_history)
            compositor["final_inverse_luminance_r16_bits"] = forged_bits
            compositor["history_absolute_error"] = forged_error
            compositor["history_r16_ulp_distance"] = 2999
            compositor["history_conditioning_bound"] = forged_error
            compositor["history_binary32_rounding_bound"] = 0.0
            compositor["history_allowed_error"] = (
                forged_error + compositor["history_storage_ulp"]
            )
            report_path.write_text(
                json.dumps(report) + "\n", encoding="utf-8"
            )
            self.refresh_rt4_attestation(root, ("report",))
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "RT4 HDR compositor evidence failed",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_compositor_bytes_are_independently_recomputed(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre-rt4-compositor-bytes-"
        ) as temp:
            root = Path(temp)
            self.write_baseline(root)
            compositor_path = root / VERIFY.RT4_COMPOSITOR_ARTIFACT
            payload = bytearray(compositor_path.read_bytes())
            payload[0] ^= 0x01
            compositor_path.write_bytes(payload)
            self.refresh_rt4_attestation(
                root,
                ("compositor",),
                refresh_compositor_slices=True,
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "RT4 HDR compositor first_ui_free attachment mismatch",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_repeat_requires_exact_canonical_bytes(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre-rt4-repeat-bytes-"
        ) as temp:
            root = Path(temp)
            self.write_baseline(root)
            repeat_report = root / VERIFY.RT4_REPEAT_REPORT_ARTIFACT
            repeat_report.write_bytes(repeat_report.read_bytes() + b" \n")
            self.refresh_rt4_attestation(root, ("repeat_report",))
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "RT4 deterministic repeat bytes differ",
            ):
                VERIFY.verify_artifact_set(root)

    def test_rt4_gate_rejects_explicit_source_anchor_mismatch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-rt4-anchor-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "expected RoR commit differs"
            ):
                VERIFY.verify_artifact_set(
                    root, expected_ror_commit="f" * 40
                )

    def test_metal_n2_gate_cross_checks_pass_attestation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n2-artifacts-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n2(root, "pass")
            manifest = VERIFY.verify_artifact_set(
                root, verify_metal_n2_evidence=True
            )
            self.assertIn(
                VERIFY.METAL_N2_PROBE_ARTIFACT,
                [entry["path"] for entry in manifest],
            )
            (root / VERIFY.METAL_N2_PROBE_ARTIFACT).write_bytes(b"tampered")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "probe attestation mismatch"
            ):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n2_evidence=True
                )

    def test_metal_n2_gate_accepts_skip_but_rejects_stale_probe(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n2-skip-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n2(root, "skip")
            VERIFY.verify_artifact_set(root, verify_metal_n2_evidence=True)
            (root / VERIFY.METAL_N2_PROBE_ARTIFACT).write_bytes(b"stale")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "stale probe"
            ):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n2_evidence=True
                )

    def test_metal_n3_gate_cross_checks_all_pass_images(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n3-artifacts-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n3(root, "pass")
            manifest = VERIFY.verify_artifact_set(
                root, verify_metal_n3_evidence=True
            )
            image_names = [name for _, name in VERIFY.METAL_N3_IMAGE_ARTIFACTS]
            for name in image_names:
                self.assertIn(name, [entry["path"] for entry in manifest])
            (root / image_names[1]).write_bytes(b"tampered")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "attestation mismatch"
            ):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n3_evidence=True
                )

    def test_metal_n3_gate_requires_v2_image_contract(self) -> None:
        for value in (None, 1, True, 2.0):
            with self.subTest(image_version=value):
                def mutate(report, replacement=value):
                    if replacement is None:
                        report["contract"].pop("image_version")
                    else:
                        report["contract"]["image_version"] = replacement

                self.assert_metal_n3_report_rejected(mutate, "image_contract")

    def test_metal_n3_gate_requires_simultaneous_rt4_layout_contract(self) -> None:
        mutations = (
            ("raster_feature_tier", "STATIC_PBR_N1"),
            ("native_feature_tier", "METAL_RAY_TRACING_N2"),
            ("vertex_layout", "POSITION_NORMAL_FLOAT32_24"),
            ("vertex_stride_bytes", 24),
            ("vertex_stride_bytes", True),
            ("authored_tangent_uv0", False),
            ("base_color_texture", "RGBA8_UNORM"),
            ("directional_light_lux", 0),
            ("directional_light_lux", True),
            ("ray_material_parity_claimed", True),
        )
        for field, value in mutations:
            with self.subTest(field=field, value=value):
                def mutate(report, name=field, replacement=value):
                    report["raster_contract"][name] = replacement

                self.assert_metal_n3_report_rejected(
                    mutate, "simultaneous_raster_contract"
                )

    def test_metal_n3_gate_requires_exact_texture_allocation_audit(self) -> None:
        paths = (
            ("live", "version", 1),
            ("live", "source_textures", 0),
            ("live", "sampled_rgba", 0),
            ("live", "linear_rgba", 1),
            ("live", "normal_rg8", 1),
            ("live", "exact_usage", False),
            ("after_shutdown", "version", 1),
            ("after_shutdown", "destroys", 0),
            ("after_shutdown", "live", 1),
            ("after_shutdown", "retired_name_rejections", 0),
        )
        for phase, field, value in paths:
            with self.subTest(phase=phase, field=field):
                def mutate(
                    report,
                    allocation_phase=phase,
                    name=field,
                    replacement=value,
                ):
                    report["raster_contract"]["texture_allocations"][
                        allocation_phase
                    ][name] = replacement

                self.assert_metal_n3_report_rejected(
                    mutate, "texture_allocation_contract"
                )

    def test_metal_n3_gate_rejects_boolean_allocation_count_aliases(self) -> None:
        paths = (
            ("live", "version", True),
            ("live", "source_textures", True),
            ("live", "sampled_rgba", True),
            ("live", "linear_rgba", False),
            ("live", "roughness_r8", False),
            ("live", "metallic_r8", False),
            ("live", "normal_rg8", False),
            ("live", "creates", True),
            ("live", "destroys", False),
            ("live", "live", True),
            ("after_shutdown", "version", True),
            ("after_shutdown", "creates", True),
            ("after_shutdown", "destroys", True),
            ("after_shutdown", "live", False),
            ("after_shutdown", "retired_name_lookups", True),
            ("after_shutdown", "retired_name_rejections", True),
        )
        for phase, field, value in paths:
            with self.subTest(phase=phase, field=field):
                def mutate(
                    report,
                    allocation_phase=phase,
                    name=field,
                    replacement=value,
                ):
                    report["raster_contract"]["texture_allocations"][
                        allocation_phase
                    ][name] = replacement

                self.assert_metal_n3_report_rejected(
                    mutate, "texture_allocation_contract"
                )

    def test_metal_n3_gate_requires_exact_build_contract_json_types(self) -> None:
        mutations = (
            ("schema", lambda contract: contract.__setitem__("schema_version", 5.0)),
            (
                "ror_commit",
                lambda contract: contract["ror_source"].__setitem__(
                    "commit", int("1" * 40)
                ),
            ),
            (
                "ogre_commit",
                lambda contract: contract["provenance"].__setitem__(
                    "commit", int("2" * 40)
                ),
            ),
        )
        for label, mutation in mutations:
            with self.subTest(label=label):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre-n3-contract-type-"
                ) as temp:
                    root = Path(temp)
                    self.write_baseline(root)
                    self.write_metal_n3(root, "pass")
                    contract_path = root / VERIFY.REQUIRED_ARTIFACTS[0]
                    contract = json.loads(
                        contract_path.read_text(encoding="utf-8")
                    )
                    mutation(contract)
                    contract_path.write_text(
                        json.dumps(contract) + "\n", encoding="utf-8"
                    )
                    with self.assertRaisesRegex(
                        VERIFY.ArtifactSetError, "source identity is invalid"
                    ):
                        VERIFY.verify_artifact_set(
                            root, verify_metal_n3_evidence=True
                        )

    def test_metal_n3_gate_cross_checks_build_contract_provenance(self) -> None:
        for field, value in (
            ("ror_repository", "https://example.invalid/forged"),
            ("ror_repository", None),
            ("ogre_next_commit", "9" * 40),
            ("ogre_next_commit", None),
        ):
            with self.subTest(field=field, value=value):
                def mutate(report, name=field, replacement=value):
                    if replacement is None:
                        report["provenance"].pop(name)
                    else:
                        report["provenance"][name] = replacement

                self.assert_metal_n3_report_rejected(
                    mutate, "build-contract provenance mismatch"
                )

    def test_metal_n3_gate_requires_new_identity_and_far_proofs(self) -> None:
        fields = (
            "camera_mismatch_rejected",
            "snapshot_transform_mismatch_rejected",
            "off_axis_far_plane_hit_passed",
        )
        for field in fields:
            for value in (None, False):
                with self.subTest(field=field, value=value):
                    def mutate(report, proof_field=field, replacement=value):
                        if replacement is None:
                            report["proof"].pop(proof_field)
                        else:
                            report["proof"][proof_field] = replacement

                    self.assert_metal_n3_report_rejected(
                        mutate, "required_proofs"
                    )

    def test_metal_n3_gate_requires_non_boolean_far_pixel_count(self) -> None:
        for value in (None, 0, True):
            with self.subTest(far_pixels=value):
                def mutate(report, replacement=value):
                    if replacement is None:
                        report["proof"].pop(
                            "off_axis_far_plane_contribution_pixels"
                        )
                    else:
                        report["proof"][
                            "off_axis_far_plane_contribution_pixels"
                        ] = replacement

                self.assert_metal_n3_report_rejected(mutate, "far_plane_count")

    def test_metal_n3_gate_requires_followup_view_and_resize_evidence(self) -> None:
        for record in ("second_view_contribution", "resized_hybrid"):
            for field, value in (("sha256", "invalid"), ("width", 0)):
                with self.subTest(record=record, field=field):
                    def mutate(
                        report,
                        record_name=record,
                        metric=field,
                        replacement=value,
                    ):
                        report[record_name][metric] = replacement

                    self.assert_metal_n3_report_rejected(
                        mutate,
                        "second_view" if record == "second_view_contribution" else "resize",
                    )

    def test_metal_n3_gate_recomputes_hybrid_contribution_mapping(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n3-mapping-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n3(root, "pass")
            raster_path = root / VERIFY.METAL_N3_IMAGE_ARTIFACTS[0][1]
            hybrid_path = root / VERIFY.METAL_N3_IMAGE_ARTIFACTS[2][1]
            raster = raster_path.read_bytes()
            hybrid_path.write_bytes(raster)
            report_path = root / VERIFY.METAL_N3_REQUIRED_ARTIFACTS[0]
            report = json.loads(report_path.read_text(encoding="utf-8"))
            report["hybrid_hdr"] = VERIFY._metal_n3_image_metrics(raster)
            report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
            self.update_metal_n3_attestation(root)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "contribution did not change hybrid RGB",
            ):
                VERIFY.verify_artifact_set(root, verify_metal_n3_evidence=True)

    def test_metal_n3_gate_accepts_skip_but_rejects_stale_image(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n3-skip-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n3(root, "skip")
            VERIFY.verify_artifact_set(root, verify_metal_n3_evidence=True)
            stale = root / VERIFY.METAL_N3_IMAGE_ARTIFACTS[0][1]
            stale.write_bytes(b"stale")
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "stale"):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n3_evidence=True
                )

    def test_metal_n3_gate_rejects_skip_without_reason(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n3-skip-reason-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n3(root, "skip")
            self.mutate_metal_n3_report(root, lambda report: report.pop("reason"))
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "no reason"):
                VERIFY.verify_artifact_set(root, verify_metal_n3_evidence=True)

    def test_metal_n4_gate_recomputes_every_pass_artifact(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n4-artifacts-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n4(root, "pass")
            manifest = VERIFY.verify_artifact_set(
                root, verify_metal_n4_evidence=True
            )
            manifest_paths = {entry["path"] for entry in manifest}
            for _, name, _, _ in VERIFY.METAL_N4_IMAGE_ARTIFACTS:
                self.assertIn(name, manifest_paths)
            visibility_path = root / VERIFY.METAL_N4_IMAGE_ARTIFACTS[1][1]
            payload = bytearray(visibility_path.read_bytes())
            payload[0:2] = struct.pack("<H", 0x3800)
            visibility_path.write_bytes(payload)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "artifact metrics mismatch"
            ):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n4_evidence=True
                )

    def test_metal_n4_gate_rejects_forged_mapping_after_hash_refresh(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n4-mapping-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n4(root, "pass")
            hybrid_path = root / VERIFY.METAL_N4_IMAGE_ARTIFACTS[3][1]
            payload = bytearray(hybrid_path.read_bytes())
            occluded_offset = 100 * 8
            payload[occluded_offset : occluded_offset + 2] = struct.pack(
                "<H", 0x3400
            )
            hybrid_path.write_bytes(payload)

            def refresh_hash(report):
                report["artifacts"]["hybrid"]["sha256"] = (
                    VERIFY.sha256_file(hybrid_path)
                )
                report["samples"][1]["hybrid_rgba16_bits"] = (
                    VERIFY._metal_n4_rgba16_strings(payload, occluded_offset)
                )

            self.mutate_metal_n4_report(root, refresh_hash)
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "occluded pixel mapping"
            ):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n4_evidence=True
                )

    def test_metal_n4_gate_rejects_noncanonical_rgba16_after_hash_refresh(
        self,
    ) -> None:
        cases = (
            ("negative-zero", 0, 0x8000, "canonical finite nonnegative"),
            ("negative-rgb", 0, 0xBC00, "canonical finite nonnegative"),
            ("alpha-above-one", 3, 0x3C01, "alpha exceeds"),
        )
        for label, channel, bits, expected in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix=f"ror-ogre-n4-{label}-"
            ) as temp:
                root = Path(temp)
                self.write_baseline(root)
                self.write_metal_n4(root, "pass")
                # Pixel one is visible but intentionally absent from the two
                # report samples. Mutate both payloads coherently so this test
                # exercises the independent all-pixel semantic gate.
                pixel_offset = 8
                channel_offset = pixel_offset + channel * 2
                for key in ("raster", "hybrid"):
                    path = root / dict(
                        (artifact_key, name)
                        for artifact_key, name, _, _ in (
                            VERIFY.METAL_N4_IMAGE_ARTIFACTS
                        )
                    )[key]
                    payload = bytearray(path.read_bytes())
                    payload[channel_offset : channel_offset + 2] = struct.pack(
                        "<H", bits
                    )
                    path.write_bytes(payload)

                def refresh_hashes(report):
                    for key in ("raster", "hybrid"):
                        path = root / dict(
                            (artifact_key, name)
                            for artifact_key, name, _, _ in (
                                VERIFY.METAL_N4_IMAGE_ARTIFACTS
                            )
                        )[key]
                        report["artifacts"][key]["sha256"] = (
                            VERIFY.sha256_file(path)
                        )

                self.mutate_metal_n4_report(root, refresh_hashes)
                with self.assertRaisesRegex(VERIFY.ArtifactSetError, expected):
                    VERIFY.verify_artifact_set(
                        root, verify_metal_n4_evidence=True
                    )

    def test_metal_n4_gate_cross_checks_coverage_and_samples(self) -> None:
        self.assert_metal_n4_report_rejected(
            lambda report: report["coverage"].__setitem__(
                "occluded_pixels", 99
            ),
            "coverage differs",
        )
        self.assert_metal_n4_report_rejected(
            lambda report: report["samples"][1].__setitem__(
                "secondary_blocker_instance_id", 0
            ),
            "sample differs",
        )

    def test_metal_n4_gate_requires_exact_runtime_sequence(self) -> None:
        cases = (
            (
                lambda report: report["runtime_sequence"]["exact_repeat"].__setitem__(
                    "hybrid_sha256", "8" * 64
                ),
                "exact repeat differs",
            ),
            (
                lambda report: report["runtime_sequence"][
                    "moved_occluder"
                ].__setitem__(
                    "visibility_sha256",
                    report["artifacts"]["visibility"]["sha256"],
                ),
                "moved occluder",
            ),
            (
                lambda report: report["runtime_sequence"][
                    "resized_extent"
                ].__setitem__("width", 96),
                "coverage is invalid",
            ),
        )
        for mutation, expected in cases:
            with self.subTest(expected=expected):
                self.assert_metal_n4_report_rejected(mutation, expected)

    def test_metal_n4_gate_requires_every_explicit_proof(self) -> None:
        for field in VERIFY.METAL_N4_REQUIRED_PROOF_BOOLEANS:
            with self.subTest(field=field):
                self.assert_metal_n4_report_rejected(
                    lambda report, name=field: report["proof"].__setitem__(
                        name, False
                    ),
                    "proof is incomplete",
                )

    def test_metal_n4_gate_accepts_skip_but_rejects_stale_pass_data(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n4-skip-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n4(root, "skip")
            VERIFY.verify_artifact_set(root, verify_metal_n4_evidence=True)
            stale = root / VERIFY.METAL_N4_IMAGE_ARTIFACTS[0][1]
            stale.write_bytes(b"stale")
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "stale raster"):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n4_evidence=True
                )

    def test_metal_n4_gate_rejects_inexact_capability_skip(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n4-skip-bad-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n4(root, "skip")
            self.mutate_metal_n4_report(
                root,
                lambda report: report.__setitem__(
                    "required_apple_gpu_family", True
                ),
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "capability skip is not exact"
            ):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n4_evidence=True
                )


if __name__ == "__main__":
    unittest.main()
