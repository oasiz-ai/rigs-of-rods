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


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
SPEC = importlib.util.spec_from_file_location("verify_ogre_next_artifacts", SCRIPT_PATH)
assert SPEC and SPEC.loader
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


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

    def build_contract(self) -> dict[str, object]:
        rapidjson = self.lock["dependencies"]["rapidjson"]
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
            "schema_version": 3,
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
            "shader_media": copy.deepcopy(self.lock["shader_media"]),
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
                "compositor2_core": True,
                "json_materials": True,
                "mesh_lod": True,
                "dds_codec": True,
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
                "native_d32_atlas_allocation_use_readback_verified": True,
                "native_d32_atlas_cleanup_verified": True,
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
                "tight_caster_bounds_verified": True,
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
                "receiver_clone_same_frame_retry_verified": True,
                "workspace_node_same_frame_retry_verified": True,
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
        identity = VERIFY._expected_rt4_build_identity(contract, report)
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
                "version": 2,
                "successful_capture_count": 1,
                "failed_capture_count": 0,
                "live_probe_count": 1,
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
        ppm_pixels = bytes(
            channel
            for pixel_offset in range(0, len(baseline_sdr), 4)
            for channel in baseline_sdr[pixel_offset : pixel_offset + 3]
        )
        ppm_path = root / VERIFY.RT4_PPM_ARTIFACT
        ppm_path.write_bytes(b"P6\n192 128\n255\n" + ppm_pixels)
        colours = {
            ppm_pixels[pixel_offset : pixel_offset + 3]
            for pixel_offset in range(0, len(ppm_pixels), 3)
        }
        hdr_metrics = VERIFY._attachment_metrics(baseline_hdr, True)
        sdr_metrics = VERIFY._attachment_metrics(baseline_sdr, False)
        shader_media = contract["shader_media"]
        notice = shader_media["third_party_notice"]
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
                "shader_media_manifest_sha256": "c" * 64,
                "shader_media_manifest_file_count": 107,
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
            "texture_allocations": copy.deepcopy(
                VERIFY.RT4_EXPECTED_TEXTURE_ALLOCATIONS
            ),
            "texture_upload_rollback": copy.deepcopy(
                VERIFY.RT4_EXPECTED_TEXTURE_UPLOAD_ROLLBACK
            ),
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
                "schema": "ror.ogre_next_rt4_texture_isolation.v1",
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
                "manifest_sha256": "c" * 64,
                "manifest_file_count": 107,
            },
            "files": {
                "build_contract": file_entry(root / VERIFY.REQUIRED_ARTIFACTS[0]),
                "report": file_entry(report_path),
                "ppm": file_entry(ppm_path),
                "isolation": file_entry(isolation_path),
                "reflection": file_entry(reflection_path),
                "executable": file_entry(executable_path),
            },
            "isolation_slices": slices,
            "reflection_slices": reflection_slices,
        }
        (root / VERIFY.RT4_ATTESTATION_ARTIFACT).write_text(
            json.dumps(attestation) + "\n", encoding="utf-8"
        )

    def refresh_rt4_attestation(
        self,
        root: Path,
        file_keys: tuple[str, ...] = (),
        refresh_slices: bool = False,
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
            "schema": "ror.ogre_next_metal_rt_n3.v2",
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
                                "source_textures": 1,
                                "sampled_rgba": 1,
                                "roughness_r8": 0,
                                "metallic_r8": 0,
                                "creates": 1,
                                "destroys": 0,
                                "live": 1,
                                "exact_usage": True,
                            },
                            "after_shutdown": {
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

    def test_requires_every_exact_nonempty_regular_file(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-artifacts-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            manifest = VERIFY.verify_artifact_set(root)
            self.assertEqual(
                [entry["path"] for entry in manifest],
                [
                    *VERIFY.REQUIRED_ARTIFACTS,
                    "bin/ror_ogre_next_pssm_shadow_smoke",
                    VERIFY.PSSM_EVIDENCE_ARTIFACT,
                    VERIFY.PSSM_EXECUTION_RECEIPT_ARTIFACT,
                    VERIFY.PSSM_ATTESTATION_ARTIFACT,
                    VERIFY.PSSM_ARTIFACT_MANIFEST_ARTIFACT,
                    (
                        "ror-ogre-next-n1-package/bin/"
                        "ror_ogre_next_frontend_n1_smoke"
                    ),
                ],
            )
            missing = root / VERIFY.REQUIRED_ARTIFACTS[-1]
            missing.unlink()
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "missing"):
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
                    "d32_render_target_supported": True,
                    "d32_atlas_allocation_verified": True,
                    "d32_atlas_readback_verified": True,
                    "d32_atlas_cleanup_verified": True,
                },
                "backend_substitution": False,
            }
            report_path.write_text(
                json.dumps(unsupported) + "\n", encoding="utf-8"
            )
            (root / VERIFY.PSSM_EVIDENCE_ARTIFACT).unlink()
            self.refresh_pssm_integrity(root)
            VERIFY.verify_artifact_set(root)
            unsupported["capability_evidence"]["reason"] = "arbitrary skip"
            report_path.write_text(
                json.dumps(unsupported) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError,
                "unsupported capability evidence is not exact",
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
                VERIFY.ArtifactSetError, "RT4 PPM/isolation report mismatch"
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
            "texture_allocations",
            "texture_upload_rollback",
            "texture_retirement",
            "texture_isolation",
            "tangent_handedness",
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
            ("live", "source_textures", 0),
            ("live", "sampled_rgba", 0),
            ("live", "exact_usage", False),
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
            ("live", "source_textures", True),
            ("live", "sampled_rgba", True),
            ("live", "roughness_r8", False),
            ("live", "metallic_r8", False),
            ("live", "creates", True),
            ("live", "destroys", False),
            ("live", "live", True),
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
            ("schema", lambda contract: contract.__setitem__("schema_version", 2.0)),
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


if __name__ == "__main__":
    unittest.main()
