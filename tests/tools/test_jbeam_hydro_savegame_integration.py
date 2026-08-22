#!/usr/bin/env python3
"""Production wiring checks for native JBeam hydro savegame state."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class JBeamHydroSavegameIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (ROOT / "source/main/physics/Savegame.cpp").read_text()

    def test_capture_uses_validated_actor_snapshot(self) -> None:
        start = self.source.index("bool BuildJBeamHydroPayload(")
        end = self.source.index("bool TryStageJBeamHydroRestore(", start)
        body = self.source[start:end]
        self.assertIn("BuildLiveJBeamHydros(actor, nullptr", body)
        self.assertIn("JBeamHydroSavegame::TryCapture(", body)

        save = self.source.index("bool ActorManager::SaveScene(")
        actor_entry = self.source.index(
            "JBeamHydroSavegame::ActorPayload hydro_payload;", save
        )
        serialize = self.source.index(
            "JBeamHydroSavegame::SerializeJson(", actor_entry
        )
        publish_actor = self.source.index(
            "j_actors.PushBack(j_entry", serialize
        )
        self.assertLess(actor_entry, serialize)
        self.assertLess(serialize, publish_actor)
        self.assertIn('"jbeam_hydro_state_v1"', self.source)

    def test_restore_stages_before_any_actor_mutation(self) -> None:
        restore = self.source.index("bool ActorManager::RestoreSavedState(")
        stage = self.source.index("TryStageJBeamHydroRestore(", restore)
        first_mutation = self.source.index("actor->m_spawn_rotation =", restore)
        self.assertLess(stage, first_mutation)

        beam_restore = self.source.index(
            "actor->ar_beams[i].L                  = data[4].GetFloat();",
            restore,
        )
        state_publish = self.source.index(
            "hydro.hb_jbeam_state = restored.state;", beam_restore
        )
        length_publish = self.source.index(
            "restored.runtime_rest_length;", state_publish
        )
        hooks = self.source.index('auto hooks = j_entry["hooks"]', length_publish)
        self.assertLess(beam_restore, state_publish)
        self.assertLess(state_publish, length_publish)
        self.assertLess(length_publish, hooks)

    def test_present_payload_fails_closed_without_legacy_fallback(self) -> None:
        helper = self.source[
            self.source.index("bool TryStageJBeamHydroRestore(") :
            self.source.index("void FailClosedMaterialRestore(")
        ]
        self.assertIn(
            "has_payload = j_entry.HasMember(JBEAM_HYDRO_STATE_MEMBER);",
            helper,
        )
        self.assertIn("if (!has_payload)\n        return true;", helper)
        self.assertIn("JBeamHydroSavegame::ParseJson", helper)
        self.assertIn("JBeamHydroSavegame::TryStage", helper)

        fail_closed = self.source[
            self.source.index("void FailClosedJBeamHydroRestore(") :
            self.source.index("} // namespace")
        ]
        self.assertIn("if (!hydro.hb_has_jbeam_runtime)", fail_closed)
        self.assertIn("hydro.hb_jbeam_state.fault_latched = true;", fail_closed)
        self.assertIn(
            "JBeamHydroRuntimeFault::INVALID_PREVIOUS_STATE", fail_closed
        )


if __name__ == "__main__":
    unittest.main()
