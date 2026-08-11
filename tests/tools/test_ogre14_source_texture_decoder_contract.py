#!/usr/bin/env python3
"""Static contract for authenticated renderer-neutral DDS/PNG/JPEG decoding."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RENDER_ROOT = ROOT / "source/main/gfx/render"
HEADER = RENDER_ROOT / "Ogre14SourceTextureDecoder.h"
SOURCE = RENDER_ROOT / "Ogre14SourceTextureDecoder.cpp"
CPP_TEST = ROOT / "tests/gfx/render/Ogre14SourceTextureDecoderTests.cpp"
STB_ROOT = RENDER_ROOT / "third_party/stb"
STB_HEADER = STB_ROOT / "stb_image.h"
STB_LOCK = STB_ROOT / "stb-image-source.lock.json"
STB_LICENSE = STB_ROOT / "LICENSE.txt"
STB_HEADER_SHA256 = (
    "594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3"
)
STB_LOCK_SHA256 = (
    "9902dd2891f8d8733d24cc06316ec98e23eac5b108ccca6c1a519cc94ddf61b6"
)
STB_LICENSE_SHA256 = (
    "771d43eb5017cb859978ad3ddb027fb80ea6119681f286950053404d95b21707"
)
STB_DEFINITIONS = (
    "STB_IMAGE_IMPLEMENTATION",
    "STB_IMAGE_STATIC",
    "STBI_ONLY_PNG",
    "STBI_ONLY_JPEG",
    "STBI_NO_STDIO",
    "STBI_NO_LINEAR",
    "STBI_NO_HDR",
    "STBI_NO_SIMD",
    "STBI_NO_FAILURE_STRINGS",
    "STBI_MAX_DIMENSIONS=8192",
)
PROVENANCE_PATH_COUNTS = {
    "tests/gfx/render/Ogre14SourceTextureDecoderTests.cpp": (3, 2, 1, 1),
    "tests/tools/test_ogre14_source_texture_decoder_contract.py": (3, 2, 1, 1),
    "cmake/VerifyStbImageSource.cmake": (3, 2, 1, 1),
}


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def strict_json_object(payload: str) -> object:
    def reject_duplicates(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    return json.loads(payload, object_pairs_hook=reject_duplicates)


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
            "top-down RGBA8_UNORM",
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

    def test_generic_api_preserves_the_dds_wrapper(self) -> None:
        self.assertIn("DecodeOgre14SourceTexture(", self.header)
        self.assertIn("DecodeOgre14SourceTextureDds(", self.header)
        for token in (
            "admitted legacy 2D DDS, PNG, or JPEG",
            "does not authenticate",
            "authenticated source-texture receipt",
            "generic source decoder changed the legacy DDS wrapper",
        ):
            self.assertIn(token, self.header + self.cpp_test)

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

    def test_pinned_stb_source_identity_and_license_are_exact(self) -> None:
        for path in (STB_HEADER, STB_LOCK, STB_LICENSE):
            self.assertTrue(path.is_file())
            self.assertFalse(path.is_symlink())
        header_bytes = STB_HEADER.read_bytes()
        lock_bytes = STB_LOCK.read_bytes()
        license_bytes = STB_LICENSE.read_bytes()
        self.assertEqual(len(header_bytes), 283010)
        self.assertEqual(sha256(header_bytes), STB_HEADER_SHA256)
        self.assertEqual(sha256(lock_bytes), STB_LOCK_SHA256)
        self.assertEqual(sha256(license_bytes), STB_LICENSE_SHA256)
        lock = strict_json_object(lock_bytes.decode("utf-8"))
        self.assertEqual(
            lock,
            {
                "schema": "ror.ogre14_source_image_codec.v1",
                "dependency": {
                    "name": "stb_image",
                    "repository": "https://github.com/nothings/stb",
                    "commit": "2c980bb59875b0d32144a71867fbdebb2f77cd20",
                    "source_path": "stb_image.h",
                    "source_url": (
                        "https://raw.githubusercontent.com/nothings/stb/"
                        "2c980bb59875b0d32144a71867fbdebb2f77cd20/"
                        "stb_image.h"
                    ),
                    "vendored_path": (
                        "source/main/gfx/render/third_party/stb/stb_image.h"
                    ),
                    "bytes": 283010,
                    "sha256": STB_HEADER_SHA256,
                },
                "license": {
                    "expression": "MIT OR Unlicense",
                    "source_path": (
                        "source/main/gfx/render/third_party/stb/stb_image.h"
                    ),
                    "notice_path": (
                        "source/main/gfx/render/third_party/stb/LICENSE.txt"
                    ),
                    "notice_bytes": 2362,
                    "notice_sha256": STB_LICENSE_SHA256,
                },
                "compile_contract": {
                    "implementation_linkage": "private-static",
                    "formats": ["PNG", "JPEG"],
                    "definitions": list(STB_DEFINITIONS),
                    "product_input": "authenticated-source-bytes-only",
                },
            },
        )

    def test_stb_macro_contract_is_private_ordered_and_complete(self) -> None:
        actual = []
        for line in self.source.splitlines():
            if line.startswith("#define STB_IMAGE_") or line.startswith(
                "#define STBI_"
            ):
                definition = line[len("#define ") :].replace(" ", "=", 1)
                actual.append(definition)
        self.assertEqual(tuple(actual), STB_DEFINITIONS)
        include = '#include "third_party/stb/stb_image.h"'
        self.assertEqual(self.source.count(include), 1)
        self.assertLess(
            self.source.index("#if defined(STBIDEF)"),
            self.source.index("#define STB_IMAGE_IMPLEMENTATION"),
        )
        self.assertLess(
            self.source.index("#define STB_IMAGE_IMPLEMENTATION"),
            self.source.index(include),
        )
        self.assertEqual(self.source.index("#include"), self.source.index(include))
        consulted = set(
            re.findall(
                r"(?:defined\s*\(\s*|^\s*#\s*(?:ifdef|ifndef)\s+)"
                r"(STBIDEF|STB_IMAGE_[A-Z0-9_]+|STBI_[A-Z0-9_]+)",
                STB_HEADER.read_text(encoding="utf-8"),
                flags=re.MULTILINE,
            )
        )
        guard = self.source[
            self.source.index("#if defined(STBIDEF)") :
            self.source.index("#define STB_IMAGE_IMPLEMENTATION")
        ]
        guarded = set(
            re.findall(
                r"defined\s*\(\s*"
                r"(STBIDEF|STB_IMAGE_[A-Z0-9_]+|STBI_[A-Z0-9_]+)",
                guard,
            )
        )
        self.assertEqual(guarded, consulted)
        for token in STB_DEFINITIONS:
            self.assertNotIn(token.split("=")[0], self.header)
        for forbidden in (
            "#include <png.h>",
            "#include <jpeglib.h>",
            "setjmp(",
            "longjmp(",
            "#include <ogre",
        ):
            self.assertNotIn(forbidden, self.source.lower())

        implementation_owners = []
        for source_root in (ROOT / "source", ROOT / "tests", ROOT / "tools"):
            for suffix in ("*.c", "*.cc", "*.cpp", "*.m", "*.mm"):
                for path in source_root.rglob(suffix):
                    if any(
                        re.match(
                            rb"^[ \t]*#[ \t]*define[ \t]+"
                            rb"STB_IMAGE_IMPLEMENTATION(?:[ \t]|$)",
                            line,
                        )
                        for line in path.read_bytes().splitlines()
                    ):
                        implementation_owners.append(path.resolve())
        self.assertEqual(implementation_owners, [SOURCE.resolve()])

    def test_cmake_verifier_rejects_hostile_owner_header_and_target_injection(
        self,
    ) -> None:
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)

        def run_case(kind: str) -> subprocess.CompletedProcess[str]:
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "repository"
                copies = (
                    Path("cmake/VerifyStbImageSource.cmake"),
                    Path("source/main/gfx/render/Ogre14SourceTextureDecoder.cpp"),
                    Path("source/main/gfx/render/Ogre14SourceTextureDecoder.h"),
                    Path("source/main/gfx/render/third_party/stb/stb_image.h"),
                    Path(
                        "source/main/gfx/render/third_party/stb/"
                        "stb-image-source.lock.json"
                    ),
                    Path("source/main/gfx/render/third_party/stb/LICENSE.txt"),
                )
                for relative in copies:
                    destination = root / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(ROOT / relative, destination)
                (root / "CMakeLists.txt").write_text(
                    "# clean verifier fixture\n", encoding="utf-8"
                )
                if kind == "owner":
                    hostile = root / "source/hostile_owner.cpp"
                    hostile.parent.mkdir(parents=True, exist_ok=True)
                    hostile.write_text(
                        "# define STB_IMAGE_IMPLEMENTATION\n",
                        encoding="utf-8",
                    )
                elif kind == "header":
                    public_header = (
                        root
                        / "source/main/gfx/render/Ogre14SourceTextureDecoder.h"
                    )
                    public_header.write_text(
                        "#define STBI_NO_PNG\n"
                        + public_header.read_text(encoding="utf-8"),
                        encoding="utf-8",
                    )
                elif kind == "cmake":
                    (root / "CMakeLists.txt").write_text(
                        "target_compile_definitions(fake PRIVATE STBI_NO_PNG)\n",
                        encoding="utf-8",
                    )
                wrapper = root / "verify.cmake"
                wrapper.write_text(
                    'include("${CMAKE_CURRENT_LIST_DIR}/cmake/'
                    'VerifyStbImageSource.cmake")\n'
                    'ror_verify_stb_image_source("${CMAKE_CURRENT_LIST_DIR}")\n',
                    encoding="utf-8",
                )
                return subprocess.run(
                    [str(cmake), "-P", str(wrapper)],
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="strict",
                )

        clean = run_case("clean")
        self.assertEqual(clean.returncode, 0, clean.stdout + clean.stderr)
        for kind in ("owner", "header", "cmake"):
            with self.subTest(kind=kind):
                hostile = run_case(kind)
                self.assertNotEqual(hostile.returncode, 0)
                self.assertIn("stb_image configuration", hostile.stdout + hostile.stderr)

    def test_png_jpeg_preflight_is_exact_and_bounded(self) -> None:
        for token in (
            "kOgre14SourceImageCodecMaximumDimension",
            "kOgre14SourceImageCodecMaximumPngChunks",
            "PngChunkCrc32",
            "kPngIhdr",
            "kPngPlte",
            "kPngTrns",
            "kPngIdat",
            "kPngIend",
            "kPngIccp",
            "kPngItxt",
            "kPngActl",
            "kPngZtxt",
            "IsAdmittedJpegApplicationMarker",
            "kJpegMarkerSof0",
            "kJpegMarkerSof2",
            "ValidateJpegDqt",
            "ValidateJpegDht",
            "stbi_load_from_memory",
            "source_channels != 3",
        ):
            self.assertIn(token, self.header + self.source)
        for token in (
            "palette+tRNS PNG canonical golden changed",
            "opaque RGB PNG canonical golden changed",
            "Adam7 PNG top-down RGBA golden changed",
            "baseline/progressive JPEG canonical golden changed",
            "admitted uncompressed metadata changed PNG pixels or semantics",
            "PNG trailing byte was accepted",
            "bad PNG chunk CRC was accepted",
            "16-bit PNG samples were accepted",
            "corrupt PNG deflate payload passed pinned decoder validation",
            "APNG animation control was accepted",
            "compressed PNG text was accepted",
            "unknown PNG ancillary chunk was accepted",
            "unknown PNG critical chunk was accepted",
            "compressed PNG iTXt was accepted",
            "JPEG trailing byte was accepted",
            "unsupported JPEG SOF1 was accepted",
            "12-bit JPEG sample precision was accepted",
            "grayscale JPEG frame was accepted",
            "unaudited JPEG APP3 marker was accepted",
            "corrupt JPEG Huffman payload passed pinned decoder validation",
        ):
            self.assertIn(token, self.cpp_test)

    def test_dds_parser_remains_byte_explicit_bounded_and_fail_closed(self) -> None:
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
            "catch (...)" ,
            "std::is_nothrow_move_assignable",
            "output = std::move(candidate)",
            "std::unique_ptr<stbi_uc, StbiPixelsDeleter>",
        ):
            self.assertIn(token, self.header + self.source)
        for token in (
            "bad_alloc/unexpected exception changed decoder output",
            "failure changed transactional decoder output",
            "successful source texture commit did not publish atomically",
            "PNG/JPEG exception changed transactional decoder output",
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
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        vendor_cmake = (
            ROOT / "cmake/VerifyStbImageSource.cmake"
        ).read_text(encoding="utf-8")
        for cmake in (native_cmake, probe_cmake):
            self.assertIn("ror_ogre14_source_texture_decoder_tests", cmake)
            self.assertIn("Ogre14SourceTextureDecoderTests.cpp", cmake)
            self.assertIn("Ogre14SourceTextureDecoder.cpp", cmake)
            self.assertIn("ror_verify_stb_image_source", cmake)
        self.assertIn("Ogre14SourceTextureDecoder.{h,cpp}", source_cmake)
        self.assertIn("third_party/stb/stb_image.h", source_cmake)
        strict_fp_sources = source_cmake.split(
            "set(ROR_RENDER_CONTRACT_STRICT_FP_SOURCES", maxsplit=1
        )[1].split(")", maxsplit=1)[0]
        self.assertIn(
            "gfx/render/Ogre14SourceTextureDecoder.cpp", strict_fp_sources
        )
        self.assertIn('COMPILE_OPTIONS "/fp:strict"', source_cmake)
        self.assertIn(
            'COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off"',
            source_cmake,
        )
        self.assertIn("ror_verify_stb_image_source", root_cmake)
        for token in (
            "ROR_STB_IMAGE_SOURCE_SCHEMA",
            "ROR_STB_IMAGE_UPSTREAM_COMMIT",
            "ROR_STB_IMAGE_SOURCE_LOCK_RELATIVE_PATH",
            "ROR_STB_IMAGE_SOURCE_LOCK_SHA256",
            "ROR_STB_IMAGE_HEADER_RELATIVE_PATH",
            "ROR_STB_IMAGE_HEADER_SIZE",
            "ROR_STB_IMAGE_HEADER_SHA256",
            "ROR_STB_IMAGE_LICENSE_RELATIVE_PATH",
            "ROR_STB_IMAGE_LICENSE_SIZE",
            "ROR_STB_IMAGE_LICENSE_SHA256",
            "ROR_STB_IMAGE_MACRO_CONTRACT_VERIFIED_JSON",
            "CACHE INTERNAL",
        ):
            self.assertIn(token, vendor_cmake)
        self.assertIn("ror_ogre14_source_texture_decoder_tests", workflow)
        self.assertIn("-R '^ror_ogre14_source_texture_decoder$'", workflow)
        self.assertIn(
            "python tests/tools/test_ogre14_source_texture_decoder_contract.py",
            workflow,
        )
        self.assertIn(
            "python -O tests/tools/test_ogre14_source_texture_decoder_contract.py",
            workflow,
        )
        self.assertEqual(workflow.count("- cmake/VerifyStbImageSource.cmake"), 2)
        for platform in ("macos", "linux", "windows"):
            self.assertIn(f"platform: {platform}", workflow)

    def test_provenance_lists_cover_decoder_vendor_and_tests(self) -> None:
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
        for path, expected_counts in PROVENANCE_PATH_COUNTS.items():
            for manifest, expected in zip(manifests, expected_counts):
                with self.subTest(path=path, expected=expected):
                    self.assertEqual(manifest.count(path), expected)
        for manifest in manifests:
            self.assertIn("source/main/gfx/render", manifest)


if __name__ == "__main__":
    unittest.main()
