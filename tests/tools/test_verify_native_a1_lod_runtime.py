#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPOSITORY_ROOT / "tools/verify_native_a1_lod_runtime.py"
PACKAGE_BYTES = b"synthetic-A1-package"
PACKAGE_SHA256 = hashlib.sha256(PACKAGE_BYTES).hexdigest()
COMMIT = "b1a5296cd59260622f9432a70f79c5703639c952"
LIGHTING_MODE = (
    "metal-rt-sun-visibility-v2" if sys.platform == "darwin" else "raster-hdr-pssm"
)
PIPELINE = (
    "rt4_pbr_hdr_metal_sun_visibility_v2"
    if sys.platform == "darwin"
    else "rt4_pbr_pssm_hdr_preview"
)
NATIVE_RT = "true" if sys.platform == "darwin" else "false"
EXPECTED_NATIVE_SCENE_DRAWS = 30 if sys.platform == "darwin" else 10


def runtime_log(package_sha256: str = PACKAGE_SHA256) -> str:
    backend = {
        "darwin": "ogre-next-metal",
        "linux": "ogre-next-vulkan",
        "win32": "ogre-next-d3d11",
    }[sys.platform]
    native_lighting_topology = (
        "pbs=9 casters=0 receivers=0 hdr_topology=0 pssm=false "
        "pssm_populated_finalize=false"
        if sys.platform == "darwin"
        else "pbs=9 casters=4 receivers=7 hdr_topology=1 pssm=true "
        "pssm_populated_finalize=true"
    )
    lines = [
            "[RoR|RendererCombined|Startup] presentation_owner=ogre-next "
            f"visible_window=true legacy_visible_fallback=false backend={backend}",
            "[RoR|RendererCombined|Startup] resource_host=ogre14 "
            "visible_window=false protected=true",
            "[RoR|RendererCombined|NativeShowcase] Selected exact forward-native "
            "scene: path='__PACKAGE_PATH__', package='rorng_a1_native_course_60m', "
            f"sha256='{package_sha256}', assets=38, instances=9, source_version=1, "
            f"pipeline='{PIPELINE}', hdr=true, native_rt={NATIVE_RT}, "
            "profile=a1_native_course, motion='turntable_thin_glass_slab', "
            "fixed_hz=60, revolution_ticks=360, "
            "refraction=thin_parallel_slab_screen_space, motion_vectors=false",
            "[RoR|RendererCombined|NativeLighting] schema_version=6 available=true "
            f"{native_lighting_topology} "
            "lod_items=2 lod_reduced=2 lod_max=1 "
            "lod_level_sum=2 triangles_base=588 triangles_selected=252 "
            "lod_exact=true reflection_initialized=true native_scene_lighting=true "
            "gpu_only=true no_ogre14_lighting=true "
            "completed_frames=12",
    ]
    if sys.platform == "darwin":
        for frame in range(1, 16):
            lines.append(
                "[RoR|RendererCombined|MetalRT|SunVisibilityV2] "
                f"schema_version=2 frame={frame} snapshot={frame} view=1 plan=1 "
                "selected=9 admitted=7 excluded=2 receivers=7 casters=4 "
                "unique_meshes=7 "
                f"blas_build={7 if frame == 1 else 0} "
                f"blas_hit={0 if frame == 1 else 7} blas_refit=0 "
                f"tlas_build={1 if frame == 1 else 0} "
                f"tlas_hit={0 if frame == 1 else 1} tlas_refit=0 "
                "primary_rays=100 sun_rays=10 visible_texels=90 "
                "occluded_texels=10 gpu_ns=1 supports_rt=true "
                "apple_family9=true same_ogre_device=true same_ogre_queue=true "
                "same_ogre_timeline=true shader_lock=true sun_direct_only=true "
                "completed=true cpu_content_readbacks=0 gpu_content_readbacks=0 "
                f"completed_frames={frame}"
            )
    lines.append("")
    return "\n".join(lines)


class NativeA1LodRuntimeReceiptTests(unittest.TestCase):
    def run_tool(
        self,
        log_text: str,
        *,
        selected: int = 252,
        package_bytes: bytes = PACKAGE_BYTES,
        lighting_mode: str = LIGHTING_MODE,
        stderr_text: str = "",
        frame_receipt_overrides: dict[str, object] | None = None,
    ) -> tuple[
        subprocess.CompletedProcess[str],
        Path,
        tempfile.TemporaryDirectory[str],
    ]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        log = root / "showcase.log"
        stderr_log = root / "showcase.stderr.log"
        frame_receipt = root / "frame-budget.json"
        package = root / "a1.rornative"
        executable = root / "RoR-Combined"
        output = root / "receipt.json"
        package.write_bytes(package_bytes)
        executable.write_bytes(b"synthetic-executable")
        log.write_text(
            log_text.replace("__PACKAGE_PATH__", str(package)),
            encoding="utf-8",
        )
        stderr_log.write_text(stderr_text, encoding="utf-8")
        frame_receipt_document: dict[str, object] = {
            "accepted_frames": 12,
            "format": "ror-frame-time-budget-v1",
            "mode": "measure",
            "verdict": "advisory",
            "passed": False,
            "minimum_frames": 8,
            "observed_frames": 14,
            "warmup_frames": 2,
            "warmup_frames_requested": 2,
            "native_scene_draw_exact_samples": 12,
            "native_scene_draw_rejected_samples": 0,
            "native_scene_draw_p99": EXPECTED_NATIVE_SCENE_DRAWS,
            "native_scene_draw_maximum": EXPECTED_NATIVE_SCENE_DRAWS,
            "native_scene_draw_p99_limit": 2500,
            "presents_frames": True,
            "rejected_frames": 0,
            "renderer": "ogre-next-combined",
            "requested_frames": 12,
            "requires_native_scene_draw_metrics": True,
            "scenario_id": "ci.synthetic.a1-lod",
        }
        if frame_receipt_overrides is not None:
            frame_receipt_document.update(frame_receipt_overrides)
        frame_receipt.write_text(
            json.dumps(frame_receipt_document, sort_keys=True),
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                sys.executable,
                str(TOOL),
                "--log", str(log),
                "--stderr-log", str(stderr_log),
                "--frame-budget-receipt", str(frame_receipt),
                "--package", str(package),
                "--executable", str(executable),
                "--output", str(output),
                "--platform", sys.platform,
                "--expected-lighting-mode", lighting_mode,
                "--package-sha256", PACKAGE_SHA256,
                "--repository-commit", COMMIT,
                "--scenario-id", "ci.synthetic.a1-lod",
                "--accepted-frames", "12",
                "--expected-lod-items", "2",
                "--expected-lod-reduced", "2",
                "--expected-lod-max", "1",
                "--expected-lod-level-sum", "2",
                "--expected-triangles-base", "588",
                "--expected-triangles-selected", str(selected),
                "--expected-completed-frames", "12",
            ],
            cwd=REPOSITORY_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        return result, output, temporary

    def test_emits_bound_runtime_log_and_frame_receipt(self) -> None:
        result, output, temporary = self.run_tool(runtime_log())
        self.addCleanup(temporary.cleanup)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        receipt = json.loads(output.read_text(encoding="ascii"))
        self.assertEqual(receipt["schema"], "ror.native_a1_distance_lod_runtime.v1")
        self.assertEqual(receipt["package"]["sha256"], PACKAGE_SHA256)
        self.assertEqual(receipt["presentation_ownership"]["presentation_owner"], "ogre-next")
        self.assertFalse(receipt["presentation_ownership"]["legacy_visible_fallback"])
        self.assertEqual(receipt["native_distance_lod"]["triangles_selected"], 252)
        self.assertEqual(receipt["lighting_mode"]["id"], LIGHTING_MODE)
        self.assertEqual(receipt["lighting_mode"]["pipeline"], PIPELINE)
        self.assertEqual(
            receipt["stderr_log_sha256"], hashlib.sha256(b"").hexdigest()
        )
        expected_topology = 0 if sys.platform == "darwin" else 1
        self.assertEqual(
            receipt["native_distance_lod"]["hdr_topology"],
            expected_topology,
        )
        self.assertEqual(
            receipt["evidence_class"],
            "runtime-input-and-receipt-verification",
        )

    def test_wrong_expected_selection_fails_without_output(self) -> None:
        result, output, temporary = self.run_tool(runtime_log(), selected=204)
        self.addCleanup(temporary.cleanup)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())

    def test_substituted_package_identity_fails_without_output(self) -> None:
        result, output, temporary = self.run_tool(runtime_log("0" * 64))
        self.addCleanup(temporary.cleanup)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())

    def test_substituted_staged_package_bytes_fail_without_output(self) -> None:
        result, output, temporary = self.run_tool(
            runtime_log(), package_bytes=b"substituted-package"
        )
        self.addCleanup(temporary.cleanup)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())

    def test_duplicate_selected_or_native_fields_fail_without_output(self) -> None:
        mutations = (
            runtime_log().replace(
                f"sha256='{PACKAGE_SHA256}'",
                f"sha256='{'0' * 64}', sha256='{PACKAGE_SHA256}'",
            ),
            runtime_log().replace("lod_items=2", "lod_items=0 lod_items=2"),
        )
        for log_text in mutations:
            with self.subTest(log_text=log_text[-300:]):
                result, output, temporary = self.run_tool(log_text)
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_shadow_or_malformed_selected_fields_fail_without_output(self) -> None:
        mutations = (
            runtime_log().replace(
                f"sha256='{PACKAGE_SHA256}'",
                f"sha256 = '{'0' * 64}', sha256='{PACKAGE_SHA256}'",
            ),
            runtime_log().replace(
                f"sha256='{PACKAGE_SHA256}'",
                f"SHA256='{'0' * 64}', sha256='{PACKAGE_SHA256}'",
            ),
        )
        for log_text in mutations:
            with self.subTest(log_text=log_text[-300:]):
                result, output, temporary = self.run_tool(log_text)
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_pipeline_and_native_rt_are_one_closed_mode_pair(self) -> None:
        wrong_pipeline = (
            "rt4_pbr_pssm_hdr_preview"
            if sys.platform == "darwin"
            else "rt4_pbr_hdr_metal_sun_visibility_v2"
        )
        mutations = (
            runtime_log().replace(f"pipeline='{PIPELINE}'", f"pipeline='{wrong_pipeline}'"),
            runtime_log().replace(f"native_rt={NATIVE_RT}", f"native_rt={'false' if NATIVE_RT == 'true' else 'true'}"),
        )
        for log_text in mutations:
            with self.subTest(log_text=log_text[:300]):
                result, output, temporary = self.run_tool(log_text)
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_mode_platform_mismatch_fails_without_output(self) -> None:
        wrong_mode = (
            "raster-hdr-pssm"
            if LIGHTING_MODE == "metal-rt-sun-visibility-v2"
            else "metal-rt-sun-visibility-v2"
        )
        result, output, temporary = self.run_tool(
            runtime_log(), lighting_mode=wrong_mode
        )
        self.addCleanup(temporary.cleanup)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())

    @unittest.skipUnless(sys.platform == "darwin", "Metal receipt is Darwin-only")
    def test_metal_receipt_gap_or_readback_fails_without_output(self) -> None:
        mutations = (
            runtime_log().replace("schema_version=2 frame=8 ", "schema_version=2 frame=18 "),
            runtime_log().replace("gpu_content_readbacks=0 completed_frames=15", "gpu_content_readbacks=1 completed_frames=15"),
            runtime_log().replace(
                "completed=true cpu_content_readbacks=0",
                "unexpected=1 completed=true cpu_content_readbacks=0",
                1,
            ),
        )
        for log_text in mutations:
            with self.subTest(log_text=log_text[-500:]):
                result, output, temporary = self.run_tool(log_text)
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_runtime_refusal_fails_without_output(self) -> None:
        result, output, temporary = self.run_tool(
            runtime_log()
            + "[RoR|Perf] Refusing frame budget: no exact completed native scene\n"
        )
        self.addCleanup(temporary.cleanup)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())

    def test_stderr_is_bound_and_cannot_carry_failure_or_receipt_markers(
        self,
    ) -> None:
        mutations = (
            "OGRE EXCEPTION synthetic failure\n",
            "[RoR|Perf] Refusing frame budget: synthetic refusal\n",
            "[RoR|RendererCombined|NativeLighting] schema_version=6\n",
        )
        for stderr_text in mutations:
            with self.subTest(stderr_text=stderr_text):
                result, output, temporary = self.run_tool(
                    runtime_log(), stderr_text=stderr_text
                )
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_native_scene_draw_partition_is_exactly_pinned(self) -> None:
        for overrides in (
            {"native_scene_draw_p99": EXPECTED_NATIVE_SCENE_DRAWS - 1},
            {"native_scene_draw_maximum": EXPECTED_NATIVE_SCENE_DRAWS + 1},
            {"native_scene_draw_p99_limit": 2501},
        ):
            with self.subTest(overrides=overrides):
                result, output, temporary = self.run_tool(
                    runtime_log(), frame_receipt_overrides=overrides
                )
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_frame_receipt_boolean_fields_reject_integer_impostors(self) -> None:
        for overrides in (
            {"passed": 0},
            {"presents_frames": 1},
            {"requires_native_scene_draw_metrics": 1},
        ):
            with self.subTest(overrides=overrides):
                result, output, temporary = self.run_tool(
                    runtime_log(), frame_receipt_overrides=overrides
                )
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_native_lighting_topology_is_closed_by_platform(self) -> None:
        log_text = runtime_log()
        if sys.platform == "darwin":
            mutations = (
                log_text.replace("casters=0", "casters=4", 1),
                log_text.replace("receivers=0", "receivers=7", 1),
                log_text.replace("hdr_topology=0", "hdr_topology=1", 1),
                log_text.replace("pssm=false", "pssm=true", 1),
            )
        else:
            mutations = (
                log_text.replace("casters=4", "casters=0", 1),
                log_text.replace("receivers=7", "receivers=6", 1),
                log_text.replace("hdr_topology=1", "hdr_topology=0", 1),
                log_text.replace(
                    "pssm_populated_finalize=true",
                    "pssm_populated_finalize=false",
                    1,
                ),
            )
        for mutation in mutations:
            with self.subTest(mutation=mutation[:400]):
                result, output, temporary = self.run_tool(mutation)
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_selected_path_must_name_staged_package(self) -> None:
        result, output, temporary = self.run_tool(
            runtime_log().replace("__PACKAGE_PATH__", "/tmp/not-the-package")
        )
        self.addCleanup(temporary.cleanup)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
