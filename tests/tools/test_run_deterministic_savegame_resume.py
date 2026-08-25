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
                "beams": [[0.0, 0.0, 0.0, 1.0, 1.0, False, False, False, -1, 0.0]],
                "deterministic_runtime_flags_v1": {
                    "update_physics": True,
                    "collision_relevant": True,
                    "ongoing_reset": False,
                },
                "deterministic_solver_state_v1": {
                    "wheels": [[0.0] * 8],
                    "wheel_differentials": [],
                    "axle_differentials": [],
                    "intra_collision_cadence": [],
                    "inter_collision_cadence": [],
                    "actor": {
                        "fusedrag": [0.0, 0.0, 0.0],
                        "sleep_counter": 0.0,
                        "stabilizer_shock_sleep": 0.0,
                        "stabilizer_shock_ratio": 0.0,
                        "stabilizer_shock_request": 0,
                        "tc_timer": 0.0,
                        "tc_pulse_state": False,
                        "alb_timer": 0.0,
                        "alb_pulse_state": False,
                        "anim_previous_crank": 0.0,
                    },
                },
                "filename": "agora.zip:" + gate.VEHICLE,
                "nodes": [[506.0, 0.95, 510.75, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -9.81, 0.0, True, False, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 100.0, 0.0, 1.0, 1.0, 1.0]],
                "physics_origin": [506.0, 0.95, 510.75],
                "physics_step": gate.SAVE_STEP,
                "player_actor": True,
            }
        ],
    }


def valid_inspection(first: int) -> dict[str, object]:
    return {
        "format": "ror-d0-state-trace-inspection-v2",
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
            wrong_origin = valid_checkpoint()
            wrong_origin["actors"][0]["physics_origin"] = [0.0, float("nan"), 0.0]
            mutations.append(wrong_origin)
            wrong_node = valid_checkpoint()
            wrong_node["actors"][0]["nodes"][0][22] = float("nan")
            mutations.append(wrong_node)
            wrong_flags = valid_checkpoint()
            wrong_flags["actors"][0]["deterministic_runtime_flags_v1"].pop(
                "collision_relevant"
            )
            mutations.append(wrong_flags)
            wrong_solver = valid_checkpoint()
            wrong_solver["actors"][0]["deterministic_solver_state_v1"]["wheels"][0][4] = float("inf")
            mutations.append(wrong_solver)
            wrong_stress = valid_checkpoint()
            wrong_stress["actors"][0]["beams"][0][9] = float("inf")
            mutations.append(wrong_stress)

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

    def test_autosave_projection_reports_first_physics_difference(self) -> None:
        record = {
            "completed_physics_steps": gate.FINAL_STEP,
            "physics_paused": True,
            "player_position": [1.0, 2.0, 3.0],
            "actors": [
                {
                    "filename": "agora.zip:" + gate.VEHICLE,
                    "physics_step": gate.FINAL_STEP,
                    "nodes": [[1.0, 2.0, 3.0, 4.0, 5.0, 6.0]],
                    "beams": [[0.0, 0.0, 0.0, 1.0, 2.0]],
                    "lights": 0,
                }
            ],
        }
        resume = copy.deepcopy(record)
        resume["player_position"] = [9.0, 9.0, 9.0]
        self.assertIsNone(
            gate.first_json_difference(
                gate.autosave_physics_projection(record),
                gate.autosave_physics_projection(resume),
            )
        )
        resume["actors"][0]["nodes"][0][4] = 5.5
        self.assertEqual(
            gate.first_json_difference(
                gate.autosave_physics_projection(record),
                gate.autosave_physics_projection(resume),
            ),
            {
                "path": "$.actors[0].nodes[0][4]",
                "kind": "value",
                "left": 5.0,
                "right": 5.5,
            },
        )

    def test_github_error_escapes_workflow_commands(self) -> None:
        self.assertEqual(
            gate.format_github_error("bad%line\nnext\rline"),
            "::error title=Deterministic save-resume gate::"
            "bad%25line%0Anext%0Dline",
        )

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

            packaged = Path(
                "/tmp/package/RoR-Combined.app/Contents/MacOS/RoR-Combined"
            )
            packaged_user, packaged_logs = gate.runtime_layout(
                Path("/tmp/isolated"), packaged
            )
            self.assertEqual(
                packaged_user,
                Path(
                    "/tmp/isolated/Library/Application Support/Rigs of Rods"
                ),
            )
            self.assertEqual(
                packaged_logs,
                Path("/tmp/isolated/Library/Logs/Rigs of Rods"),
            )

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
        self.assertIn('if (mode == "off")', resume_source)
        self.assertIn("restored-input-activation-timeout", resume_source)
        self.assertIn('if (mode != "record" ||', resume_source)
        self.assertIn("completed != SAVE_STEP", resume_source)
        self.assertNotIn("-resume", resume_source)


if __name__ == "__main__":
    unittest.main()
