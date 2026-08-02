#!/usr/bin/env python3
"""Contracts for the cross-platform real OgreNext product package."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PACKAGER_PATH = (
    REPOSITORY_ROOT
    / "tools"
    / "ogre_next_probe"
    / "package_ogre_next_product.py"
)


def _load_packager():
    spec = importlib.util.spec_from_file_location(
        "ror_ogre_next_product_packager", PACKAGER_PATH
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PACKAGER = _load_packager()


class OgreNextProductPackagerTests(unittest.TestCase):
    def make_inputs(self, root: Path, policy: str = "macos-arm64-metal"):
        identity = (
            "ror-ogre-next-production-child-v1"
            "|ror=" + "1" * 40
            + "|ogre=" + "2" * 40
            + f"|platform={policy}"
        )
        child_name = PACKAGER.POLICY_CHILD_NAMES[policy]
        child = root / "native" / child_name
        child.parent.mkdir()
        child.write_bytes(b"native-prefix\0" + identity.encode("ascii") + b"\0native-suffix")

        n1 = root / "n1"
        media = n1 / PACKAGER.N1_MEDIA_RELATIVE
        (media / "Hlms" / "Pbs").mkdir(parents=True)
        (media / "Hlms" / "Pbs" / "Main_piece.any").write_text(
            "pinned hlms\n", encoding="utf-8"
        )
        (media / "2.0" / "scripts" / "Compositors").mkdir(parents=True)
        (media / "2.0" / "scripts" / "Compositors" / "Hdr.compositor").write_text(
            "pinned compositor\n", encoding="utf-8"
        )
        (n1 / ".stage-v10").write_bytes(b"")
        licenses = n1 / "licenses"
        licenses.mkdir()
        for name in PACKAGER.BASE_NOTICES:
            (licenses / name).write_text(f"notice {name}\n", encoding="utf-8")

        presentation = root / "presentation"
        (presentation / "CommonCopy").mkdir(parents=True)
        (presentation / "CommonCopy" / "Copy.material").write_text(
            "copy shader\n", encoding="utf-8"
        )
        build_contract = root / "ogre-next-build-contract.json"
        build_contract.write_text(
            json.dumps(
                {
                    "schema_version": 6,
                    "platform": {"policy": policy},
                    "components": {
                        "headless_child_bootstrap": True,
                        "headless_child_output_name": "RoR-OgreNext",
                        "headless_child_packaged": True,
                        "headless_child_production_admitted": False,
                    },
                    "shader_media": {"root": str(media)},
                }
            ),
            encoding="utf-8",
        )
        return child, n1, presentation, build_contract, identity

    def test_stage_is_atomic_relocatable_and_byte_exact(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogrenext-product-") as temp:
            root = Path(temp).resolve()
            child, n1, presentation, contract, identity = self.make_inputs(root)
            output = root / "product"
            manifest = PACKAGER.stage_package(
                child=child,
                n1_package=n1,
                presentation_root=presentation,
                build_contract=contract,
                output=output,
                identity=identity,
                policy="macos-arm64-metal",
            )
            self.assertEqual(manifest["schema"], PACKAGER.MANIFEST_SCHEMA)
            self.assertFalse(manifest["production_admitted"])
            self.assertEqual((output / "RoR-OgreNext").read_bytes(), child.read_bytes())
            self.assertFalse(any(path.is_symlink() for path in output.rglob("*")))
            product_contract = json.loads(
                (output / PACKAGER.BUILD_CONTRACT_RELATIVE).read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(
                product_contract["shader_media"]["root"],
                "resources/ogrenext/Hlms",
            )
            self.assertNotIn(str(root), json.dumps(product_contract))
            PACKAGER.verify_package(
                output,
                expected_policy="macos-arm64-metal",
                expected_identity=identity,
                strict_root=True,
            )

    def test_probe_binary_cannot_substitute_for_product_child(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogrenext-probe-substitute-") as temp:
            root = Path(temp).resolve()
            child, n1, presentation, contract, identity = self.make_inputs(root)
            child.write_bytes(b"probe-only-non-admitted\n")
            with self.assertRaisesRegex(
                PACKAGER.PackageError, "probe/smoke substitution"
            ):
                PACKAGER.stage_package(
                    child=child,
                    n1_package=n1,
                    presentation_root=presentation,
                    build_contract=contract,
                    output=root / "product",
                    identity=identity,
                    policy="macos-arm64-metal",
                )

    def test_payload_tamper_breaks_completion_sealed_verification(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogrenext-product-tamper-") as temp:
            root = Path(temp).resolve()
            child, n1, presentation, contract, identity = self.make_inputs(root)
            output = root / "product"
            PACKAGER.stage_package(
                child=child,
                n1_package=n1,
                presentation_root=presentation,
                build_contract=contract,
                output=output,
                identity=identity,
                policy="macos-arm64-metal",
            )
            target = output / "resources/ogrenext/Presentation/CommonCopy/Copy.material"
            target.write_text("tampered\n", encoding="utf-8")
            with self.assertRaisesRegex(PACKAGER.PackageError, "payload changed"):
                PACKAGER.verify_package(output, strict_root=True)

    def test_stage_replaces_cmake_precreated_output_scaffold(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogrenext-product-scaffold-") as temp:
            root = Path(temp).resolve()
            child, n1, presentation, contract, identity = self.make_inputs(root)
            output = root / "product"
            # CMake/Ninja materializes parents for OUTPUT and nested BYPRODUCTS
            # before it runs the packaging command.
            (output / "provenance").mkdir(parents=True)
            PACKAGER.stage_package(
                child=child,
                n1_package=n1,
                presentation_root=presentation,
                build_contract=contract,
                output=output,
                identity=identity,
                policy="macos-arm64-metal",
            )
            PACKAGER.verify_package(
                output,
                expected_policy="macos-arm64-metal",
                expected_identity=identity,
                strict_root=True,
            )

    def test_stage_refuses_unsealed_nonempty_predecessor(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogrenext-product-partial-") as temp:
            root = Path(temp).resolve()
            child, n1, presentation, contract, identity = self.make_inputs(root)
            output = root / "product"
            output.mkdir()
            (output / "partial.bin").write_bytes(b"incomplete")
            with self.assertRaisesRegex(
                PACKAGER.PackageError, "product manifest must be one regular file"
            ):
                PACKAGER.stage_package(
                    child=child,
                    n1_package=n1,
                    presentation_root=presentation,
                    build_contract=contract,
                    output=output,
                    identity=identity,
                    policy="macos-arm64-metal",
                )
            self.assertEqual((output / "partial.bin").read_bytes(), b"incomplete")

    def test_all_three_product_policies_have_exact_child_names(self) -> None:
        self.assertEqual(
            PACKAGER.POLICY_CHILD_NAMES,
            {
                "macos-arm64-metal": "RoR-OgreNext",
                "linux-x86_64-vulkan": "RoR-OgreNext",
                "windows-x64-d3d11": "RoR-OgreNext.exe",
            },
        )


class OgreNextProductPackagingStaticContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.root_cmake = (REPOSITORY_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.module = (
            REPOSITORY_ROOT / "cmake" / "OgreNextProductionPackage.cmake"
        ).read_text(encoding="utf-8")
        cls.source_cmake = (
            REPOSITORY_ROOT / "source/main/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        cls.probe_cmake = (
            REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        cls.mac_stager = (
            REPOSITORY_ROOT / "cmake/macos/StageMacOSBundle.cmake"
        ).read_text(encoding="utf-8")
        cls.facts = (
            REPOSITORY_ROOT / "cmake/RendererLauncherPackageConfig.cmake"
        ).read_text(encoding="utf-8")

    def test_public_suite_defaults_to_isolated_verified_product_stage(self) -> None:
        for token in (
            'set(_ror_ogre_next_product_package_default\n    "${ROR_RENDERER_PUBLIC_LAUNCHER}")',
            "ROR_OGRE_NEXT_PRODUCTION_PACKAGE",
            "include(OgreNextProductionPackage)",
            "ror_add_ogre_next_production_package()",
        ):
            self.assertIn(token, self.root_cmake)
        for token in (
            "ExternalProject_Add(",
            "ror_ogre_next_product_external",
            '"-DCMAKE_SUPPRESS_REGENERATION=ON"',
            '"-DROR_OGRE_NEXT_PRODUCT_STAGE=ON"',
            "--target ror_ogre_next_product_stage",
            "BUILD_BYPRODUCTS",
            "BUILD_ALWAYS TRUE",
        ):
            self.assertIn(token, self.module)
        self.assertNotIn("add_subdirectory(tools/ogre_next_probe", self.root_cmake)

    def test_real_child_not_probe_executable_owns_the_product_identity(self) -> None:
        for token in (
            "renderer_ogre_next_product_identity.cpp",
            "ror_renderer_ogre_next_child_runtime",
            "package_ogre_next_product.py",
            "--child $<TARGET_FILE:ror_renderer_ogre_next_child_runtime>",
            "--strict-root",
        ):
            self.assertIn(token, self.probe_cmake)
        self.assertNotIn("frontend_n1_smoke>\n                --n1-package", self.probe_cmake)

    def test_linux_windows_install_and_cpack_preserve_stage_layout(self) -> None:
        for token in (
            "if (ROR_OGRE_NEXT_PRODUCTION_PACKAGE AND NOT APPLE)",
            'DIRECTORY "${ROR_OGRE_NEXT_PRODUCT_STAGE_ROOT}/"',
            'DESTINATION .',
            'COMPONENT "Base_Game"',
            "USE_SOURCE_PERMISSIONS",
        ):
            self.assertIn(token, self.source_cmake)

    def test_macos_bundle_authenticates_media_and_signs_child_before_app(self) -> None:
        for token in (
            "ROR_OGRE_NEXT_PRODUCT_ROOT",
            "ror.ogre_next.product_package_completion.v1",
            "macos-arm64-metal",
            "OgreNext product payload differs from its manifest",
            "Copying authenticated OgreNext media",
            "_ror_compare_regular_trees(",
        ):
            self.assertIn(token, self.mac_stager)
        nested_sign = self.mac_stager.index(
            "foreach(_binary IN LISTS\n        _ror_framework_binaries\n"
            "        _ror_plugin_binaries\n        _ror_bundle_executables)"
        )
        outer_sign = self.mac_stager.index(
            '"Ad-hoc signing ${ROR_BUNDLE_NAME}.app"'
        )
        self.assertLess(nested_sign, outer_sign)

    def test_admission_facts_remain_false_until_second_pass(self) -> None:
        for token in (
            'ROR_RENDERER_LAUNCHER_OGRE_NEXT_CHILD_PRESENT "false"',
            'ROR_RENDERER_LAUNCHER_OGRE_NEXT_PRODUCTION_READY "false"',
            'ROR_RENDERER_LAUNCHER_OGRE_NEXT_PSSM_ADMITTED "false"',
        ):
            self.assertIn(token, self.facts)


if __name__ == "__main__":
    unittest.main()
