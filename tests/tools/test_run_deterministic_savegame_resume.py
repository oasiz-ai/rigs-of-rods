#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[2]
MODULE_PATH = REPOSITORY / "tools/run_deterministic_savegame_resume.py"
SPEC = importlib.util.spec_from_file_location("savegame_resume_gate", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


def valid_checkpoint() -> dict[str, object]:
    return {
        "format_version": 3,
        "terrain_name": gate.TERRAIN,
        "completed_physics_steps": gate.SAVE_STEP,
        "physics_paused": False,
        "deterministic_input_continuation_v1": "abc_DEF-123",
        "actors": [
            {
                "filename": "agora.zip:" + gate.VEHICLE,
                "physics_step": gate.SAVE_STEP,
                "player_actor": True,
            }
        ],
    }


def valid_inspection(first: int) -> dict[str, object]:
    return {
        "format": "ror-d0-state-trace-inspection-v1",
        "status": "valid",
        "metadata": {
            "scenario_id": gate.SCENARIO_ID,
            "first_physics_step": first,
            "physics_step_numerator": 1,
            "physics_step_denominator": 2000,
        },
        "step_count": gate.FINAL_STEP - first,
        "has_final_step": True,
        "final_step": {
            "physics_step": gate.FINAL_STEP - 1,
            "actor_count": 1,
            "contact_count": 7,
            "state_digest": "ab" * 32,
        },
    }


class SavegameResumeGateTests(unittest.TestCase):
    def write_checkpoint(self, payload: object, directory: Path) -> Path:
        path = directory / gate.CHECKPOINT
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def test_checkpoint_contract_and_hostiles(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            directory = Path(raw)
            path = self.write_checkpoint(valid_checkpoint(), directory)
            self.assertEqual(
                gate.validate_checkpoint(path)["completed_physics_steps"],
                gate.SAVE_STEP,
            )

            mutations = []
            for field, value in (
                ("format_version", 2),
                ("terrain_name", "other.terrn2"),
                ("completed_physics_steps", gate.SAVE_STEP + 1),
                ("physics_paused", True),
                ("deterministic_input_continuation_v1", "bad=value"),
                ("actors", []),
            ):
                payload = valid_checkpoint()
                payload[field] = value
                mutations.append(payload)
            wrong_actor = valid_checkpoint()
            wrong_actor["actors"][0]["physics_step"] = gate.SAVE_STEP - 1
            mutations.append(wrong_actor)
            wrong_vehicle = valid_checkpoint()
            wrong_vehicle["actors"][0]["filename"] = "agora.zip:other.truck"
            mutations.append(wrong_vehicle)

            for index, payload in enumerate(mutations):
                with self.subTest(index=index):
                    path = self.write_checkpoint(payload, directory)
                    with self.assertRaises(gate.ResumeFailure):
                        gate.validate_checkpoint(path)

    def test_trace_inspection_span_and_terminal_equivalence(self) -> None:
        record = gate.validate_inspection(valid_inspection(0))
        resume = gate.validate_inspection(valid_inspection(gate.SAVE_STEP))
        gate.validate_trace_span(record, resumed=False)
        gate.validate_trace_span(resume, resumed=True)
        gate.validate_final_equivalence(record, resume)

        divergent = copy.deepcopy(resume)
        divergent["final_step"]["state_digest"] = "cd" * 32
        with self.assertRaises(gate.ResumeFailure):
            gate.validate_final_equivalence(record, divergent)

        invalid = valid_inspection(0)
        invalid["metadata"]["physics_step_denominator"] = 1000
        with self.assertRaises(gate.ResumeFailure):
            gate.validate_inspection(invalid)

    def test_runtime_log_contract_is_phase_specific_and_fail_closed(self) -> None:
        save_script = "\n".join(gate.SAVE_SCRIPT_MARKERS)
        save_engine = "\n".join(gate.SAVE_ENGINE_MARKERS)
        gate.validate_logs(0, "", save_engine, save_script, resumed=False)

        resume_script = "\n".join(gate.RESUME_SCRIPT_MARKERS)
        resume_engine = "\n".join(gate.RESUME_ENGINE_MARKERS)
        gate.validate_logs(0, "", resume_engine, resume_script, resumed=True)

        with self.assertRaises(gate.ResumeFailure):
            gate.validate_logs(
                0,
                "",
                save_engine + "\nRefusing input runtime",
                save_script,
                resumed=False,
            )
        with self.assertRaises(gate.ResumeFailure):
            gate.validate_logs(7, "", save_engine, save_script, resumed=False)

    def test_command_and_isolated_layout_do_not_fake_resume_cli(self) -> None:
        executable = Path("/tmp/build/bin/RoR-Combined")
        with mock.patch.object(gate.sys, "platform", "darwin"):
            user, logs = gate.runtime_layout(Path("/tmp/isolated"), executable)
            self.assertEqual(user, Path("/tmp/isolated/RigsOfRods"))
            self.assertEqual(logs, user / "logs")
            initial = gate.build_command(executable, gate.SAVE_SCRIPT, True)
            resumed = gate.build_command(executable, gate.RESUME_SCRIPT, False)
        self.assertIn("-map", initial)
        self.assertIn("-truck", initial)
        self.assertIn("-enter", initial)
        self.assertNotIn("-map", resumed)
        self.assertNotIn("-resume", resumed)
        self.assertEqual(resumed[-2:], ("-runscript", gate.RESUME_SCRIPT))

    def test_scripts_bind_exact_save_and_resume_boundaries(self) -> None:
        save_source = (
            REPOSITORY / "resources/scripts" / gate.SAVE_SCRIPT
        ).read_text(encoding="utf-8")
        resume_source = (
            REPOSITORY / "resources/scripts" / gate.RESUME_SCRIPT
        ).read_text(encoding="utf-8")
        self.assertIn("game.saveScene(CHECKPOINT)", save_source)
        self.assertIn("completed == SAVE_STEP", save_source)
        self.assertIn("completed == FINAL_STEP", save_source)
        self.assertIn('console.cVarSet("sim_state", "" + SIM_STATE_PAUSED)', save_source)
        self.assertIn("MSG_SIM_LOAD_SAVEGAME_REQUESTED", resume_source)
        self.assertIn('gInputMode.getStr() != "record"', resume_source)
        self.assertIn("completed != SAVE_STEP", resume_source)
        self.assertNotIn("-resume", resume_source)


if __name__ == "__main__":
    unittest.main()
