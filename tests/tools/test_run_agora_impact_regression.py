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
TOOL_PATH = REPOSITORY_ROOT / "tools/run_agora_impact_regression.py"
SCRIPT_PATH = REPOSITORY_ROOT / "resources/scripts/example_calibrated_agora_impact.as"
ACTOR_HEADER = REPOSITORY_ROOT / "source/main/physics/Actor.h"
ACTOR_BINDING = REPOSITORY_ROOT / "source/main/scripting/bindings/ActorAngelscript.cpp"

SPEC = importlib.util.spec_from_file_location("run_agora_impact_regression", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load Agora impact tool")
IMPACT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPACT)


def minimal_source() -> bytes:
    return (
        "Bus RVI Agora L\n"
        "fileinfo 95bbUID,  107,  1\n"
        "guid 0e943cc8-d9a5-4d24-89fc-907423a0ddfb\n"
        "beams\n"
        ";chassis front structural\n"
        "0,2,i\n"
        "\n"
        "hydros\n"
    ).encode("utf-8")


def comparison(
    left_workers: int = 1,
    right_workers: int = 8,
    *,
    status: str = "match",
    steps: int = IMPACT.EXPECTED_STEPS,
) -> dict[str, object]:
    def side(workers: int) -> dict[str, object]:
        return {
            "metadata": {
                "worker_count": workers,
                "scenario_id": IMPACT.SCENARIO_ID,
                "first_physics_step": 0,
                "physics_step_numerator": 1,
                "physics_step_denominator": 2000,
            }
        }

    return {
        "format": "ror-d0-state-trace-comparison-v1",
        "status": status,
        "steps_compared": steps,
        "left": side(left_workers),
        "right": side(right_workers),
    }


class AgoraImpactRegressionTests(unittest.TestCase):
    def test_derived_fixture_is_exactly_scoped(self) -> None:
        fixture = IMPACT.derive_fixture_payload(minimal_source())
        text = fixture.decode("utf-8")
        self.assertIn("P1 calibrated Agora fixed-speed impact regression", text)
        self.assertIn("fileinfo p1impactUID", text)
        self.assertIn("not physical Agora calibration", text)
        self.assertEqual(text.count("set_calibrated_beam_material 1, on,"), 1)
        self.assertEqual(text.count("set_calibrated_beam_material 1, off"), 1)
        self.assertLess(
            text.index("set_calibrated_beam_material 1, on,"),
            text.index("0,2,i"),
        )
        self.assertLess(
            text.index("0,2,i"),
            text.index("set_calibrated_beam_material 1, off"),
        )
        self.assertLess(
            text.index("set_calibrated_beam_material 1, off"),
            text.index("hydros"),
        )

    def test_derived_fixture_rejects_structural_drift(self) -> None:
        source = minimal_source()
        for mutation in (
            source.replace(b"Bus RVI Agora L", b"Other bus"),
            source.replace(b"beams\n", b"beams \n"),
            source + b"Bus RVI Agora L\n",
        ):
            with self.subTest(mutation=mutation[:20]):
                with self.assertRaises(IMPACT.ImpactFailure):
                    IMPACT.derive_fixture_payload(mutation)
        with self.assertRaises(IMPACT.ImpactFailure):
            IMPACT.generate_fixture(source)

    def test_full_source_derives_pinned_fixture(self) -> None:
        source = REPOSITORY_ROOT / "content" / IMPACT.SOURCE_RELATIVE
        payload = IMPACT.generate_fixture(source.read_bytes())
        self.assertEqual(len(payload), IMPACT.FIXTURE_SIZE)
        self.assertEqual(IMPACT.support.sha256_bytes(payload), IMPACT.FIXTURE_SHA256)
        start = payload.index(b"set_calibrated_beam_material 1, on,")
        end = payload.index(b"set_calibrated_beam_material 1, off")
        beam_lines = 0
        for line in payload[start:end].decode("utf-8").splitlines()[1:]:
            stripped = line.split(";", 1)[0].strip()
            if stripped:
                beam_lines += 1
        self.assertEqual(beam_lines, IMPACT.EXPECTED_CALIBRATED_BEAMS)

    def test_fixture_archive_is_deterministic_and_exact(self) -> None:
        fixture = IMPACT.derive_fixture_payload(minimal_source())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.zip"
            second = root / "second.zip"
            first_sha = IMPACT.write_fixture_archive(first, fixture)
            second_sha = IMPACT.write_fixture_archive(second, fixture)
            self.assertEqual(first_sha, second_sha)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(archive.namelist(), [IMPACT.FIXTURE_MEMBER])
                self.assertEqual(archive.read(IMPACT.FIXTURE_MEMBER), fixture)

    def test_log_gate_requires_complete_impact_receipt(self) -> None:
        pass_receipt = (
            "[RoR|P1|AgoraImpact] PASS actors=1 nodes=297 "
            "calibrated_beams=675 steps=6000 peak_deceleration=150.5 "
            "initial_energy=2000000 final_energy=500000 "
            "absorbed_energy=1500000 permanent_rms=0.12 "
            "permanent_max=0.8 broken_beams=24 fractures=20 "
            "disabled=20 final_com_speed=0.4"
        )
        script_log = "\n".join(
            (IMPACT.START_MARKER, IMPACT.ARM_MARKER, pass_receipt)
        )
        engine_log = "\n".join(IMPACT.ENGINE_MARKERS)
        telemetry = IMPACT.validate_logs(0, "", engine_log, script_log)
        self.assertEqual(telemetry["fractures"], 20)
        self.assertEqual(telemetry["absorbed_energy"], 1500000.0)

        with self.assertRaisesRegex(
            IMPACT.ImpactFailure,
            "runtime logged a fatal marker",
        ):
            IMPACT.validate_logs(
                0,
                "",
                "\n".join(IMPACT.ENGINE_MARKERS[:-1]),
                script_log + "\n[RoR|P1|AgoraImpact] FAIL reason=hostile",
            )

        for bad in (
            script_log.replace("fractures=20", "fractures=0"),
            script_log.replace("permanent_rms=0.12", "permanent_rms=0"),
            script_log.replace("absorbed_energy=1500000", "absorbed_energy=-1"),
            script_log.replace("final_com_speed=0.4", "final_com_speed=12"),
            script_log + "\n" + pass_receipt,
            script_log.replace(IMPACT.ARM_MARKER, ""),
        ):
            with self.subTest(bad=bad[-100:]):
                with self.assertRaises(IMPACT.ImpactFailure):
                    IMPACT.validate_logs(0, "", engine_log, bad)

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
            payload = IMPACT.compare_traces(tool, left, right, 1, 8, 10)
            self.assertEqual(payload["status"], "match")

            bad = comparison(steps=5999)
            tool.write_text(
                "#!/usr/bin/env python3\n"
                "import json\n"
                f"print(json.dumps({bad!r}))\n",
                encoding="utf-8",
            )
            with self.assertRaises(IMPACT.ImpactFailure):
                IMPACT.compare_traces(tool, left, right, 1, 8, 10)

    def test_script_and_native_boundary_are_closed(self) -> None:
        script = SCRIPT_PATH.read_text(encoding="utf-8")
        header = ACTOR_HEADER.read_text(encoding="utf-8")
        binding = ACTOR_BINDING.read_text(encoding="utf-8")
        for token in (
            "EXPECTED_PHYSICS_STEPS = 6000",
            "EXPECTED_CALIBRATED_BEAMS = 675",
            '"sim_deterministic_fixed_steps_per_frame", "1"',
            "trySetDeterministicImpactVelocity",
            "getBrokenBeamCount",
            "CenterOfMassVelocity",
            "vector3 weightedVelocity(0.0f, 0.0f, 0.0f)",
            "MechanicalEnergy",
            "MeasurePermanentShape",
            'formatFloat(value, "e", 0, 17)',
            "not a claim of physical Agora calibration",
        ):
            self.assertIn(token, script)
        for method in (
            "trySetDeterministicImpactVelocity",
            "getBrokenBeamCount",
        ):
            self.assertIn(method, header)
            self.assertIn(method, binding)
        self.assertEqual(
            IMPACT.build_command(Path("/tmp/RoR"))[-5:],
            (
                "-truck",
                IMPACT.FIXTURE_MEMBER,
                "-enter",
                "-runscript",
                IMPACT.SCENARIO_SCRIPT,
            ),
        )

    def test_report_contract_is_bounded_and_offline(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        self.assertIn("ror-p1-agora-impact-regression-v1", source)
        self.assertIn(
            "numerical-impact-fixture-not-physical-calibration",
            source,
        )
        self.assertIn("--allow-worker-count-difference", source)
        self.assertNotIn("urlopen", source)
        self.assertNotIn("requests.", source)


if __name__ == "__main__":
    unittest.main()
