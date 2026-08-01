#!/usr/bin/env python3
"""Static hygiene gate for the renderer-neutral public boundary."""

from __future__ import annotations

import pathlib
import re
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
BOUNDARY_ROOT = REPOSITORY_ROOT / "source" / "main" / "gfx" / "render"


class RendererBoundaryContractTests(unittest.TestCase):
    def test_code_has_no_renderer_sdk_includes_or_types(self) -> None:
        code_files = sorted(BOUNDARY_ROOT.glob("*.h")) + sorted(
            BOUNDARY_ROOT.glob("*.cpp")
        )
        self.assertTrue(code_files)
        forbidden = {
            "renderer SDK include": re.compile(
                r"^\s*#\s*include\s*[<\"][^>\"]*(?:Ogre|OGRE|Metal|d3d12|vulkan)",
                re.MULTILINE,
            ),
            "OGRE C++ type": re.compile(r"\bOgre::"),
            "platform graphics pointer": re.compile(
                r"\b(?:ID3D12\w+|MTL\w+|Vk\w+)\s*\*"
            ),
        }
        for path in code_files:
            text = path.read_text(encoding="utf-8")
            for label, pattern in forbidden.items():
                self.assertIsNone(pattern.search(text), f"{path}: {label}")

    def test_quoted_public_includes_stay_inside_boundary(self) -> None:
        for path in sorted(BOUNDARY_ROOT.glob("*.h")):
            text = path.read_text(encoding="utf-8")
            for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.MULTILINE):
                self.assertNotIn("..", include, f"{path}: parent include escaped boundary")
                self.assertTrue(
                    (BOUNDARY_ROOT / include).is_file(),
                    f"{path}: quoted include is outside boundary: {include}",
                )

    def test_interfaces_and_fail_closed_proofs_are_present(self) -> None:
        frontend = (BOUNDARY_ROOT / "RendererFrontend.h").read_text(encoding="utf-8")
        for contract in (
            "class IRendererFrontend",
            "class NativeRenderInterop",
            "class INativeRayTracingBackend",
            "CreateMesh",
            "CreateTexture",
            "CreateSampler",
            "AcquireContext",
            "UpdateSurface",
            "ValidateFrontendSurfaceUpdate",
            "ValidateFrontendSurfaceTransition",
            "ValidateRenderFramePresentation",
            "ValidateNativeGeometryInteropProofSet",
            "ValidateGeometryLease",
            "ValidateInteropEvidence",
            "OUTSTANDING_LEASES",
            "owner/render thread",
            "native_ray_tracing_probe_passed = false",
            "native_ray_tracing_geometry_interop_ready = false",
            "geometry_interop_proven = false",
        ):
            self.assertIn(contract, frontend)


if __name__ == "__main__":
    unittest.main()
