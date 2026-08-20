#!/usr/bin/env python3
"""Offline fail-closed contract for the probe-only Ogre-Next window presenter."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / "tools" / "ogre_next_probe"
LOCK = PROBE / "ogre-next-presentation-copy-v1.lock.json"
LOCK_SHA256 = "0d3351644a41778c3b658a74c9c4aa7bbacb0fc8d95cbaedc645c22d3dcadf5a"


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


class OgreNextWindowPresentationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock_bytes = LOCK.read_bytes()
        cls.lock = json.loads(
            cls.lock_bytes.decode("utf-8"), object_pairs_hook=_unique_object
        )
        cls.frontend_h = (
            ROOT / "source/main/gfx/render/ogrenext/OgreNextN1Frontend.h"
        ).read_text(encoding="utf-8")
        cls.frontend = (
            ROOT / "source/main/gfx/render/ogrenext/OgreNextN1Frontend.cpp"
        ).read_text(encoding="utf-8")
        cls.policy = (
            ROOT / "source/main/gfx/render/ogrenext/OgreNextN1Policy.cpp"
        ).read_text(encoding="utf-8")
        cls.integrity = (
            ROOT / "source/main/gfx/render/ogrenext/OgreNextN1MediaIntegrity.cpp"
        ).read_text(encoding="utf-8")
        cls.smoke = (PROBE / "src/window_present_smoke.cpp").read_text(
            encoding="utf-8"
        )
        cls.cmake = (PROBE / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")

    def test_copy_media_lock_is_exact_and_minimal(self) -> None:
        self.assertEqual(hashlib.sha256(self.lock_bytes).hexdigest(), LOCK_SHA256)
        self.assertEqual(
            set(self.lock),
            {
                "schema",
                "ogre_next_commit",
                "material",
                "license",
                "files",
                "scope",
            },
        )
        self.assertEqual(
            self.lock["schema"], "ror.ogre_next.presentation_copy_media.v1"
        )
        self.assertEqual(
            self.lock["ogre_next_commit"],
            "37149a802de747f6806996fa3067b0748ecc1084",
        )
        self.assertEqual(self.lock["material"], "Ogre/Copy/4xFP32")
        self.assertEqual(len(self.lock["files"]), 8)
        self.assertEqual(
            self.lock["scope"],
            {
                "programs_registered": 2,
                "material_registered": "Ogre/Copy/4xFP32",
                "cpu_window_copy_allowed": False,
                "production_admitted": False,
                "packaged": False,
            },
        )
        paths = [entry["path"] for entry in self.lock["files"]]
        self.assertEqual(len(paths), len(set(paths)))
        for entry in self.lock["files"]:
            self.assertEqual(set(entry) - {"source"}, {"path", "size", "sha256"})
            if entry["path"].startswith("tools/"):
                payload = (ROOT / entry["path"]).read_bytes()
                self.assertEqual(len(payload), entry["size"])
                self.assertEqual(hashlib.sha256(payload).hexdigest(), entry["sha256"])

    def test_one_workspace_has_exact_two_channel_gpu_graph(self) -> None:
        for token in (
            '"MainRT", 0U',
            '"PresentationRT", 1U',
            "node->setNumTargetPass(request.present ? 2U : 1U)",
            "main_target->addPass(Ogre::PASS_SCENE)",
            "scene->mIncludeOverlays = false",
            "presentation_target->addPass(Ogre::PASS_QUAD)",
            'copy->mMaterialName = "Ogre/Copy/4xFP32"',
            'copy->addQuadTextureSource(0U, "MainRT")',
            "workspace_definition->connectExternal(0U",
            "workspace_definition->connectExternal(1U",
            "external_channels.push_back(target)",
            "external_channels.push_back(window_texture)",
            "workspace->getExternalRenderTargets()",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.frontend)
        presented_graph = self.frontend[
            self.frontend.index('target_text = "RoRN1Target_"') :
            self.frontend.index("NativePssmReadback observed_shadow_state")
        ]
        self.assertEqual(presented_graph.count("addPass(Ogre::PASS_SCENE)"), 1)
        self.assertEqual(presented_graph.count("addPass(Ogre::PASS_QUAD)"), 1)
        self.assertNotIn("memcpy", presented_graph)

    def test_post_show_surface_is_transactional_and_retryable(self) -> None:
        for token in (
            "FrontendSurfaceUpdate *acknowledged_surface",
            "ValidateFrontendSurfaceTransition(",
            "ValidateRenderFramePresentation(request, acknowledged_surface)",
            "RefreshPresentationWindowExtent(",
            "impl_->surface = acknowledged_surface",
            "RenderOperationCode::RESOURCE_STALE",
            "presentation surface out of date after native show ACK",
            "acknowledged_window_texture != window_texture",
            "compositors->removeWorkspace(workspace)",
            "rebound_channels.push_back(acknowledged_window_texture)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.frontend_h + self.frontend)
        commit = self.frontend.index("impl_->surface = acknowledged_surface")
        retry = self.frontend.index("RenderOperationCode::RESOURCE_STALE", commit)
        render = self.frontend.index("impl_->root->renderOneFrame()", retry)
        self.assertLess(commit, retry)
        self.assertLess(retry, render)

    def test_gui_only_presentation_is_overlay_only_and_restores_scene_graphs(
        self,
    ) -> None:
        """A GUI-only frame renders overlays, needs no light, and leaves no trace.

        An empty scene cannot be rendered - OgreNextPssmShadowPolicy demands
        exactly one shadow-casting directional light - so the GUI-only graph
        must never contain a scene's worth of anything: no shadow node, no
        source texture, no quad copy, and a render-queue window that only
        overlays live in.
        """
        graph_start = self.frontend.index("EnsureMenuPresentationGraph()")
        graph = self.frontend[
            graph_start :
            self.frontend.index(
                "RebindBootstrapPresentationWorkspace(", graph_start
            )
        ]
        for token in (
            '"PresentationRT", 0U',
            "node->setNumTargetPass(1U)",
            "target->setNumPasses(1U)",
            "target->addPass(Ogre::PASS_SCENE)",
            "scene->mFirstRQ = kOgreNextOverlayFirstRenderQueue;",
            "scene->mLastRQ = kOgreNextOverlayLastRenderQueue;",
            "scene->mIncludeOverlays = true;",
            "scene->setVisibilityMask(0U);",
            "scene->setAllLoadActions(Ogre::LoadAction::Clear);",
        ):
            with self.subTest(token=token):
                self.assertIn(token, graph)
        for forbidden in (
            "ShadowNode",
            "PASS_QUAD",
            "MainRT",
            "authored_view_visibility",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, graph)

        rebind_start = self.frontend.index("RebindMenuPresentationWorkspace(")
        rebind = self.frontend[
            rebind_start : self.frontend.index("EnsureMenuPresentationGraph()")
        ]
        # Created disabled: renderOneFrame() runs every enabled workspace, so a
        # GUI-only workspace left enabled would execute inside a scene frame.
        self.assertIn(
            "kMenuPresentationWorkspaceName, false)", rebind
        )
        self.assertIn("menu_workspace->getEnabled() || observed.size() != 1U", rebind)

        present = self.frontend[
            self.frontend.index(
                "OgreNextN1Frontend::PresentUiOverlayFrame("
            ) : self.frontend.index("OgreNextN1Frontend::UpdateSurface(")
        ]
        for token in (
            # Every scene graph is suspended for the frame and restored to the
            # exact state it was found in.
            "suspend(impl_->bootstrap_workspace)",
            "suspend(impl_->production_workspace)",
            "suspend(impl_->hdr_workspace)",
            "suspend(impl_->hdr_v2_continuation_workspace)",
            "impl_->root->renderOneFrame()",
            # Identity post-condition, mirroring the clear-only bootstrap.
            "TrackedSnapshotIdentityCount()",
            "tracked_snapshots_before",
            "presented_frames_before",
            "impl_->registry.get() != registry_before",
            "production_output_handles.live_count() != 0U",
            "scene-free GUI-only presentation changed portable renderer state",
            "ui_overlay_presented_frames",
        ):
            with self.subTest(token=token):
                self.assertIn(token, present)
        # No frame identity is consumed, so no HDR temporal accounting may run.
        for forbidden in (
            "SynchronizeAssets(",
            "hdr_temporal_state",
            "AccountRetiredFrame",
            "PrepareFrame",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, present)

    def test_clear_only_bootstrap_precedes_scene_and_replaces_transactionally(self) -> None:
        # The end anchor must be searched FROM the start anchor: the first
        # DestroyProductionPresentationGraph() call in the file is inside the
        # HDR compositor teardown, far above the bootstrap graph, which sliced
        # this window down to the empty string and made every assertion below
        # unreachable.
        bootstrap_start = self.frontend.index(
            "RebindBootstrapPresentationWorkspace("
        )
        bootstrap = self.frontend[
            bootstrap_start :
            self.frontend.index(
                "DestroyProductionPresentationGraph()", bootstrap_start
            )
        ]
        self.assertNotEqual(bootstrap, "")
        for token in (
            '"PresentationRT", 0U',
            "node->setNumTargetPass(1U)",
            "target->setNumPasses(1U)",
            "target->addPass(Ogre::PASS_CLEAR)",
            "setBuffersToClear(Ogre::RenderPassDescriptor::Colour0)",
            "channels.push_back(window_texture)",
            "show_after_workspace_ready(",
            "workspace_ready_before_show = true",
        ):
            with self.subTest(token=token):
                self.assertIn(token, bootstrap)
        self.assertEqual(bootstrap.count("addPass(Ogre::PASS_CLEAR)"), 1)
        self.assertNotIn("addPass(Ogre::PASS_SCENE)", bootstrap)
        self.assertNotIn("addPass(Ogre::PASS_QUAD)", bootstrap)
        self.assertIn(
            '"Ogre-Next bootstrap drawable rebind rollback"', bootstrap
        )
        self.assertLess(
            bootstrap.index("RebindBootstrapPresentationWorkspace("),
            bootstrap.index("show_after_workspace_ready("),
        )

        present = self.frontend[
            self.frontend.index(
                "OgreNextN1Frontend::PresentBootstrapFrame()"
            ) : self.frontend.index(
                "OgreNextN1Frontend::UpdateSurface("
            )
        ]
        self.assertIn("impl_->root->renderOneFrame()", present)
        self.assertIn("bootstrap_window_swap_completions", present)
        self.assertIn("TrackedSnapshotIdentityCount() != 0U", present)
        self.assertNotIn("SynchronizeAssets(", present)
        self.assertNotIn("RenderFrameRequest", present)

        replacement = self.frontend[
            self.frontend.index("EnsureProductionPresentationGraph(") :
            self.frontend.index("DestroyPresentationResources()")
        ]
        disabled_bind = replacement.index(
            "window_texture, !replacing_bootstrap"
        )
        disable_bootstrap = replacement.index(
            "bootstrap_workspace->setEnabled(false)"
        )
        enable_scene = replacement.index(
            "production_workspace->setEnabled(true)"
        )
        retire_bootstrap = replacement.index(
            "DestroyBootstrapPresentationGraph()", enable_scene
        )
        self.assertLess(disabled_bind, enable_scene)
        self.assertLess(enable_scene, disable_bootstrap)
        self.assertLess(disable_bootstrap, retire_bootstrap)
        self.assertIn("rollback_to_bootstrap", replacement)

    def test_platform_bindings_are_fail_closed_before_device_init(self) -> None:
        for token in (
            "NativeWindowSystem::COCOA",
            '"externalWindowHandle"',
            '"presentsWithTransaction", "false"',
            "NativeWindowSystem::WINDOWS",
            '"vsyncInterval", "0"',
            "NativeWindowSystem::X11",
            '"Interface", "xcb"',
            '"windowType", "null"',
            '"SDL2x11"',
            "renderer->setConfigOption(option.name, option.value)",
            "root->initialise(false)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.frontend)
        self.assertLess(
            self.frontend.index("renderer->setConfigOption(option.name, option.value)"),
            self.frontend.index("impl_->root->initialise(false)"),
        )

    def test_present_mode_is_one_frame_and_headless_compatible_by_default(self) -> None:
        self.assertIn(
            "the optional native-presentation milestone requires its one presented frame",
            self.policy,
        )
        self.assertIn("presentation_audit.presented_frames != 0U", self.frontend)
        self.assertIn("request.present || persistent_hdr ? 1U : 3U", self.frontend)
        for token in (
            "render_one_frame_calls",
            "source_scene_passes",
            "presentation_quad_passes",
            "window_final_target_updates",
            "window_swap_completions",
            "source_readbacks",
        ):
            self.assertIn(token, self.frontend_h)
            self.assertIn(f"presentation_audit.{token}", self.frontend)
        self.assertIn("presented_attachment.bytes", self.smoke)
        self.assertIn("headless_output.attachments.front().bytes", self.smoke)
        self.assertIn("source-only attachment bytes changed", self.smoke)

    def test_copy_media_integrity_and_material_shape_are_runtime_checked(self) -> None:
        for token in (
            "VerifyOgreNextN1PresentationMedia",
            'resolved_presentation_media_root, {"CommonCopy"}, true',
            "kOgreNextN1PresentationMediaManifest",
            "resourceGroupExists(kOgreNextPresentationResourceGroup)",
            'material_manager.getByName("Ogre/Copy/4xFP32")',
            'getByName("Ogre/Copy/4xFP32"',
            "getNumTechniques() != 1U",
            "getNumPasses() != 1U",
            "getNumTextureUnitStates() != 1U",
            'getVertexProgramName() != "Ogre/Compositor/Quad_vs"',
            'getFragmentProgramName() != "Ogre/Copy/4xFP32_ps"',
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.integrity + self.frontend)

    def test_cleanup_stops_before_root_and_retains_failed_resources(self) -> None:
        cleanup = self.frontend[
            self.frontend.index("[[nodiscard]] bool DestroyPresentationResources()") :
            self.frontend.index("Ogre::AbiCookie abi_cookie")
        ]
        self.assertIn("if (!DestroyPresentationResources())", cleanup)
        self.assertIn("return false;", cleanup)
        self.assertLess(
            cleanup.index("if (!DestroyPresentationResources())"),
            cleanup.index("root.reset()"),
        )
        self.assertLess(cleanup.index("root.reset()"), cleanup.index("plugin.reset()"))
        window_destroy = cleanup[
            cleanup.index("if (presentation_window != nullptr)") :
            cleanup.index("if (bootstrap_window != nullptr)")
        ]
        group_destroy = cleanup[
            cleanup.index("if (presentation_resource_group_created)") :
            cleanup.index("return clean;", cleanup.index("if (presentation_resource_group_created)"))
        ]
        self.assertLess(
            window_destroy.index("presentation_window = nullptr"),
            window_destroy.index("catch (...)"),
        )
        self.assertLess(
            group_destroy.index("presentation_resource_group_created = false"),
            group_destroy.index("catch (...)"),
        )

    def test_strict_smoke_and_separate_evidence_are_registered(self) -> None:
        for token in (
            "ror_ogre_next_window_present_smoke",
            "src/window_present_smoke.cpp",
            "ogre-next-window-present.ppm",
            "ogre-next-window-present.json",
            '--media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"',
            "TIMEOUT 60",
        ):
            self.assertIn(token, self.cmake)
        presentation_test = self.cmake[
            self.cmake.index("if (TARGET ror_ogre_next_window_present_smoke)") :
            self.cmake.index("endif ()", self.cmake.index("if (TARGET ror_ogre_next_window_present_smoke)"))
        ]
        self.assertNotIn("SKIP_RETURN_CODE", presentation_test)
        self.assertIn("ror.ogre_next_n1_native_presentation.v1", self.smoke)
        self.assertIn("arguments.media_root", self.smoke)
        self.assertNotIn("ROR_OGRE_NEXT_N1_SHADER_MEDIA_ROOT", self.smoke)
        self.assertIn("strict native window Initialize failed", self.smoke)
        self.assertNotIn("return 77", self.smoke)
        report_target = self.cmake[
            self.cmake.index(
                "add_custom_target(\n        ror_ogre_next_frontend_n1_report"
            ) : self.cmake.index(
                "add_test(NAME ror_ogre_next_frontend_n1_runtime"
            )
        ]
        self.assertEqual(
            report_target.count("ror_ogre_next_window_present_smoke"), 1
        )
        for artifact in (
            "ogre-next-window-present.json",
            "ogre-next-window-present.ppm",
        ):
            self.assertIn(artifact, self.workflow)
        self.assertIn(
            "ror-ogre-next-n1-package/share/ror/ogre-next/media",
            self.workflow,
        )


if __name__ == "__main__":
    unittest.main()
