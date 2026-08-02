#!/usr/bin/env python3
"""Fail-closed source contract for persistent Ogre-Next presentation."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class OgreNextWindowRunLoopContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "source/main/gfx/render/ogrenext/OgreNextN1Frontend.h"
        ).read_text(encoding="utf-8")
        cls.frontend = (
            ROOT / "source/main/gfx/render/ogrenext/OgreNextN1Frontend.cpp"
        ).read_text(encoding="utf-8")

    def test_production_mode_is_separate_and_not_the_default(self) -> None:
        self.assertIn("enum class OgreNextN1PresentationMode", self.header)
        self.assertIn("EXACT_ONE_FRAME_GATE = 0", self.header)
        self.assertIn("PRODUCTION_RUN_LOOP", self.header)
        configuration = self.header[
            self.header.index("struct OgreNextN1PresentationConfiguration") :
            self.header.index("struct OgreNextN1PresentationAudit")
        ]
        self.assertIn(
            "OgreNextN1PresentationMode::EXACT_ONE_FRAME_GATE", configuration
        )
        self.assertIn(
            "configuration.mode !=\n"
            "            OgreNextN1PresentationMode::EXACT_ONE_FRAME_GATE",
            self.frontend,
        )

    def test_exact_one_frame_rejection_remains_mode_guarded(self) -> None:
        render = self.frontend[self.frontend.index("OgreNextN1Frontend::Render(") :]
        rejection = render.index(
            "the first native presentation gate admits exactly one presented frame"
        )
        guarded = render.rfind("if (!production_presentation", 0, rejection)
        self.assertNotEqual(guarded, -1)
        self.assertIn(
            "impl_->presentation_audit.presented_frames != 0U",
            render[guarded:rejection],
        )

    def test_production_graph_has_stable_gpu_only_two_channel_shape(self) -> None:
        graph = self.frontend[
            self.frontend.index("RebindProductionPresentationWorkspace(") :
            self.frontend.index("[[nodiscard]] bool DestroyPresentationResources()")
        ]
        for token in (
            "kProductionPresentationTargetName",
            "kProductionPresentationNodeName",
            "kProductionPresentationWorkspaceName",
            "Ogre::TextureFlags::RenderToTexture",
            '"MainRT", 0U',
            '"PresentationRT", 1U',
            "node->setNumTargetPass(2U)",
            "main_target->addPass(Ogre::PASS_SCENE)",
            "scene->mIncludeOverlays = false",
            "presentation_target->addPass(Ogre::PASS_QUAD)",
            'copy->mMaterialName = "Ogre/Copy/4xFP32"',
            "channels.push_back(production_source_target)",
            "channels.push_back(window_texture)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, graph)
        self.assertNotIn("memcpy", graph)

    def test_render_reuses_owned_graph_instead_of_frame_local_resources(self) -> None:
        render = self.frontend[self.frontend.index("OgreNextN1Frontend::Render(") :]
        production = render[
            render.index("if (production_presentation) {") :
            render.index("} else if (!persistent_hdr)")
        ]
        self.assertIn("EnsureProductionPresentationGraph", production)
        self.assertIn("impl_->production_source_target", production)
        self.assertIn("impl_->production_workspace", production)
        cleanup = render[
            render.index("const auto cleanup_scene") :
            render.index("const auto destroy_retained_target")
        ]
        self.assertGreaterEqual(
            cleanup.count("!persistent_hdr && !production_presentation"), 2
        )

    def test_surface_transitions_retire_before_graph_rebuild(self) -> None:
        update = self.frontend[
            self.frontend.index("OgreNextN1Frontend::UpdateSurface(") :
            self.frontend.index("OgreNextN1Frontend::SynchronizeAssets(")
        ]
        self.assertIn("retire_production_graph", update)
        self.assertIn("DestroyProductionPresentationGraph()", update)
        self.assertIn("suspended_surface_updates", update)
        self.assertIn("restored_surface_updates", update)
        self.assertIn("production_window_texture", update)
        cleanup = self.frontend[
            self.frontend.index("[[nodiscard]] bool CleanupBackend()") :
            self.frontend.index("Ogre::AbiCookie abi_cookie")
        ]
        self.assertLess(
            cleanup.index("DestroyProductionPresentationGraph()"),
            cleanup.index("destroySceneManager"),
        )

    def test_audit_tracks_lifetime_and_monotonic_frames(self) -> None:
        for token in (
            "monotonic_presented_frame_ids",
            "show_callback_calls",
            "source_target_creates",
            "source_target_destroys",
            "compositor_node_definition_creates",
            "compositor_node_definition_destroys",
            "compositor_workspace_creates",
            "compositor_workspace_destroys",
            "compositor_workspace_rebinds",
            "surface_graph_rebuilds",
            "first_presented_frame_id",
            "last_presented_frame_id",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header)
                self.assertIn(f"presentation_audit.{token}", self.frontend)
        self.assertIn(
            "request.frame_id >\n"
            "                impl_->presentation_audit.last_presented_frame_id",
            self.frontend,
        )


if __name__ == "__main__":
    unittest.main()
