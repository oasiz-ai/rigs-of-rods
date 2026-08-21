import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.verify_native_visual_showcase import VerificationError, verify


CONTRACT = ROOT / "tools/ogre_next_probe/native-visual-showcase-v1.contract.json"


def _digest(character: str) -> str:
    return character * 64


def _valid_report() -> dict:
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    report = {
        "schema": "ror.native_visual_showcase_report.v1",
        "contract_sha256": hashlib.sha256(CONTRACT.read_bytes()).hexdigest(),
        "renderer": "ogre-next",
        "product_path": "forward-native",
        "commit": "1" * 40,
        "source_dirty": False,
        "package_sha256": _digest("2"),
        "asset_origin": "project_original",
        "provenance_approved": True,
        "hardware": {
            "native_api": "metal",
            "native_rt_requested": True,
            "native_rt_admitted": True,
            "supports_ray_tracing": True,
            "same_renderer_device": True,
            "same_renderer_queue": True,
            "fallback_reason": None,
        },
        "modes": [],
        "artifacts": [],
    }
    for mode_id in contract["required_modes"]:
        mode = {
            "id": mode_id,
            "completed_frames": contract["minimum_main_sequence_frames"],
            "warmup_frames": 120,
            "legacy_render_passes": 0,
            "visible_legacy_assets": 0,
            "legacy_material_fallbacks": 0,
            "missing_resource_fallbacks": 0,
            "runtime_asset_conversions": 0,
            "production_content_readbacks": 0,
            "production_framebuffer_readbacks": 0,
            "acceptance_readbacks_scoped_to_artifacts": True,
            "passes": [],
        }
        for index, required_pass in enumerate(contract["required_passes"]):
            if mode_id not in required_pass["modes"]:
                continue
            mode["passes"].append(
                {
                    "id": required_pass["id"],
                    "version": required_pass["version"],
                    "status": "executed",
                    "completed_frames": required_pass["minimum_completed_frames"],
                    "state_verifications": required_pass[
                        "minimum_completed_frames"
                    ],
                    "gpu_timestamp_samples": required_pass[
                        "minimum_completed_frames"
                    ],
                    "gpu_time_ns_p50": 100 + index,
                    "gpu_time_ns_p95": 200 + index,
                    "production_content_readbacks": 0,
                    "input_lineage": _digest("3"),
                    "output_lineage": _digest("4"),
                    "witness": {
                        "metric": required_pass["witness_metric"],
                        "value": required_pass["minimum_witness_value"],
                        "before_sha256": _digest("5"),
                        "after_sha256": _digest("6"),
                    },
                }
            )
        report["modes"].append(mode)
    for index, name in enumerate(contract["required_artifacts"]):
        report["artifacts"].append(
            {
                "name": name,
                "mode": (
                    "native_rt_ultra"
                    if name.startswith("native_rt")
                    else "raster_high"
                ),
                "sha256": format(index + 10, "064x"),
                "width": contract["minimum_width"],
                "height": contract["minimum_height"],
                "frame_id": index + 1,
                "color_space": "srgb",
                "accepted": True,
            }
        )
    return report


def _write_report(directory: Path, report: dict) -> Path:
    path = directory / "report.json"
    path.write_text(json.dumps(report, sort_keys=True), encoding="utf-8")
    return path


class NativeVisualShowcaseVerifierTests(unittest.TestCase):
    def test_complete_all_pass_report_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            verify(CONTRACT, _write_report(Path(raw), _valid_report()))

    def test_missing_rt_reflection_pass_is_rejected(self) -> None:
        report = _valid_report()
        rt_mode = next(mode for mode in report["modes"] if mode["id"] == "native_rt_ultra")
        rt_mode["passes"] = [
            item for item in rt_mode["passes"] if item["id"] != "native_rt_reflections"
        ]
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "missing passes"):
                verify(CONTRACT, _write_report(Path(raw), report))

    def test_pass_without_visible_off_on_change_is_rejected(self) -> None:
        report = _valid_report()
        witness = report["modes"][0]["passes"][0]["witness"]
        witness["after_sha256"] = witness["before_sha256"]
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "no visible off/on change"):
                verify(CONTRACT, _write_report(Path(raw), report))

    def test_production_content_readback_is_rejected(self) -> None:
        report = _valid_report()
        report["modes"][0]["passes"][0]["production_content_readbacks"] = 1
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "production content readback"):
                verify(CONTRACT, _write_report(Path(raw), report))

    def test_legacy_or_missing_resource_fallback_is_rejected(self) -> None:
        for field in (
            "legacy_render_passes",
            "visible_legacy_assets",
            "legacy_material_fallbacks",
            "missing_resource_fallbacks",
            "runtime_asset_conversions",
        ):
            report = _valid_report()
            report["modes"][0][field] = 1
            with self.subTest(field=field), tempfile.TemporaryDirectory() as raw:
                with self.assertRaisesRegex(VerificationError, field):
                    verify(CONTRACT, _write_report(Path(raw), report))

    def test_native_rt_fallback_or_distinct_queue_is_rejected(self) -> None:
        for field, value in (
            ("native_rt_admitted", False),
            ("same_renderer_queue", False),
            ("fallback_reason", "unsupported"),
        ):
            report = _valid_report()
            report["hardware"][field] = value
            with self.subTest(field=field), tempfile.TemporaryDirectory() as raw:
                with self.assertRaises(VerificationError):
                    verify(CONTRACT, _write_report(Path(raw), report))

    def test_missing_artifact_and_short_sequence_are_rejected(self) -> None:
        report = _valid_report()
        report["artifacts"].pop()
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "missing showcase artifacts"):
                verify(CONTRACT, _write_report(Path(raw), report))
        report = _valid_report()
        report["modes"][0]["completed_frames"] = 599
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "completed_frames"):
                verify(CONTRACT, _write_report(Path(raw), report))

    def test_contract_hash_and_dirty_source_are_rejected(self) -> None:
        report = _valid_report()
        report["contract_sha256"] = _digest("f")
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "contract hash mismatch"):
                verify(CONTRACT, _write_report(Path(raw), report))
        report = _valid_report()
        report["source_dirty"] = True
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "dirty source"):
                verify(CONTRACT, _write_report(Path(raw), report))

    def test_unapproved_provenance_is_rejected(self) -> None:
        report = _valid_report()
        report["provenance_approved"] = False
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "provenance is not approved"):
                verify(CONTRACT, _write_report(Path(raw), report))

    def test_contract_dependency_cycle_is_rejected(self) -> None:
        contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
        first = contract["required_passes"][0]
        first["depends_on"] = ["ui_composite"]
        with tempfile.TemporaryDirectory() as raw:
            directory = Path(raw)
            contract_path = directory / "contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            report = _valid_report()
            report["contract_sha256"] = hashlib.sha256(
                contract_path.read_bytes()
            ).hexdigest()
            with self.assertRaisesRegex(VerificationError, "dependency cycle"):
                verify(contract_path, _write_report(directory, report))

    def test_boolean_cannot_spoof_numeric_evidence(self) -> None:
        report = _valid_report()
        report["modes"][0]["passes"][0]["gpu_time_ns_p50"] = True
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(VerificationError, "finite number"):
                verify(CONTRACT, _write_report(Path(raw), report))

    def test_duplicate_keys_and_nonfinite_json_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            directory = Path(raw)
            duplicate = directory / "duplicate.json"
            duplicate.write_text('{"schema":"a","schema":"b"}', encoding="utf-8")
            with self.assertRaisesRegex(VerificationError, "duplicate JSON key"):
                verify(CONTRACT, duplicate)
            nonfinite = directory / "nonfinite.json"
            nonfinite.write_text('{"schema":NaN}', encoding="utf-8")
            with self.assertRaisesRegex(VerificationError, "non-finite JSON constant"):
                verify(CONTRACT, nonfinite)


if __name__ == "__main__":
    unittest.main()
