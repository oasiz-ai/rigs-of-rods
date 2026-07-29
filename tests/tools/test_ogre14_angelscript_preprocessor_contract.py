#!/usr/bin/env python3
"""Preprocessor-portability contracts for OGRE 14 AngelScript bindings."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
OGRE_ANGELSCRIPT = (
    REPOSITORY_ROOT
    / "source"
    / "main"
    / "scripting"
    / "bindings"
    / "OgreAngelscript.cpp"
)


def directives_embedded_in_asfunctionpr(source: str) -> list[tuple[int, str]]:
    """Return directives lexically nested in an asFUNCTIONPR invocation."""

    violations: list[tuple[int, str]] = []
    macro_depth = 0

    for line_number, line in enumerate(source.splitlines(), start=1):
        if macro_depth and line.lstrip().startswith("#"):
            violations.append((line_number, line.strip()))

        search_from = 0
        while macro_depth == 0:
            invocation = line.find("asFUNCTIONPR(", search_from)
            if invocation < 0:
                break
            macro_depth = 1
            search_from = invocation + len("asFUNCTIONPR(")

        if macro_depth:
            for character in line[search_from:]:
                if character == "(":
                    macro_depth += 1
                elif character == ")":
                    macro_depth -= 1
                    if macro_depth == 0:
                        break

    return violations


class OgreAngelscriptPreprocessorContractTests(unittest.TestCase):
    def test_detector_finds_directives_inside_macro_arguments(self) -> None:
        source = """\
asFUNCTIONPR([]() {
#if OGRE_VERSION_MAJOR >= 14
    return true;
#else
    return false;
#endif
}, (), bool)
"""

        self.assertEqual(
            directives_embedded_in_asfunctionpr(source),
            [
                (2, "#if OGRE_VERSION_MAJOR >= 14"),
                (4, "#else"),
                (6, "#endif"),
            ],
        )

    def test_asfunctionpr_arguments_do_not_embed_preprocessor_directives(
        self,
    ) -> None:
        source = OGRE_ANGELSCRIPT.read_text(encoding="utf-8")

        self.assertEqual(directives_embedded_in_asfunctionpr(source), [])

    def test_overlay_template_version_switch_is_outside_registration_macro(
        self,
    ) -> None:
        source = OGRE_ANGELSCRIPT.read_text(encoding="utf-8")

        self.assertIn(
            "bool isElementTemplateHelper(Ogre::OverlayManager* self, "
            "const std::string& name)",
            source,
        )
        self.assertIn(
            "asFUNCTIONPR(isElementTemplateHelper, "
            "(Ogre::OverlayManager*, const std::string&), bool)",
            source,
        )
        self.assertIn("#if OGRE_VERSION_MAJOR >= 14", source)
        self.assertIn("return self->hasOverlayElement(name);", source)
        self.assertIn("return self->isTemplate(name);", source)


if __name__ == "__main__":
    unittest.main()
