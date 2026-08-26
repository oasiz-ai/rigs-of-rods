#!/usr/bin/env python3
"""Hostile contract for the isolated Ogre 1.11 compatibility workflow."""

from __future__ import annotations

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/ogre11-compat-native.yml"
LINUX_LAUNCHER = ROOT / "tools/linux/RunRoR"
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import ogre11_compat_runtime_audit as runtime_audit


class Ogre11CompatibilityWorkflowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_selectors_and_matrix_cover_exact_head_linux_and_windows(self) -> None:
        text = self.workflow
        self.assertIn("branches: [master, nextgen/playable-performance-gate]", text)
        self.assertIn("workflow_dispatch:", text)
        self.assertIn("runner: ubuntu-22.04", text)
        self.assertIn("platform: linux-x86_64", text)
        self.assertIn("cc: gcc-11", text)
        self.assertIn("runner: windows-2022", text)
        self.assertIn("platform: windows-x86_64", text)
        self.assertIn('toolset: "14.44"', text)
        self.assertIn("contents: read", text)
        self.assertNotIn("secrets.", text)

    def test_real_build_uses_exact_profiles_locks_and_forced_recipe_audits(self) -> None:
        text = self.workflow
        for fragment in (
            'cmake/conan/recipes/ogre3d-legacy',
            'cmake/conan/recipes/ogre3d-caelum-legacy',
            'cmake/conan/recipes/ogre3d-pagedgeometry-legacy',
            '--lockfile="${GITHUB_WORKSPACE}/cmake/conan/locks/ror-ogre11-${{ matrix.platform }}-release.lock"',
            "-o='&:ogre14=False'",
            'assert_ogre11_app_graph.py',
            '"-DCONAN_INSTALL_ARGS=--build=missing;--build=ogre3d/*;--build=ogre3d-caelum/*;--build=ogre3d-pagedgeometry/*"',
            '-DCONAN_HOST_PROFILE="${GITHUB_WORKSPACE}/${{ matrix.profile }}"',
            '-DCONAN_BUILD_PROFILE="${GITHUB_WORKSPACE}/${{ matrix.profile }}"',
        ):
            self.assertIn(fragment, text)

    def test_compatibility_flags_build_ctest_install_and_relocation_are_mandatory(self) -> None:
        text = self.workflow
        for option in (
            "-DROR_OGRE14=OFF",
            "-DROR_RENDERER_PUBLIC_LAUNCHER=OFF",
            "-DROR_OGRE_NEXT_PRODUCTION_PACKAGE=OFF",
            "-DROR_OGRE_NEXT_DEMO_ADMISSION=OFF",
        ):
            self.assertEqual(text.count(option), 1)
        self.assertIn('--target all', text)
        self.assertIn('--no-tests=error', text)
        self.assertIn('cmake --install', text)
        self.assertIn(
            'cmake -E rename "$ROR_OGRE11_INSTALL_DIR" "$ROR_OGRE11_ARTIFACT_DIR"',
            text,
        )
        self.assertIn('test ! -e "$ROR_OGRE11_INSTALL_DIR"', text)
        self.assertIn('tools/ogre11_compat_runtime_audit.py', text)
        self.assertIn('--forbidden-prefix "$GITHUB_WORKSPACE"', text)
        self.assertIn('--forbidden-prefix "$CONAN_HOME"', text)
        self.assertNotIn('ogre14_native_runtime_smoke.py', text)
        self.assertNotIn('run_playable_performance_scene.py', text)

    def test_success_and_failure_artifacts_cannot_be_confused_with_product(self) -> None:
        text = self.workflow
        success = text.index("- name: Upload passed Ogre 1.11 compatibility artifact")
        failure = text.index("- name: Upload failed compatibility diagnostics")
        success_step = text[success:failure]
        failure_step = text[failure:]
        self.assertIn("if: success()", success_step)
        self.assertIn("if-no-files-found: error", success_step)
        self.assertIn("RoR-Ogre11-CgFree-compat-", success_step)
        self.assertNotIn("OgreNext", success_step)
        self.assertNotIn("runtime-smoke", success_step)
        self.assertIn("if: failure()", failure_step)
        self.assertIn("CgFree-compat-diagnostics-", failure_step)
        self.assertNotIn("if: always()\n        uses: actions/upload-artifact", text)
        for field in (
            '"product_runtime": False',
            '"ogre_next_visible_frames_proven": False',
            '"renderer_runtime_smoke_proven": False',
            '"playability_proven": False',
            '"cg_source_files_present": False',
            '"active_cg_routes_present": False',
        ):
            self.assertIn(field, text)

    def test_plugin_contract_rejects_renderer_drift(self) -> None:
        contract = runtime_audit.PLATFORM_CONTRACTS["linux-x86_64"]
        valid = (
            "PluginFolder=lib\n"
            "Plugin=Codec_FreeImage\n"
            "Plugin=RenderSystem_GL\n"
            "Plugin=Plugin_ParticleFX\n"
            "Plugin=Plugin_OctreeSceneManager\n"
            "Plugin=libCaelum.so\n"
        )
        self.assertEqual(
            runtime_audit.parse_plugins_config(
                valid, contract, forbidden_prefixes=()
            ),
            contract.plugins,
        )
        for hostile in (
            valid.replace("RenderSystem_GL", "RenderSystem_Direct3D9"),
            valid + "Plugin=Plugin_CgProgramManager\n",
            valid.replace("PluginFolder=lib", "PluginFolder=/tmp/cache/lib"),
        ):
            with self.subTest(hostile=hostile[-50:]):
                with self.assertRaises(runtime_audit.AuditError):
                    runtime_audit.parse_plugins_config(
                        hostile,
                        contract,
                        forbidden_prefixes=("/tmp/cache",),
                    )

    def test_symlink_and_dependency_parsers_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre11-audit-") as temporary:
            root = Path(temporary)
            outside = root.parent / f"{root.name}-outside"
            outside.write_text("outside", encoding="utf-8")
            try:
                (root / "escape").symlink_to(outside)
                with self.assertRaises(runtime_audit.AuditError):
                    runtime_audit.validate_symlinks(root)
            finally:
                outside.unlink(missing_ok=True)
        with self.assertRaises(runtime_audit.AuditError):
            runtime_audit.parse_ldd_paths("libOgreMain.so => not found\n")
        self.assertEqual(
            runtime_audit.parse_dumpbin_dependencies(
                "  KERNEL32.dll\n  OgreMain.dll\nnot-a-library\n"
            ),
            ("KERNEL32.dll", "OgreMain.dll"),
        )

    @unittest.skipUnless(os.name == "posix", "requires POSIX shell semantics")
    def test_linux_launcher_resolves_its_own_artifact_from_a_foreign_cwd(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre11-launcher-artifact-"
        ) as artifact_tmp, tempfile.TemporaryDirectory(
            prefix="ror-ogre11-launcher-foreign-"
        ) as foreign_tmp:
            artifact = Path(artifact_tmp)
            foreign = Path(foreign_tmp)
            launcher = artifact / "RunRoR"
            shutil.copy2(LINUX_LAUNCHER, launcher)
            launcher.chmod(0o755)
            (artifact / "lib").mkdir()
            probe = artifact / "RoR"
            probe.write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' \"$0\" \"${LD_LIBRARY_PATH:-}\" \"$@\"\n",
                encoding="utf-8",
            )
            probe.chmod(0o755)
            hostile = foreign / "RoR"
            hostile.write_text("#!/bin/sh\nexit 97\n", encoding="utf-8")
            hostile.chmod(0o755)

            environment = os.environ.copy()
            environment.pop("LD_LIBRARY_PATH", None)
            result = subprocess.run(
                [str(launcher), "--contract-probe"],
                cwd=foreign,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout.splitlines(),
                [
                    str(probe.resolve()),
                    str((artifact / "lib").resolve()),
                    "--contract-probe",
                ],
            )

    def test_runtime_contract_uses_exact_relocated_renderer_paths(self) -> None:
        linux = runtime_audit.PLATFORM_CONTRACTS["linux-x86_64"]
        windows = runtime_audit.PLATFORM_CONTRACTS["windows-x86_64"]
        self.assertEqual(
            linux.required_runtime_paths,
            (
                "lib/libOgreMain.so",
                "lib/Codec_FreeImage.so",
                "lib/RenderSystem_GL.so",
                "lib/Plugin_ParticleFX.so",
                "lib/Plugin_OctreeSceneManager.so",
                "lib/libCaelum.so",
            ),
        )
        self.assertEqual(
            windows.required_runtime_paths,
            (
                "OgreMain.dll",
                "Codec_FreeImage.dll",
                "RenderSystem_Direct3D11.dll",
                "Plugin_ParticleFX.dll",
                "Plugin_OctreeSceneManager.dll",
                "Caelum.dll",
            ),
        )
        self.assertNotIn("RenderSystem_GL3Plus", linux.plugins)
        self.assertNotIn("RenderSystem_Direct3D9", windows.plugins)

    def test_packaged_cg_audit_ignores_comments_but_rejects_active_routes(self) -> None:
        comments = (
            "// vertex_program Ignored CG\n"
            "/* source ignored.cg */\n"
            "# Plugin=Plugin_CgProgramManager\n"
            "vertex_program Modern glsl\n"
            "{\n  source modern.vert\n}\n"
        )
        with tempfile.TemporaryDirectory(prefix="ror-ogre11-cg-clean-") as temporary:
            root = Path(temporary)
            (root / "clean.material").write_text(comments, encoding="utf-8")
            with zipfile.ZipFile(root / "clean.zip", "w") as archive:
                archive.writestr("scripts/", b"")
                archive.writestr("scripts/clean.program", comments)
            result = runtime_audit.scan_packaged_cg_routes(root)
            self.assertEqual(result["loose_resource_scripts"], 1)
            self.assertEqual(result["resource_archives"], 1)
            self.assertEqual(result["archived_resource_scripts"], 1)

        hostile_files = {
            "legacy.CG": "void main() {}\n",
            "legacy.CgInC": "float4 helper();\n",
            "declaration.material": "  VeRtEx_PrOgRaM old CG { }\n",
            "reference.program": "fragment_program modern glsl\n{\n SoUrCe shader.CgInC\n}\n",
            "plugin.cfg": "  PlUgIn = Plugin_CgProgramManager\n",
        }
        for name, payload in hostile_files.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix="ror-ogre11-cg-hostile-"
            ) as temporary:
                root = Path(temporary)
                (root / name).write_text(payload, encoding="utf-8")
                with self.assertRaises(runtime_audit.AuditError):
                    runtime_audit.scan_packaged_cg_routes(root)

    def test_packaged_cg_audit_rejects_archive_members_and_routes(self) -> None:
        for member, payload in (
            ("Shaders/Legacy.cG", "void main() {}\n"),
            (
                "Scripts/legacy.material",
                "fragment_program old cG\n{\n source old.cg\n}\n",
            ),
        ):
            with self.subTest(member=member), tempfile.TemporaryDirectory(
                prefix="ror-ogre11-cg-archive-"
            ) as temporary:
                root = Path(temporary)
                with zipfile.ZipFile(root / "resources.zip", "w") as archive:
                    archive.writestr(member, payload)
                with self.assertRaises(runtime_audit.AuditError):
                    runtime_audit.scan_packaged_cg_routes(root)


if __name__ == "__main__":
    unittest.main()
