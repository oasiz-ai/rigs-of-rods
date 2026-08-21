#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_calibrated_beam_soak.py"
SCRIPT_PATH = (
    REPOSITORY_ROOT / "resources/scripts/example_calibrated_beam_soak.as"
)
ACTOR_HEADER = REPOSITORY_ROOT / "source/main/physics/Actor.h"
ACTOR_BINDING = (
    REPOSITORY_ROOT
    / "source/main/scripting/bindings/ActorAngelscript.cpp"
)

SPEC = importlib.util.spec_from_file_location("run_calibrated_beam_soak", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load calibrated-beam soak tool")
SOAK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SOAK)


def minimal_source() -> bytes:
    return (
        "Daf Semi truck\n"
        "fileinfo b6b0UID,  160,  1\n"
        "guid a87c50a0-e11b-48b6-9cfe-f2de121e7d40\n"
        ";main chassis structural\n"
        "0,1,i\n"
        "27,29,i\n"
        ";\n"
        "0,2,i\n"
    ).encode("utf-8")


def comparison(
    left_workers: int = 1,
    right_workers: int = 8,
    *,
    status: str = "match",
    steps: int = SOAK.EXPECTED_STEPS,
) -> dict[str, object]:
    def side(workers: int) -> dict[str, object]:
        return {
            "metadata": {
                "worker_count": workers,
                "scenario_id": SOAK.SCENARIO_ID,
                "first_physics_step": 0,
                "physics_step_numerator": 1,
                "physics_step_denominator": 2000,
            }
        }

    return {
        "format": "ror-d0-state-trace-comparison-v2",
        "status": status,
        "steps_compared": steps,
        "left": side(left_workers),
        "right": side(right_workers),
    }


class CalibratedBeamSoakTests(unittest.TestCase):
    def test_derived_fixture_is_exactly_scoped(self) -> None:
        fixture = SOAK.derive_fixture_payload(minimal_source())
        text = fixture.decode("utf-8")
        self.assertIn("P1 calibrated-beam DAF numerical soak fixture", text)
        self.assertIn("fileinfo p1calibUID", text)
        self.assertIn("not physical DAF calibration", text)
        self.assertEqual(text.count("set_calibrated_beam_material 1, on,"), 1)
        self.assertEqual(text.count("set_calibrated_beam_material 1, off"), 1)
        self.assertLess(
            text.index("set_calibrated_beam_material 1, on,"),
            text.index("0,1,i"),
        )
        self.assertLess(
            text.index("27,29,i"),
            text.index("set_calibrated_beam_material 1, off"),
        )
        self.assertLess(
            text.index("set_calibrated_beam_material 1, off"),
            text.index("0,2,i"),
        )

    def test_derived_fixture_rejects_structural_drift(self) -> None:
        source = minimal_source()
        for mutation in (
            source.replace(b"Daf Semi truck", b"Other truck"),
            source.replace(b"0,1,i", b"0, 1, i"),
            source + b"Daf Semi truck\n",
        ):
            with self.subTest(mutation=mutation[:20]):
                with self.assertRaises(SOAK.SoakFailure):
                    SOAK.derive_fixture_payload(mutation)
        with self.assertRaises(SOAK.SoakFailure):
            SOAK.generate_fixture(source)

    def test_fixture_archive_is_deterministic_and_exact(self) -> None:
        fixture = SOAK.derive_fixture_payload(minimal_source())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.zip"
            second = root / "second.zip"
            first_sha = SOAK.write_fixture_archive(first, fixture)
            second_sha = SOAK.write_fixture_archive(second, fixture)
            self.assertEqual(first_sha, second_sha)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(archive.namelist(), [SOAK.FIXTURE_MEMBER])
                self.assertEqual(archive.read(SOAK.FIXTURE_MEMBER), fixture)

    def test_log_gate_requires_exact_receipts_and_ranges(self) -> None:
        pass_receipt = (
            "[RoR|P1|CalibratedBeamSoak] PASS actors=1 nodes=176 "
            "calibrated_beams=15 steps=120000 active_history=15 "
            "max_abs_strain=0.0125 max_plastic_strain=0 max_damage=0"
        )
        script_log = "\n".join(
            (SOAK.START_MARKER, SOAK.ARM_MARKER, pass_receipt)
        )
        engine_log = "\n".join(SOAK.ENGINE_MARKERS)
        telemetry = SOAK.validate_logs(0, "", engine_log, script_log)
        self.assertEqual(telemetry["active_history"], 15)
        self.assertEqual(telemetry["max_damage"], 0.0)

        for bad in (
            script_log.replace("active_history=15", "active_history=0"),
            script_log.replace("max_abs_strain=0.0125", "max_abs_strain=0"),
            script_log + "\n" + pass_receipt,
            script_log.replace(SOAK.ARM_MARKER, ""),
        ):
            with self.subTest(bad=bad[-80:]):
                with self.assertRaises(SOAK.SoakFailure):
                    SOAK.validate_logs(0, "", engine_log, bad)
        for marker in SOAK.FATAL_MARKERS:
            with self.subTest(marker=marker):
                with self.assertRaises(SOAK.SoakFailure):
                    SOAK.validate_logs(0, marker, engine_log, script_log)

    def test_trace_comparison_requires_exact_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "trace-tool.py"
            left = root / "left.rortrace"
            right = root / "right.rortrace"
            left.write_bytes(b"left")
            right.write_bytes(b"right")
            tool.write_text(
                "#!/usr/bin/env python3\n"
                "import json\n"
                f"print(json.dumps({comparison()!r}))\n",
                encoding="utf-8",
            )
            tool.chmod(0o755)
            payload = SOAK.compare_traces(tool, left, right, 1, 8, 10)
            self.assertEqual(payload["status"], "match")

            bad = comparison(steps=119999)
            tool.write_text(
                "#!/usr/bin/env python3\n"
                "import json\n"
                f"print(json.dumps({bad!r}))\n",
                encoding="utf-8",
            )
            with self.assertRaises(SOAK.SoakFailure):
                SOAK.compare_traces(tool, left, right, 1, 8, 10)

    def test_runtime_layout_and_config_are_isolated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target in ("darwin", "win32", "linux"):
                layout = SOAK.runtime_layout(root / target, target)
                SOAK.write_runtime_config(layout["config"], 8, True)
                config = (layout["config"] / "RoR.cfg").read_text()
                self.assertIn("app_num_workers=8", config)
                self.assertIn("app_force_cache_update=true", config)
                self.assertNotIn(str(Path.home()), config)
            self.assertEqual(
                SOAK.runtime_layout(root / "darwin", "darwin")["user"],
                root / "darwin" / "RigsOfRods",
            )
            with self.assertRaises(SOAK.SoakFailure):
                SOAK.runtime_layout(root, "unknown")

    def test_script_and_native_audit_binding_are_closed(self) -> None:
        script = SCRIPT_PATH.read_text(encoding="utf-8")
        header = ACTOR_HEADER.read_text(encoding="utf-8")
        binding = ACTOR_BINDING.read_text(encoding="utf-8")
        self.assertIn("EXPECTED_PHYSICS_STEPS = 120000", script)
        self.assertIn("EXPECTED_CALIBRATED_BEAMS = 15", script)
        self.assertIn('"sim_deterministic_fixed_steps_per_frame", "100"', script)
        self.assertIn("not a claim of physically calibrated DAF", script)
        self.assertNotIn("MSG_SIM_SPAWN_ACTOR_REQUESTED", script)
        self.assertEqual(
            SOAK.build_command(Path("/tmp/RoR"))[-5:],
            (
                "-truck",
                SOAK.FIXTURE_MEMBER,
                "-enter",
                "-runscript",
                SOAK.SCENARIO_SCRIPT,
            ),
        )
        for method in (
            "getCalibratedBeamCount",
            "getCalibratedBeamFaultCount",
            "getCalibratedBeamFractureCount",
            "getCalibratedBeamDisabledCount",
            "getCalibratedBeamActiveHistoryCount",
            "hasFiniteCalibratedBeamState",
            "hasValidCalibratedBeamState",
            "getCalibratedBeamMaxAbsTotalStrain",
            "getCalibratedBeamMaxAccumulatedPlasticStrain",
            "getCalibratedBeamMaxDamage",
        ):
            self.assertIn(method, header)
            self.assertIn(method, binding)
            self.assertIn(method, script)

    def test_report_contract_keeps_material_claim_bounded(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        self.assertIn("ror-p1-calibrated-beam-runtime-soak-v1", source)
        self.assertIn(
            "numerical-integration-fixture-not-physical-calibration",
            source,
        )
        self.assertIn("--allow-worker-count-difference", source)
        self.assertNotIn("urlopen", source)
        self.assertNotIn("requests.", source)


if __name__ == "__main__":
    unittest.main()
