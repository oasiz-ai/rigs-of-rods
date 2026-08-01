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
            "schema_version": 2,
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
            VERIFY.RT4_ATTESTATION_ARTIFACT,
        }
        for name in VERIFY.REQUIRED_ARTIFACTS[1:]:
            if name in rt4_names:
                continue
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"baseline")
        self.write_rt4(root, contract)

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

        isolation_path = root / VERIFY.RT4_ISOLATION_ARTIFACT
        isolation_path.write_bytes(evidence)
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
            "schema": "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v1",
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
                "evidence_bytes": len(evidence),
            },
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
                "executable": file_entry(executable_path),
            },
            "isolation_slices": slices,
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
                [entry["path"] for entry in manifest][:-1],
                list(VERIFY.REQUIRED_ARTIFACTS),
            )
            self.assertEqual(
                manifest[-1]["path"],
                "ror-ogre-next-n1-package/bin/ror_ogre_next_frontend_n1_smoke",
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
