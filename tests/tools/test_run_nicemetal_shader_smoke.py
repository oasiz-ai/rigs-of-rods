#!/usr/bin/env python3
"""Focused tests for the packaged NiceMetal resource-host smoke driver."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import warnings
import zipfile


ROOT = Path(__file__).resolve().parents[2]
DRIVER_PATH = ROOT / "tools/run_nicemetal_shader_smoke.py"
SPEC = importlib.util.spec_from_file_location("run_nicemetal_shader_smoke", DRIVER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load NiceMetal smoke driver: {DRIVER_PATH}")
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


def complete_log(target_platform: str) -> str:
    policy = DRIVER.PLATFORM_POLICY[target_platform]
    lines = [
        f"RenderSystem Name: {policy['resource_host_render_system']}",
        (
            f"{DRIVER.PRESENTATION_OWNER_PREFIX} "
            f"backend={policy['visible_render_system']}"
        ),
        DRIVER.RESOURCE_HOST_RECEIPT,
        DRIVER.SPAWN_MARKER,
    ]
    lines.extend(
        (
            f"{DRIVER.PLACEHOLDER_PREFIX} '{material}' "
            f"in group '{DRIVER.RESOURCE_GROUP}'"
        )
        for material in DRIVER.MATERIAL_TEMPLATES
    )
    return "\n".join(lines) + "\n"


class DeterministicFixtureTests(unittest.TestCase):
    def test_fixture_archive_is_exact_and_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "first" / DRIVER.ARCHIVE_NAME
            second = root / "second" / DRIVER.ARCHIVE_NAME
            first_receipt = DRIVER.create_deterministic_fixture_archive(
                DRIVER.DEFAULT_FIXTURE_DIR, first
            )
            second_receipt = DRIVER.create_deterministic_fixture_archive(
                DRIVER.DEFAULT_FIXTURE_DIR, second
            )
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(
                first_receipt["archive_sha256"], DRIVER.ARCHIVE_SHA256
            )
            self.assertEqual(first_receipt, second_receipt)
            self.assertEqual(
                [entry["path"] for entry in first_receipt["members"]],
                sorted(DRIVER.FIXTURE_SHA256),
            )

    def test_fixture_rejects_changed_or_extra_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = Path(temporary) / "fixture"
            fixture.mkdir()
            for source in DRIVER.DEFAULT_FIXTURE_DIR.iterdir():
                (fixture / source.name).write_bytes(source.read_bytes())
            (fixture / "nicemetal_base.ppm").write_bytes(b"changed")
            with self.assertRaisesRegex(
                DRIVER.NiceMetalSmokeFailure, "fixture digest changed"
            ):
                DRIVER.validate_fixture(fixture)
            (fixture / "nicemetal_base.ppm").write_bytes(
                (DRIVER.DEFAULT_FIXTURE_DIR / "nicemetal_base.ppm").read_bytes()
            )
            (fixture / "unexpected.txt").write_text("no", encoding="utf-8")
            with self.assertRaisesRegex(
                DRIVER.NiceMetalSmokeFailure, "fixture inventory changed"
            ):
                DRIVER.validate_fixture(fixture)


class RuntimeEvidenceTests(unittest.TestCase):
    def test_all_platforms_accept_exact_positive_resource_host_evidence(self) -> None:
        for platform in DRIVER.PLATFORM_POLICY:
            with self.subTest(platform=platform):
                evidence = DRIVER.validate_runtime_evidence(
                    complete_log(platform), "clean console\n", platform
                )
                self.assertIsNotNone(evidence)
                self.assertEqual(evidence["spawn_marker_count"], 1)
                self.assertEqual(
                    evidence["placeholder_marker_counts"],
                    {name: 1 for name in DRIVER.MATERIAL_TEMPLATES},
                )
                self.assertEqual(
                    len(
                        evidence[
                            "managed_public_programs_derived_from_template_contract"
                        ]
                    ),
                    10,
                )
                expected_suffix = DRIVER.PLATFORM_POLICY[platform][
                    "backend_child_suffix"
                ]
                self.assertTrue(
                    all(
                        child.endswith(expected_suffix)
                        for child in evidence[
                            "backend_children_derived_from_static_contract_and_observed_backend"
                        ]
                    )
                )
                self.assertFalse(evidence["backend_child_names_directly_logged"])

    def test_incomplete_evidence_waits_but_final_validation_fails(self) -> None:
        log = complete_log("linux").replace(
            "placeholder for material 'NiceMetalMesh'", "not-yet NiceMetalMesh"
        )
        self.assertIsNone(
            DRIVER.validate_runtime_evidence(
                log, "clean console\n", "linux", require_complete=False
            )
        )
        with self.assertRaisesRegex(
            DRIVER.NiceMetalSmokeFailure, "placeholder:NiceMetalMesh"
        ):
            DRIVER.validate_runtime_evidence(log, "clean console\n", "linux")

    def test_duplicate_and_unexpected_placeholders_fail_closed(self) -> None:
        base = complete_log("linux")
        duplicate = base + (
            f"{DRIVER.PLACEHOLDER_PREFIX} 'NiceMetalMesh' "
            f"in group '{DRIVER.RESOURCE_GROUP}'\n"
        )
        with self.assertRaisesRegex(
            DRIVER.NiceMetalSmokeFailure, "duplicated NiceMetal placeholder"
        ):
            DRIVER.validate_runtime_evidence(
                duplicate, "clean console\n", "linux"
            )
        unexpected = base + (
            f"{DRIVER.PLACEHOLDER_PREFIX} 'OtherMaterial' "
            f"in group '{DRIVER.RESOURCE_GROUP}'\n"
        )
        with self.assertRaisesRegex(
            DRIVER.NiceMetalSmokeFailure, "unexpected placeholder"
        ):
            DRIVER.validate_runtime_evidence(
                unexpected, "clean console\n", "linux"
            )

    def test_every_shader_fallback_and_fatal_diagnostic_is_rejected(self) -> None:
        diagnostics = (
            "Program 'NiceMetal_PS_mm/GL3Plus' is not supported",
            "NiceMetal_PS_mm failed to load with an error",
            "Error compiling SimpleMetal_transp_PS_mm",
            "RTSS: fallback selected for 'managed/nicemetal'",
            "Error: ScriptCompiler bad token in nicemetal_mm.program",
            "Using the RTSS-compatible material",
            (
                "[RoR] DBG ActorSpawner::ProcessManagedMaterial(): "
                "Placeholder already exists: 'NiceMetalMesh'"
            ),
            "OGRE EXCEPTION",
        )
        for diagnostic in diagnostics:
            with self.subTest(diagnostic=diagnostic):
                with self.assertRaisesRegex(
                    DRIVER.NiceMetalSmokeFailure, "rejected shader/fallback"
                ):
                    DRIVER.validate_runtime_evidence(
                        complete_log("linux"), diagnostic, "linux"
                    )

    def test_unrelated_error_path_containing_nicemetal_is_not_a_shader_error(self) -> None:
        evidence = DRIVER.validate_runtime_evidence(
            complete_log("darwin"),
            "Error loading language file: /tmp/RoR-NiceMetal.app/en/ror.mo\n",
            "darwin",
        )
        self.assertIsNotNone(evidence)

    def test_visible_owner_and_hidden_backend_are_exact(self) -> None:
        wrong_visible = complete_log("linux").replace(
            "ogre-next-vulkan", "ogre-next-metal"
        )
        with self.assertRaisesRegex(
            DRIVER.NiceMetalSmokeFailure, "backend does not match"
        ):
            DRIVER.validate_runtime_evidence(
                wrong_visible, "clean console\n", "linux"
            )
        wrong_hidden = complete_log("linux").replace(
            "OpenGL 3+ Rendering Subsystem", "Direct3D11 Rendering Subsystem"
        )
        with self.assertRaisesRegex(
            DRIVER.NiceMetalSmokeFailure, "render system changed"
        ):
            DRIVER.validate_runtime_evidence(
                wrong_hidden, "clean console\n", "linux"
            )


class DriverBoundaryTests(unittest.TestCase):
    def test_runtime_build_commit_defaults_to_source_commit(self) -> None:
        source_commit = "1" * 40
        parsed = DRIVER.parse_args(
            [
                "--executable",
                "runtime",
                "--runtime-content",
                "content",
                "--artifact-dir",
                "evidence",
                "--repository-commit",
                source_commit,
            ]
        )
        self.assertEqual(parsed.repository_commit, source_commit)
        self.assertIsNone(parsed.runtime_build_commit)

    def test_configs_force_authored_materials_and_command_has_no_showcase(self) -> None:
        ror_config = DRIVER.build_ror_config()
        self.assertIn("gfx_alt_actor_materials=false", ror_config)
        self.assertIn("app_force_cache_update=true", ror_config)
        for platform, policy in DRIVER.PLATFORM_POLICY.items():
            with self.subTest(platform=platform):
                config = DRIVER.build_ogre_config(platform)
                self.assertIn(
                    f"Render System={policy['resource_host_render_system']}",
                    config,
                )
                command = DRIVER.build_command(Path("runtime"), platform)
                self.assertIn(DRIVER.TERRAIN, command)
                self.assertIn(DRIVER.ACTOR_FILE, command)
                self.assertNotIn("--native-visual-showcase", command)
                if platform == "darwin":
                    self.assertIn("-ApplePersistenceIgnoreState", command)

    def test_packaged_linux_entrypoint_and_content_must_be_colocated(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary) / "stage"
            content = stage / "content"
            content.mkdir(parents=True)
            (content / "simple2-terrain.zip").write_bytes(b"terrain")
            launcher = stage / "RunRoR"
            binary = stage / "RoR-Combined"
            launcher.write_bytes(b"launcher")
            binary.write_bytes(b"runtime")
            launcher.chmod(0o755)
            binary.chmod(0o755)
            receipt = DRIVER.validate_packaged_runtime(
                launcher, content, "linux"
            )
            self.assertEqual(receipt["entrypoint"], "RunRoR")
            self.assertEqual(receipt["runtime_binary"], "RoR-Combined")
            elsewhere = Path(temporary) / "elsewhere"
            elsewhere.mkdir()
            with self.assertRaisesRegex(
                DRIVER.NiceMetalSmokeFailure, "not colocated"
            ):
                DRIVER.validate_packaged_runtime(launcher, elsewhere, "linux")

    def test_source_manifest_binds_all_six_packaged_shader_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary) / "stage"
            executable = stage / "RunRoR"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"launcher")
            resources = stage / "resources"
            resources.mkdir()
            for family in ("materials", "managed_materials"):
                members = [
                    relative.split("/", maxsplit=1)[1]
                    for relative in DRIVER.SHADER_SOURCE_PATHS
                    if relative.startswith(f"{family}/")
                ]
                with zipfile.ZipFile(
                    resources / f"{family}.zip", mode="w"
                ) as archive:
                    for member in members:
                        archive.writestr(
                            member,
                            (ROOT / "resources" / family / member).read_bytes(),
                        )
            loose_managed = resources / "managed_materials"
            loose_managed.mkdir()
            for relative in DRIVER.SHADER_SOURCE_PATHS:
                if relative.startswith("managed_materials/"):
                    member = relative.split("/", maxsplit=1)[1]
                    (loose_managed / member).write_bytes(
                        (ROOT / "resources" / relative).read_bytes()
                    )
            manifest = DRIVER.validate_packaged_shader_sources(
                executable, "linux"
            )
            self.assertEqual(
                manifest["schema"], "ror.nicemetal_shader_source_manifest.v1"
            )
            self.assertEqual(manifest["file_count"], 6)
            self.assertTrue(manifest["removed_cg_absent"])
            self.assertEqual(len(manifest["archives"]), 2)
            self.assertTrue(manifest["managed_loose_source_closure_complete"])
            self.assertEqual(
                sum(
                    bool(item["selected_for_resource_host"])
                    for item in manifest["files"]
                ),
                4,
            )
            missing_loose = loose_managed / "nicemetal_mm_gl3plus.glsl"
            missing_bytes = missing_loose.read_bytes()
            missing_loose.unlink()
            with self.assertRaisesRegex(
                DRIVER.NiceMetalSmokeFailure,
                "runtime-selected loose managed-material source is missing",
            ):
                DRIVER.validate_packaged_shader_sources(executable, "linux")
            missing_loose.write_bytes(missing_bytes)
            with zipfile.ZipFile(
                resources / "materials.zip", mode="w"
            ) as archive:
                for relative in DRIVER.SHADER_SOURCE_PATHS:
                    if not relative.startswith("materials/"):
                        continue
                    member = relative.split("/", maxsplit=1)[1]
                    data = (ROOT / "resources" / relative).read_bytes()
                    if member == "nicemetal_gl3plus.glsl":
                        data = b"changed"
                    archive.writestr(member, data)
            with self.assertRaisesRegex(
                DRIVER.NiceMetalSmokeFailure, "differs from the checkout"
            ):
                DRIVER.validate_packaged_shader_sources(executable, "linux")

            with zipfile.ZipFile(
                resources / "materials.zip", mode="w"
            ) as archive:
                for relative in DRIVER.SHADER_SOURCE_PATHS:
                    if not relative.startswith("materials/"):
                        continue
                    member = relative.split("/", maxsplit=1)[1]
                    archive.writestr(
                        member, (ROOT / "resources" / relative).read_bytes()
                    )
            with zipfile.ZipFile(
                resources / "materials.zip", mode="a"
            ) as archive:
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore", UserWarning)
                    archive.writestr("nicemetal_gl3plus.glsl", b"changed")
            with self.assertRaisesRegex(
                DRIVER.NiceMetalSmokeFailure, "duplicate members"
            ):
                DRIVER.validate_packaged_shader_sources(executable, "linux")

    def test_source_manifest_rejects_packaged_cg(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary) / "stage"
            executable = stage / "RunRoR"
            resources = stage / "resources"
            resources.mkdir(parents=True)
            executable.write_bytes(b"launcher")
            for family in ("materials", "managed_materials"):
                members = [
                    relative.split("/", maxsplit=1)[1]
                    for relative in DRIVER.SHADER_SOURCE_PATHS
                    if relative.startswith(f"{family}/")
                ]
                with zipfile.ZipFile(
                    resources / f"{family}.zip", mode="w"
                ) as archive:
                    for member in members:
                        archive.writestr(
                            member,
                            (ROOT / "resources" / family / member).read_bytes(),
                        )
                    if family == "materials":
                        archive.writestr("nicemetal.cg", b"legacy")
            loose_managed = resources / "managed_materials"
            loose_managed.mkdir()
            for relative in DRIVER.SHADER_SOURCE_PATHS:
                if relative.startswith("managed_materials/"):
                    member = relative.split("/", maxsplit=1)[1]
                    (loose_managed / member).write_bytes(
                        (ROOT / "resources" / relative).read_bytes()
                    )
            with self.assertRaisesRegex(
                DRIVER.NiceMetalSmokeFailure, "retained Cg"
            ):
                DRIVER.validate_packaged_shader_sources(executable, "linux")

    def test_termination_targets_only_the_passed_child(self) -> None:
        class Child:
            def __init__(self, time_out: bool) -> None:
                self.time_out = time_out
                self.terminated = 0
                self.killed = 0
                self.waited = 0

            def poll(self) -> None:
                return None

            def terminate(self) -> None:
                self.terminated += 1

            def kill(self) -> None:
                self.killed += 1

            def wait(self, timeout: float) -> int:
                self.waited += 1
                if self.time_out and self.waited == 1:
                    raise subprocess.TimeoutExpired("child", timeout)
                return -9 if self.killed else -15

        child = Child(False)
        result = DRIVER.terminate_child(child)
        self.assertEqual((child.terminated, child.killed, child.waited), (1, 0, 1))
        self.assertEqual(result["scope"], "direct-child-only")
        stubborn = Child(True)
        result = DRIVER.terminate_child(stubborn)
        self.assertEqual(
            (stubborn.terminated, stubborn.killed, stubborn.waited), (1, 1, 2)
        )
        self.assertEqual(result["method"], "terminate-then-kill")

    def test_receipt_write_is_atomic_and_never_overwrites(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            receipt = Path(temporary) / "receipt.json"
            document = {
                "schema": DRIVER.RECEIPT_SCHEMA,
                "passed": True,
                "visible_nicemetal_draw_proven": False,
            }
            DRIVER.write_json_atomic(receipt, document)
            self.assertEqual(
                json.loads(receipt.read_text(encoding="utf-8")), document
            )
            self.assertFalse(receipt.with_name("receipt.json.tmp").exists())
            with self.assertRaisesRegex(
                DRIVER.NiceMetalSmokeFailure, "receipt already exists"
            ):
                DRIVER.write_json_atomic(receipt, document)


class WorkflowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.native = (
            ROOT / ".github/workflows/ogre-next-combined-native.yml"
        ).read_text(encoding="utf-8")
        cls.tsan = (
            ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
        ).read_text(encoding="utf-8")

    def test_path_triggers_cover_driver_test_and_fixture(self) -> None:
        for path in (
            "tests/fixtures/nicemetal_runtime/**",
            "tests/tools/test_run_nicemetal_shader_smoke.py",
            "tools/run_nicemetal_shader_smoke.py",
        ):
            self.assertEqual(self.native.count(f"- {path}"), 2, path)
            self.assertEqual(self.tsan.count(f"- {path}"), 1, path)

    def test_native_runs_static_driver_tests_and_three_runtime_smokes(self) -> None:
        self.assertEqual(
            self.native.count("tests/tools/test_run_nicemetal_shader_smoke.py"),
            8,
        )
        self.assertEqual(
            self.native.count("python tools/run_nicemetal_shader_smoke.py"), 3
        )
        self.assertEqual(
            self.native.count("visible_nicemetal_draw_proven"), 6
        )
        self.assertEqual(
            self.native.count("ror.nicemetal_shader_source_manifest.v1"), 3
        )
        self.assertEqual(
            self.native.count(
                "qualification/nicemetal-resource-host-shader-smoke"
            ),
            6,
        )

    def test_tsan_runs_only_static_driver_tests(self) -> None:
        self.assertEqual(
            self.tsan.count("tests/tools/test_run_nicemetal_shader_smoke.py"), 3
        )
        self.assertNotIn(
            "python tools/run_nicemetal_shader_smoke.py", self.tsan
        )

    def test_fresnel_contract_and_diagnostics_are_wired_without_draw_claim(self) -> None:
        fresnel_test = "tests/tools/test_fresnel_shader_contract.py"
        for path in (
            "resources/materials/fresnel.material",
            "resources/materials/Example_Fresnel_*",
            "source/main/gfx/GfxWater.cpp",
            fresnel_test,
        ):
            self.assertEqual(self.native.count(f"- {path}"), 2, path)
            self.assertEqual(self.tsan.count(f"- {path}"), 1, path)
        self.assertEqual(self.native.count(fresnel_test), 8)
        self.assertEqual(self.tsan.count(fresnel_test), 3)
        self.assertEqual(
            self.native.count("Error: ScriptCompiler .*fresnel"), 3
        )
        self.assertEqual(
            self.tsan.count("Error: ScriptCompiler .*fresnel"), 1
        )
        self.assertEqual(
            self.native.count(
                "not Fresnel water, grass, or General draw evidence"
            ),
            3,
        )
        self.assertEqual(
            self.tsan.count(
                "not Fresnel water, grass, or General draw evidence"
            ),
            1,
        )

    def test_grass_contract_and_diagnostics_are_wired_without_draw_claim(self) -> None:
        grass_test = "tests/tools/test_grass_shader_contract.py"
        for path in (
            "resources/materials/grass.material",
            "resources/materials/grass_*",
            grass_test,
        ):
            self.assertEqual(self.native.count(f"- {path}"), 2, path)
            self.assertEqual(self.tsan.count(f"- {path}"), 1, path)
        self.assertEqual(self.native.count(grass_test), 8)
        self.assertEqual(self.tsan.count(grass_test), 3)
        self.assertEqual(
            self.native.count("Error: ScriptCompiler .*grass"), 3
        )
        self.assertEqual(
            self.tsan.count("Error: ScriptCompiler .*grass"), 1
        )
        self.assertEqual(
            self.native.count("compile/load diagnostic coverage only"), 3
        )
        self.assertEqual(
            self.tsan.count("compile/load diagnostic coverage only"), 1
        )

    def test_general_contract_and_diagnostics_are_wired_without_draw_claim(self) -> None:
        general_test = "tests/tools/test_general_shader_contract.py"
        for path in (
            "resources/materials/general*",
            general_test,
        ):
            self.assertEqual(self.native.count(f"- {path}"), 2, path)
            self.assertEqual(self.tsan.count(f"- {path}"), 1, path)
        self.assertEqual(self.native.count(general_test), 8)
        self.assertEqual(self.tsan.count(general_test), 3)
        general_program_gate = (
            "Program '(ambient_vs|ambient_ps|render_vs|render_ps|"
            "render_gr_ps|diffuse_vs|diffuse_ps|diffuse_ps_env|"
            "diffuse_sh_vs|diffuse_sh_ps|diffuse_sh_a_ps)[^']*' "
            "is not supported"
        )
        self.assertEqual(self.native.count(general_program_gate), 2)
        self.assertEqual(self.tsan.count(general_program_gate), 1)
        self.assertEqual(
            self.native.count("Error: ScriptCompiler .*general"), 3
        )
        self.assertEqual(
            self.tsan.count("Error: ScriptCompiler .*general"), 1
        )

    def test_stdquad_family_uses_wildcard_trigger(self) -> None:
        path = "resources/OgreCore/StdQuad_vp*"
        self.assertEqual(self.native.count(f"- {path}"), 2)
        self.assertEqual(self.tsan.count(f"- {path}"), 1)


if __name__ == "__main__":
    unittest.main()
