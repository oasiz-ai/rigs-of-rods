#!/usr/bin/env python3

import collections
import re
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
OGRE_CORE = REPOSITORY_ROOT / "resources" / "OgreCore"
FRESNEL_MATERIAL = REPOSITORY_ROOT / "resources" / "materials" / "fresnel.material"
NEEDLES_OVERLAY = REPOSITORY_ROOT / "resources" / "overlays" / "needles.overlay"

FONT_NAMES = {
    "Vera.fontdef": {"VeraMono", "VeraMonoBold"},
    "cent.fontdef": {"cent", "cent_black"},
    "cyberbit.fontdef": {"CyberbitEnglish"},
    "highcontrast.fontdef": {
        "highcontrast_black",
        "highcontrast_red",
        "highcontrast_green",
        "highcontrast_blue",
    },
}
IMAGE_FONT_COUNTS = {
    "cent.fontdef": 2,
    "highcontrast.fontdef": 4,
}
SEPARATOR_CODE_POINTS = (34, 58, 123, 125)

NEEDLE_PANELS = {
    "tracks/speedoneedle",
    "tracks/tachoneedle",
    "tracks/NeedlesTextContainer",
}
NEEDLE_TEXT_AREAS = {
    "tracks/Gear",
    "tracks/AGearR",
    "tracks/AGearN",
    "tracks/AGearD",
    "tracks/AGear2",
    "tracks/AGear1",
}


def overlay_element_declarations(script: str) -> list[tuple[str, str, int]]:
    declarations = []
    depth = 0

    for line in script.splitlines():
        code = line.split("//", maxsplit=1)[0]
        match = re.match(r"\s*overlay_element\s+(\S+)\s+(\S+)\s*$", code)
        if match is not None:
            declarations.append((match.group(1), match.group(2), depth))
        depth += code.count("{") - code.count("}")

    if depth != 0:
        raise AssertionError(f"unbalanced overlay braces: final depth {depth}")
    return declarations


class Ogre14CoreResourceScriptTests(unittest.TestCase):
    def test_fontdefs_use_explicit_font_objects(self) -> None:
        for filename, expected_names in FONT_NAMES.items():
            with self.subTest(filename=filename):
                script = (OGRE_CORE / filename).read_text(encoding="utf-8")
                names = set(re.findall(r"^font\s+(\S+)\s*$", script, re.MULTILINE))

                self.assertEqual(names, expected_names)
                self.assertEqual(script.count("\nfont ") + script.startswith("font "), len(names))

    def test_image_font_separator_glyphs_use_numeric_code_points(self) -> None:
        for filename, font_count in IMAGE_FONT_COUNTS.items():
            with self.subTest(filename=filename):
                script = (OGRE_CORE / filename).read_text(encoding="utf-8")
                glyphs = re.findall(r"^\s*glyph\s+(\S+)\s+", script, re.MULTILINE)

                self.assertEqual(len(glyphs), font_count * 94)
                for code_point in SEPARATOR_CODE_POINTS:
                    self.assertEqual(glyphs.count(f"u{code_point}"), font_count)
                self.assertFalse({"\"", ":", "{", "}"} & set(glyphs))

    def test_fresnel_rtt_declarations_match_precreated_textures(self) -> None:
        script = FRESNEL_MATERIAL.read_text(encoding="utf-8")
        declarations = re.findall(
            r"^\s*texture\s+(Reflection|Refraction)(?:\s+(.*?))?\s*$",
            script,
            re.MULTILINE,
        )

        self.assertEqual(
            collections.Counter(name for name, _ in declarations),
            collections.Counter({"Reflection": 4, "Refraction": 2}),
        )
        self.assertTrue(declarations)
        self.assertTrue(all(arguments == "2d 0 PF_R8G8B8" for _, arguments in declarations))

    def test_needles_text_areas_are_children_of_a_fullscreen_panel(self) -> None:
        script = NEEDLES_OVERLAY.read_text(encoding="utf-8")
        declarations = overlay_element_declarations(script)

        panels = {(name, depth) for name, kind, depth in declarations if kind == "Panel"}
        text_areas = {
            (name, depth) for name, kind, depth in declarations if kind == "TextArea"
        }
        self.assertEqual(panels, {(name, 1) for name in NEEDLE_PANELS})
        self.assertEqual(text_areas, {(name, 2) for name in NEEDLE_TEXT_AREAS})

        wrapper_start = script.index(
            "overlay_element tracks/NeedlesTextContainer Panel"
        )
        first_text_area = script.index("overlay_element tracks/Gear TextArea")
        wrapper_properties = script[wrapper_start:first_text_area]
        for property_line in (
            "metrics_mode relative",
            "horz_align left",
            "vert_align top",
            "left 0",
            "top 0",
            "width 1",
            "height 1",
        ):
            self.assertRegex(
                wrapper_properties,
                rf"(?m)^\s*{re.escape(property_line)}\s*$",
            )


if __name__ == "__main__":
    unittest.main()
