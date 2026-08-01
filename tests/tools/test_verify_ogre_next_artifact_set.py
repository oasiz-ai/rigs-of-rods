#!/usr/bin/env python3
"""Unit tests for the exact OGRE-Next CI artifact-set gate."""

from __future__ import annotations

import importlib.util
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
    ror_repository = "https://github.com/oasiz-ai/rigs-of-rods"
    ror_ref = "codex/test-native-rt"
    ror_commit = "3" * 40
    ror_manifest = "4" * 64
    ogre_repository = "https://github.com/OGRECave/ogre-next"
    ogre_commit = "7" * 40
    ogre_archive = "8" * 64

    def write_baseline(self, root: Path) -> None:
        for name in VERIFY.REQUIRED_ARTIFACTS:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if name == VERIFY.REQUIRED_ARTIFACTS[0]:
                path.write_text(
                    json.dumps(
                        {
                            "schema_version": 2,
                            "ror_source": {
                                "repository": self.ror_repository,
                                "ref": self.ror_ref,
                                "commit": self.ror_commit,
                                "relevant_manifest_sha256": self.ror_manifest,
                            },
                            "provenance": {
                                "repository": self.ogre_repository,
                                "commit": self.ogre_commit,
                                "archive_sha256": self.ogre_archive,
                            },
                        }
                    )
                    + "\n",
                    encoding="utf-8",
                )
            else:
                path.write_bytes(b"baseline")

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
            for index, name in enumerate(VERIFY.REQUIRED_ARTIFACTS, start=1):
                (root / name).write_bytes(bytes((index,)))
            manifest = VERIFY.verify_artifact_set(root)
            self.assertEqual(
                [entry["path"] for entry in manifest],
                list(VERIFY.REQUIRED_ARTIFACTS),
            )
            missing = root / VERIFY.REQUIRED_ARTIFACTS[-1]
            missing.unlink()
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "missing"):
                VERIFY.verify_artifact_set(root)

    def test_rejects_empty_artifact(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-artifacts-") as temp:
            root = Path(temp)
            for name in VERIFY.REQUIRED_ARTIFACTS:
                (root / name).write_bytes(b"x")
            (root / VERIFY.REQUIRED_ARTIFACTS[0]).write_bytes(b"")
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "empty"):
                VERIFY.verify_artifact_set(root)

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
        for value in (None, 1, True):
            with self.subTest(image_version=value):
                def mutate(report, replacement=value):
                    if replacement is None:
                        report["contract"].pop("image_version")
                    else:
                        report["contract"]["image_version"] = replacement

                self.assert_metal_n3_report_rejected(mutate, "image_contract")

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
