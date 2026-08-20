#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Alexis Saber archive paint replacement."""

import importlib.util
import io
from pathlib import Path
import sys
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]

_SPEC = importlib.util.spec_from_file_location(
    "apply_alexis_saber_paint", ROOT / "tools" / "apply_alexis_saber_paint.py"
)
apply_paint = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
sys.modules[_SPEC.name] = apply_paint
_SPEC.loader.exec_module(apply_paint)

paint = apply_paint.alexis_saber_paint

EOL = b"\r\n"

TRUCK_FIXTURE = EOL.join([
    b"Alexis Saber Coupe",
    b"",
    b"managedmaterials",
    b";SaberBody flexmesh_standard bodytemp.png - bodytempspec.png",
    b"SaberChassis flexmesh_standard AlexisSaberChassis.png - "
    b"AlexisSaberChassisSpec.png",
    b"SaberWlBand flexmesh_standard AlexisSaberBand.png -",
    b"",
])

MATERIAL_FIXTURE = EOL.join([
    b"material AurigaPaint",
    b"{",
    b"\ttechnique",
    b"\t{",
    b"\t\tpass",
    b"\t\t{",
    b"\t\t\tvertex_program_ref AurigaMatPaint_VP",
    b"\t\t\t{",
    b"\t\t\t}",
    b"\t\t}",
    b"\t}",
    b"}",
    b"",
    b"",
    b"material SaberBody : AurigaPaint",
    b"{",
    b"",
    b"\tset_texture_alias diffuseTex\tbodytemp.png",
    b"\tset_texture_alias specularTex\tbodytempspec.png",
    b"}",
    b"",
])

SKIN_FIXTURE = EOL.join(
    line
    for skin in paint.PAINT_SKINS[1:]
    for line in (
        skin.key.capitalize().encode("ascii"),
        b"{",
        b"\treplaceTexture   = bodytemp.png, " +
        skin.base_color_member.encode("ascii"),
        b"\tdescription      = For the Alexis Saber",
        b"}",
    )
) + EOL


def build_fixture_archive() -> bytes:
    """A stand-in archive with the same member shape as the real one."""

    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("AlexisSaberChassis.png", b"pretend chassis art")
        archive.writestr("AlexisSaber.mesh", b"pretend mesh" * 64)
        archive.writestr(apply_paint.TRUCK_MEMBER, TRUCK_FIXTURE)
        archive.writestr(apply_paint.SKIN_MEMBER, SKIN_FIXTURE)
        archive.writestr(apply_paint.MATERIAL_MEMBER, MATERIAL_FIXTURE)
        for skin in paint.PAINT_SKINS:
            # The shipped placeholders are tiny flat-colour PNGs; the tool
            # never inspects them, only replaces them.
            archive.writestr(skin.base_color_member,
                             b"5x5 placeholder " + skin.key.encode("ascii"))
        archive.writestr(paint.PAINT_SKINS[0].specular_member,
                         b"5x5 specular placeholder")
        archive.writestr("AlexisSabersound.soundscript", b"sounds")
    return buffer.getvalue()


def member_bytes(archive: bytes) -> dict[str, bytes]:
    return {entry.name: apply_paint._member_payload(entry)
            for entry in apply_paint._read_entries(archive)}


def raw_records(archive: bytes) -> dict[str, bytes]:
    return {entry.name: entry.local_bytes
            for entry in apply_paint._read_entries(archive)}


class ArchivePatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.original = build_fixture_archive()
        cls.patched, cls.report = apply_paint.patch_archive(cls.original)

    def test_patched_archive_still_opens(self) -> None:
        with zipfile.ZipFile(io.BytesIO(self.patched)) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(len(archive.infolist()),
                             len(apply_paint._read_entries(self.original)) + 5)

    def test_untouched_members_are_byte_identical(self) -> None:
        touched = set(paint.paint_member_names()) | {
            apply_paint.TRUCK_MEMBER, apply_paint.SKIN_MEMBER,
            apply_paint.MATERIAL_MEMBER}
        before = raw_records(self.original)
        after = raw_records(self.patched)
        untouched = [name for name in before if name not in touched]
        self.assertTrue(untouched)
        for name in untouched:
            with self.subTest(member=name):
                # Not merely equal content: the same local header, extra
                # field and deflate stream.
                self.assertEqual(before[name], after[name])

    def test_member_order_is_preserved(self) -> None:
        before = [entry.name for entry in
                  apply_paint._read_entries(self.original)]
        after = [entry.name for entry in
                 apply_paint._read_entries(self.patched)]
        self.assertEqual(after[:len(before)], before)

    def test_every_paint_member_carries_the_authored_art(self) -> None:
        authored = paint.build_paint_members()
        after = member_bytes(self.patched)
        for name, payload in authored.items():
            with self.subTest(member=name):
                self.assertEqual(after[name], payload)

    def test_the_five_paired_specular_members_are_added(self) -> None:
        before = set(member_bytes(self.original))
        after = set(member_bytes(self.patched))
        self.assertEqual(
            after - before,
            {skin.specular_member for skin in paint.PAINT_SKINS[1:]})
        self.assertEqual(before - after, set())

    def test_only_the_intended_members_changed(self) -> None:
        comparison = apply_paint.compare_archives(self.original, self.patched)
        moved = {name for name, state in comparison.items()
                 if state != "identical"}
        self.assertEqual(
            moved,
            set(paint.paint_member_names()) | {apply_paint.TRUCK_MEMBER,
                                               apply_paint.SKIN_MEMBER,
                                               apply_paint.MATERIAL_MEMBER})

    def test_rerunning_is_a_byte_identical_no_op(self) -> None:
        again, report = apply_paint.patch_archive(self.patched)
        self.assertEqual(again, self.patched)
        self.assertNotIn("replaced", report.values())
        self.assertNotIn("added", report.values())
        self.assertNotIn("patched", report.values())


class TruckPatchTests(unittest.TestCase):
    def test_the_body_declaration_is_uncommented(self) -> None:
        patched = apply_paint.patch_truck(TRUCK_FIXTURE)
        self.assertIn(EOL + apply_paint.TRUCK_RESTORED_BODY + EOL, patched)
        self.assertNotIn(apply_paint.TRUCK_COMMENTED_BODY, patched)

    def test_only_the_body_line_moves(self) -> None:
        patched = apply_paint.patch_truck(TRUCK_FIXTURE)
        self.assertEqual(len(patched.split(EOL)), len(TRUCK_FIXTURE.split(EOL)))
        self.assertIn(b"SaberWlBand flexmesh_standard AlexisSaberBand.png -",
                      patched)

    def test_patching_twice_changes_nothing(self) -> None:
        once = apply_paint.patch_truck(TRUCK_FIXTURE)
        self.assertEqual(apply_paint.patch_truck(once), once)

    def test_a_missing_declaration_is_refused(self) -> None:
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_truck(b"managedmaterials" + EOL)

    def test_a_duplicated_declaration_is_refused(self) -> None:
        doubled = TRUCK_FIXTURE + apply_paint.TRUCK_RESTORED_BODY + EOL
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_truck(doubled)


class SkinPatchTests(unittest.TestCase):
    def test_every_colour_gains_its_paired_specular(self) -> None:
        patched = apply_paint.patch_skin(SKIN_FIXTURE).decode("ascii")
        for skin in paint.PAINT_SKINS[1:]:
            with self.subTest(skin=skin.key):
                self.assertIn(
                    f"replaceTexture   = bodytempspec.png, "
                    f"{skin.specular_member}", patched)

    def test_the_paired_line_follows_its_base_colour(self) -> None:
        lines = [line.strip() for line in
                 apply_paint.patch_skin(SKIN_FIXTURE).split(EOL)]
        for skin in paint.PAINT_SKINS[1:]:
            base = (b"replaceTexture   = bodytemp.png, " +
                    skin.base_color_member.encode("ascii"))
            paired = (b"replaceTexture   = bodytempspec.png, " +
                      skin.specular_member.encode("ascii"))
            with self.subTest(skin=skin.key):
                self.assertEqual(lines[lines.index(base) + 1], paired)

    def test_patching_twice_does_not_duplicate(self) -> None:
        once = apply_paint.patch_skin(SKIN_FIXTURE)
        self.assertEqual(apply_paint.patch_skin(once), once)

    def test_an_unauthored_target_is_refused(self) -> None:
        hostile = (b"Teal" + EOL + b"{" + EOL +
                   b"\treplaceTexture   = bodytemp.png, body_teal.png" + EOL +
                   b"}" + EOL)
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_skin(hostile)

    def test_a_missing_colour_is_refused(self) -> None:
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_skin(b"Blue" + EOL + b"{" + EOL + b"}" + EOL)


class MaterialScriptPatchTests(unittest.TestCase):
    """The dead Cg SaberBody block is what makes OGRE warn that the material
    "has no supportable Techniques and will be blank": it owns the name the
    spawner wants for its placeholder, so every body submesh resolves to a
    material that cannot compile without the Cg plugin."""

    def test_the_dead_body_material_is_removed(self) -> None:
        patched = apply_paint.patch_material_script(MATERIAL_FIXTURE)
        self.assertNotIn(b"SaberBody", patched)

    def test_the_base_materials_are_left_alone(self) -> None:
        patched = apply_paint.patch_material_script(MATERIAL_FIXTURE)
        self.assertIn(b"material AurigaPaint", patched)
        self.assertIn(b"vertex_program_ref AurigaMatPaint_VP", patched)

    def test_the_remaining_braces_stay_balanced(self) -> None:
        patched = apply_paint.patch_material_script(MATERIAL_FIXTURE)
        self.assertEqual(patched.count(b"{"), patched.count(b"}"))

    def test_patching_twice_changes_nothing(self) -> None:
        once = apply_paint.patch_material_script(MATERIAL_FIXTURE)
        self.assertEqual(apply_paint.patch_material_script(once), once)

    def test_a_script_that_never_declared_it_is_accepted(self) -> None:
        plain = EOL.join([b"material AurigaChrome", b"{", b"}", b""])
        self.assertEqual(apply_paint.patch_material_script(plain), plain)

    def test_another_declaration_of_the_name_is_refused(self) -> None:
        # Anything still owning "SaberBody" would make the spawner skip its
        # placeholder again, which is the whole defect being removed.
        hostile = EOL.join([b"material SaberBody : AurigaChrome", b"{", b"}",
                            b""])
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_material_script(hostile)

    def test_a_duplicated_declaration_is_refused(self) -> None:
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_material_script(
                MATERIAL_FIXTURE + MATERIAL_FIXTURE)

    def test_an_unclosed_block_is_refused(self) -> None:
        truncated = EOL.join([b"material SaberBody : AurigaPaint", b"{",
                              b"\tset_texture_alias diffuseTex\tx.png", b""])
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_material_script(truncated)


class ArchiveReaderTests(unittest.TestCase):
    def test_a_truncated_archive_is_refused(self) -> None:
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_archive(b"not a zip at all")

    def test_an_archive_without_the_truck_is_refused(self) -> None:
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("bodytemp.png", b"placeholder")
        with self.assertRaises(apply_paint.ArchivePatchError):
            apply_paint.patch_archive(buffer.getvalue())


if __name__ == "__main__":
    unittest.main()
