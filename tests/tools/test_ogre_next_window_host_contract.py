#!/usr/bin/env python3
"""Offline contract for the non-admitted SDL/Ogre-Next window host."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
LOCK_PATH = PROBE_ROOT / "renderer-presentation-sdl2.lock.json"
SELF_PATH = "tests/tools/test_ogre_next_window_host_contract.py"
SOURCE_PATHS = (
    "source/main/system/RendererOgreNextWindowHost.cpp",
    "source/main/system/RendererOgreNextWindowHost.h",
    "source/main/system/RendererOgreNextSdlWindowRuntime.cpp",
    "source/main/system/RendererOgreNextSdlWindowRuntime.h",
    "source/main/system/RendererOgreNextSdlWindowRuntimeCocoa.mm",
    "tests/gfx/RendererOgreNextWindowHostTests.cpp",
    SELF_PATH,
)
LOCK_SHA256 = (
    "965d992c2059b3ada9c1d2c03fd2fd4a4bb8aef15267d06146a9b9d21287ecc1"
)


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


class OgreNextWindowHostContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock_bytes = LOCK_PATH.read_bytes()
        cls.lock = json.loads(
            cls.lock_bytes.decode("utf-8"), object_pairs_hook=_unique_object
        )
        cls.host_header = (
            REPOSITORY_ROOT / SOURCE_PATHS[1]
        ).read_text(encoding="utf-8")
        cls.host = (REPOSITORY_ROOT / SOURCE_PATHS[0]).read_text(
            encoding="utf-8"
        )
        cls.adapter = (REPOSITORY_ROOT / SOURCE_PATHS[2]).read_text(
            encoding="utf-8"
        )
        cls.cocoa = (REPOSITORY_ROOT / SOURCE_PATHS[4]).read_text(
            encoding="utf-8"
        )
        cls.probe_cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.pinned_cmake = (
            PROBE_ROOT / "cmake" / "PinnedOgreNext.cmake"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")

    def test_sdl_source_and_product_recipe_are_exactly_locked(self) -> None:
        self.assertEqual(hashlib.sha256(self.lock_bytes).hexdigest(), LOCK_SHA256)
        self.assertEqual(
            set(self.lock),
            {
                "schema",
                "version",
                "archive_url",
                "archive_sha256",
                "license",
                "conan",
                "scope",
            },
        )
        self.assertEqual(
            self.lock["schema"], "ror.renderer_presentation_sdl2_source.v1"
        )
        self.assertEqual(self.lock["version"], "2.32.10")
        self.assertEqual(
            self.lock["archive_url"],
            "https://www.libsdl.org/release/SDL2-2.32.10.tar.gz",
        )
        self.assertEqual(
            self.lock["archive_sha256"],
            "5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165",
        )
        self.assertEqual(
            self.lock["license"],
            {
                "spdx": "Zlib",
                "path": "LICENSE.txt",
                "sha256": "97f35b302b361680ec1e891e95d2d52097bb95abff361434916d99dc1305f127",
            },
        )
        self.assertEqual(
            self.lock["conan"],
            {
                "reference": "sdl/2.32.10",
                "recipe_revision": "19432981a8779c918a13682d4186fa3b",
                "cmake_target": "SDL2::SDL2",
            },
        )
        self.assertEqual(
            self.lock["scope"],
            {
                "source_dependency_locked": True,
                "probe_linked": True,
                "live_window_smoke": True,
                "production_admitted": False,
                "packaged": False,
            },
        )
        conan = (REPOSITORY_ROOT / "conanfile.py").read_text(encoding="utf-8")
        dependencies = (
            REPOSITORY_ROOT / "cmake/DependenciesConfig.cmake"
        ).read_text(encoding="utf-8")
        self.assertIn(
            'SDL2_RECIPE_REVISION = "19432981a8779c918a13682d4186fa3b"',
            conan,
        )
        self.assertIn('"sdl/2.32.10#{SDL2_RECIPE_REVISION}"', conan)
        self.assertIn("find_package(SDL2 2.32.10 EXACT CONFIG REQUIRED)", dependencies)

    def test_lock_is_enforced_before_pinned_source_build(self) -> None:
        for token in (
            LOCK_SHA256,
            "ROR_SDL2_PRESENTATION_ARCHIVE_SHA256",
            "FETCHCONTENT_SOURCE_DIR_ROR_SDL2",
            "FetchContent_Declare(\n    ror_sdl2",
            'URL_HASH "SHA256=${ROR_SDL2_PRESENTATION_ARCHIVE_SHA256}"',
            "FetchContent_MakeAvailable(ror_sdl2)",
            "Pinned SDL2 license hash changed",
            "SDL_SHARED OFF",
            "SDL_STATIC ON",
            "SDL2_DISABLE_SDL2MAIN ON",
            "SDL2_DISABLE_INSTALL ON",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.pinned_cmake)

    def test_linux_builds_null_bootstrap_and_xcb_presentation_only(self) -> None:
        for token in (
            'set(OGRE_CONFIG_UNIX_NO_X11 OFF CACHE BOOL "" FORCE)',
            'set(OGRE_VULKAN_WINDOW_NULL ON CACHE BOOL "" FORCE)',
            'set(OGRE_VULKAN_WINDOW_XCB ON CACHE BOOL "" FORCE)',
            'set(SDL_X11 ON CACHE BOOL "" FORCE)',
            'set(SDL_X11_SHARED OFF CACHE BOOL "" FORCE)',
            'set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)',
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.pinned_cmake)
        for stale in (
            'set(OGRE_CONFIG_UNIX_NO_X11 ON CACHE BOOL "" FORCE)',
            'set(OGRE_VULKAN_WINDOW_XCB OFF CACHE BOOL "" FORCE)',
        ):
            self.assertNotIn(stale, self.pinned_cmake)

    def test_platform_handle_translation_is_exact_and_fail_closed(self) -> None:
        for token in (
            '"externalWindowHandle"',
            '"presentsWithTransaction"',
            '"SDL2x11"',
            '"Interface", "xcb"',
            '"windowType",\n                 "null"',
            "REJECTED_WAYLAND_UNSUPPORTED",
            "RendererOgreNextWindowBridge::COCOA_OGRE_METAL_VIEW",
            "RendererOgreNextWindowBridge::WIN32_EXTERNAL_HWND",
            "RendererOgreNextWindowBridge::X11_XCB_SDL2_PAIR",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.host)
        self.assertNotIn('"parentWindowHandle"', self.host)
        self.assertIn(
            "RendererOgreNextWindowHost(RendererOgreNextWindowHost &&) = delete",
            self.host_header,
        )
        self.assertIn("PointerString(&binding.x11_pair)", self.host)

    def test_sdl_syswm_and_x11_pair_have_compile_time_abi_proof(self) -> None:
        for token in (
            "#include <SDL_syswm.h>",
            "SDL_GetWindowWMInfo",
            "SDL_SYSWM_COCOA",
            "SDL_SYSWM_WINDOWS",
            "SDL_SYSWM_X11",
            "AuditedSdl2X11Pair",
            "std::is_standard_layout<RendererOgreNextX11WindowPair>",
            "offsetof(RendererOgreNextX11WindowPair, display)",
            "offsetof(RendererOgreNextX11WindowPair, window)",
            "decltype(static_cast<SDL_SysWMinfo *>(nullptr)->info.x11.display)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.adapter)

    def test_cocoa_owns_autoresizing_ogre_metal_view(self) -> None:
        for token in (
            '#import "OgreMetalView.h"',
            "window.contentView",
            "initWithFrame:content_view.bounds",
            "NSViewWidthSizable | NSViewHeightSizable",
            "[content_view addSubview:view]",
            "__bridge_retained void *",
            "__bridge_transfer OgreMetalView *",
            "[view removeFromSuperview]",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cocoa)

    def test_resize_requires_native_configure_ack_before_metric_commit(self) -> None:
        header_callback = self.host_header[
            self.host_header.index("resize_sdl_window_and_wait_for_configure") - 500 :
            self.host_header.index("resize_sdl_window_and_wait_for_configure") + 500
        ]
        self.assertIn("matching Cocoa/WM_SIZE/X11 ConfigureNotify", header_callback)
        self.assertIn("immediate post-SDL_SetWindowSize query is not", header_callback)
        resize = self.host[
            self.host.index("RendererOgreNextWindowHost::Resize(") :
            self.host.index("RendererOgreNextWindowHost::RefreshMetrics")
        ]
        self.assertLess(
            resize.index("resize_sdl_window_and_wait_for_configure"),
            resize.index("CommitMetrics"),
        )
        self.assertNotIn("query_sdl_native_window", resize)
        for token in (
            "SDL_SetWindowSize",
            "SDL_AddEventWatch",
            "SDL_DelEventWatch",
            "ScopedWindowEventWatch",
            "SDL_WINDOWEVENT_RESIZED",
            "SDL_WINDOWEVENT_SIZE_CHANGED",
            "ObserveWindowEvent",
            "QueryNativeWindow(context, sdl_window, window)",
            "timed out awaiting native SDL configure acknowledgement",
        ):
            self.assertIn(token, self.adapter)
        adapter_resize = self.adapter[
            self.adapter.index(
                "RendererOgreNextSdlWindowRuntime::ResizeWindowAndWaitForConfigure"
            ) :
            self.adapter.index(
                "RendererOgreNextSdlWindowRuntime::DestroyMetalView"
            )
        ]
        self.assertNotIn("SDL_GETEVENT", adapter_resize)
        self.assertNotIn("SDL_PeepEvents", adapter_resize)

    def test_visibility_is_also_bounded_and_event_acknowledged(self) -> None:
        visibility = self.adapter[
            self.adapter.index(
                "RendererOgreNextSdlWindowRuntime::SetWindowVisibleAndWaitForAck"
            ) :
            self.adapter.index(
                "RendererOgreNextSdlWindowRuntime::ResizeWindowAndWaitForConfigure"
            )
        ]
        for token in (
            "WindowWatchKind::VISIBILITY",
            "ScopedWindowEventWatch",
            "SDL_ShowWindow",
            "SDL_HideWindow",
            "HasSettledVisibility",
            "timed out awaiting native SDL visibility acknowledgement",
        ):
            self.assertIn(token, visibility)
        resume = self.host[
            self.host.index("RendererOgreNextWindowHost::Resume()") :
            self.host.index("RendererOgreNextWindowHost::Suspend()")
        ]
        show_ack = resume.index("set_sdl_window_visible_and_wait_for_ack")
        post_ack_query = resume.index("query_sdl_native_window", show_ack)
        self.assertLess(show_ack, post_ack_query)
        self.assertLess(post_ack_query, resume.index("CommitMetrics"))
        tests = (
            REPOSITORY_ROOT / "tests/gfx/RendererOgreNextWindowHostTests.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("post_show_drawable_width", tests)
        self.assertIn(
            "resume did not re-query native metrics after the show ack", tests
        )

    def test_logical_and_drawable_surface_revisions_are_distinct(self) -> None:
        for token in (
            "struct RendererOgreNextWindowMetrics",
            "std::uint64_t generation",
            "logical_width",
            "drawable_width",
            "content_scale_x",
            "content_scale_y",
            "SDL_GetWindowSizeInPixels",
        ):
            self.assertIn(
                token,
                self.host_header + self.adapter,
            )
        self.assertIn("std::numeric_limits<std::uint64_t>::max()", self.host)
        refresh = self.host[
            self.host.index("RendererOgreNextWindowHost::RefreshMetrics") :
            self.host.index("RendererOgreNextWindowHost::Shutdown()")
        ]
        self.assertIn("query_sdl_native_window", refresh)
        self.assertIn("CommitMetrics", refresh)
        self.assertNotIn("resize_sdl_window_and_wait_for_configure", refresh)
        tests = (
            REPOSITORY_ROOT / "tests/gfx/RendererOgreNextWindowHostTests.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("same-logical HiDPI migration", tests)
        self.assertIn("2560U", tests)
        self.assertIn("1750U", tests)

    def test_probe_targets_compile_and_run_without_product_admission(self) -> None:
        for token in (
            "ror_renderer_ogre_next_window_host_tests",
            "ror_renderer_ogre_next_sdl_window_runtime",
            "RendererOgreNextSdlWindowRuntimeCocoa.mm",
            "PUBLIC SDL2::SDL2-static",
            "ror_renderer_ogre_next_window_host_smoke",
            "src/window_host_smoke.cpp",
            "SKIP_RETURN_CODE 77",
            "TIMEOUT 20",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.probe_cmake)
        normal_cmake = (REPOSITORY_ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("ror_renderer_ogre_next_window_host_tests", normal_cmake)
        package = self.probe_cmake[
            self.probe_cmake.index("set(ROR_OGRE_NEXT_N1_PACKAGE_ROOT") :
            self.probe_cmake.index(
                "add_custom_target(ror_ogre_next_frontend_n1_package ALL"
            )
        ]
        for prohibited in (
            "ror_renderer_ogre_next_window_host_smoke",
            "ror_renderer_ogre_next_sdl_window_runtime",
            "RendererOgreNextSdlWindowRuntime",
        ):
            self.assertNotIn(prohibited, package)
        report = self.probe_cmake[
            self.probe_cmake.index(
                "add_custom_target(\n        ror_ogre_next_frontend_n1_report"
            ) :
            self.probe_cmake.index(
                "add_test(NAME ror_ogre_next_frontend_n1_runtime"
            )
        ]
        for required in (
            "ror_renderer_ogre_next_window_host_tests",
            "ror_renderer_ogre_next_window_host_smoke",
        ):
            self.assertEqual(report.count(required), 1)
        product_files = (
            REPOSITORY_ROOT / "CMakeLists.txt",
            REPOSITORY_ROOT / "source/main/CMakeLists.txt",
            REPOSITORY_ROOT / "cmake/RendererLauncherPackageConfig.cmake",
        )
        for product_file in product_files:
            with self.subTest(product_file=product_file):
                self.assertNotIn(
                    "RendererOgreNextSdlWindowRuntime",
                    product_file.read_text(encoding="utf-8"),
                )

    def test_workflow_has_cross_platform_static_native_and_live_gates(self) -> None:
        self.assertEqual(self.workflow.count(f"python {SELF_PATH}"), 1)
        self.assertEqual(self.workflow.count(f"python -O {SELF_PATH}"), 1)
        for token in (
            "libx11-xcb-dev",
            "libxcb1-dev",
            "xauth",
            "xvfb",
            "Linux x86_64 Vulkan null bootstrap plus XCB window-host",
            "Run the non-admitted SDL window-host contract",
            "-R '^ror_renderer_ogre_next_window_host$'",
            "Run the non-admitted hidden SDL native-window smoke",
            "if: runner.os != 'Linux'",
            "-R '^ror_renderer_ogre_next_window_host_smoke$'",
            "Require the hidden SDL X11 window host under Xvfb",
            "xvfb-run --auto-servernum",
            'smoke="$build_root/bin/ror_renderer_ogre_next_window_host_smoke"',
        ):
            self.assertIn(token, self.workflow)
        linux_live_gate = self.workflow[
            self.workflow.index(
                "- name: Require the hidden SDL X11 window host under Xvfb"
            ) :
            self.workflow.index(
                "- name: Build and validate the independent Apple Metal N2 proof"
            )
        ]
        self.assertNotIn("ctest", linux_live_gate)
        self.assertNotIn("SKIP_RETURN_CODE", linux_live_gate)

    def test_relevant_source_manifests_cover_every_new_contract_file(self) -> None:
        manifests = (
            PROBE_ROOT / "CMakeLists.txt",
            PROBE_ROOT / "cmake/VerifyN2SourceProvenance.cmake",
            REPOSITORY_ROOT / "tools/run_ogre_next_probe.py",
            REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py",
        )
        for manifest in manifests:
            content = manifest.read_text(encoding="utf-8")
            for source_path in SOURCE_PATHS:
                with self.subTest(manifest=manifest, source_path=source_path):
                    self.assertGreaterEqual(content.count(source_path), 1)
        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(
            encoding="utf-8"
        )
        for pattern in (
            "source/main/system/RendererOgreNextSdlWindowRuntime* text eol=lf",
            "source/main/system/RendererOgreNextWindowHost.* text eol=lf",
            "tests/gfx/RendererOgreNextWindowHostTests.cpp text eol=lf",
            f"{SELF_PATH} text eol=lf",
        ):
            self.assertIn(pattern, attributes)

    def test_static_contract_does_not_claim_a_presented_ogre_frame(self) -> None:
        smoke = (PROBE_ROOT / "src/window_host_smoke.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("no Ogre presentation or package admission claimed", smoke)
        self.assertNotRegex(smoke, re.compile(r"createRenderWindow|Compositor|swapBuffers"))
        build_contract = (
            PROBE_ROOT / "ogre_next_build_contract.json.in"
        ).read_text(encoding="utf-8")
        self.assertIn('"headless_child_production_admitted": false', build_contract)


if __name__ == "__main__":
    unittest.main()
