#!/usr/bin/env python3
"""Product-closure checks for deterministic scenario actor identity."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class DeterministicScenarioIdentityContractTests(unittest.TestCase):
    def test_spawn_gate_precedes_allocation_and_registration(self) -> None:
        source = (ROOT / "source/main/physics/ActorManager.cpp").read_text()
        start = source.index(
            "ActorPtr ActorManager::CreateNewActor(ActorSpawnRequest rq,"
        )
        end = source.index("void ActorManager::RemoveStreamSource(", start)
        body = source[start:end]
        resolve = body.index("DeterministicScenarioIdentity::Resolve(")
        duplicate = body.index("Rejected duplicate live actor")
        reserve = body.index("m_actors.reserve(")
        allocate = body.index("ActorPtr actor = new Actor(")
        register = body.index("RegisterGfxActor(")
        self.assertLess(resolve, duplicate)
        self.assertLess(duplicate, reserve)
        self.assertLess(reserve, allocate)
        self.assertLess(allocate, register)

    def test_actor_uses_resolved_scenario_identity(self) -> None:
        source = (ROOT / "source/main/physics/Actor.cpp").read_text()
        start = source.index("Actor::Actor(")
        body = source[start : start + 6500]
        self.assertIn("rq.asr_deterministic_scenario_seed", body)
        self.assertIn("rq.asr_deterministic_actor_stream_id", body)
        self.assertIn(
            "m_deterministic_seed = identity.deterministic_seed;", body
        )
        self.assertNotIn("MakeActorSeed(\n            static_cast", body)

    def test_savegame_prevalidates_exact_identity_set(self) -> None:
        source = (ROOT / "source/main/physics/Savegame.cpp").read_text()
        load = source.index("bool ActorManager::LoadScene(")
        validate = source.index(
            "ValidateSavedDeterministicScenarioIdentities", load
        )
        stage = source.index("StageDeterministicActorInputSavegame(", load)
        first_scene_mutation = source.index("m_forced_awake =", load)
        self.assertLess(validate, stage)
        self.assertLess(validate, first_scene_mutation)
        helper = source[
            source.index("bool TryReadSavedDeterministicScenarioIdentity(") : load
        ]
        self.assertIn("identity.MemberCount() != 3U", helper)
        self.assertIn("RevalidatesStoredSeed(", helper)
        self.assertIn("!identities.insert", source)

    def test_save_and_spawn_retain_identity(self) -> None:
        source = (ROOT / "source/main/physics/Savegame.cpp").read_text()
        self.assertIn('"deterministic_scenario_identity_v1"', source)
        self.assertIn(
            "actor->m_deterministic_scenario_seed", source
        )
        self.assertIn(
            "rq->asr_deterministic_scenario_seed =", source
        )
        self.assertIn(
            "actor->m_deterministic_actor_stream_id = "
            "saved_identity.actor_stream_id;",
            source,
        )

    def test_script_api_is_explicit_and_additive(self) -> None:
        implementation = (ROOT / "source/main/scripting/GameScript.cpp").read_text()
        binding = (
            ROOT / "source/main/scripting/bindings/GameScriptAngelscript.cpp"
        ).read_text()
        self.assertEqual(
            implementation.count("ActorPtr GameScript::spawnTruckDeterministic("),
            1,
        )
        self.assertIn(
            "rq.asr_deterministic_scenario_seed = scenarioSeed;",
            implementation,
        )
        self.assertIn(
            "rq.asr_deterministic_actor_stream_id = actorStreamId;",
            implementation,
        )
        self.assertIn(
            "if (scenarioSeed == 0U || actorStreamId == 0U)",
            implementation,
        )
        self.assertIn(
            'spawnTruckDeterministic(string &in, vector3 &in, vector3 &in, '
            'uint64, uint64)',
            binding,
        )
        self.assertEqual(
            implementation.count("ActorPtr GameScript::spawnTruck("), 1
        )

    def test_script_actor_receipt_is_read_only(self) -> None:
        binding = (
            ROOT / "source/main/scripting/bindings/ActorAngelscript.cpp"
        ).read_text()
        self.assertIn(
            'bool hasExplicitDeterministicScenarioIdentity() const', binding
        )
        self.assertIn(
            'uint64 getDeterministicScenarioSeed() const', binding
        )
        self.assertIn(
            'uint64 getDeterministicActorStreamId() const', binding
        )
        self.assertNotIn("setDeterministicScenarioSeed", binding)
        self.assertNotIn("setDeterministicActorStreamId", binding)

    def test_capture_seed_override_revokes_old_explicit_pair(self) -> None:
        source = (ROOT / "source/main/physics/Actor.cpp").read_text()
        start = source.index("bool Actor::PrepareWorldModelCaptureReset(")
        body = source[start : start + 1800]
        clear_scenario = body.index("m_deterministic_scenario_seed = 0U;")
        clear_stream = body.index("m_deterministic_actor_stream_id = 0U;")
        override_seed = body.index("m_deterministic_seed = reset_seed;")
        self.assertLess(clear_scenario, override_seed)
        self.assertLess(clear_stream, override_seed)

    def test_bundle_reload_retains_stable_identity(self) -> None:
        source = (ROOT / "source/main/GameContext.cpp").read_text()
        reload_start = source.index(
            "else if (rq.amr_type == ActorModifyRequest::Type::RELOAD)"
        )
        reload_end = source.index(
            "else if (rq.amr_type == ActorModifyRequest::Type::SOFT_RESPAWN)",
            reload_start,
        )
        block = source[reload_start:reload_end]
        copy_scenario = block.index("actor->GetDeterministicScenarioSeed()")
        copy_stream = block.index("actor->GetDeterministicActorStreamId()")
        delete_bundle = block.index("MSG_EDI_RELOAD_BUNDLE_REQUESTED")
        respawn = block.index("MSG_SIM_SPAWN_ACTOR_REQUESTED")
        self.assertLess(copy_scenario, delete_bundle)
        self.assertLess(copy_stream, delete_bundle)
        self.assertLess(delete_bundle, respawn)


if __name__ == "__main__":
    unittest.main()
