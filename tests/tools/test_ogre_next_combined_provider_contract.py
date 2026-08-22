#!/usr/bin/env python3
"""Offline fail-closed contract tests for the root combined renderer provider."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ROOT_CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
MAIN_CMAKE = (ROOT / "source/main/CMakeLists.txt").read_text(encoding="utf-8")
PROVIDER = (ROOT / "cmake/ogre_next_embedded/CMakeLists.txt").read_text(
    encoding="utf-8"
)
PROVIDER_ENTRY = (ROOT / "cmake/OgreNextEmbeddedRuntime.cmake").read_text(
    encoding="utf-8"
)
PINNED = (ROOT / "tools/ogre_next_probe/cmake/PinnedOgreNext.cmake").read_text(
    encoding="utf-8"
)
EXECUTABLE_CONTRACT = (
    ROOT / "source/main/ogre-next-combined-executable-contract.json.in"
).read_text(encoding="utf-8")
PROVIDER_CONTRACT = (
    ROOT / "cmake/ogre_next_embedded/provider-contract.json.in"
).read_text(encoding="utf-8")
VERIFIER_PATH = ROOT / "tools/verify_ogre_next_combined_binary_closure.py"
VERIFIER = VERIFIER_PATH.read_text(encoding="utf-8")
ELF_VERIFIER_PATH = ROOT / "tools/verify_ogre_next_combined_elf_closure.py"
ELF_VERIFIER = ELF_VERIFIER_PATH.read_text(encoding="utf-8")
NAMESPACE_AUDIT = (
    ROOT / "tools/ogre_next_probe/audit_embedded_namespace.py"
).read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/ogre-next-probe.yml").read_text(
    encoding="utf-8"
)
COMBINED_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-native.yml"
).read_text(encoding="utf-8")
LINUX_COMBINED_LAUNCHER = (ROOT / "tools/linux/RunRoR-combined").read_text(
    encoding="utf-8"
)


def block(source: str, start: str, end: str) -> str:
    start_index = source.index(start)
    return source[start_index : source.index(end, start_index)]


class CombinedProviderContractTests(unittest.TestCase):
    def test_root_option_is_off_and_provider_precedes_game_target(self) -> None:
        self.assertRegex(
            ROOT_CMAKE,
            r'option\(\s*ROR_OGRE_NEXT_COMBINED_RUNTIME\s*\n\s*"[^"]+"\s*\n\s*OFF\)',
        )
        dependency_index = ROOT_CMAKE.index("include(DependenciesConfig)")
        provider_index = ROOT_CMAKE.index("ror_add_ogre_next_embedded_runtime()")
        game_index = ROOT_CMAKE.index("add_subdirectory(source/main)")
        self.assertLess(dependency_index, provider_index)
        self.assertLess(provider_index, game_index)
        for gate in (
            "if (APPLE)",
            "elseif (WIN32)",
            'elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")',
            "NOT CMAKE_SIZEOF_VOID_P EQUAL 8",
            'NOT CMAKE_BUILD_TYPE STREQUAL "Release"',
            'NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"',
            "ROR_RENDERER_PUBLIC_LAUNCHER",
            "ROR_OGRE_NEXT_PRODUCTION_PACKAGE",
            "ROR_OGRE_NEXT_DEMO_ADMISSION",
        ):
            self.assertIn(gate, ROOT_CMAKE)

    def test_provider_reuses_exact_root_sdl_and_never_fetches_it(self) -> None:
        for token in (
            "sdl_SDL2_SDL2_LIBRARIES_TARGETS",
            "IMPORTED_LOCATION_RELEASE",
            "ROR_ROOT_SDL_IMPORTED_ARTIFACT_SHA256",
            "ROR_ROOT_SDL_PACKAGE_IDENTITY_SHA256",
            "ROR_ROOT_SDL_CONANINFO_SHA256",
            "ROR_ROOT_SDL_CONANMANIFEST_SHA256",
            "ror-ogre14-${_ror_root_conan_lock_platform}-release.lock",
            'set(_ror_root_conan_lock_platform "macos-arm64")',
            'set(_ror_root_conan_lock_platform "windows-x86_64")',
            'set(_ror_root_conan_lock_platform "linux-x86_64")',
            'CACHE INTERNAL "Selected embedded Ogre-Next renderer target" FORCE',
        ):
            self.assertIn(token, PROVIDER)
        root_sdl = PINNED[: PINNED.index("include(FetchContent)")]
        self.assertIn("TARGET SDL2::SDL2", root_sdl)
        presentation_sdl = block(
            PINNED,
            "if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)\n    # DependenciesConfig.cmake",
            "else ()\n    # Compile the exact SDL source",
        )
        self.assertIn("SDL2::SDL2", presentation_sdl)
        self.assertNotIn("FetchContent", presentation_sdl)
        self.assertIn('basename == "libSDL2.a"', VERIFIER)
        self.assertIn("root_sdl_static_archive_members_extracted", VERIFIER)

    def test_facade_does_not_export_ogre_next_compile_usage(self) -> None:
        facade = block(
            PROVIDER,
            "target_link_libraries(ror_ogre_next_embedded_runtime INTERFACE",
            "add_library(RoR::OgreNextEmbeddedRuntime ALIAS",
        )
        self.assertIn("ror_ogre_next_in_process_presenter", facade)
        for raw_target in (
            "OgreNextMain",
            "RenderSystem_Metal",
            "OgreNextHlmsPbs",
            "OgreNextHlmsUnlit",
            "OgreNextOverlay",
        ):
            self.assertNotIn(raw_target, facade)
            self.assertIn(raw_target, PROVIDER)
        self.assertIn("--isolated-consumer-source", PROVIDER)
        self.assertIn("--isolated-consumer-target-name RoR-Combined", PROVIDER)

    def test_rapidjson_is_authenticated_private_target_usage(self) -> None:
        private_closure = block(
            PROVIDER,
            "# The root Conan toolchain intentionally prefers config packages.",
            'set(_ror_render_root "${CMAKE_SOURCE_DIR}/source/main/gfx/render")',
        )
        for target in (
            "OgreNextMain",
            "OgreNextHlmsPbs",
            "OgreNextHlmsUnlit",
        ):
            self.assertIn(target, private_closure)
        for token in (
            "${rapidjson_SOURCE_DIR}/include",
            "rapidjson/document.h",
            "SYSTEM PRIVATE",
            "INTERFACE_INCLUDE_DIRECTORIES",
            "file(REAL_PATH",
            "_ror_rapidjson_canonical_include",
            "_ror_rapidjson_existing_private_usage_count EQUAL 0",
            "_ror_rapidjson_private_usage_count EQUAL 1",
            "_ror_rapidjson_interface_leaked",
            "ror_ogre_next_rapidjson_private_closure_verify",
            "ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER_SHA256",
        ):
            self.assertIn(token, private_closure)
        self.assertNotIn("target_link_libraries", private_closure)
        self.assertIn('"rapidjson_interface_exported": false', PROVIDER_CONTRACT)
        self.assertIn(
            '"rapidjson_private_consumers": ["OgreNextMain", '
            '"OgreNextHlmsPbs", "OgreNextHlmsUnlit"]',
            PROVIDER_CONTRACT,
        )

    def test_direct_provider_owns_neutral_sources_exactly_once(self) -> None:
        owned = block(
            PROVIDER,
            "set(_ror_combined_owned_sources",
            "foreach (_ror_direct_source IN LISTS _ror_combined_owned_sources)",
        )
        self.assertIn("RenderPayloadDigest.cpp", owned)
        self.assertIn("RendererGameInputTarget.cpp", owned)
        for forbidden in ("Bridge", "Transport", "Envelope", "/detail/"):
            self.assertNotIn(forbidden, owned)
        self.assertIn("_ror_combined_remove_source_exactly_once", MAIN_CMAKE)
        self.assertIn("ROR_OGRE_NEXT_COMBINED_OWNED_SOURCES", MAIN_CMAKE)
        retired = block(
            MAIN_CMAKE,
            "set(_ror_combined_retired_renderer_sources",
            "foreach (_ror_retired_source IN LISTS",
        )
        for source in (
            "RenderTransportEnvelope.cpp",
            "RendererBridgeChannel.cpp",
            "RendererChildLauncher.cpp",
            "RendererOgreNextLiveSession.cpp",
            "RendererPackagedMediaPath.cpp",
        ):
            self.assertIn(source, retired)

    def test_language_remap_and_strict_fp_are_explicit(self) -> None:
        self.assertIn("PRIVATE -fno-fast-math -ffp-contract=off", PROVIDER)
        strict_fp = block(
            PROVIDER,
            "# The host's optimized configuration globally enables -ffast-math.",
            "# The root Conan toolchain intentionally prefers config packages.",
        )
        for token in (
            "ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_TARGETS",
            "ROR_OGRE_NEXT_STRICT_FP_APPLIED",
            "-fno-fast-math",
            "-ffp-contract=off",
            "ROR_OGRE_NEXT_UPSTREAM_STRICT_FP_TARGET_COUNT",
        ):
            self.assertIn(token, strict_fp)
        self.assertIn("--require-upstream-strict-fp", PROVIDER)
        self.assertIn('"ogre_next_upstream_strict_fp": true', PROVIDER_CONTRACT)
        for invocation in (
            "ror_ogre_next_embedded_n1_runtime LANGUAGES CXX OBJCXX",
            "ror_ogre_next_embedded_n1_runtime LANGUAGES CXX)",
            "ror_ogre_next_embedded_sdl_window_runtime LANGUAGES OBJCXX",
            "ror_ogre_next_in_process_presenter LANGUAGES CXX",
            "ror_ogre_next_root_next_adapter LANGUAGES CXX",
            "LANGUAGES ${_ror_ogre_next_plugin_export_language}",
            "set(_ror_ogre_next_plugin_export_language OBJCXX)",
            "set(_ror_ogre_next_plugin_export_language CXX)",
        ):
            self.assertIn(invocation, PROVIDER)

    def test_uv_affine_pbs_implementation_is_in_product_runtime(self) -> None:
        sources = block(
            PROVIDER,
            "set(_ror_embedded_n1_sources",
            "add_library(ror_ogre_next_embedded_n1_runtime STATIC",
        )
        self.assertIn("OgreNextUvAffinePbs.cpp", sources)
        self.assertIn("OgreNextN1Frontend.cpp", sources)

    def test_media_paths_and_stage_are_build_tree_authority(self) -> None:
        for macro in (
            "ROR_OGRE_NEXT_COMBINED_SHADER_MEDIA_ROOT",
            "ROR_OGRE_NEXT_COMBINED_PRESENTATION_MEDIA_ROOT",
        ):
            self.assertIn(macro, PROVIDER)
        self.assertIn("ror-ogre-next-combined-resources", PROVIDER)
        self.assertIn("stage_ogre_next_combined_resources.py", PROVIDER)
        for token in (
            "ROR_OGRE_NEXT_UV_AFFINE_PBS_MEDIA_RELATIVE",
            "ROR_OGRE_NEXT_UV_AFFINE_PBS_MEDIA_SOURCE",
            "RoR/UvAffinePbs/UvAffinePbs_piece_ps.any",
        ):
            self.assertIn(token, PROVIDER)
        self.assertIn('"${_ror_stage_relative}!|${_ror_source}', PROVIDER)
        self.assertIn('string(REGEX REPLACE "!$"', PROVIDER)
        self.assertIn("ror_ogre_next_combined_resources", MAIN_CMAKE)
        self.assertIn('"raw_build_tree_demo": true', PROVIDER_CONTRACT)
        self.assertIn('"app_bundle_staged": false', PROVIDER_CONTRACT)

    def test_presentation_manifest_matches_runtime_bytewise_order(self) -> None:
        presentation = block(
            PROVIDER,
            'set(_ror_presentation_staged_files "")',
            "set(ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_COUNT",
        )
        append_index = presentation.index(
            "list(APPEND _ror_presentation_staged_files"
        )
        sort_index = presentation.index(
            "list(SORT _ror_presentation_staged_files)"
        )
        manifest_index = presentation.index(
            'set(ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_ENTRIES "")'
        )
        record_index = presentation.index("_ror_record_media_file(")
        self.assertLess(append_index, sort_index)
        self.assertLess(sort_index, manifest_index)
        self.assertLess(manifest_index, record_index)

        lock = json.loads(
            (
                ROOT
                / "tools/ogre_next_probe/ogre-next-presentation-copy-v1.lock.json"
            ).read_text(encoding="utf-8")
        )
        relative_paths = []
        for entry in lock["files"]:
            path = entry["path"]
            if path.startswith("tools/ogre_next_probe/presentation_media/"):
                relative_paths.append(
                    path.removeprefix(
                        "tools/ogre_next_probe/presentation_media/"
                    )
                )
            else:
                relative_paths.append(
                    "CommonCopy/"
                    + path.removeprefix(
                        "Samples/Media/2.0/scripts/materials/Common/"
                    )
                )
        self.assertNotEqual(relative_paths, sorted(relative_paths))
        self.assertEqual(
            sorted(relative_paths)[0],
            "CommonCopy/GLSL/Copyback_4xFP32_ps.glsl",
        )

    def test_prefix_media_names_have_one_canonical_stage_order(self) -> None:
        stage_script = ROOT / "tools/stage_ogre_next_combined_resources.py"
        with tempfile.TemporaryDirectory() as temporary:
            build_root = Path(temporary)
            sources = build_root / "sources"
            sources.mkdir()
            base = sources / "base.bin"
            extension = sources / "extension.bin"
            base.write_bytes(b"base\n")
            extension.write_bytes(b"extension\n")

            def entry(path: str, source: Path) -> dict[str, object]:
                return {
                    "path": path,
                    "source": str(source),
                    "size": source.stat().st_size,
                    "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                }

            entries = [
                entry("ShaderMedia/Test.material", base),
                entry("ShaderMedia/Test.material.json", extension),
            ]
            manifest = build_root / "manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema": "ror.ogre_next_combined_resource_manifest.v1",
                        "files": entries,
                    }
                ),
                encoding="utf-8",
            )
            output = build_root / "ror-ogre-next-combined-resources"
            command = [
                sys.executable,
                str(stage_script),
                "--manifest",
                str(manifest),
                "--build-root",
                str(build_root),
                "--output",
                str(output),
            ]
            completed = subprocess.run(
                command, check=False, capture_output=True, text=True
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(
                (output / "ShaderMedia/Test.material").read_bytes(), b"base\n"
            )

            manifest.write_text(
                json.dumps(
                    {
                        "schema": "ror.ogre_next_combined_resource_manifest.v1",
                        "files": list(reversed(entries)),
                    }
                ),
                encoding="utf-8",
            )
            rejected = subprocess.run(
                command, check=False, capture_output=True, text=True
            )
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("not strictly sorted", rejected.stderr)

    def test_binary_proof_is_positive_negative_and_preinvalidated(self) -> None:
        for target in (
            "ror_ogre_next_combined_binary_receipt_invalidate",
            "ror_ogre_next_root_namespace_audit",
            "ror_ogre_next_embedded_provider_contract",
        ):
            self.assertIn(target, MAIN_CMAKE)
            self.assertIn(target, PROVIDER_ENTRY)
        self.assertIn("add_custom_target(ror_ogre_next_combined_verified ALL", MAIN_CMAKE)
        self.assertNotIn("add_custom_command(TARGET ${BINNAME} POST_BUILD", MAIN_CMAKE)
        for argument in (
            "--provider-contract",
            "--namespace-audit-report",
            "--required-ogre14-dylib",
            "--sdl-provider-dylib",
        ):
            self.assertIn(argument, MAIN_CMAKE)

        for evidence in (
            "REQUIRED_SYMBOL_TOKENS",
            "FORBIDDEN_SYMBOL_TOKENS",
            "FORBIDDEN_ROOT_IMAGE_CODEC_ARCHIVE_TOKENS",
            "FORBIDDEN_EXTERNAL_IMAGE_CODEC_SYMBOL_PREFIXES",
            "_external_image_codec_symbol_violations",
            "missing_archive_evidence",
            "extracted_sdl_members",
            "verified_audited_archives",
            "verified_audited_legacy",
            "isolated_consumers",
            "REVIEWED_GLOBAL_INTERSECTION_ALLOWLIST",
            "_verify_strict_fp_receipts",
            "_nm_undefined_symbol_names",
            "root_sdl_defined_symbols",
            "stb_image_nm_confirmed_live_symbols",
            "ogre14_runtime_package_root",
        ):
            self.assertIn(evidence, VERIFIER)
        for retired_object in (
            "RendererLauncherMain",
            "RendererOgreNextChild",
            "RendererOgreNextChildMain",
            "RendererOgreNextProductionSession",
            "RendererPublicLauncher",
        ):
            self.assertIn(f'"{retired_object}"', VERIFIER)
        self.assertIn('--build-root "${CMAKE_BINARY_DIR}"', MAIN_CMAKE)
        self.assertIn("STB_IMAGE_INDIRECT_INPUT_PREFIXES", NAMESPACE_AUDIT)
        self.assertIn('"indirect_input_tokens": []', NAMESPACE_AUDIT)
        self.assertIn(
            'stb_implementation.get("indirect_input_tokens") != []', VERIFIER
        )
        for standalone_input in (
            '"${ROR_OGRE_NEXT_STANDALONE_ROOT}/*.py"',
            '"${ROR_OGRE_NEXT_STANDALONE_ROOT}/embedded_namespace/*"',
            '"${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/*"',
            '"${ROR_OGRE_NEXT_STANDALONE_ROOT}/presentation_media/*"',
        ):
            self.assertIn(standalone_input, PROVIDER)
        self.assertIn("if receipt.exists() or receipt.is_symlink():", VERIFIER)
        self.assertIn('"provider_contract":', EXECUTABLE_CONTRACT)
        self.assertIn('"namespace_audit_report":', EXECUTABLE_CONTRACT)

    def test_linux_binary_proof_uses_the_platform_renderer_and_elf_tools(self) -> None:
        for token in (
            'elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")',
            'file(REAL_PATH "${CMAKE_NM}" _ror_combined_nm_tool)',
            'file(REAL_PATH "${ROR_OGRE_NEXT_DYNAMIC_AUDIT_TOOL}"',
            '--nm "${_ror_combined_nm_tool}"',
            '--readelf "${_ror_combined_dynamic_audit_tool}"',
            "verify_ogre_next_combined_elf_closure.py",
            "--readelf",
            "--required-ogre14-library",
            "--sdl-provider-library",
            '"LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/RoR-Combined.link-map.txt"',
            '"$<TARGET_FILE:${ROR_OGRE_NEXT_RENDERER_TARGET}>"',
        ):
            with self.subTest(token=token):
                self.assertIn(token, MAIN_CMAKE)
        for token in (
            'PLATFORM_POLICY = "linux-x86_64-vulkan"',
            "REQUIRED_SYMBOL_TOKENS",
            "FORBIDDEN_SYMBOL_TOKENS",
            "FORBIDDEN_LINK_MAP_OBJECT_TOKENS",
            '"qualification_eligible": source_checkout[',
            '"bridge_or_transport_symbols_present": False',
            '"root_sdl_symbols_present": False',
            '"ogre14_host_load_commands_present": True',
        ):
            with self.subTest(token=token):
                self.assertIn(token, ELF_VERIFIER)
        self.assertIn(
            '"tools/verify_ogre_next_combined_elf_closure.py"', PROVIDER
        )
        self.assertIn(
            "- tools/verify_ogre_next_combined_elf_closure.py", WORKFLOW
        )

    def test_linux_install_and_launcher_select_the_combined_executable(self) -> None:
        for token in (
            'set(_ror_linux_launcher_source',
            '"${CMAKE_SOURCE_DIR}/tools/linux/RunRoR-combined"',
            'set(_ror_linux_installed_game_executable "RoR-Combined")',
        ):
            self.assertIn(token, MAIN_CMAKE)
        self.assertIn(
            'exec "${ror_launcher_dir}/RoR-Combined" "$@"',
            LINUX_COMBINED_LAUNCHER,
        )
        self.assertNotIn("RoR-Ogre14", LINUX_COMBINED_LAUNCHER)
        for source in (
            ".github/workflows/ogre-next-combined-native.yml",
            "cmake/linux/LinuxRuntimeContract.cmake",
            "cmake/linux/StageLinuxRuntime.cmake",
            "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
            "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
            "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
            "tools/linux/RunRoR-combined",
        ):
            self.assertIn(f'"{source}"', PROVIDER)

    def test_linux_combined_workflow_builds_installs_and_smokes_one_target(self) -> None:
        for token in (
            "-DROR_OGRE_NEXT_COMBINED_RUNTIME=ON",
            "-DROR_RENDERER_PUBLIC_LAUNCHER=OFF",
            "-DROR_OGRE_NEXT_PRODUCTION_PACKAGE=OFF",
            "-DROR_OGRE_NEXT_DEMO_ADMISSION=OFF",
            "--target ror_ogre_next_combined_verified",
            'document.get("qualification_eligible") is not True',
            'cmake --install "$ROR_COMBINED_BUILD_DIR"',
            'ROR_COMBINED_BUILD_DIR=$RUNNER_TEMP/ror-combined-build',
            'ROR_COMBINED_STAGE_DIR=$RUNNER_TEMP/ror-combined-stage',
            'CONAN_HOME=$RUNNER_TEMP/ror-combined-conan-home',
            'test -x "$stage/RoR-Combined"',
            'test -x "$stage/RunRoR"',
            'test ! -e "$stage/RoR-Ogre14"',
            "ror.ogre_next_combined_elf_closure.v1",
            "ror.ogre_next_combined_linux_package.v1",
            '"renderer": "ogre-next"',
            '"legacy_visible_presentation": False',
            '"playability_qualified": False',
            "--native-visual-showcase",
            '"renderer": "ogre-next-combined"',
            '"presents_frames": True',
            "if: success()",
        ):
            with self.subTest(token=token):
                self.assertIn(token, COMBINED_WORKFLOW)
        self.assertNotIn("--native-visual-showcase-a0", COMBINED_WORKFLOW)
        self.assertNotIn("ROR_OGRE_NEXT_ALLOW_DIRTY_DEVELOPMENT_BUILD", COMBINED_WORKFLOW)

    def test_linux_elf_parsers_fail_closed_on_duplicate_or_missing_evidence(
        self,
    ) -> None:
        tools_path = str(ROOT / "tools")
        inserted = tools_path not in sys.path
        if inserted:
            sys.path.insert(0, tools_path)
        try:
            specification = importlib.util.spec_from_file_location(
                "combined_elf_verifier", ELF_VERIFIER_PATH
            )
            self.assertIsNotNone(specification)
            self.assertIsNotNone(specification.loader)
            module = importlib.util.module_from_spec(specification)
            specification.loader.exec_module(module)
        finally:
            if inserted:
                sys.path.remove(tools_path)

        dynamic = (
            " 0x0000000000000001 (NEEDED)             "
            "Shared library: [libOgreMain.so.14.5]\n"
        )
        self.assertEqual(
            module._needed_names(dynamic), ["libOgreMain.so.14.5"]
        )
        with self.assertRaisesRegex(ValueError, "duplicate"):
            module._needed_names(dynamic + dynamic)
        build_root = Path("/build")
        required = build_root / "lib/OgreNextMainStatic.a"
        self.assertEqual(
            module._required_archive_evidence(
                "lib/OgreNextMainStatic.a(member.cpp.o)",
                [required],
                build_root,
            ),
            {str(required): 1},
        )
        with self.assertRaisesRegex(ValueError, "lacks required"):
            module._required_archive_evidence("", [required], build_root)
        with self.assertRaisesRegex(ValueError, "escaped the build root"):
            module._required_archive_evidence(
                "outside.a(member.cpp.o)",
                [Path("/outside.a")],
                build_root,
            )

    def test_explicit_combined_build_stages_the_complete_game_resources(self) -> None:
        resource_targets = block(
            MAIN_CMAKE,
            "# An explicit RoR-Combined verification build must produce",
            "if (APPLE AND ROR_OGRE14 AND NOT ROR_OGRE_NEXT_COMBINED_RUNTIME)",
        )
        self.assertIn("if (ROR_OGRE_NEXT_COMBINED_RUNTIME)", resource_targets)
        for target in (
            "zip_folder_resources",
            "fast_copy_managed_materials",
            "fast_copy_fonts",
            "fast_copy_languages",
        ):
            self.assertIn(target, resource_targets)
        self.assertRegex(
            resource_targets,
            r"if \(ROR_CREATE_CONTENT_FOLDER\)\s*"
            r"add_dependencies\(\$\{BINNAME\} zip_folder_content\)",
        )

    def test_binary_proof_rejects_hostile_strict_fp_receipts(self) -> None:
        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier_strict_fp", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)

        provider = {
            "ogre_next_upstream_strict_fp": True,
            "ogre_next_upstream_strict_fp_target_count": 8,
        }
        namespace = {
            "upstream_strict_fp_required": True,
            "upstream_compile_entries": 127,
            "upstream_strict_fp_compile_entries": 127,
        }
        self.assertEqual(
            module._verify_strict_fp_receipts(provider, namespace),
            {
                "provider_required": True,
                "provider_target_count": 8,
                "upstream_compile_entries": 127,
                "strict_fp_compile_entries": 127,
            },
        )

        provider_flag_cases = (
            ("missing", {}),
            ("false", {"ogre_next_upstream_strict_fp": False}),
            ("integer true", {"ogre_next_upstream_strict_fp": 1}),
        )
        for label, replacement in provider_flag_cases:
            hostile = dict(provider)
            hostile.pop("ogre_next_upstream_strict_fp")
            hostile.update(replacement)
            with self.subTest(provider_flag=label):
                with self.assertRaisesRegex(ValueError, "does not require"):
                    module._verify_strict_fp_receipts(hostile, namespace)

        for hostile_count in (None, 0, -1, True, 1.0, "1"):
            hostile = dict(provider)
            hostile["ogre_next_upstream_strict_fp_target_count"] = hostile_count
            with self.subTest(provider_target_count=hostile_count):
                with self.assertRaisesRegex(ValueError, "target count"):
                    module._verify_strict_fp_receipts(hostile, namespace)

        namespace_flag_cases = (
            ("missing", {}),
            ("false", {"upstream_strict_fp_required": False}),
            ("integer true", {"upstream_strict_fp_required": 1}),
        )
        for label, replacement in namespace_flag_cases:
            hostile = dict(namespace)
            hostile.pop("upstream_strict_fp_required")
            hostile.update(replacement)
            with self.subTest(namespace_flag=label):
                with self.assertRaisesRegex(ValueError, "did not require"):
                    module._verify_strict_fp_receipts(provider, hostile)

        for hostile_count in (None, 0, -1, True, 1.0, "127"):
            hostile = dict(namespace)
            hostile["upstream_compile_entries"] = hostile_count
            with self.subTest(upstream_compile_entries=hostile_count):
                with self.assertRaisesRegex(ValueError, "compile-entry count"):
                    module._verify_strict_fp_receipts(provider, hostile)

        for hostile_count in (None, 0, 126, 128, True, 127.0, "127"):
            hostile = dict(namespace)
            hostile["upstream_strict_fp_compile_entries"] = hostile_count
            with self.subTest(strict_fp_compile_entries=hostile_count):
                with self.assertRaisesRegex(ValueError, "do not cover every"):
                    module._verify_strict_fp_receipts(provider, hostile)

    def test_authenticated_source_decoder_is_exactly_receipt_bound(self) -> None:
        for source in (
            "cmake/VerifyStbImageSource.cmake",
            "tests/gfx/render/Ogre14SourceTextureDecoderTests.cpp",
            "tests/tools/test_ogre14_source_texture_decoder_contract.py",
        ):
            self.assertIn(f'"{source}"', PROVIDER)
        for token in (
            '"authenticated_source_image_decoder": {',
            '"schema": "@ROR_STB_IMAGE_SOURCE_SCHEMA@"',
            '"upstream_commit": "@ROR_STB_IMAGE_UPSTREAM_COMMIT@"',
            '"macro_contract_verified": '
            "@ROR_STB_IMAGE_MACRO_CONTRACT_VERIFIED_JSON@",
            '"path": "@ROR_STB_IMAGE_HEADER_PATH@"',
            '"sha256": "@ROR_STB_IMAGE_HEADER_SHA256@"',
            '"size": @ROR_STB_IMAGE_LICENSE_SIZE@',
        ):
            self.assertIn(token, PROVIDER_CONTRACT)
        self.assertIn(
            "_verify_authenticated_source_image_decoder", VERIFIER
        )
        self.assertIn(
            '"authenticated_source_image_decoder": (', VERIFIER
        )

        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier_stb_image", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)

        source_records: dict[str, dict[str, object]] = {}
        manifest_lines = []
        for name, expected in module.STB_IMAGE_SOURCE_FILES.items():
            relative_path = expected["relative_path"]
            path = ROOT.joinpath(*Path(relative_path).parts)
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            record: dict[str, object] = {
                "relative_path": relative_path,
                "path": str(path),
                "sha256": digest,
            }
            if "size" in expected:
                record["size"] = path.stat().st_size
            source_records[name] = record
            manifest_lines.append(
                f"{relative_path}|{path.stat().st_size}|{digest}"
            )

        provider = {
            "authenticated_source_image_decoder": {
                "schema": module.STB_IMAGE_SOURCE_SCHEMA,
                "upstream_commit": module.STB_IMAGE_UPSTREAM_COMMIT,
                "macro_contract_verified": True,
                **source_records,
            }
        }
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "provider-source-manifest.txt"
            manifest.write_text(
                "\n".join(sorted(manifest_lines)) + "\n", encoding="utf-8"
            )
            report = module._verify_authenticated_source_image_decoder(
                provider, manifest, ROOT
            )
            self.assertEqual(report["schema"], module.STB_IMAGE_SOURCE_SCHEMA)
            self.assertTrue(report["macro_contract_verified"])
            self.assertEqual(set(report["files"]), set(source_records))
            self.assertTrue(
                all(
                    item["provider_source_manifest_attested"] is True
                    for item in report["files"].values()
                )
            )

            hostile = json.loads(json.dumps(provider))
            hostile["authenticated_source_image_decoder"][
                "macro_contract_verified"
            ] = 1
            with self.assertRaisesRegex(ValueError, "identity changed"):
                module._verify_authenticated_source_image_decoder(
                    hostile, manifest, ROOT
                )

            hostile = json.loads(json.dumps(provider))
            hostile["authenticated_source_image_decoder"]["header"][
                "sha256"
            ] = "0" * 64
            with self.assertRaisesRegex(ValueError, "contract digest changed"):
                module._verify_authenticated_source_image_decoder(
                    hostile, manifest, ROOT
                )

            manifest.write_text(
                "\n".join(
                    line
                    for line in sorted(manifest_lines)
                    if "stb_image.h" not in line
                )
                + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "not attested exactly once"):
                module._verify_authenticated_source_image_decoder(
                    provider, manifest, ROOT
                )

    def test_overlay_is_production_linked_and_audited(self) -> None:
        production_n1_link = block(
            PROVIDER,
            "target_link_libraries(ror_ogre_next_embedded_n1_runtime",
            "_ror_combined_strict_target(ror_ogre_next_embedded_n1_runtime)",
        )
        final_binary_proof = block(
            MAIN_CMAKE,
            "add_custom_target(ror_ogre_next_combined_verified ALL",
            'COMMENT "Proving RoR-Combined excludes renderer bridge/transport symbols"',
        )
        namespace_audit = block(
            PROVIDER,
            "add_custom_target(ror_ogre_next_root_namespace_audit",
            'COMMENT "Auditing full root OGRE14/OgreNext namespace collision closure"',
        )
        self.assertIn("OgreNextOverlay", production_n1_link)
        self.assertIn(
            '--required-archive "$<TARGET_FILE:OgreNextOverlay>"',
            final_binary_proof,
        )
        self.assertIn("OgreNextOverlay", final_binary_proof)
        self.assertIn(
            "set(_ror_namespace_audit_next_archive_targets", PROVIDER
        )
        self.assertIn("OgreNextOverlay", PROVIDER)
        self.assertIn(
            '--next-archive "$<TARGET_FILE:${_ror_namespace_audit_target}>"',
            PROVIDER,
        )
        self.assertIn(
            "${_ror_namespace_audit_next_archive_arguments}", namespace_audit
        )

    def test_dirty_development_build_is_explicit_and_unqualified(self) -> None:
        self.assertIn(
            "ROR_OGRE_NEXT_ALLOW_DIRTY_DEVELOPMENT_BUILD", PROVIDER
        )
        self.assertIn("--allow-dirty-source", PROVIDER)
        self.assertIn('"source_checkout": source_checkout', VERIFIER)
        self.assertIn(
            '"qualification_eligible": source_checkout[', VERIFIER
        )

    def test_link_map_evidence_preserves_arbitrary_literal_bytes(self) -> None:
        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier_link_map", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)

        payload = (
            b"[1] literal string: \x80\n"
            b"/build/libOgreNextMainStatic.a(OgreRoot.cpp.o)\n"
            b"RendererBridgeChannel.cpp.o\n"
        )
        self.assertTrue(
            module._link_map_contains(payload, "libOgreNextMainStatic.a")
        )
        self.assertTrue(
            module._link_map_contains(payload, "RendererBridgeChannel.cpp.o")
        )
        self.assertFalse(module._link_map_contains(payload, "libSDL2.a("))
        self.assertFalse(module._link_map_contains(payload, "libpng.a("))
        self.assertIn("link_map.read_bytes()", VERIFIER)
        self.assertNotIn("link_map.read_text", VERIFIER)

    def test_binary_proof_rejects_external_or_static_image_codec_leakage(
        self,
    ) -> None:
        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier_image_codecs", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)

        symbols = (
            "0000000100001000 T _png_create_read_struct\n"
            "0000000100002000 T _jpeg_std_error\n"
            "0000000100003000 T _stbi_load_from_memory\n"
            "0000000100004000 T _unrelated_symbol\n"
        )
        self.assertEqual(
            module._external_image_codec_symbol_violations(symbols),
            ["_jpeg_std_error", "_png_create_read_struct", "_stbi_load_from_memory"],
        )

        hostile_map = (
            b"/locked/lib/libpng16.a(pngread.o)\n"
            b"/locked/lib/libjpeg-static.a(jdapimin.o)\n"
        )
        extracted = sorted(
            token
            for token in module.FORBIDDEN_ROOT_IMAGE_CODEC_ARCHIVE_TOKENS
            if module._link_map_contains(hostile_map, token)
        )
        self.assertEqual(extracted, ["libjpeg-static.a(", "libpng16.a("])

        for token in (
            "RoR::Render::DecodeOgre14SourceTexture(",
            "RoR::Gfx::Detail::OgreNextDemoMaterialSource::",
            '"external_image_codec_symbols_present": False',
            '"root_image_codec_static_archive_members_extracted": False',
            '"authenticated_source_texture_decoder_present": True',
        ):
            self.assertIn(token, VERIFIER)

    def test_binary_proof_parses_exact_symbols_and_complete_codec_intersection(
        self,
    ) -> None:
        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier_exact_symbols", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)

        decoder_token = "RoR::Render::DecodeOgre14SourceTexture("
        fake = (
            "0000000100001000 t "
            "FakeRoR::Render::DecodeOgre14SourceTexture(int)\n"
        )
        self.assertIn(
            decoder_token, module._missing_required_demangled_symbols(fake)
        )
        exact = (
            "0000000100001000 t "
            "RoR::Render::DecodeOgre14SourceTexture(int)\n"
        )
        self.assertNotIn(
            decoder_token, module._missing_required_demangled_symbols(exact)
        )

        intersection, unexpected = module._unexpected_symbol_intersection(
            {"_jcopy_sample_rows", "_compress"},
            {"_jcopy_sample_rows", "_compress"},
            module.REVIEWED_CODEC_FREEIMAGE_DEFINED_INTERSECTION_ALLOWLIST,
        )
        self.assertEqual(intersection, ["_compress", "_jcopy_sample_rows"])
        self.assertEqual(unexpected, ["_jcopy_sample_rows"])
        undefined = module._nm_undefined_symbol_names(
            "_jcopy_sample_rows\n_UndefinedOther\n"
        )
        self.assertEqual(
            undefined, {"_jcopy_sample_rows", "_UndefinedOther"}
        )
        undefined_intersection, undefined_unexpected = (
            module._unexpected_symbol_intersection(
                undefined | {"_crc32"},
                {"_jcopy_sample_rows", "_crc32"},
                module.REVIEWED_CODEC_FREEIMAGE_UNDEFINED_INTERSECTION_ALLOWLIST,
            )
        )
        self.assertEqual(
            undefined_intersection, ["_crc32", "_jcopy_sample_rows"]
        )
        self.assertEqual(undefined_unexpected, ["_jcopy_sample_rows"])
        self.assertEqual(
            module._sdl_definition_symbols({"_SDL_Init", "_unrelated"}),
            ["_SDL_Init"],
        )

    def test_ogre14_runtime_manifest_rehashes_exact_configured_dylib_set(
        self,
    ) -> None:
        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier_ogre14_manifest", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            (package / "lib/OGRE").mkdir(parents=True)
            paths = (
                package / "lib/libOgreMain.14.5.dylib",
                package / "lib/libOgreBites.14.5.dylib",
                package / "lib/OGRE/Codec_FreeImage.14.5.dylib",
            )
            for index, path in enumerate(paths):
                path.write_bytes(f"runtime-{index}".encode("ascii"))
            records = [
                {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                for path in paths
            ]
            serialized = "".join(
                f"{path.relative_to(package).as_posix()}|{path.stat().st_size}|"
                f"{hashlib.sha256(path.read_bytes()).hexdigest()}\n"
                for path in sorted(paths)
            )
            contract = {
                "ogre14_runtime_package_root": str(package),
                "ogre14_runtime_library_count": len(paths),
                "ogre14_runtime_manifest_sha256": hashlib.sha256(
                    serialized.encode("utf-8")
                ).hexdigest(),
                "ogre14_main_runtime": str(paths[0]),
                "ogre14_sdl_provider_runtime": str(paths[1]),
            }
            report = module._verify_ogre14_runtime_manifest(contract, records)
            self.assertEqual(report["library_count"], 3)
            self.assertEqual(
                report["codec_freeimage"], str(paths[2].resolve(strict=True))
            )

            paths[2].write_bytes(b"post-config replacement")
            with self.assertRaisesRegex(ValueError, "changed after namespace audit"):
                module._verify_ogre14_runtime_manifest(contract, records)

    def test_structural_link_map_requires_exact_archives_and_one_stb_owner(
        self,
    ) -> None:
        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier_structural_map", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "build"
            binary = build / "bin/RoR-Combined"
            archive = build / "bin/librequired.a"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"binary")
            archive.write_bytes(b"archive")
            decoder_object = module.COMBINED_STB_DECODER_OBJECT
            decoder_symbols = {"__ZL15stbi__load_mainP"}

            def payload(
                archive_row: str,
                extra_owner: bool = False,
                dead_extra_owner: bool = False,
                root_sdl_symbol: bool = False,
                sdl_import_stub: bool = False,
                inject_second_header: bool = False,
                retired_object_row=None,
                unrelated_stbi_type_symbol: bool = False,
            ) -> bytes:
                rows = [
                    "# Path: bin/RoR-Combined",
                    "# Object files:",
                    "[  0] linker synthesized",
                    f"[  1] {archive_row}",
                    f"[  2] {decoder_object}",
                ]
                if extra_owner or dead_extra_owner:
                    rows.append("[  3] generated/private_stb_owner.cpp.o")
                if sdl_import_stub:
                    rows.append("[  4] /authenticated/libOgreBites.dylib")
                if retired_object_row is not None:
                    rows.append(f"[  5] {retired_object_row}")
                if unrelated_stbi_type_symbol:
                    rows.append("[  6] generated/unrelated_helper.cpp.o")
                rows.extend(
                    [
                        "# Sections:",
                        "# Symbols:",
                        "0x1000 0x10 [  2] __ZL15stbi__load_mainP",
                        (
                            "0x1008 0x08 [  2] "
                            "l___const._ZL22stbi__create_png_imageP9stbi__pngPhjiiii.xorig"
                        ),
                    ]
                )
                if extra_owner:
                    rows.append("0x1010 0x10 [  3] _stbi_image_free")
                if root_sdl_symbol:
                    rows.append("0x1020 0x10 [  2] _SDL_Init")
                if sdl_import_stub:
                    rows.append("0x1028 0x10 [  4] _SDL_Init.stub")
                if unrelated_stbi_type_symbol:
                    rows.append(
                        "0x102C 0x10 [  6] "
                        "__ZL11other_ownerP13stbi__context"
                    )
                if dead_extra_owner:
                    rows.extend(
                        [
                            "# Dead Stripped Symbols:",
                            "<<dead>> 0x10 [  3] _stbi_image_free",
                        ]
                    )
                if inject_second_header:
                    rows.extend(
                        [
                            "# Object files:",
                            "[  4] bin/librequired.a(fake.o)",
                            "# Sections:",
                            "# Symbols:",
                            "0x1030 0x10 [  4] _stbi_image_free",
                        ]
                    )
                return ("\n".join(rows) + "\n").encode("utf-8")

            report = module._structural_link_map_evidence(
                payload("bin/librequired.a(member.o)"),
                binary,
                build,
                [archive],
                decoder_symbols,
            )
            self.assertEqual(report["missing_required_archives"], [])
            self.assertEqual(report["stb_image_symbol_count"], 1)
            self.assertTrue(
                module._is_private_stbi_link_map_symbol(b"_stbi_image_free")
            )
            self.assertTrue(
                module._is_private_stbi_link_map_symbol(
                    b"__ZL15stbi__load_mainP"
                )
            )
            self.assertTrue(
                module._is_private_stbi_link_map_symbol(
                    b"__ZZL22stbi__check_png_headerP13stbi__contextE7png_sig"
                )
            )
            self.assertFalse(
                module._is_private_stbi_link_map_symbol(
                    b"l___const._ZL22stbi__create_png_imageP9stbi__png.xorig"
                )
            )
            self.assertFalse(
                module._is_private_stbi_link_map_symbol(
                    b"__ZL11other_ownerP13stbi__context"
                )
            )
            unrelated_report = module._structural_link_map_evidence(
                payload(
                    "bin/librequired.a(member.o)",
                    unrelated_stbi_type_symbol=True,
                ),
                binary,
                build,
                [archive],
                decoder_symbols | {"__ZL11other_ownerP13stbi__context"},
            )
            self.assertEqual(unrelated_report["stb_image_symbol_count"], 1)
            self.assertEqual(
                unrelated_report["stb_image_owner_objects"],
                [str((build / decoder_object).resolve(strict=False))],
            )
            bracket_report = module._structural_link_map_evidence(
                payload("bin/librequired.a[member.o]"),
                binary,
                build,
                [archive],
                decoder_symbols,
            )
            self.assertEqual(bracket_report["missing_required_archives"], [])

            decoy = build / "decoy/librequired.a"
            decoy.parent.mkdir()
            decoy.write_bytes(b"decoy")
            decoy_report = module._structural_link_map_evidence(
                payload("decoy/librequired.a(member.o)"),
                binary,
                build,
                [archive],
                decoder_symbols,
            )
            self.assertEqual(
                decoy_report["missing_required_archives"],
                [str(archive.resolve(strict=True))],
            )
            with self.assertRaisesRegex(ValueError, "unreviewed object owner"):
                module._structural_link_map_evidence(
                    payload("bin/librequired.a(member.o)", extra_owner=True),
                    binary,
                    build,
                    [archive],
                    decoder_symbols | {"_stbi_image_free"},
                )
            with self.assertRaisesRegex(ValueError, "unreviewed object owner"):
                module._structural_link_map_evidence(
                    payload(
                        "bin/librequired.a(member.o)",
                        dead_extra_owner=True,
                    ),
                    binary,
                    build,
                    [archive],
                    decoder_symbols,
                )
            root_sdl_report = module._structural_link_map_evidence(
                payload(
                    "bin/librequired.a(member.o)", root_sdl_symbol=True
                ),
                binary,
                build,
                [archive],
                decoder_symbols,
            )
            self.assertEqual(
                root_sdl_report["root_sdl_defined_symbols"], ["_SDL_Init"]
            )
            import_stub_report = module._structural_link_map_evidence(
                payload(
                    "bin/librequired.a(member.o)", sdl_import_stub=True
                ),
                binary,
                build,
                [archive],
                decoder_symbols,
            )
            self.assertEqual(
                import_stub_report["root_sdl_defined_symbols"], []
            )
            for retired_row in (
                "bin/libretired.a(RendererBridgeChannel.cpp.o)",
                "bin/libretired.a[RendererBridgeChannel.cpp.o]",
            ):
                retired_report = module._structural_link_map_evidence(
                    payload(
                        "bin/librequired.a(member.o)",
                        retired_object_row=retired_row,
                    ),
                    binary,
                    build,
                    [archive],
                    decoder_symbols,
                )
                self.assertEqual(
                    retired_report["forbidden_objects"],
                    ["RendererBridgeChannel.cpp.o"],
                )
            with self.assertRaisesRegex(
                ValueError, "duplicate or out-of-order Object files"
            ):
                module._structural_link_map_evidence(
                    payload(
                        "bin/librequired.a(member.o)",
                        inject_second_header=True,
                    ),
                    binary,
                    build,
                    [archive],
                    decoder_symbols | {"_stbi_image_free"},
                )
            forged_symbol_payload = payload(
                "bin/librequired.a(member.o)"
            ) + b"0x1040 0x10 [  2] _stbi_image_free\n"
            with self.assertRaisesRegex(
                ValueError, "absent from defined nm"
            ):
                module._structural_link_map_evidence(
                    forged_symbol_payload,
                    binary,
                    build,
                    [archive],
                    decoder_symbols,
                )

            absolute_decoder = build / module.COMBINED_STB_DECODER_OBJECT
            absolute_payload = (
                f"# Path: {binary}\n"
                "# Object files:\n"
                "[  0] linker synthesized\n"
                f"[  1] {archive}(member.o)\n"
                f"[  2] {absolute_decoder}\n"
                "# Sections:\n"
                "# Symbols:\n"
                "0x1000 0x10 [  2] __ZL15stbi__load_mainP\n"
            ).encode("utf-8")
            absolute_report = module._structural_link_map_evidence(
                absolute_payload,
                binary,
                build,
                [archive],
                decoder_symbols,
            )
            self.assertEqual(absolute_report["missing_required_archives"], [])

    def test_dynamic_load_gate_resolves_only_exact_authenticated_ogre14_paths(
        self,
    ) -> None:
        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier_dynamic_loads", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package_lib = root / "package/lib"
            package_lib.mkdir(parents=True)
            binary = root / "build/bin/RoR-Combined"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"binary")
            names = [
                f"{prefix}14.5.dylib"
                for prefix in module.EXPECTED_OGRE14_DIRECT_LOAD_PREFIXES
            ]
            names.append("Codec_FreeImage.14.5.dylib")
            libraries = []
            for name in names:
                path = package_lib / name
                path.write_bytes(name.encode("ascii"))
                libraries.append({"path": str(path)})
            manifest = {"libraries": libraries}
            loads = "binary:\n" + "".join(
                f"\t@rpath/{name} (compatibility version 1.0.0)\n"
                for name in names[:-1]
            )
            commands = (
                "Load command 1\n"
                "          cmd LC_RPATH\n"
                "      cmdsize 64\n"
                f"         path {package_lib} (offset 12)\n"
            )
            report = module._direct_dynamic_load_evidence(
                loads, commands, binary, manifest
            )
            self.assertEqual(report["unexpected_non_system"], [])
            self.assertEqual(report["resolution_failures"], [])

            extra_codec_loads = loads + (
                "\t@rpath/Codec_FreeImage.14.5.dylib "
                "(compatibility version 1.0.0)\n"
            )
            extra_report = module._direct_dynamic_load_evidence(
                extra_codec_loads, commands, binary, manifest
            )
            self.assertEqual(
                extra_report["unexpected_non_system"],
                ["@rpath/Codec_FreeImage.14.5.dylib"],
            )

            hostile_lib = root / "hostile"
            hostile_lib.mkdir()
            (hostile_lib / names[0]).write_bytes(b"renamed hostile codec")
            ambiguous_commands = (
                "Load command 1\n"
                "          cmd LC_RPATH\n"
                "      cmdsize 64\n"
                f"         path {hostile_lib} (offset 12)\n"
                "Load command 2\n"
                "          cmd LC_RPATH\n"
                "      cmdsize 64\n"
                f"         path {package_lib} (offset 12)\n"
            )
            ambiguous = module._direct_dynamic_load_evidence(
                loads, ambiguous_commands, binary, manifest
            )
            self.assertIn(f"@rpath/{names[0]}", ambiguous["resolution_failures"])

    def test_manifests_bind_build_authority_and_rehash_at_postlink(self) -> None:
        for path in (
            '"CMakeLists.txt"',
            '"cmake/DependenciesConfig.cmake"',
            '"conanfile.py"',
            '"source/main/CMakeLists.txt"',
            '"source/main/main.cpp"',
            '".github/workflows/ogre-next-probe.yml"',
            '"tests/tools/test_ogre_next_combined_provider_contract.py"',
        ):
            self.assertIn(path, PROVIDER)
        self.assertIn("RoR-Combined.selected-sources.txt", MAIN_CMAKE)
        self.assertIn("file(SHA256", MAIN_CMAKE)
        self.assertIn("_verify_source_manifest(", VERIFIER)

        specification = importlib.util.spec_from_file_location(
            "combined_binary_verifier", VERIFIER_PATH
        )
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temporary:
            source_root = Path(temporary) / "source"
            source_root.mkdir()
            source = source_root / "unit.cpp"
            source.write_bytes(b"one\n")
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            manifest = Path(temporary) / "manifest.txt"
            manifest.write_text(
                f"unit.cpp|{source.stat().st_size}|{digest}\n", encoding="utf-8"
            )
            manifest_digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
            report = module._verify_source_manifest(
                manifest, source_root, manifest_digest, 1, "unit manifest"
            )
            self.assertTrue(report["build_time_rehash"])
            source.write_bytes(b"two\n")
            with self.assertRaisesRegex(ValueError, "digest changed"):
                module._verify_source_manifest(
                    manifest, source_root, manifest_digest, 1, "unit manifest"
                )

    def test_neutral_digest_preserves_transport_api(self) -> None:
        digest_header = (
            ROOT / "source/main/gfx/render/RenderPayloadDigest.h"
        ).read_text(encoding="utf-8")
        envelope_header = (
            ROOT / "source/main/gfx/render/RenderTransportEnvelope.h"
        ).read_text(encoding="utf-8")
        legacy_authority = (
            ROOT
            / "source/main/gfx/ogre14/Ogre14LegacyNativeMaterialCaptureAuthority.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("ComputeRenderPayloadDigest", digest_header)
        self.assertIn('#include "RenderPayloadDigest.h"', envelope_header)
        self.assertIn("ComputeRenderTransportPayloadDigest", envelope_header)
        self.assertIn("return ComputeRenderPayloadDigest", envelope_header)
        self.assertIn('#include "gfx/render/RenderPayloadDigest.h"', legacy_authority)
        self.assertNotIn("RenderTransportEnvelope.h", legacy_authority)

    def test_workflow_runs_this_contract_with_and_without_assertions(self) -> None:
        path = "tests/tools/test_ogre_next_combined_provider_contract.py"
        self.assertEqual(WORKFLOW.count(f"python {path}"), 1)
        self.assertEqual(WORKFLOW.count(f"python -O {path}"), 1)
        for trigger in (
            "cmake/OgreNextEmbeddedRuntime.cmake",
            "cmake/ogre_next_embedded/**",
            "tools/stage_ogre_next_combined_resources.py",
            "tools/verify_ogre_next_combined_binary_closure.py",
            "tools/verify_ogre_next_combined_elf_closure.py",
        ):
            self.assertEqual(WORKFLOW.count(f"- {trigger}"), 2)


if __name__ == "__main__":
    unittest.main()
