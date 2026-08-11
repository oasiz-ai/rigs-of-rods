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
WORKFLOW = (ROOT / ".github/workflows/ogre-next-probe.yml").read_text(
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
            "NOT APPLE",
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
            "ror-ogre14-macos-arm64-release.lock",
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
        self.assertIn("libSDL2.a(", VERIFIER)
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
            "_ror_rapidjson_private_usage_count EQUAL 1",
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
        for invocation in (
            "ror_ogre_next_embedded_n1_runtime LANGUAGES CXX OBJCXX",
            "ror_ogre_next_embedded_sdl_window_runtime LANGUAGES OBJCXX",
            "ror_ogre_next_in_process_presenter LANGUAGES CXX",
            "ror_ogre_next_root_next_adapter LANGUAGES CXX",
            "ror_ogre_next_root_plugin_export_probe LANGUAGES OBJCXX",
        ):
            self.assertIn(invocation, PROVIDER)

    def test_media_paths_and_stage_are_build_tree_authority(self) -> None:
        for macro in (
            "ROR_OGRE_NEXT_COMBINED_SHADER_MEDIA_ROOT",
            "ROR_OGRE_NEXT_COMBINED_PRESENTATION_MEDIA_ROOT",
        ):
            self.assertIn(macro, PROVIDER)
        self.assertIn("ror-ogre-next-combined-resources", PROVIDER)
        self.assertIn("stage_ogre_next_combined_resources.py", PROVIDER)
        self.assertIn('"${_ror_stage_relative}!|${_ror_source}', PROVIDER)
        self.assertIn('string(REGEX REPLACE "!$"', PROVIDER)
        self.assertIn("ror_ogre_next_combined_resources", MAIN_CMAKE)
        self.assertIn('"raw_build_tree_demo": true', PROVIDER_CONTRACT)
        self.assertIn('"app_bundle_staged": false', PROVIDER_CONTRACT)

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
            "missing_archive_evidence",
            "extracted_sdl_members",
            "verified_audited_archives",
            "verified_audited_legacy",
            "isolated_consumers",
            "REVIEWED_GLOBAL_INTERSECTION_ALLOWLIST",
        ):
            self.assertIn(evidence, VERIFIER)
        self.assertIn("if receipt.exists() or receipt.is_symlink():", VERIFIER)
        self.assertIn('"provider_contract":', EXECUTABLE_CONTRACT)
        self.assertIn('"namespace_audit_report":', EXECUTABLE_CONTRACT)

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
        self.assertIn("link_map.read_bytes()", VERIFIER)
        self.assertNotIn("link_map.read_text", VERIFIER)

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
        ):
            self.assertEqual(WORKFLOW.count(f"- {trigger}"), 2)


if __name__ == "__main__":
    unittest.main()
