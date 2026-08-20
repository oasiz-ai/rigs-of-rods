#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static closure checks for rigid actor geometry - mesh-wheel rims and props.

A FlexMeshWheel owns two meshes: the deformable tyre band and a rigid rim on
its own re-posed scene node. Prop meshes are rigid too, and the static path
walks only the terrain object inventory, so neither family reached the
combined runtime until the capture enumerated them explicitly. Both are only
observable with a live Metal device, so the wiring the C++ suites cannot see is
locked here: which owners are enumerated, which node each transform is read
from, that identities cannot collide, and that nothing is dropped unnamed.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RigidActorCaptureContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.scene = (ROOT / "source/main/gfx/GfxScene.cpp").read_text()
        self.header = (ROOT / "source/main/gfx/GfxScene.h").read_text()
        self.source_header = (
            ROOT / "source/main/gfx/render/Ogre14GraphicsSceneSource.h"
        ).read_text()
        self.source = (
            ROOT / "source/main/gfx/render/Ogre14GraphicsSceneSource.cpp"
        ).read_text()
        self.flex_wheel = (
            ROOT / "source/main/physics/flex/FlexMeshWheel.h"
        ).read_text()

    def test_rigid_families_have_their_own_permanent_component_kinds(self) -> None:
        # The component kind is one byte of the section identity key, so a rim
        # or prop section can never collide with a tyre or a flexbody.
        start = self.source_header.index(
            "enum class Ogre14GraphicsSceneDynamicComponentKind"
        )
        block = self.source_header[start : start + 500]
        for entry in (
            "CAB = 0U,",
            "FLEXBODY = 1U,",
            "FLEXMESH_WHEEL = 2U,",
            "MESHWHEEL_TIRE = 3U,",
            "MESHWHEEL_RIM = 4U,",
            "PROP = 5U,",
            "PROP_STEERING_WHEEL = 6U,",
        ):
            self.assertIn(entry, block, entry)

    def test_identity_validation_accepts_every_declared_kind(self) -> None:
        # A kind the enum declares but IsKnownDynamicComponentKind rejects
        # would fail identity derivation for every section carrying it.
        start = self.source.index("bool IsKnownDynamicComponentKind(")
        block = self.source[start : start + 700]
        for kind in (
            "CAB",
            "FLEXBODY",
            "FLEXMESH_WHEEL",
            "MESHWHEEL_TIRE",
            "MESHWHEEL_RIM",
            "PROP",
            "PROP_STEERING_WHEEL",
        ):
            self.assertIn(
                "Ogre14GraphicsSceneDynamicComponentKind::" + kind, block, kind
            )

    def test_the_wheel_arm_captures_the_rim_from_the_rim_node(self) -> None:
        # The tyre hangs off a node that is never oriented; reading the rim's
        # transform from it would freeze the rim flat at the wheel centre.
        self.assertIn("Ogre::Entity* GetRimEntity()", self.flex_wheel)
        self.assertIn("Ogre::SceneNode* GetRimSceneNode()", self.flex_wheel)
        start = self.scene.index("kOgre14CaptureEnumeratesMeshWheelRims)")
        window = self.scene[start : start + 1200]
        self.assertIn(
            "Ogre14GraphicsSceneDynamicComponentKind::MESHWHEEL_RIM", window
        )
        self.assertIn("meshwheel->GetRimEntity()", window)
        self.assertIn("meshwheel->GetRimSceneNode()", window)

    def test_props_are_enumerated_with_their_own_pivot_nodes(self) -> None:
        start = self.scene.index("if (kOgre14CaptureEnumeratesProps)")
        window = self.scene[start : start + 6000]
        self.assertIn("actor->getProps()", window)
        self.assertIn("prop.pp_mesh_obj->getEntity(), prop.pp_scene_node", window)
        self.assertIn("prop.pp_wheel_mesh_obj->getEntity()", window)
        self.assertIn("prop.pp_wheel_scene_node", window)
        self.assertIn(
            "Ogre14GraphicsSceneDynamicComponentKind::PROP", window
        )
        self.assertIn("PROP_STEERING_WHEEL", window)
        # pp_id is the creation identity; a vector position would renumber.
        self.assertIn("static_cast<std::uint32_t>(prop.pp_id)", window)

    def test_props_whose_visual_state_cannot_be_captured_are_refused(self) -> None:
        start = self.scene.index("if (kOgre14CaptureEnumeratesProps)")
        window = self.scene[start : start + 4200]
        # Mirrors and video cameras are textured by a native RenderTexture the
        # combined runtime never draws.
        self.assertIn("videocamera.vcam_prop_scenenode", window)
        self.assertIn("renders_into_a_native_render_target", window)
        # Beacon flares are BillboardSets, not meshes.
        self.assertIn("refused_beacon_flare_billboards", window)
        # A tuneup-emptied slot draws nothing in the legacy scene either.
        self.assertIn("prop.pp_mesh_obj == nullptr", window)

    def test_every_rigid_refusal_is_named_and_counted(self) -> None:
        start = self.scene.index("struct Ogre14RigidActorCaptureCounters")
        ledger = self.scene[start : start + 1800]
        for counter in (
            "refused_detached_entity",
            "refused_animated_entity",
            "refused_rendering_distance",
            "refused_submesh_inventory",
            "refused_unexpected_parent_node",
            "refused_noncanonical_transform",
            "refused_mirrored_transform",
            "refused_non_uniform_scale",
            "refused_render_target_material",
            "refused_runtime_mutated_material",
            "refused_beacon_flare_billboards",
            "refused_geometry",
            "refused_material",
            "last_refusal",
        ):
            self.assertIn(counter, ledger, counter)
        # Each counter reaches the audit line, so a refusal is never a count
        # nobody can read.
        start = self.scene.index("ReportOgre14RigidActorCaptureCoverage(")
        report = self.scene[start : start + 2200]
        for counter in (
            "refused_detached_entity={}",
            "refused_unexpected_parent_node={}",
            "refused_mirrored_transform={}",
            "refused_non_uniform_scale={}",
            "refused_render_target_material={}",
            "refused_runtime_mutated_material={}",
            "refused_beacon_flare_billboards={}",
            "last_refusal='{}'",
        ):
            self.assertIn(counter, report, counter)

    def test_a_mirrored_or_non_uniform_transform_cannot_fail_the_frame(self) -> None:
        # The inventory builder rejects both outright, and the presenter drops
        # a non-uniformly scaled instance. One odd prop must not blank the car.
        start = self.scene.index(
            "RoR::Render::ValidationResult CaptureOgre14RigidActorEntitySections("
        )
        body = self.scene[start : start + 6000]
        self.assertIn("HasInvertibleAffineTransform(render_from_object)", body)
        self.assertIn("LinearDeterminant(render_from_object) < 0.0F", body)
        self.assertIn(
            "HasEffectivelyUniformLinearScale(render_from_object)", body
        )

    def test_admission_is_frozen_for_the_producer_lifetime(self) -> None:
        # A published section identity may never disappear and return, so a
        # transient refusal must not retire an identity the next frame brings
        # back - the inventory builder rejects that outright.
        start = self.scene.index(
            "RoR::Render::ValidationResult CaptureOgre14FrozenRigidActorComponent("
        )
        body = self.scene[start : start + 2400]
        self.assertIn("BuildOgre14RigidActorComponentKey(identity)", body)
        self.assertIn("frozen_decisions.find(decision_key)", body)
        self.assertIn("frozen_decisions.emplace(decision_key, admitted)", body)
        self.assertIn(
            "an admitted rigid actor component stopped being capturable", body
        )
        self.assertIn(
            "m_ogre14_rigid_actor_capture_decisions", self.header
        )
        # The ledger is map-generation scoped, like every other capture owner.
        reset = self.scene[
            self.scene.index("void GfxScene::ResetOgre14GraphicsSceneGeneration") :
        ][:4000]
        self.assertIn("m_ogre14_rigid_actor_capture_decisions.clear();", reset)
        self.assertIn(
            "m_ogre14_actor_capture_coverage_log_snapshots.clear();", reset
        )

    def test_rigid_geometry_is_read_once_and_only_the_transform_republished(
        self,
    ) -> None:
        start = self.scene.index(
            "RoR::Render::ValidationResult CaptureOgre14RigidActorEntitySections("
        )
        body = self.scene[start : start + 20000]
        # Its own cache namespace, so a rigid entry can never satisfy a
        # deformable's cache probe or the reverse.
        self.assertIn('cache_key.append("/OgreNextDemoRT4Rigid/v1");', body)
        self.assertIn("if (cache_matches)", body)
        self.assertIn("ExtractOgre14CpuMeshSection(", body)
        # Dynamic storage is required: the section republishes a full state
        # every frame exactly as a deformable does.
        self.assertIn("candidate_mesh.dynamic = true;", body)
        # The per-frame state is the immutable object-space geometry itself,
        # which is what lets the builder reuse the previous deformation owner.
        self.assertIn("state->positions = published_mesh.positions;", body)
        self.assertIn(
            "state->updated_local_bounds = published_mesh.local_bounds;", body
        )
        # That state is constant, so it is built once per immutable payload
        # and only reused while the payload owner it came from is the one
        # being published.
        self.assertIn("state_cache.find(cache_key)", body)
        self.assertIn(
            "cached_state->second.payload != section.mesh_payload", body
        )
        self.assertIn("section.state = cached_state->second.state;", body)
        self.assertIn(
            "m_ogre14_rigid_actor_state_cache.clear();", self.scene
        )
        self.assertIn("m_ogre14_rigid_actor_state_cache", self.header)

    def test_the_spawn_probe_shares_its_coverage_flags_with_the_capture(
        self,
    ) -> None:
        # A probe with its own copy of "what is enumerated" would drift from
        # the capture it audits and quietly under-report the gap.
        self.assertIn(
            "constexpr bool kOgre14CaptureEnumeratesMeshWheelRims = true;",
            self.scene,
        )
        self.assertIn(
            "constexpr bool kOgre14CaptureEnumeratesProps = true;", self.scene
        )
        start = self.scene.index(
            "void GfxScene::ProbeOgre14ActorCaptureCoverage("
        )
        probe = self.scene[start : start + 6000]
        self.assertIn("kOgre14CaptureEnumeratesMeshWheelRims", probe)
        self.assertIn("kOgre14CaptureEnumeratesProps", probe)
        self.assertIn("meshwheel->GetRimEntity()", probe)
        self.assertIn("prop.pp_mesh_obj->getEntity()", probe)
        self.assertIn("unenumerated={}/{}", probe)


if __name__ == "__main__":
    unittest.main()
