#!/usr/bin/env python3
"""Offline executable checks for the portable native hard-shadow contract."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source/main/gfx/render"
CONTRACT_HEADER = (
    RENDER_ROOT / "ogrenext/NativeDirectionalShadowContract.h"
)
CONTRACT_SOURCE = (
    RENDER_ROOT / "ogrenext/NativeDirectionalShadowContract.cpp"
)
PROBE_CMAKE = REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt"
NATIVE_INTEROP_HEADER = (
    RENDER_ROOT / "ogrenext/OgreNextN1NativeInterop.h"
)
FRONTEND_SOURCE = RENDER_ROOT / "ogrenext/OgreNextN1Frontend.cpp"
WORKFLOW = REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"


HARNESS_SOURCE = r"""
#include "NativeDirectionalShadowContract.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using namespace RoR::Render;

bool Check(bool condition, const char *detail) {
  if (!condition) {
    std::cerr << detail << '\n';
  }
  return condition;
}

NativeDirectionalShadowCapabilities CompleteCapabilities(
    NativeDirectionalShadowBackend backend) {
  NativeDirectionalShadowCapabilities capabilities;
  capabilities.backend = backend;
  capabilities.hardware_ray_tracing = true;
  capabilities.same_device_raster_and_ray_queue = true;
  capabilities.two_level_acceleration_structures = true;
  capabilities.primary_camera_rays = true;
  capabilities.secondary_directional_visibility_rays = true;
  capabilities.r16_float_visibility = true;
  capabilities.rgba16_float_hybrid_composite = true;
  return capabilities;
}

NativeDirectionalShadowPassContract CompletePass(
    NativeDirectionalShadowVisibility visibility) {
  NativeDirectionalShadowPassContract contract;
  contract.tier =
      NativeDirectionalShadowTier::NATIVE_DIRECTIONAL_HARD_SHADOW_V1;
  contract.raster_feature_tier =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  contract.capabilities =
      CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  contract.frame_id = 41U;
  contract.snapshot_id = 42U;
  contract.view_id = 43U;
  contract.receiver_instance_id = 100U;
  contract.occluder_instance_id = 200U;
  contract.blas_count = kNativeDirectionalShadowRequiredBlasCount;
  contract.tlas_instance_count =
      kNativeDirectionalShadowRequiredTlasInstanceCount;
  contract.receiver_blas_built = true;
  contract.occluder_blas_built = true;
  contract.tlas_built = true;
  contract.primary_camera_ray_count =
      kNativeDirectionalShadowRequiredPrimaryRayCount;
  contract.secondary_visibility_ray_count =
      kNativeDirectionalShadowRequiredVisibilityRayCount;
  contract.primary_hit_instance_id = contract.receiver_instance_id;
  contract.secondary_blocker_instance_id =
      visibility == NativeDirectionalShadowVisibility::OCCLUDED
          ? contract.occluder_instance_id
          : 0U;
  contract.primary_camera_ray_geometry_exact = true;
  contract.secondary_directional_ray_geometry_exact = true;
  contract.native_submission_completed = true;
  contract.raster_source_ui_free = true;
  contract.visibility_readback_completed = true;
  contract.hybrid_readback_completed = true;
  contract.visibility = visibility;
  contract.raster_rgba16.channels = {
      0x3c00U, 0x4000U, 0x4200U, 0x3800U};
  contract.native_hybrid_rgba16 = contract.raster_rgba16;
  if (visibility == NativeDirectionalShadowVisibility::VISIBLE) {
    contract.native_visibility_r16_bits =
        kNativeDirectionalShadowVisibleR16;
  } else {
    contract.native_visibility_r16_bits =
        kNativeDirectionalShadowOccludedR16;
    contract.native_hybrid_rgba16.channels[0U] = 0U;
    contract.native_hybrid_rgba16.channels[1U] = 0U;
    contract.native_hybrid_rgba16.channels[2U] = 0U;
  }
  return contract;
}

bool CheckCapabilityAdmission() {
  NativeDirectionalShadowCapabilities empty;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(empty),
             "default capabilities did not fail closed") ||
      !Check(ResolveNativeDirectionalShadowTier(true, empty) ==
                 NativeDirectionalShadowTier::PORTABLE_PSSM_FALLBACK_V1,
             "incomplete capabilities did not preserve PSSM fallback")) {
    return false;
  }

  const std::array<NativeDirectionalShadowBackend, 3U> backends{
      NativeDirectionalShadowBackend::METAL,
      NativeDirectionalShadowBackend::VULKAN_KHR,
      NativeDirectionalShadowBackend::DIRECT3D12_DXR};
  for (NativeDirectionalShadowBackend backend : backends) {
    NativeDirectionalShadowCapabilities capabilities =
        CompleteCapabilities(backend);
    if (!Check(IsKnownNativeDirectionalShadowBackend(backend),
               "recognized backend was rejected") ||
        !Check(HasAttestedNativeDirectionalShadowCapabilities(capabilities),
               "complete capabilities were rejected") ||
        !Check(ResolveNativeDirectionalShadowTier(true, capabilities) ==
                   NativeDirectionalShadowTier::
                       NATIVE_DIRECTIONAL_HARD_SHADOW_V1,
               "complete requested capability did not select native") ||
        !Check(ResolveNativeDirectionalShadowTier(false, capabilities) ==
                   NativeDirectionalShadowTier::PORTABLE_PSSM_FALLBACK_V1,
               "unrequested native path replaced PSSM")) {
      return false;
    }
  }

  NativeDirectionalShadowCapabilities mutation =
      CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.version = kNativeDirectionalShadowContractVersion + 1U;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
             "unsupported capability version was admitted")) {
    return false;
  }
  mutation = CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.backend = NativeDirectionalShadowBackend::INVALID;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
             "invalid backend was admitted") ||
      !Check(!IsKnownNativeDirectionalShadowBackend(
                 static_cast<NativeDirectionalShadowBackend>(255U)),
             "unknown backend was admitted")) {
    return false;
  }

  mutation = CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.hardware_ray_tracing = false;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
             "missing hardware RT was admitted")) {
    return false;
  }
  mutation = CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.same_device_raster_and_ray_queue = false;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
             "cross-device raster/RT was admitted")) {
    return false;
  }
  mutation = CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.two_level_acceleration_structures = false;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
             "missing acceleration structures were admitted")) {
    return false;
  }
  mutation = CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.primary_camera_rays = false;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
             "missing primary rays were admitted")) {
    return false;
  }
  mutation = CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.secondary_directional_visibility_rays = false;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
             "missing visibility rays were admitted")) {
    return false;
  }
  mutation = CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.r16_float_visibility = false;
  if (!Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
             "missing R16 visibility was admitted")) {
    return false;
  }
  mutation = CompleteCapabilities(NativeDirectionalShadowBackend::METAL);
  mutation.rgba16_float_hybrid_composite = false;
  return Check(!HasAttestedNativeDirectionalShadowCapabilities(mutation),
               "missing RGBA16 hybrid support was admitted");
}

bool CheckSampleOracle() {
  NativeDirectionalShadowRgba16Pixel raster;
  raster.channels = {0x3c00U, 0x4000U, 0x4200U, 0x3800U};
  NativeDirectionalShadowSampleOracle visible;
  if (!Check(TryBuildNativeDirectionalShadowSampleOracle(
                 NativeDirectionalShadowVisibility::VISIBLE, raster,
                 visible).ok(),
             "visible oracle was rejected") ||
      !Check(visible.version == kNativeDirectionalShadowContractVersion &&
                 visible.visibility ==
                     NativeDirectionalShadowVisibility::VISIBLE &&
                 visible.visibility_r16_bits ==
                     kNativeDirectionalShadowVisibleR16 &&
                 visible.hybrid_rgba16.channels == raster.channels,
             "visible oracle changed raster bytes")) {
    return false;
  }

  NativeDirectionalShadowSampleOracle occluded;
  if (!Check(TryBuildNativeDirectionalShadowSampleOracle(
                 NativeDirectionalShadowVisibility::OCCLUDED, raster,
                 occluded).ok(),
             "occluded oracle was rejected") ||
      !Check(occluded.visibility_r16_bits ==
                     kNativeDirectionalShadowOccludedR16 &&
                 occluded.hybrid_rgba16.channels ==
                     std::array<std::uint16_t, 4U>{0U, 0U, 0U, 0x3800U},
             "occluded oracle did not zero RGB and preserve alpha")) {
    return false;
  }

  NativeDirectionalShadowSampleOracle sentinel;
  sentinel.version = 99U;
  sentinel.visibility = NativeDirectionalShadowVisibility::VISIBLE;
  sentinel.visibility_r16_bits = 123U;
  sentinel.hybrid_rgba16.channels = {1U, 2U, 3U, 4U};
  const NativeDirectionalShadowSampleOracle before = sentinel;
  if (!Check(!TryBuildNativeDirectionalShadowSampleOracle(
                 NativeDirectionalShadowVisibility::INVALID, raster,
                 sentinel),
             "invalid visibility produced an oracle") ||
      !Check(sentinel.version == before.version &&
                 sentinel.visibility == before.visibility &&
                 sentinel.visibility_r16_bits == before.visibility_r16_bits &&
                 sentinel.hybrid_rgba16.channels ==
                     before.hybrid_rgba16.channels,
             "failed oracle construction changed output")) {
    return false;
  }

  for (std::uint16_t invalid :
       std::array<std::uint16_t, 4U>{0x8000U, 0xbc00U, 0x7c00U,
                                    0x7e00U}) {
    NativeDirectionalShadowRgba16Pixel mutated = raster;
    mutated.channels[0U] = invalid;
    if (!Check(!TryBuildNativeDirectionalShadowSampleOracle(
                   NativeDirectionalShadowVisibility::VISIBLE, mutated,
                   sentinel),
               "noncanonical RGB was accepted")) {
      return false;
    }
  }
  NativeDirectionalShadowRgba16Pixel high_alpha = raster;
  high_alpha.channels[3U] = 0x3c01U;
  return Check(!TryBuildNativeDirectionalShadowSampleOracle(
                   NativeDirectionalShadowVisibility::VISIBLE, high_alpha,
                   sentinel),
               "alpha above one was accepted");
}

bool Rejected(const NativeDirectionalShadowPassContract &contract) {
  return !ValidateNativeDirectionalShadowPassContract(contract);
}

bool CheckPassContract() {
  NativeDirectionalShadowPassContract visible =
      CompletePass(NativeDirectionalShadowVisibility::VISIBLE);
  NativeDirectionalShadowPassContract occluded =
      CompletePass(NativeDirectionalShadowVisibility::OCCLUDED);
  if (!Check(ValidateNativeDirectionalShadowPassContract(visible).ok(),
             "valid visible pass was rejected") ||
      !Check(ValidateNativeDirectionalShadowPassContract(occluded).ok(),
             "valid occluded pass was rejected")) {
    return false;
  }

  NativeDirectionalShadowPassContract mutation = visible;
  mutation.version += 1U;
  if (!Check(Rejected(mutation), "unsupported pass version was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.tier =
      NativeDirectionalShadowTier::PORTABLE_PSSM_FALLBACK_V1;
  if (!Check(Rejected(mutation), "fallback tier forged a native pass")) {
    return false;
  }
  mutation = visible;
  mutation.raster_feature_tier = OgreNextRasterFeatureTier::STATIC_PBR_N1;
  if (!Check(Rejected(mutation), "N1 raster forged an N4 native pass")) {
    return false;
  }
  mutation = visible;
  mutation.capabilities.secondary_directional_visibility_rays = false;
  if (!Check(Rejected(mutation), "partial capability set was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.frame_id = 0U;
  if (!Check(Rejected(mutation), "zero frame lineage was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.occluder_instance_id = mutation.receiver_instance_id;
  if (!Check(Rejected(mutation), "aliased receiver/occluder was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.blas_count = 1U;
  if (!Check(Rejected(mutation), "one-BLAS pass was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.occluder_blas_built = false;
  if (!Check(Rejected(mutation), "unbuilt occluder BLAS was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.tlas_instance_count = 3U;
  if (!Check(Rejected(mutation), "three-instance TLAS was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.primary_camera_ray_count = 2U;
  if (!Check(Rejected(mutation), "extra primary ray was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.secondary_visibility_ray_count = 0U;
  if (!Check(Rejected(mutation), "missing visibility ray was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.primary_hit_instance_id = mutation.occluder_instance_id;
  if (!Check(Rejected(mutation), "primary ray missed receiver lineage")) {
    return false;
  }
  mutation = visible;
  mutation.secondary_directional_ray_geometry_exact = false;
  if (!Check(Rejected(mutation), "unattested light ray was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.secondary_blocker_instance_id = mutation.occluder_instance_id;
  if (!Check(Rejected(mutation), "visible sample reported a blocker")) {
    return false;
  }
  mutation = occluded;
  mutation.secondary_blocker_instance_id = 0U;
  if (!Check(Rejected(mutation), "occluded sample omitted its blocker")) {
    return false;
  }
  mutation = visible;
  mutation.native_submission_completed = false;
  if (!Check(Rejected(mutation), "incomplete submission was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.raster_source_ui_free = false;
  if (!Check(Rejected(mutation), "UI-bearing raster was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.visibility_readback_completed = false;
  if (!Check(Rejected(mutation), "missing visibility readback was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.visibility = NativeDirectionalShadowVisibility::INVALID;
  if (!Check(Rejected(mutation), "invalid visibility was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.native_visibility_r16_bits =
      kNativeDirectionalShadowOccludedR16;
  if (!Check(Rejected(mutation), "forged R16 visibility was accepted")) {
    return false;
  }
  mutation = visible;
  mutation.native_hybrid_rgba16.channels[1U] = 0U;
  return Check(Rejected(mutation), "forged hybrid pixel was accepted");
}

} // namespace

int main() {
  static_assert(kNativeDirectionalShadowContractVersion == 1U,
                "native hard-shadow contract version changed");
  static_assert(kNativeDirectionalShadowOccludedR16 == 0x0000U &&
                    kNativeDirectionalShadowVisibleR16 == 0x3c00U,
                "native hard-shadow R16 oracle changed");
  if (!CheckCapabilityAdmission() || !CheckSampleOracle() ||
      !CheckPassContract()) {
    return 1;
  }
  std::cout << "native-directional-shadow-contract-v1: pass\n";
  return 0;
}
"""


class OgreNextNativeDirectionalShadowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = CONTRACT_HEADER.read_text(encoding="utf-8")
        cls.source = CONTRACT_SOURCE.read_text(encoding="utf-8")
        cls.cmake = PROBE_CMAKE.read_text(encoding="utf-8")
        cls.native_interop = NATIVE_INTEROP_HEADER.read_text(encoding="utf-8")
        cls.frontend = FRONTEND_SOURCE.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_contract_is_renderer_neutral_and_preserves_pssm_fallback(self) -> None:
        combined = self.header + self.source
        for forbidden in (
            "#include <Metal/",
            "#include <vulkan/",
            "#include <d3d12.h>",
            "id<MTL",
            "VkAccelerationStructure",
            "ID3D12",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, combined)
        for required in (
            "PORTABLE_PSSM_FALLBACK_V1",
            "NATIVE_DIRECTIONAL_HARD_SHADOW_V1",
            "VULKAN_KHR",
            "DIRECT3D12_DXR",
            "kNativeDirectionalShadowOccludedR16 = 0x0000U",
            "kNativeDirectionalShadowVisibleR16 = 0x3c00U",
        ):
            with self.subTest(required=required):
                self.assertIn(required, combined)

    def test_contract_is_compiled_by_the_isolated_frontend(self) -> None:
        self.assertIn(
            '"${_ror_render_root}/ogrenext/'
            'NativeDirectionalShadowContract.cpp"',
            self.cmake,
        )

    def test_n4_is_explicit_rt4_image_interop_and_runs_on_every_ci_host(
        self,
    ) -> None:
        tier = "METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW"
        self.assertIn(tier, self.native_interop)
        self.assertIn(tier, self.frontend)
        self.assertIn("UsesMetalImageInterop", self.frontend)
        self.assertIn(
            "native N4 and PSSM directional shadows are mutually exclusive",
            self.frontend,
        )
        test_path = (
            "tests/tools/test_ogre_next_native_directional_shadow_contract.py"
        )
        self.assertEqual(self.workflow.count(test_path), 2)
        self.assertIn("python " + test_path, self.workflow)
        self.assertIn("python -O " + test_path, self.workflow)

    def test_cpp_oracle_and_fail_closed_mutations(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-native-directional-shadow-contract-"
        ) as temporary:
            root = Path(temporary)
            source = root / "contract_harness.cpp"
            source.write_text(HARNESS_SOURCE, encoding="utf-8")
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                textwrap.dedent(
                    f"""
                    cmake_minimum_required(VERSION 3.24)
                    project(native_directional_shadow_contract LANGUAGES CXX)
                    add_executable(native_directional_shadow_contract_harness
                        "{source.as_posix()}"
                        "{CONTRACT_SOURCE.as_posix()}")
                    target_include_directories(
                        native_directional_shadow_contract_harness PRIVATE
                        "{CONTRACT_HEADER.parent.as_posix()}"
                        "{RENDER_ROOT.as_posix()}")
                    set_target_properties(
                        native_directional_shadow_contract_harness PROPERTIES
                        CXX_STANDARD 17
                        CXX_STANDARD_REQUIRED YES
                        CXX_EXTENSIONS NO)
                    if (MSVC)
                        target_compile_options(
                            native_directional_shadow_contract_harness PRIVATE
                            /W4 /WX /permissive- /Zc:__cplusplus /fp:strict)
                    else ()
                        target_compile_options(
                            native_directional_shadow_contract_harness PRIVATE
                            -Wall -Wextra -Werror -pedantic -fno-fast-math
                            -ffp-contract=off)
                    endif ()
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            build = root / "build"
            subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(root),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                ],
                cwd=REPOSITORY_ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run(
                ["cmake", "--build", str(build), "--parallel", "2"],
                cwd=REPOSITORY_ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            executable = build / (
                "native_directional_shadow_contract_harness.exe"
                if os.name == "nt"
                else "native_directional_shadow_contract_harness"
            )
            result = subprocess.run(
                [str(executable)],
                cwd=REPOSITORY_ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                result.stdout,
                "native-directional-shadow-contract-v1: pass\n",
            )


if __name__ == "__main__":
    unittest.main()
