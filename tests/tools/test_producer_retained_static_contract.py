#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static closure checks for the producer-side retained static section.

The reuse decisions live inside a transaction that needs a live Metal device
and a loaded CityWorld to execute, so the C++ suites cannot reach them. These
checks lock what makes the reuse sound rather than merely fast: the union
semantics of the new frame-input owners, the byte-identity attestation that
keeps the snapshot gate honest, the conditions that each independently disable
reuse, and the rotating audit that fails a frame closed instead of repairing
it.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ProducerRetainedStaticContractTests(unittest.TestCase):
    def setUp(self) -> None:
        render = ROOT / "source/main/gfx/render"
        self.producer_header = (
            render / "GraphicsSceneSnapshotProducer.h").read_text()
        self.producer = (
            render / "GraphicsSceneSnapshotProducer.cpp").read_text()
        self.snapshot_header = (render / "SceneSnapshot.h").read_text()
        self.snapshot = (render / "SceneSnapshot.cpp").read_text()
        self.readme = (render / "README.md").read_text()

    def test_frame_input_declares_owners_at_a_bumped_version(self) -> None:
        match = re.search(
            r"kGraphicsSceneSnapshotProducerVersion\s*=\s*(\d+)U;",
            self.producer_header,
        )
        self.assertIsNotNone(match)
        self.assertGreaterEqual(int(match.group(1)), 7)
        for token in (
            "std::shared_ptr<const std::vector<GraphicsSceneAssetInput>>\n"
            "      retained_static_assets;",
            "std::shared_ptr<const std::vector<GraphicsSceneStaticMeshInput>>\n"
            "      retained_static_meshes;",
        ):
            self.assertIn(token, self.producer_header, token)
        # Omission must keep destroying, and a change must mint a new owner.
        self.assertIn("disjoint union", self.producer_header)
        self.assertIn("MUST\n  /// mint new owner vectors", self.producer_header)

    def test_owner_order_is_proven_not_assumed(self) -> None:
        self.assertIn('"retained_static.order"', self.producer)
        anchor = self.producer.index('"retained_static.order"')
        window = self.producer[anchor - 900 : anchor + 400]
        self.assertIn("SEQUENCE_MISMATCH", window)
        self.assertIn("strictly increasing", window)

    def test_snapshot_gate_requires_byte_identity_for_reused_entries(
        self,
    ) -> None:
        self.assertIn(
            "CreateSceneSnapshotWithRetainedBlock", self.snapshot_header)
        anchor = self.snapshot.index(
            "SceneSnapshotCreateResult CreateSceneSnapshotWithRetainedBlock")
        body = self.snapshot[anchor : anchor + 4000]
        self.assertIn("std::is_trivially_copyable<MeshInstanceDescriptor>",
                      body)
        self.assertIn("std::memcmp", body)
        self.assertIn('"retained_block.attestation"', body)
        self.assertIn("ValidateSceneSnapshotDescriptorInternal", body)
        # Patched entries and the seams around them are still validated.
        self.assertIn("&patched_indices", body)

    def test_producer_copies_the_block_as_bytes(self) -> None:
        # A value-wise vector copy is not obliged to carry structure padding,
        # which the attestation compares, so the block must be memcpy'd.
        anchor = self.producer.index("descriptor.mesh_instances.resize(")
        window = self.producer[anchor - 400 : anchor + 400]
        self.assertIn("std::memcpy(descriptor.mesh_instances.data()", window)

    def test_every_reuse_precondition_is_present(self) -> None:
        anchor = self.producer.index("const bool retained_block_possible =")
        window = self.producer[anchor : anchor + 900]
        for condition in (
            "retained_owner_matches",
            "retained_static.block_source != nullptr",
            "retained_static.history_settled",
            "retained_static.catalog_stable_for_section",
            "frame.absolute_world_origin_meters == retained_static.block_origin",
            "frame.static_meshes.empty()",
            "retained_static.dynamic_ids.size()",
        ):
            self.assertIn(condition, window, condition)
        # A catalog transaction touching the retained section is the last gate.
        self.assertIn(
            "retained_block_candidate && !retained_section_asset_mutated",
            self.producer,
        )
        self.assertIn("retained_section_asset_mutated = true;", self.producer)

    def test_cache_advances_only_in_the_commit_block(self) -> None:
        publish = self.producer.index(
            "std::atomic_store_explicit(&published_snapshot")
        commit = self.producer.index("asset_catalog = std::move(candidate_asset_catalog);")
        for assignment in (
            "retained_static.block_source = created.snapshot;",
            "retained_static.catalog_stable_for_section = true;",
            "retained_static.verify_instance_cursor =",
        ):
            position = self.producer.index(assignment)
            self.assertLess(commit, position, assignment)
            self.assertLess(position, publish, assignment)
        # The one pre-commit mutation is dropping the cache after an audit
        # failure, which withdraws a memoization rather than advancing state.
        drop = "retained_static = RetainedStaticSectionCache{};"
        self.assertLess(commit, self.producer.rindex(drop))
        self.assertLess(self.producer.rindex(drop), publish)
        position = self.producer.find(drop)
        while 0 <= position < commit:
            preceding = self.producer[position - 400 : position]
            self.assertIn("retained_static.", preceding)
            self.assertIn("result.validation = Failure(", preceding)
            position = self.producer.find(drop, position + len(drop))

    def test_rotating_window_rejects_instead_of_repairing(self) -> None:
        for token in (
            "kRetainedStaticVerifyInstanceWindow = 128U",
            "kRetainedStaticVerifyAssetWindow = 64U",
            "ROR_PRODUCER_RETAINED_AUDIT",
            '"retained_static.window"',
            "EqualMeshInstanceContents",
        ):
            self.assertIn(token, self.producer, token)
        anchor = self.producer.index('"retained_static.window"')
        window = self.producer[anchor - 1200 : anchor + 400]
        self.assertIn("return result;", window)
        self.assertIn("REVISION_MISMATCH", window)
        # Keeping the cache after an audit failure would reject the same owner
        # forever instead of re-validating it once on the full path.
        for field in (
            '"retained_static.positions"',
            '"retained_static.history"',
            '"retained_static.window"',
        ):
            start = self.producer.index(field)
            self.assertIn(
                "retained_static = RetainedStaticSectionCache{};",
                self.producer[start : start + 400],
                field,
            )

    def test_retained_audit_environment_read_is_msvc_safe(self) -> None:
        start = self.producer.index("bool RetainedAuditEverythingRequested()")
        end = self.producer.index("ValidationResult Failure(", start)
        helper = self.producer[start:end]
        self.assertIn("#if defined(_WIN32)", helper)
        self.assertIn("_dupenv_s(&setting, &setting_size", helper)
        self.assertIn("std::free(setting);", helper)
        self.assertIn("#else", helper)
        self.assertIn("std::getenv(\"ROR_PRODUCER_RETAINED_AUDIT\")", helper)
        self.assertIn(
            "RetainedAuditEverythingRequested();", self.producer
        )

    def test_scoped_compatibility_replaces_the_dynamic_forcing_term(
        self,
    ) -> None:
        anchor = self.producer.index(
            "const bool requires_full_asset_compatibility_validation =")
        window = self.producer[anchor : anchor + 400]
        self.assertNotIn("!sorted_dynamic_objects.empty()", window)
        self.assertIn("candidate_mesh_asset_pairs != validated_mesh_asset_pairs",
                      window)
        self.assertIn("ValidateSceneSnapshotAssetsScoped", self.producer)
        self.assertIn("scene_asset_compatibility_scoped_validations",
                      self.producer_header)

    def test_generation_finalize_drops_the_cache(self) -> None:
        anchor = self.producer.index(
            "if (finalize_scene_generation || !retained_owner_present) {")
        window = self.producer[anchor : anchor + 300]
        self.assertIn("retained_static = RetainedStaticSectionCache{};", window)

    def test_readme_states_the_owner_contract(self) -> None:
        collapsed = " ".join(self.readme.split())
        self.assertIn("retained_static_assets", collapsed)
        self.assertIn("one disjoint union", collapsed)
        self.assertIn("must mint a new vector for any change", collapsed)
        self.assertIn("CreateSceneSnapshotWithRetainedBlock", collapsed)
        self.assertIn("retained_static.window", collapsed)


if __name__ == "__main__":
    unittest.main()
