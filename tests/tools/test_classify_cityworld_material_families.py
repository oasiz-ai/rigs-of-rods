#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/classify_cityworld_material_families.py"
SCHEMA_PATH = (
    REPOSITORY_ROOT
    / "tools/schemas/cityworld-material-modernization-report-v1.schema.json"
)
FIXTURE_PATH = (
    REPOSITORY_ROOT
    / "tests/fixtures/cityworld_material_classifier/families.synthetic.material"
)

SPEC = importlib.util.spec_from_file_location(
    "classify_cityworld_material_families", TOOL_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld material classifier")
CLASSIFIER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CLASSIFIER
SPEC.loader.exec_module(CLASSIFIER)


def _zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    return info


class CityWorldMaterialFamilyClassifierTests(unittest.TestCase):
    def make_archive(
        self,
        root: Path,
        *,
        entries: list[tuple[str, bytes]] | None = None,
    ) -> Path:
        archive_path = root / "user-supplied.zip"
        selected = entries or [
            ("synthetic/families.material", FIXTURE_PATH.read_bytes()),
            ("synthetic/placeholder.png", b"synthetic-not-an-image"),
        ]
        with zipfile.ZipFile(archive_path, "w") as archive:
            for name, payload in selected:
                archive.writestr(_zip_info(name), payload)
        return archive_path

    def test_verified_families_are_derived_from_synthetic_structure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_archive(root)
            before = hashlib.sha256(archive.read_bytes()).hexdigest()
            first = CLASSIFIER.classify_archive(archive)
            second = CLASSIFIER.classify_archive(archive)
            after = hashlib.sha256(archive.read_bytes()).hexdigest()
            rendered = CLASSIFIER.canonical_json(first)

        self.assertEqual(first, second)
        self.assertEqual(before, after)
        self.assertNotIn(str(root), rendered)
        self.assertEqual(
            first["schema"],
            "ror.cityworld.material-modernization-report.v1",
        )
        self.assertEqual(
            first["format"],
            "ror-cityworld-material-modernization-report-v1",
        )
        self.assertEqual(
            first["classifier"]["algorithm"],
            "ror-cityworld-material-family-classifier-v1",
        )
        self.assertEqual(
            first["classifier"]["tool_sha256"],
            hashlib.sha256(TOOL_PATH.read_bytes()).hexdigest(),
        )
        self.assertEqual(
            first["classifier"]["schema_sha256"],
            hashlib.sha256(SCHEMA_PATH.read_bytes()).hexdigest(),
        )
        self.assertEqual(first["summary"]["material_definitions"], 14)
        self.assertEqual(
            first["summary"]["family_counts"],
            {
                "ADDITIVE_SPECULAR_FURNITURE": 1,
                "CIELO_PLANAR_WINDOW_COMPOSITE": 1,
                "CLEAN_TWO_PASS_ALPHA_REJECTED_EMISSIVE": 2,
                "COMBINED_ENVIRONMENT_EMISSIVE": 1,
                "COMBINED_PLANAR_EMISSIVE": 1,
                "PLANAR_SURFACE_METAL": 1,
                "SIMPLE_SINGLE_PASS": 1,
                "SPHERICAL_BASE_SPEC_CURRENT_ALPHA_ENVIRONMENT": 2,
                "SUSPICIOUS_B_LIKE_ORPHAN_TEXTURE_UNIT": 1,
                "SUSPICIOUS_CUBE_PLANAR_ENVIRONMENT": 1,
                "TRANSPARENT_SPHERICAL_BUS_STOP": 1,
                "UNSUPPORTED_STRUCTURE": 1,
            },
        )
        self.assertEqual(
            first["summary"]["structural_trait_counts"],
            {
                "ORPHAN_NESTED_TEXTURE_UNIT_DIRECTIVE": 1,
                "ORPHAN_PASS_LEVEL_TEXTURE_UNIT_DIRECTIVE": 1,
                "PLANAR_CIELO": 1,
                "PLANAR_ENVIRONMENT": 5,
                "PLANAR_SUPERFICIE_METALICA": 3,
                "SPHERICAL_ENVIRONMENT": 3,
            },
        )

        records = {record["name"]: record for record in first["materials"]}
        spherical = records["Synthetic/SphericalValid"]
        self.assertEqual(
            spherical["fidelity"]["assigned_label"],
            "DECLARED_PBR_MODERNIZATION",
        )
        self.assertTrue(
            spherical["fidelity"][
                "declared_pbr_modernization_eligible"
            ]
        )
        self.assertTrue(
            spherical["fidelity"]["legacy_semantic_equivalent_eligible"]
        )
        self.assertIn(
            "PASS_0_UNIT_2:BLEND_CURRENT_ALPHA(texture,current)"
            "@SPHERICAL_ENV",
            spherical["structure"]["layer_equations"],
        )

        default_depth = records[
            "Synthetic/EmissiveDefaultDepthWrite"
        ]
        explicit_off = records[
            "Synthetic/EmissiveExplicitDepthWriteOff"
        ]
        self.assertEqual(
            default_depth["structure"][
                "authored_depth_write_second_pass"
            ],
            "DEFAULT_ON",
        )
        self.assertEqual(
            explicit_off["structure"][
                "authored_depth_write_second_pass"
            ],
            "EXPLICIT_OFF",
        )

    def test_repair_plan_is_exactly_gated_and_never_applied(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = self.make_archive(Path(temporary))
            report = CLASSIFIER.classify_archive(archive)
            with zipfile.ZipFile(archive) as source:
                payload = source.read("synthetic/families.material")

        self.assertEqual(len(report["repair_plans"]), 1)
        plan = report["repair_plans"][0]
        self.assertFalse(plan["apply_automatically"])
        self.assertEqual(plan["review_state"], "PENDING_HUMAN_REVIEW")
        self.assertEqual(
            plan["issue"], "ORPHAN_NESTED_TEXTURE_UNIT_DIRECTIVE"
        )
        self.assertEqual(
            plan["gate"]["archive_sha256"], report["archive"]["sha256"]
        )
        token_span = plan["gate"]["token_source_span"]
        token_bytes = payload[
            token_span["byte_start"] : token_span["byte_end_exclusive"]
        ]
        self.assertEqual(token_bytes, b"texture_unit")
        self.assertEqual(
            hashlib.sha256(token_bytes).hexdigest(), token_span["sha256"]
        )

        material = next(
            record
            for record in report["materials"]
            if record["name"] == "Synthetic/SphericalRepairReview"
        )
        material_span = material["source"]["span"]
        material_bytes = payload[
            material_span["byte_start"] : material_span[
                "byte_end_exclusive"
            ]
        ]
        self.assertEqual(
            hashlib.sha256(material_bytes).hexdigest(),
            plan["gate"]["material_source_span_sha256"],
        )
        self.assertEqual(
            material["classification"]["status"], "REVIEW_BLOCKED"
        )
        self.assertFalse(
            material["fidelity"]["declared_pbr_modernization_eligible"]
        )
        self.assertEqual(material["repair_plan_ids"], [plan["repair_id"]])

    def test_pass_level_orphan_is_separate_and_has_no_auto_repair(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report = CLASSIFIER.classify_archive(
                self.make_archive(Path(temporary))
            )
        suspicious = next(
            record
            for record in report["materials"]
            if record["name"] == "Synthetic/SuspiciousBLike"
        )
        self.assertEqual(
            suspicious["classification"],
            {
                "family": "SUSPICIOUS_B_LIKE_ORPHAN_TEXTURE_UNIT",
                "status": "REVIEW_BLOCKED",
            },
        )
        self.assertEqual(suspicious["repair_plan_ids"], [])
        self.assertEqual(len(report["repair_plans"]), 1)
        anomaly = suspicious["structure"]["anomalies"][0]
        self.assertEqual(
            anomaly["code"], "ORPHAN_PASS_LEVEL_TEXTURE_UNIT_DIRECTIVE"
        )
        self.assertEqual(anomaly["owning_scope"], "pass")

    def test_schema_is_closed_and_tracks_generated_contract(self) -> None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as temporary:
            report = CLASSIFIER.classify_archive(
                self.make_archive(Path(temporary))
            )
        self.assertEqual(schema["additionalProperties"], False)
        self.assertEqual(
            schema["properties"]["schema"]["const"], report["schema"]
        )
        self.assertEqual(
            schema["properties"]["format"]["const"], report["format"]
        )
        self.assertEqual(set(report), set(schema["required"]))
        material_schema = schema["$defs"]["material"]
        self.assertEqual(material_schema["additionalProperties"], False)
        self.assertEqual(
            set(report["materials"][0]), set(material_schema["required"])
        )
        structure_schema = schema["$defs"]["structure"]
        self.assertEqual(
            set(report["materials"][0]["structure"]),
            set(structure_schema["required"]),
        )
        family_enum = set(
            schema["$defs"]["classification"]["properties"]["family"][
                "enum"
            ]
        )
        self.assertTrue(
            set(report["summary"]["family_counts"]).issubset(family_enum)
        )

        tampered = json.loads(CLASSIFIER.canonical_json(report))
        tampered["repair_plans"][0]["apply_automatically"] = True
        with self.assertRaisesRegex(
            CLASSIFIER.AuditFailure, "repair plan is not fail-closed"
        ):
            CLASSIFIER.validate_report_contract(tampered)
        report["unknown"] = True
        with self.assertRaisesRegex(
            CLASSIFIER.AuditFailure, "root does not match schema"
        ):
            CLASSIFIER.validate_report_contract(report)

    def test_parser_preserves_recoverable_brace_anomalies(self) -> None:
        malformed = b"""\
material Synthetic/Anonymous
{
    {
        technique { pass {} }
    }
material Synthetic/After
{
    technique { pass {} }
}
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            archive = self.make_archive(
                Path(temporary),
                entries=[("synthetic/malformed.material", malformed)],
            )
            report = CLASSIFIER.classify_archive(archive)
        self.assertEqual(report["summary"]["material_definitions"], 2)
        self.assertGreaterEqual(report["summary"]["parser_anomalies"], 2)
        codes = {
            anomaly["code"]
            for script in report["scripts"]
            for anomaly in script["parser_anomalies"]
        }
        self.assertIn("ANONYMOUS_SCOPE", codes)
        self.assertIn("MISSING_CLOSING_BRACE_BEFORE_NEXT_MATERIAL", codes)
        self.assertIn("STRAY_ROOT_CLOSING_BRACE", codes)
        anonymous = report["materials"][0]
        self.assertEqual(
            anonymous["classification"]["family"], "SUSPICIOUS_STRUCTURE"
        )
        self.assertFalse(report["summary"]["automatic_modernization_ready"])

    def test_hostile_archives_and_parser_limits_fail_closed(self) -> None:
        simple = b"material Synthetic/Simple { technique { pass {} } }\n"
        for member in ("../unsafe.material", "/absolute.material"):
            with self.subTest(member=member), tempfile.TemporaryDirectory() as tmp:
                archive = self.make_archive(
                    Path(tmp), entries=[(member, simple)]
                )
                with self.assertRaisesRegex(
                    CLASSIFIER.AuditFailure, "unsafe ZIP member"
                ):
                    CLASSIFIER.classify_archive(archive)

        with tempfile.TemporaryDirectory() as temporary:
            archive = self.make_archive(
                Path(temporary),
                entries=[
                    ("same.material", simple),
                    ("SAME.MATERIAL", simple),
                ],
            )
            with self.assertRaisesRegex(
                CLASSIFIER.AuditFailure, "case-colliding"
            ):
                CLASSIFIER.classify_archive(archive)

        bad_scripts = (
            b"material Nul {}\x00",
            b'material Quote { technique { pass { texture_unit { texture "x } } } }',
            b"material Comment { /* never closed",
            b"material Brace { technique { pass {} }",
        )
        for payload in bad_scripts:
            with self.subTest(payload=payload), tempfile.TemporaryDirectory() as tmp:
                archive = self.make_archive(
                    Path(tmp), entries=[("bad.material", payload)]
                )
                with self.assertRaises(CLASSIFIER.AuditFailure):
                    CLASSIFIER.classify_archive(archive)

        original_token_cap = CLASSIFIER.MAX_TOKENS_PER_SCRIPT
        original_total_byte_cap = (
            CLASSIFIER.MAX_TOTAL_MATERIAL_SCRIPT_BYTES
        )
        original_total_token_cap = CLASSIFIER.MAX_TOTAL_TOKENS
        original_material_cap = CLASSIFIER.MAX_MATERIAL_DEFINITIONS
        try:
            CLASSIFIER.MAX_TOKENS_PER_SCRIPT = 2
            with tempfile.TemporaryDirectory() as temporary:
                archive = self.make_archive(
                    Path(temporary), entries=[("many.material", simple)]
                )
                with self.assertRaisesRegex(
                    CLASSIFIER.AuditFailure, "token count exceeds"
                ):
                    CLASSIFIER.classify_archive(archive)
            CLASSIFIER.MAX_TOKENS_PER_SCRIPT = original_token_cap
            CLASSIFIER.MAX_MATERIAL_DEFINITIONS = 1
            with tempfile.TemporaryDirectory() as temporary:
                archive = self.make_archive(
                    Path(temporary),
                    entries=[("two.material", simple + simple)],
                )
                with self.assertRaisesRegex(
                    CLASSIFIER.AuditFailure, "definition count exceeds"
                ):
                    CLASSIFIER.classify_archive(archive)
            CLASSIFIER.MAX_MATERIAL_DEFINITIONS = original_material_cap
            CLASSIFIER.MAX_TOTAL_MATERIAL_SCRIPT_BYTES = 1
            with tempfile.TemporaryDirectory() as temporary:
                archive = self.make_archive(
                    Path(temporary), entries=[("large.material", simple)]
                )
                with self.assertRaisesRegex(
                    CLASSIFIER.AuditFailure, "script bytes exceed"
                ):
                    CLASSIFIER.classify_archive(archive)
            CLASSIFIER.MAX_TOTAL_MATERIAL_SCRIPT_BYTES = original_total_byte_cap
            CLASSIFIER.MAX_TOTAL_TOKENS = 2
            with tempfile.TemporaryDirectory() as temporary:
                archive = self.make_archive(
                    Path(temporary), entries=[("tokens.material", simple)]
                )
                with self.assertRaisesRegex(
                    CLASSIFIER.AuditFailure, "token count exceeds"
                ):
                    CLASSIFIER.classify_archive(archive)
        finally:
            CLASSIFIER.MAX_TOKENS_PER_SCRIPT = original_token_cap
            CLASSIFIER.MAX_TOTAL_MATERIAL_SCRIPT_BYTES = original_total_byte_cap
            CLASSIFIER.MAX_TOTAL_TOKENS = original_total_token_cap
            CLASSIFIER.MAX_MATERIAL_DEFINITIONS = original_material_cap

        with tempfile.TemporaryDirectory() as temporary:
            archive = self.make_archive(
                Path(temporary), entries=[("not-a-script.txt", b"safe")]
            )
            with self.assertRaisesRegex(
                CLASSIFIER.AuditFailure, "no material scripts"
            ):
                CLASSIFIER.classify_archive(archive)
        with tempfile.TemporaryDirectory() as temporary:
            archive = self.make_archive(
                Path(temporary),
                entries=[("empty.material", b"// no definitions\n")],
            )
            with self.assertRaisesRegex(
                CLASSIFIER.AuditFailure, "no material definitions"
            ):
                CLASSIFIER.classify_archive(archive)
        with tempfile.TemporaryDirectory() as temporary:
            invalid = Path(temporary) / "invalid.zip"
            invalid.write_bytes(b"not a zip")
            with self.assertRaisesRegex(
                CLASSIFIER.AuditFailure, "not a valid ZIP"
            ):
                CLASSIFIER.classify_archive(invalid)

    def test_hash_gate_cli_and_atomic_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_archive(root)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            output = root / "report.json"
            self.assertEqual(
                CLASSIFIER.main(
                    [
                        str(archive),
                        "--expect-sha256",
                        digest.upper(),
                        "--output",
                        str(output),
                    ]
                ),
                0,
            )
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["archive"]["sha256"], digest)
            self.assertEqual(
                CLASSIFIER.main(
                    [
                        str(archive),
                        "--output",
                        str(output),
                        "--require-no-review-blockers",
                    ]
                ),
                2,
            )
            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8"))["schema"],
                CLASSIFIER.SCHEMA_ID,
            )
            sentinel = "do-not-replace\n"
            output.write_text(sentinel, encoding="utf-8")
            self.assertEqual(
                CLASSIFIER.main(
                    [
                        str(archive),
                        "--expect-sha256",
                        "0" * 64,
                        "--output",
                        str(output),
                    ]
                ),
                1,
            )
            self.assertEqual(output.read_text(encoding="utf-8"), sentinel)

    def test_utf8_bom_and_windows_1252_spans_are_raw_byte_exact(self) -> None:
        scripts = [
            ("bom.material", b"\xef\xbb\xbfmaterial Synthetic/Bom {}\n"),
            (
                "cp1252.material",
                "material Synthetic/Caf\xe9 {}\n".encode("cp1252"),
            ),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            archive = self.make_archive(Path(temporary), entries=scripts)
            report = CLASSIFIER.classify_archive(archive)
        encodings = {
            script["member"]: script["encoding"]
            for script in report["scripts"]
        }
        self.assertEqual(encodings["bom.material"], "UTF-8-BOM")
        self.assertEqual(encodings["cp1252.material"], "WINDOWS-1252")
        records = {record["name"]: record for record in report["materials"]}
        self.assertEqual(records["Synthetic/Bom"]["source"]["span"]["byte_start"], 3)
        cp_span = records["Synthetic/Caf\xe9"]["source"]["span"]
        self.assertEqual(
            hashlib.sha256(
                scripts[1][1][
                    cp_span["byte_start"] : cp_span["byte_end_exclusive"]
                ]
            ).hexdigest(),
            cp_span["sha256"],
        )


if __name__ == "__main__":
    unittest.main()
