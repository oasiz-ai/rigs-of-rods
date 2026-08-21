#!/usr/bin/env python3
"""Static closure checks for the production D0 Actor input owner."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class DeterministicInputLiveRuntimeContractTests(unittest.TestCase):
    def test_non_archived_lifecycle_cvars_are_explicit(self) -> None:
        source = (ROOT / "source/main/system/CVar.cpp").read_text()
        for name in (
            "sim_deterministic_input_mode",
            "sim_deterministic_input_path",
            "sim_deterministic_input_scenario_id",
            "sim_deterministic_input_target_id",
            "sim_deterministic_input_step_limit",
        ):
            anchor = f'App::{name} = this->cVarCreate('
            self.assertEqual(source.count(anchor), 1)
            block = source[source.index(anchor) : source.index(anchor) + 420]
            self.assertNotIn("CVAR_ARCHIVE", block)

    def test_fixed_step_order_is_input_then_observer_then_solver(self) -> None:
        source = (ROOT / "source/main/physics/ActorManager.cpp").read_text()
        function = source[source.index(
            "void ActorManager::UpdatePhysicsSimulation(\n"
            "    FixedStepCaptureBridge::ObservationBatch* observation_batch)"
        ) :]
        input_offset = function.index("ProcessDeterministicActorInputStep()")
        observer_offset = function.index("ObserveFixedStepStart(")
        state_trace_offset = function.index("PrepareDeterministicStateTraceStep()")
        solver_offset = function.index("CalcForcesEulerPrepare")
        self.assertLess(input_offset, observer_offset)
        self.assertLess(observer_offset, state_trace_offset)
        self.assertLess(state_trace_offset, solver_offset)

    def test_state_trace_binds_the_exact_accepted_input_prefix(self) -> None:
        source = (ROOT / "source/main/physics/ActorManager.cpp").read_text()
        process_start = source.index(
            "bool ActorManager::ProcessDeterministicActorInputStep()"
        )
        process_end = source.index(
            "bool ActorManager::PrepareDeterministicStateTraceStep()",
            process_start,
        )
        process = source[process_start:process_end]
        reset = process.index(
            "m_deterministic_input_step_digest_valid = false;"
        )
        accepted = process.index("if (!advanced)")
        prefix = process.index("GetProcessedPrefixDigest().bytes", accepted)
        teardown = process.index(
            "this->FinishDeterministicActorInput(", prefix
        )
        self.assertLess(reset, accepted)
        self.assertLess(accepted, prefix)
        self.assertLess(prefix, teardown)

        capture_start = source.index(
            "void ActorManager::CaptureDeterministicStateTraceStep("
        )
        capture_end = source.index(
            "void ActorManager::UpdatePhysicsSimulation()", capture_start
        )
        capture = source[capture_start:capture_end]
        binding = capture.index(
            "record.input_flags =\n"
            "            DeterministicStateTrace::"
            "STEP_INPUT_AUTHENTICATED_PREFIX;"
        )
        append = capture.index("runtime.writer->Append(record)")
        self.assertLess(binding, append)
        self.assertIn(
            "m_deterministic_input_step_digest_physics_step ==\n"
            "            m_completed_physics_steps",
            capture,
        )

    def test_replay_suppresses_unrecorded_actor_input(self) -> None:
        source = (ROOT / "source/main/main.cpp").read_text()
        anchor = "const bool deterministic_replay_owns_input ="
        self.assertEqual(source.count(anchor), 1)
        block = source[source.index(anchor) : source.index(anchor) + 1900]
        self.assertIn("ShouldSuppressLiveInputForDeterministicReplay(actor)", block)
        guard = block.index("if (!deterministic_replay_owns_input)")
        self.assertGreater(block.index("UpdateCommonInputEvents", guard), guard)
        self.assertGreater(block.index("UpdateTruckInputEvents", guard), guard)
        self.assertGreater(block.index("UpdatePropAnimInputEvents", guard), guard)

    def test_policy_is_fail_closed_and_transactional(self) -> None:
        header = (ROOT / (
            "source/main/physics/DeterministicVehicleInputActorAdapter.h"
        )).read_text()
        implementation = (ROOT / (
            "source/main/physics/DeterministicVehicleInputActorAdapter.cpp"
        )).read_text()
        manager = (ROOT / "source/main/physics/ActorManager.cpp").read_text()
        self.assertIn("UNSUPPORTED_GEARBOX", header)
        self.assertIn("GEAR_CHANGE_UNSUPPORTED", header)
        self.assertIn("CONTROLLER_ENABLED", header)
        self.assertIn("SIMULATED_EVENT_OVERRIDE", header)
        self.assertIn("policy.gearbox_mode != MANUAL_GEARBOX_MODE", implementation)
        self.assertIn("candidate.controls.gear != policy.fixed_gear", implementation)
        self.assertIn("ApplyPlan plan;", manager)
        self.assertLess(
            manager.index("BuildApplyPlan(\n            current_policy"),
            manager.index("CommitActorInputPlan(actor, plan)"),
        )

    def test_identity_and_io_are_authenticated_and_bounded(self) -> None:
        source = (ROOT / "source/main/physics/ActorManager.cpp").read_text()
        self.assertIn("REQUIRED_SEMANTIC_FLAGS", source)
        self.assertIn("physics_step_denominator = 2000", source)
        self.assertIn("RegistrySourceName()", source)
        self.assertIn("ComputeSha256(", source)
        self.assertIn("MAX_LIVE_INPUT_BYTES", source)
        self.assertIn("CreateEmptyFileExclusive(path)", source)
        self.assertIn("BeginReplay(bytes, metadata, limits)", source)
        self.assertIn("FinalizeRecording(bytes)", source)
        self.assertIn("App::app_async_physics->getBool()", source)

    def test_worker_pause_request_is_transferred_atomically(self) -> None:
        header = (ROOT / "source/main/physics/ActorManager.h").read_text()
        source = (ROOT / "source/main/physics/ActorManager.cpp").read_text()
        self.assertIn("std::atomic<bool>", header)
        self.assertIn(
            "m_deterministic_actor_input_pause_requested.store(", source
        )
        exchange = source.index(
            "m_deterministic_actor_input_pause_requested.exchange("
        )
        pause = source.index("m_simulation_paused = true;", exchange)
        update = source.index("void ActorManager::UpdateActors(")
        self.assertLess(update, exchange)
        self.assertLess(exchange, pause)

    def test_engine_has_narrow_noexcept_ignition_commit(self) -> None:
        header = (ROOT / "source/main/gameplay/Engine.h").read_text()
        implementation = (ROOT / "source/main/gameplay/Engine.cpp").read_text()
        self.assertIn(
            "setDeterministicInputIgnition(\n"
            "                       bool contact,\n"
            "                       bool starter) noexcept;",
            header,
        )
        anchor = "void Engine::setDeterministicInputIgnition("
        block = implementation[
            implementation.index(anchor) : implementation.index(anchor) + 220
        ]
        self.assertIn("m_contact = contact;", block)
        self.assertIn("m_starter = starter;", block)
        self.assertNotIn("SOUND_", block)


if __name__ == "__main__":
    unittest.main()
