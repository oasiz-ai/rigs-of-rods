#!/usr/bin/env python3
"""Hostile offline contract tests for the private OgreNext namespace fork."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"


def load_module(name: str, relative: str):
    path = REPOSITORY_ROOT / relative
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


AUDIT = load_module(
    "ror_audit_embedded_namespace",
    "tools/ogre_next_probe/audit_embedded_namespace.py",
)
RUNNER = load_module("ror_run_ogre_next_probe", "tools/run_ogre_next_probe.py")
VERIFIER = load_module(
    "ror_verify_ogre_next_artifact_set",
    "tools/verify_ogre_next_artifact_set.py",
)


EMBEDDED_PROVENANCE_PATHS = (
    "tools/ogre_next_probe/audit_embedded_namespace.py",
    "tools/ogre_next_probe/embedded_namespace/RoROgreNextNamespaceRemap.h",
    "tools/ogre_next_probe/patches/0006-embedded-namespace-plugin-symbols.patch",
    "tools/ogre_next_probe/src/embedded_namespace/main.cpp",
    "tools/ogre_next_probe/src/embedded_namespace/metal_plugin_export_probe.mm",
    "tools/ogre_next_probe/src/embedded_namespace/n1_session_adapter.cpp",
    "tools/ogre_next_probe/src/embedded_namespace/presenter_link_adapter.cpp",
    "tools/ogre_next_probe/src/embedded_namespace/next_adapter.cpp",
    "tools/ogre_next_probe/src/embedded_namespace/ogre14_adapter.cpp",
)


class EmbeddedNamespaceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock = json.loads(
            (PROBE_ROOT / "ogre-next.lock.json").read_text(encoding="utf-8")
        )
        cls.cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.pinned = (PROBE_ROOT / "cmake/PinnedOgreNext.cmake").read_text(
            encoding="utf-8"
        )
        cls.template = (PROBE_ROOT / "ogre_next_build_contract.json.in").read_text(
            encoding="utf-8"
        )
        cls.audit_source = (PROBE_ROOT / "audit_embedded_namespace.py").read_text(
            encoding="utf-8"
        )
        cls.n2_prelink = (
            PROBE_ROOT / "cmake/VerifyN2SourceProvenance.cmake"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")

    def test_stb_implementation_owner_pattern_accepts_lf_and_crlf(self) -> None:
        for source in (
            b"#define STB_IMAGE_IMPLEMENTATION\n",
            b"#define STB_IMAGE_IMPLEMENTATION\r\n",
            b"  #  define\tSTB_IMAGE_IMPLEMENTATION  \n",
            b"  #  define\tSTB_IMAGE_IMPLEMENTATION  \r\n",
        ):
            with self.subTest(source=source):
                self.assertEqual(
                    AUDIT.count_stb_implementation_definitions(source), 1
                )
        for source in (
            b"// #define STB_IMAGE_IMPLEMENTATION\n",
            b"#define STB_IMAGE_IMPLEMENTATION_EXTRA\n",
            b"#define STB_IMAGE_IMPLEMENTATION 1\n",
        ):
            with self.subTest(source=source):
                self.assertEqual(
                    AUDIT.count_stb_implementation_definitions(source), 0
                )

    def test_compile_target_paths_are_separator_neutral(self) -> None:
        windows = {
            "command": (
                r"cl /Fosource\main\CMakeFiles\RoR-Combined.dir\decoder.obj"
            )
        }
        self.assertIn(
            "CMakeFiles/RoR-Combined.dir/",
            AUDIT.normalized_command_path_text(windows),
        )
        remap = Path(r"D:\a\repo\RoROgreNextNamespaceRemap.h")
        self.assertTrue(
            AUDIT.command_contains_path(
                r"cl /FID:/a/repo/RoROgreNextNamespaceRemap.h source.cpp",
                remap,
            )
        )

    def test_canonical_lock_binds_conditional_fork_inputs(self) -> None:
        self.assertEqual(self.lock["schema_version"], 6)
        embedded = self.lock["embedded_namespace"]
        self.assertEqual(embedded["namespace"], "RoROgreNext")
        self.assertEqual(
            embedded["cmake_option"], "ROR_OGRE_NEXT_EMBEDDED_NAMESPACE"
        )
        self.assertIs(embedded["default_enabled"], False)
        for key in ("patch", "remap_header"):
            entry = embedded[key]
            path = PROBE_ROOT / entry["path"]
            self.assertTrue(path.is_file())
            self.assertFalse(path.is_symlink())
            self.assertEqual(
                hashlib.sha256(path.read_bytes()).hexdigest(), entry["sha256"]
            )
        remap = (
            PROBE_ROOT / embedded["remap_header"]["path"]
        ).read_text(encoding="utf-8")
        self.assertIn(
            "#define RAPIDJSON_NAMESPACE RoROgreNextRapidJson", remap
        )
        self.assertIn("#define rapidjson RoROgreNextRapidJson", remap)

    def test_build_contract_binds_mode_and_does_not_claim_full_n1_link(self) -> None:
        self.assertIn('"schema_version": 7', self.template)
        for token in (
            '"enabled": @ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_ENABLED_JSON@',
            '"namespace": "@ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_NAME@"',
            '"path": "@ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_PATH@"',
            '"sha256": "@ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_SHA256@"',
            '"path": "@ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP_PATH@"',
            '"sha256": "@ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP_SHA256@"',
            '"path": "@ROR_OGRE_NEXT_VULKAN_SKY_PATCH_PATH@"',
            '"sha256": "@ROR_OGRE_NEXT_VULKAN_SKY_PATCH_SHA256@"',
            '"patched_sha256": "@ROR_OGRE_NEXT_VULKAN_SKY_PATCHED_SHA256@"',
            '"path": "@ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_PATH@"',
            '"sha256": "@ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_SHA256@"',
            (
                '"header_patched_sha256": '
                '"@ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATCHED_SHA256@"'
            ),
            (
                '"implementation_patched_sha256": '
                '"@ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATCHED_SHA256@"'
            ),
            '"full_n1_link_evidence": "not_evaluated"',
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.template)
        self.assertIn(
            'set(ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_ENABLED_JSON "false")',
            self.cmake,
        )

    def test_build_contract_patch_array_renders_to_canonical_lock(self) -> None:
        rendered = self.template.split('  "patches": ', 1)[1].split(
            ',\n  "embedded_namespace":', 1
        )[0]
        patches = self.lock["patches"]
        bindings = {
            "ROR_OGRE_NEXT_PATCH_PATH": patches[0]["path"],
            "ROR_OGRE_NEXT_PATCH_SHA256": patches[0]["sha256"],
            "ROR_OGRE_NEXT_PATCH_REASON": patches[0]["reason"],
            "ROR_OGRE_NEXT_IBL_PATCH_PATH": patches[1]["path"],
            "ROR_OGRE_NEXT_IBL_PATCH_SHA256": patches[1]["sha256"],
            "ROR_OGRE_NEXT_IBL_PATCH_REASON": patches[1]["reason"],
            "ROR_OGRE_NEXT_IBL_PATCH_SOURCE_PATH": patches[1][
                "source_path"
            ],
            "ROR_OGRE_NEXT_IBL_PATCH_SOURCE_SHA256": patches[1][
                "source_sha256"
            ],
            "ROR_OGRE_NEXT_IBL_PATCHED_SHA256": patches[1][
                "patched_sha256"
            ],
            "ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_PATH": patches[2]["path"],
            "ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_SHA256": patches[2][
                "sha256"
            ],
            "ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_REASON": patches[2][
                "reason"
            ],
            "ROR_OGRE_NEXT_METAL_ANISOTROPY_SOURCE_PATH": patches[2][
                "source_path"
            ],
            "ROR_OGRE_NEXT_METAL_ANISOTROPY_SOURCE_SHA256": patches[2][
                "source_sha256"
            ],
            "ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCHED_SHA256": patches[2][
                "patched_sha256"
            ],
            "ROR_OGRE_NEXT_VULKAN_SKY_PATCH_PATH": patches[3]["path"],
            "ROR_OGRE_NEXT_VULKAN_SKY_PATCH_SHA256": patches[3]["sha256"],
            "ROR_OGRE_NEXT_VULKAN_SKY_PATCH_REASON": patches[3]["reason"],
            "ROR_OGRE_NEXT_VULKAN_SKY_SOURCE_PATH": patches[3][
                "source_path"
            ],
            "ROR_OGRE_NEXT_VULKAN_SKY_SOURCE_SHA256": patches[3][
                "source_sha256"
            ],
            "ROR_OGRE_NEXT_VULKAN_SKY_PATCHED_SHA256": patches[3][
                "patched_sha256"
            ],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_PATH": patches[4]["path"],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_SHA256": patches[4][
                "sha256"
            ],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_REASON": patches[4][
                "reason"
            ],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATH": patches[4][
                "header_source_path"
            ],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_SOURCE_SHA256": patches[4][
                "header_source_sha256"
            ],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATCHED_SHA256": patches[4][
                "header_patched_sha256"
            ],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATH": patches[4][
                "implementation_source_path"
            ],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_SOURCE_SHA256": patches[4][
                "implementation_source_sha256"
            ],
            "ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATCHED_SHA256": patches[4][
                "implementation_patched_sha256"
            ],
        }
        for name, value in bindings.items():
            rendered = rendered.replace(
                f"@{name}@", json.dumps(value)[1:-1]
            )
        self.assertNotIn("@ROR_OGRE_NEXT_", rendered)
        self.assertEqual(json.loads(rendered), patches)

    def test_every_ogrenext_header_target_explicitly_registers_helper(self) -> None:
        targets = (
            "ror_ogre_next_probe",
            "ror_ogre_next_frame_probe",
            "ror_ogre_next_dual_runtime_next_adapter",
            "ror_ogre_next_embedded_plugin_export_probe",
            "ror_ogre_next_frontend_n1",
            "ror_ogre_next_frontend_n1_runtime",
            "ror_renderer_ogre_next_sdl_window_runtime",
            "ror_ogre_next_vulkan_rt5_smoke",
            "ror_ogre_next_vulkan_rt6_smoke",
            "ror_ogre_next_windows_dxr7_smoke",
        )
        for target in targets:
            with self.subTest(target=target):
                self.assertRegex(
                    self.cmake,
                    re.compile(
                        r"ror_ogre_next_enable_embedded_namespace\(\s*"
                        + re.escape(target)
                        + r"(?:\s+LANGUAGES\s+OBJCXX)?\s*\)",
                        re.MULTILINE,
                    ),
                )
        self.assertRegex(
            self.cmake,
            re.compile(
                r"ror_ogre_next_enable_embedded_namespace\(\s*"
                r"ror_renderer_ogre_next_sdl_window_runtime\s+"
                r"LANGUAGES\s+OBJCXX\s*\)",
                re.MULTILINE,
            ),
        )

    def test_neutral_and_ogre14_targets_are_not_registered(self) -> None:
        for target in (
            "ror_ogre_next_n1_contract",
            "ror_ogre_next_dual_runtime_ogre14_adapter",
            "ror_ogre_next_dual_runtime_link_smoke",
            "ror_ogre_next_frontend_n1_smoke",
            "ror_renderer_ogre_next_child_runtime",
        ):
            with self.subTest(target=target):
                self.assertNotRegex(
                    self.cmake,
                    re.compile(
                        r"ror_ogre_next_enable_embedded_namespace\(\s*"
                        + re.escape(target)
                        + r"\s*\)",
                        re.MULTILINE,
                    ),
                )

    def test_helper_preserves_off_noop_and_supports_narrow_language_scope(self) -> None:
        start = self.pinned.index(
            "function(ror_ogre_next_enable_embedded_namespace"
        )
        end = self.pinned.index("endfunction()", start)
        helper = self.pinned[start:end]
        return_offset = helper.index("return()")
        target_mutation_offset = helper.index("target_compile_options")
        self.assertLess(return_offset, target_mutation_offset)
        self.assertIn("cmake_parse_arguments(PARSE_ARGV 1", helper)
        self.assertIn("_ror_namespace_LANGUAGES CXX OBJCXX", helper)
        self.assertIn("$<COMPILE_LANGUAGE:${_ror_namespace_language_expression}>", helper)

    def test_compile_database_boundary_check_rejects_both_leak_directions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            remap = root / "RoROgreNextNamespaceRemap.h"
            next_source = root / "next.cpp"
            neutral_source = root / "neutral.cpp"
            for path in (remap, next_source, neutral_source):
                path.write_text("// fixture\n", encoding="utf-8")
            entries = [
                {
                    "file": str(next_source),
                    "command": f"clang++ -include{remap} -c {next_source}",
                },
                {
                    "file": str(neutral_source),
                    "command": f"clang++ -c {neutral_source}",
                },
            ]
            namespaced = AUDIT.audit_compile_sources(
                entries,
                [next_source],
                remap,
                expect_remap=True,
                require_entries=True,
            )
            neutral = AUDIT.audit_compile_sources(
                entries,
                [neutral_source],
                remap,
                expect_remap=False,
                require_entries=True,
            )
            self.assertEqual(namespaced[0]["forced_remap"], True)
            self.assertEqual(neutral[0]["forced_remap"], False)

            missing = copy.deepcopy(entries)
            missing[0]["command"] = f"clang++ -c {next_source}"
            with self.assertRaisesRegex(RuntimeError, "lacks forced namespace remap"):
                AUDIT.audit_compile_sources(
                    missing,
                    [next_source],
                    remap,
                    expect_remap=True,
                    require_entries=True,
                )
            leaked = copy.deepcopy(entries)
            leaked[1]["command"] = f"clang++ -include{remap} -c {neutral_source}"
            with self.assertRaisesRegex(RuntimeError, "leaked into neutral/OGRE14"):
                AUDIT.audit_compile_sources(
                    leaked,
                    [neutral_source],
                    remap,
                    expect_remap=False,
                    require_entries=True,
                )

    def test_strict_fp_compile_order_rejects_host_fast_math_last(self) -> None:
        AUDIT.require_strict_fp_compile_command(
            "clang++ -O2 -ffast-math -fno-fast-math -ffp-contract=off -c a.cpp",
            "good",
        )
        with self.assertRaisesRegex(RuntimeError, "does not override"):
            AUDIT.require_strict_fp_compile_command(
                "clang++ -fno-fast-math -ffp-contract=off -ffast-math -c a.cpp",
                "bad-order",
            )
        with self.assertRaisesRegex(RuntimeError, "does not end"):
            AUDIT.require_strict_fp_compile_command(
                "clang++ -ffast-math -fno-fast-math -ffp-contract=fast -c a.cpp",
                "bad-contract",
            )
        AUDIT.require_strict_fp_compile_command(
            "cl /O2 /fp:fast /fp:strict /c a.cpp", "msvc-good"
        )
        with self.assertRaisesRegex(RuntimeError, "does not end"):
            AUDIT.require_strict_fp_compile_command(
                "cl /fp:strict /fp:fast /c a.cpp", "msvc-bad-order"
            )

    def test_runtime_closure_patterns_are_platform_specific(self) -> None:
        fixtures = {
            "macos-arm64-metal": (
                "libOgreMain.14.5.dylib",
                "libOgreMain.so.14.5",
            ),
            "linux-x86_64-vulkan": (
                "libOgreMain.so.14.5",
                "libOgreMain.14.5.dylib",
            ),
            "windows-x64-d3d11": (
                "OgreMain.dll",
                "libOgreMain.so.14.5",
            ),
        }
        for policy, (accepted, rejected) in fixtures.items():
            with self.subTest(policy=policy):
                pattern = AUDIT.LEGACY_RUNTIME_LIBRARY_PATTERNS[policy]
                self.assertIsNotNone(pattern.fullmatch(accepted))
                self.assertIsNone(pattern.fullmatch(rejected))

    def test_plugin_symbol_audit_distinguishes_static_and_dynamic_linkage(self) -> None:
        exports = "RoROgreNext_dllStartPlugin\nRoROgreNext_dllStopPlugin\n"
        static = AUDIT.audit_plugin_symbol_ownership(exports, "", "static")
        self.assertEqual(static["dynamic_lookup_names"], "not_applicable_static")

        dynamic = AUDIT.audit_plugin_symbol_ownership(
            exports,
            "RoROgreNext_dllStartPlugin\nRoROgreNext_dllStopPlugin\n",
            "dynamic",
        )
        self.assertEqual(dynamic["dynamic_lookup_names"], "verified")

        with self.assertRaisesRegex(RuntimeError, "did not compile"):
            AUDIT.audit_plugin_symbol_ownership(exports, "", "dynamic")
        with self.assertRaisesRegex(RuntimeError, "unprefixed dynamic"):
            AUDIT.audit_plugin_symbol_ownership(
                exports,
                "Cannot find symbol dllStartPlugin in library",
                "static",
            )

    def test_windows_symbol_checks_use_exact_msvc_decorations(self) -> None:
        raw = "\n".join(
            (
                "?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ",
                "?getSingletonPtr@Root@RoROgreNext@@SAPEAV12@XZ",
                "??0OgreNextN1Frontend@Render@RoR@@QEAA@XZ",
                "?owner@Widget@RoROgreNextRapidJson@@QEAAXXZ",
            )
        )
        self.assertTrue(
            AUDIT.cpp_symbol_present(
                raw,
                "",
                "windows-x64-d3d11",
                "Ogre::Root::getSingletonPtr()",
                "?getSingletonPtr@Root@Ogre@@",
            )
        )
        self.assertTrue(
            AUDIT.cpp_namespace_present(
                raw,
                "",
                "windows-x64-d3d11",
                "RoROgreNextRapidJson::",
                "@RoROgreNextRapidJson@@",
            )
        )
        self.assertFalse(
            AUDIT.cpp_namespace_present(
                "?owner@Root@RoROgreNext@@QEAAXXZ",
                "",
                "windows-x64-d3d11",
                "Ogre::",
                "@Ogre@@",
            )
        )

    def test_windows_dumpbin_parser_rejects_undefined_owners(self) -> None:
        archive_dump = "\n".join(
            (
                "00A 00000000 SECT3 notype () External | ?defined@Root@RoROgreNext@@SAXXZ",
                "00B 00000000 UNDEF notype () External | ?missing@Root@Ogre@@SAXXZ",
                "00C 00000000 SECT4 notype Static | local_symbol",
            )
        )
        self.assertEqual(
            AUDIT.msvc_defined_symbols(archive_dump, exports=False),
            {"?defined@Root@RoROgreNext@@SAXXZ"},
        )
        export_dump = "\n".join(
            (
                "    1    0 00001000 ?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ",
                "    2    1 00002000 named_export = forwarded.target",
            )
        )
        self.assertEqual(
            AUDIT.msvc_defined_symbols(export_dump, exports=True),
            {
                "?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ",
                "named_export",
            },
        )
        link_member_dump = "\n".join(
            (
                "    2 public symbols",
                "      109 ?defined@Root@RoROgreNext@@SAXXZ",
                "      20A ?other@Root@RoROgreNext@@SAXXZ",
                "    2 offsets",
                "      109",
                "      20A",
            )
        )
        self.assertEqual(
            AUDIT.msvc_defined_symbols(
                link_member_dump, exports=False, link_members=True
            ),
            {
                "?defined@Root@RoROgreNext@@SAXXZ",
                "?other@Root@RoROgreNext@@SAXXZ",
            },
        )
        with self.assertRaisesRegex(RuntimeError, "cannot be exports"):
            AUDIT.msvc_defined_symbols(
                link_member_dump, exports=True, link_members=True
            )

    def test_windows_symbol_checks_accept_dumpbin_undecoration(self) -> None:
        readable = (
            "public: static class RoROgreNext::Root * __cdecl "
            "RoROgreNext::Root::getSingletonPtr(void)"
        )
        self.assertTrue(
            AUDIT.cpp_symbol_present(
                readable,
                "",
                "windows-x64-d3d11",
                "RoROgreNext::Root::getSingletonPtr",
                "?getSingletonPtr@Root@RoROgreNext@@",
            )
        )
        self.assertTrue(
            AUDIT.cpp_namespace_present(
                readable,
                "",
                "windows-x64-d3d11",
                "RoROgreNext::",
                "@RoROgreNext@@",
            )
        )

    def test_windows_final_root_proof_accepts_only_retained_adapter_owner(self) -> None:
        windows_map = "ror_embedded_ogre_next_root_address\n"
        self.assertTrue(
            AUDIT.final_symbol_owner_present(
                windows_map,
                "",
                "windows-x64-d3d11",
                "RoROgreNext::Root::getSingletonPtr()",
                "?getSingletonPtr@Root@RoROgreNext@@",
                "ror_embedded_ogre_next_root_address",
            )
        )
        self.assertFalse(
            AUDIT.final_symbol_owner_present(
                windows_map,
                "",
                "windows-x64-d3d11",
                "Ogre::Root::getSingletonPtr()",
                "?getSingletonPtr@Root@Ogre@@",
                "ror_ogre14_root_address",
            )
        )
        self.assertFalse(
            AUDIT.final_symbol_owner_present(
                "ror_embedded_ogre_next_root_address",
                "",
                "linux-x86_64-vulkan",
                "RoROgreNext::Root::getSingletonPtr()",
                "?getSingletonPtr@Root@RoROgreNext@@",
                "ror_embedded_ogre_next_root_address",
            )
        )

    def test_windows_link_map_requires_named_public_symbol_table(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "ror_ogre_next_root_provider_link_smoke.exe"
            executable.write_bytes(b"fixture")
            link_map = root / "provider.map"
            payload = "\n".join(
                (
                    " ror_ogre_next_root_provider_link_smoke",
                    "",
                    "  Address         Publics by Value              Rva+Base       Lib:Object",
                    " 0001:00000000 ?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ 0000000140001000 f OgreMain.lib:OgreRoot.obj",
                )
            )
            link_map.write_bytes(payload.encode("utf-16"))
            self.assertIn(
                "?getSingletonPtr@Root@Ogre@@",
                AUDIT.read_msvc_link_map(link_map, executable),
            )
            link_map.write_text("wrong executable\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "does not name"):
                AUDIT.read_msvc_link_map(link_map, executable)

    def test_only_itanium_std_symbols_are_toolchain_owned_collisions(self) -> None:
        for symbol in (
            "_ZNKSt5ctypeIcE8do_widenEc",
            "_ZNSt6vectorIiSaIiEE17_M_default_appendEm",
            "_ZStplIcSt11char_traitsIcESaIcEENSt7__cxx1112basic_stringIcT0_T1_EEOS7_PKS4_",
            "_ZTISt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE",
            "__ZNSt3__119piecewise_constructE",
        ):
            with self.subTest(symbol=symbol):
                self.assertTrue(AUDIT.is_toolchain_owned_global_collision(symbol))

    def test_audit_requires_private_rapidjson_namespace_in_final_link(self) -> None:
        self.assertIn(
            '"RoROgreNextRapidJson::", "@RoROgreNextRapidJson@@"',
            self.audit_source,
        )
        self.assertIn(
            '"rapidjson::", "@rapidjson@@"', self.audit_source
        )
        self.assertIn(
            "cpp_namespace_present(",
            self.audit_source,
        )
        self.assertNotIn(
            '"rapidjson::" in executable_demangled', self.audit_source
        )
        self.assertIn(
            '"rapidjson_namespace": "RoROgreNextRapidJson"',
            self.audit_source,
        )
        for symbol in (
            "_ZN4Ogre4Root15getSingletonPtrEv",
            "_ZN12RoROgreNext4Root15getSingletonPtrEv",
            "plain_c_export",
        ):
            with self.subTest(symbol=symbol):
                self.assertFalse(AUDIT.is_toolchain_owned_global_collision(symbol))

    def test_audit_binds_exact_contract_inputs_and_source_commit(self) -> None:
        embedded = self.lock["embedded_namespace"]
        contract = {
            "schema_version": 7,
            "ror_source": {"commit": "a" * 40},
            "patches": copy.deepcopy(self.lock["patches"]),
            "embedded_namespace": {
                "enabled": True,
                "namespace": embedded["namespace"],
                "cmake_option": embedded["cmake_option"],
                "default_enabled": embedded["default_enabled"],
                "patch": {**embedded["patch"], "applied": True},
                "remap_header": {
                    **embedded["remap_header"],
                    "forced_include": True,
                },
                "full_n1_link_evidence": "not_evaluated",
            },
        }
        validated = AUDIT.validate_embedded_build_contract(
            contract,
            self.lock,
            REPOSITORY_ROOT,
            "a" * 40,
            PROBE_ROOT / embedded["patch"]["path"],
            PROBE_ROOT / embedded["remap_header"]["path"],
        )
        self.assertIs(validated["enabled"], True)
        tampered = copy.deepcopy(contract)
        tampered["embedded_namespace"]["full_n1_link_evidence"] = "passed"
        with self.assertRaisesRegex(RuntimeError, "not canonical"):
            AUDIT.validate_embedded_build_contract(
                tampered,
                self.lock,
                REPOSITORY_ROOT,
                "a" * 40,
                PROBE_ROOT / embedded["patch"]["path"],
                PROBE_ROOT / embedded["remap_header"]["path"],
            )

    def test_provenance_and_workflow_closures_include_embedded_sources(self) -> None:
        test_path = "tests/tools/test_ogre_next_embedded_namespace_contract.py"
        for path in (*EMBEDDED_PROVENANCE_PATHS, test_path):
            with self.subTest(path=path):
                self.assertIn(path, RUNNER.RELEVANT_SOURCE_PATHS)
                self.assertIn(path, VERIFIER.RELEVANT_SOURCE_PATHS)
                self.assertIn(path, self.cmake)
                self.assertIn(path, self.n2_prelink)
        self.assertIn("- tools/ogre_next_probe/**", self.workflow)
        self.assertIn("- tests/tools/test_ogre_next_*.py", self.workflow)
        self.assertIn(f"python {test_path}", self.workflow)
        self.assertIn(f"python -O {test_path}", self.workflow)

    def test_audit_command_requires_contract_digest_and_exact_commit_inputs(self) -> None:
        for token in (
            "--source-root",
            "--expected-source-commit",
            "--build-contract",
            "--canonical-lock",
            "--patch",
            "--next-plugin-linkage",
            "--namespaced-source",
            "--neutral-source",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cmake)
                self.assertIn(token, self.audit_source)
        self.assertIn('"sha256": digest(build_contract)', self.audit_source)
        self.assertIn('"ror_source_commit": actual_source_commit', self.audit_source)
        self.assertIn(
            '== "tools/ogre_next_probe/ogre-next.lock.json"',
            self.audit_source,
        )
        self.assertIn('"full_n1_runtime_link":', self.audit_source)
        self.assertIn('"not_evaluated"', self.audit_source)

    def test_dirty_checkout_is_explicit_and_never_qualification_evidence(self) -> None:
        clean = AUDIT.classify_source_checkout("", False)
        self.assertTrue(clean["clean"])
        self.assertTrue(clean["qualification_eligible"])
        self.assertFalse(clean["dirty_development_build_allowed"])

        with self.assertRaisesRegex(RuntimeError, "development-only"):
            AUDIT.classify_source_checkout(" M source/main/main.cpp", False)

        dirty = AUDIT.classify_source_checkout(
            " M source/main/main.cpp\n?? local-note.txt", True
        )
        self.assertFalse(dirty["clean"])
        self.assertFalse(dirty["qualification_eligible"])
        self.assertTrue(dirty["dirty_development_build_allowed"])
        self.assertEqual(dirty["porcelain_entry_count"], 2)
        self.assertRegex(dirty["porcelain_sha256"], r"^[0-9a-f]{64}$")
        for source in (
            "OgreNextVulkanExternalDeviceBootstrap.cpp",
            "OgreNextVulkanRayTracingBootstrap.cpp",
            "OgreNextD3D12DxrBootstrap.cpp",
            "vulkan_rt5_smoke.cpp",
            "vulkan_rt6_smoke.cpp",
            "windows_dxr7_smoke.cpp",
            "RendererFrontendDirectDispatcher.cpp",
            "RendererFrontendPresentationPolicy.cpp",
        ):
            with self.subTest(source=source):
                self.assertIn(source, self.cmake)


if __name__ == "__main__":
    unittest.main()
