#!/usr/bin/env python3
"""Static contract for deterministic renderer-neutral legacy DDS decoding."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "source/main/gfx/render/Ogre14SourceTextureDecoder.h"
SOURCE = ROOT / "source/main/gfx/render/Ogre14SourceTextureDecoder.cpp"
CPP_TEST = ROOT / "tests/gfx/render/Ogre14SourceTextureDecoderTests.cpp"
PROVENANCE_PATHS = (
    "tests/gfx/render/Ogre14SourceTextureDecoderTests.cpp",
    "tests/tools/test_ogre14_source_texture_decoder_contract.py",
)


class Ogre14SourceTextureDecoderContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")

    def test_output_is_canonical_and_semantics_stay_external(self) -> None:
        for token in (
            "Ogre14SourceTextureColorSemantic",
            "UNSPECIFIED",
            "SRGB_COLOR",
            "LINEAR_DATA",
            "Ogre14SourceTextureBc1AlphaMode",
            "OPAQUE",
            "ONE_BIT_ALPHA",
            "row_pitch_bytes",
            "slice_pitch_bytes",
            "rgba8_unorm",
            "source_has_alpha",
        ):
            self.assertIn(token, self.header)
        self.assertIn("width * 4 bytes", self.header)
        self.assertIn("decoder never", self.header)
        self.assertIn("external color semantic", self.cpp_test)
        for forbidden in (
            "tolower(",
            "filename",
            "guess_srgb",
            "std::pow",
            "powf(",
        ):
            self.assertNotIn(forbidden, self.source.lower())

    def test_supported_formats_and_integer_modes_are_explicit(self) -> None:
        for token in (
            "kFourCcDxt1",
            "kFourCcDxt3",
            "kFourCcDxt5",
            "kFourCcAti1",
            "kFourCcAti2",
            "kFourCcBc4u",
            "kFourCcBc5u",
            "BC1_UNORM",
            "BC2_UNORM",
            "BC3_UNORM",
            "BC4_UNORM",
            "BC5_UNORM",
            "RGBA8_UNORM",
            "RGBX8_UNORM",
            "BGRA8_UNORM",
            "BGRX8_UNORM",
            "InterpolateColorThird",
            "BuildInterpolatedChannelTable",
        ):
            self.assertIn(token, self.header + self.source)
        self.assertNotIn("float", self.source)
        for token in (
            "TestBc1ModesAndEdgeClipping",
            "TestBc2ExplicitAlpha",
            "TestBc3BothAlphaInterpolationModes",
            "TestBc4Bc5AndLegacyAliases",
            "TestCompressedMipChain",
            "170U, 0U, 85U",
            "218U, 182U, 145U",
        ):
            self.assertIn(token, self.cpp_test)

    def test_parser_is_byte_explicit_bounded_and_fail_closed(self) -> None:
        for token in (
            "ReadU32LittleEndian",
            "ReadBlockU16",
            "ReadBlockU32",
            "ReadBlockU48",
            "ReadBlockU64",
            "CheckedAdd",
            "CheckedMultiply",
            "maximum_dimension",
            "maximum_mip_levels",
            "maximum_encoded_bytes",
            "maximum_decoded_bytes",
            "trailing_bytes",
            "cube_map",
            "volume",
            "dx10",
            "mip_count",
        ):
            self.assertIn(token, self.header + self.source)
        for forbidden in (
            "reinterpret_cast",
            "static_cast<const dds",
            "memcpy(",
            "ogre::",
            "#include <ogre",
            "stb_",
            "squish",
            "directxtex",
        ):
            self.assertNotIn(forbidden, self.source.lower())
        for token in (
            "truncated header was accepted",
            "truncated mip payload was accepted",
            "trailing DDS byte was accepted",
            "cube map was accepted",
            "volume caps were accepted",
            "DX10 extension/array container was accepted",
            "unsupported or signed FourCC was accepted",
            "overflow-scale width was accepted",
        ):
            self.assertIn(token, self.cpp_test)

    def test_output_commit_rolls_back_all_failures(self) -> None:
        for token in (
            "AFTER_HEADER_VALIDATION",
            "AFTER_FIRST_MIP_DECODE",
            "BEFORE_COMMIT",
            "catch (const std::bad_alloc &)",
            "catch (...)",
            "std::is_nothrow_move_assignable",
            "output = std::move(candidate)",
        ):
            self.assertIn(token, self.header + self.source)
        for token in (
            "bad_alloc/unexpected exception changed decoder output",
            "failure changed transactional decoder output",
            "successful source texture commit did not publish atomically",
        ):
            self.assertIn(token, self.cpp_test)

    def test_native_and_probe_builds_run_the_cross_platform_gate(self) -> None:
        native_cmake = (ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        probe_cmake = (
            ROOT / "tools/ogre_next_probe/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        workflow = (
            ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")
        source_cmake = (ROOT / "source/main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        for cmake in (native_cmake, probe_cmake):
            self.assertIn("ror_ogre14_source_texture_decoder_tests", cmake)
            self.assertIn("Ogre14SourceTextureDecoderTests.cpp", cmake)
            self.assertIn("Ogre14SourceTextureDecoder.cpp", cmake)
        self.assertIn("Ogre14SourceTextureDecoder.{h,cpp}", source_cmake)
        self.assertIn("ror_ogre14_source_texture_decoder_tests", workflow)
        self.assertIn("-R '^ror_ogre14_source_texture_decoder$'", workflow)
        for platform in ("macos", "linux", "windows"):
            self.assertIn(f"platform: {platform}", workflow)

    def test_provenance_lists_cover_decoder_tests(self) -> None:
        manifests = (
            (
                ROOT / "tools/ogre_next_probe/CMakeLists.txt"
            ).read_text(encoding="utf-8"),
            (
                ROOT
                / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake"
            ).read_text(encoding="utf-8"),
            (ROOT / "tools/run_ogre_next_probe.py").read_text(
                encoding="utf-8"
            ),
            (ROOT / "tools/verify_ogre_next_artifact_set.py").read_text(
                encoding="utf-8"
            ),
        )
        for path in PROVENANCE_PATHS:
            probe_count = 3 if path.endswith("Tests.cpp") else 2
            expected_counts = (probe_count, 2, 1, 1)
            for manifest, expected in zip(manifests, expected_counts):
                with self.subTest(path=path, expected=expected):
                    self.assertEqual(manifest.count(path), expected)


if __name__ == "__main__":
    unittest.main()
