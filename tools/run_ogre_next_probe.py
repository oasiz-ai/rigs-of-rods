#!/usr/bin/env python3
"""Build and validate the isolated, pinned OGRE-Next capability probe."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import ntpath
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
PROBE_SOURCE = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
LOCK_PATH = PROBE_SOURCE / "ogre-next.lock.json"
NORMAL_MAP_SOURCE_LOCK_PATH = (
    PROBE_SOURCE / "ogre-next-normal-map-source.lock.json"
)
NORMAL_MAP_SOURCE_LOCK_SHA256 = (
    "7d180c54c54e7cc26b0081753c621b7164551d2b631c1127f818fbb22645f682"
)
DISPLAY_DOMAIN_MEDIA_PATH = (
    PROBE_SOURCE
    / "media/Hlms/RoR/DisplayDomain/DisplayDomain_piece_ps.any"
)
DISPLAY_DOMAIN_MEDIA_RELATIVE = (
    "Hlms/RoR/DisplayDomain/DisplayDomain_piece_ps.any"
)
DISPLAY_DOMAIN_NOTICE_PATH = "licenses/Rigs-of-Rods-GPL-3.0.txt"
DISPLAY_DOMAIN_LICENSE_EXPRESSION = "GPL-3.0-or-later"
DISPLAY_DOMAIN_NOTICE_SOURCE = REPOSITORY_ROOT / "COPYING"
LINUX_SHADER_TOOLCHAIN_LOCK_PATH = (
    PROBE_SOURCE / "linux-shader-toolchain.lock.json"
)
LINUX_SHADER_TOOLCHAIN_LOCK_SHA256 = (
    "02d2a965f817786e295212161686c8fc1ff33f0000946b5f90ebd4c161eac35e"
)
REPORT_NAME = "ror-ogre-next-probe-report.json"
BUILD_CONTRACT_NAME = "ogre-next-build-contract.json"
FRAME_REPORT_NAME = "ror-ogre-next-frame-probe-report.json"
FRAME_IMAGE_NAME = "ror-ogre-next-frame-probe.ppm"
N1_REPORT_NAME = "ror-ogre-next-frontend-n1-report.json"
N1_IMAGE_NAME = "ror-ogre-next-frontend-n1.ppm"
RT4_PBR_REPORT_NAME = "ror-ogre-next-frontend-rt4-pbr-v1-report.json"
RT4_PBR_IMAGE_NAME = "ror-ogre-next-frontend-rt4-pbr-v1.ppm"
RT4_PBR_EVIDENCE_NAME = (
    "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin"
)
RT4_PBR_REFLECTION_EVIDENCE_NAME = (
    "ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin"
)
RT4_PBR_COMPOSITOR_EVIDENCE_NAME = (
    "ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin"
)
RT4_PBR_REPEAT_DIRECTORY = "ror-ogre-next-frontend-rt4-pbr-v1-repeat"
RT4_PBR_ATTESTATION_NAME = (
    "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json"
)
RT4_PBR_REPORT_SCHEMA = "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v3"
RT4_PBR_ATTESTATION_SCHEMA = (
    "ror.ogre_next_frontend_rt4_pbr_v1.attestation.v4"
)
RT4_REFLECTION_SCHEMA = "ror.ogre_next_rt4_reflection_probes.v1"
RT4_REFLECTION_RESOLUTION = 32
RT4_REFLECTION_FACE_COUNT = 6
RT4_REFLECTION_RAW_BYTES = (
    RT4_REFLECTION_RESOLUTION
    * RT4_REFLECTION_RESOLUTION
    * RT4_REFLECTION_FACE_COUNT
    * 8
)
RT4_REFLECTION_FILTERED_DIMENSIONS = (32, 16)
RT4_REFLECTION_FILTERED_BYTES = sum(
    dimension * dimension * RT4_REFLECTION_FACE_COUNT * 8
    for dimension in RT4_REFLECTION_FILTERED_DIMENSIONS
)
RT4_REFLECTION_EVIDENCE_BYTES = (
    RT4_REFLECTION_RAW_BYTES + RT4_REFLECTION_FILTERED_BYTES
)
RT4_REFLECTION_BACKENDS = {
    "macos-arm64-metal": "OGRE_NEXT_METAL",
    "windows-x64-d3d11": "OGRE_NEXT_D3D11",
    "linux-x86_64-vulkan": "OGRE_NEXT_VULKAN",
}
N1_PACKAGE_NAME = "ror-ogre-next-n1-package"
N1_PACKAGE_EXECUTABLE_STEM = "ror_ogre_next_frontend_n1_smoke"
N2_REPORT_NAME = "ror-ogre-next-metal-n2-report.json"
N2_PROBE_NAME = "ror-ogre-next-metal-n2-probe.bin"
N2_ATTESTATION_NAME = "ror-ogre-next-metal-n2-attestation.json"
LINUX_STATIC_CLOSURE_MANIFEST_NAME = "ogre-next-linux-static-closure.json"
N3_REPORT_NAME = "ror-ogre-next-metal-n3-report.json"
N3_RASTER_NAME = "ror-ogre-next-metal-n3-raster.bin"
N3_CONTRIBUTION_NAME = "ror-ogre-next-metal-n3-contribution.bin"
N3_HYBRID_NAME = "ror-ogre-next-metal-n3-hybrid.bin"
N3_ATTESTATION_NAME = "ror-ogre-next-metal-n3-attestation.json"
FRAME_VALIDATOR = REPOSITORY_ROOT / "tools" / "validate_ogre_next_frame_probe.py"
BUILD_SENTINEL_NAME = ".ror-ogre-next-probe-build-v1"
BUILD_SENTINEL_CONTENT = "ror-ogre-next-probe-build-v1\n"
REQUIRED_CONFIG = "Release"
ROR_SOURCE_REPOSITORY = "https://github.com/oasiz-ai/rigs-of-rods"
RELEVANT_SOURCE_PATHS = (
    "CMakeLists.txt",
    "cmake/VerifyStbImageSource.cmake",
    "doc/nextgen/OGRE14_MATERIAL_SEMANTIC_CATALOG_V2.md",
    "cmake/OgreNextProductionPackage.cmake",
    "cmake/RendererLauncherPackageConfig.cmake",
    "cmake/macos/StageMacOSBundle.cmake",
    "cmake/conan/locks/ogre3d-14.5.2-linux-x86_64-release.lock",
    "cmake/conan/locks/ogre3d-14.5.2-macos-arm64-release.lock",
    "cmake/conan/locks/ogre3d-14.5.2-windows-x86_64-release.lock",
    "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
    "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
    "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
    "cmake/conan/recipes/mygui/conanfile.py",
    "cmake/conan/recipes/ogre3d/conandata.yml",
    "cmake/conan/recipes/ogre3d/patches/14.5.2/archive-manager-load-rollback.patch",
    "cmake/conan/recipes/ogre3d/patches/14.5.2/terrain-composite-revision-metal-readback.patch",
    "cmake/conan/recipes/ogre3d/patches/14.5.2/exact-material-script-preopen.patch",
    "cmake/conan/recipes/ogre3d/patches/14.5.2/expose-shadow-material-declaration-names.patch",
    "cmake/conan/recipes/ogre3d/README.md",
    "cmake/conan/recipes/ogre3d/test_package/CMakeLists.txt",
    "cmake/conan/recipes/ogre3d/test_package/conanfile.py",
    "cmake/conan/recipes/ogre3d/test_package/src/ogre_material_script_preopen_probe.cpp",
    "cmake/conan/recipes/ogre3d/test_package/src/ogre_recipe_probe.cpp",
    "conanfile.py",
    "doc/nextgen/GRAPHICS_SCENE_SNAPSHOT_PRODUCER.md",
    "doc/nextgen/OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_RECEIPT.md",
    "doc/nextgen/OGRE14_AUTHENTICATED_TEXTURE_RECEIPTS.md",
    "doc/nextgen/OGRE14_EXACT_MATERIAL_SCRIPT_PREOPEN.md",
    "doc/nextgen/OGRE14_TERRAIN_COMPOSITE_CAPTURE_RECEIPTS.md",
    "source/main/Application.cpp",
    "source/main/GameContext.cpp",
    "source/main/GameContext.h",
    "source/main/gfx/GfxActorCaptureInventory.h",
    "source/main/gfx/RendererBackendPolicy.cpp",
    "source/main/gfx/RendererBackendPolicy.h",
    "source/main/gfx/GfxScene.cpp",
    "source/main/gfx/GfxScene.h",
    "source/main/utils/MeshObject.cpp",
    "source/main/utils/MeshObject.h",
    "source/main/gfx/RendererStartupHandoff.cpp",
    "source/main/gfx/RendererStartupHandoff.h",
    "source/main/gfx/RendererStartupPlan.cpp",
    "source/main/gfx/RendererStartupPlan.h",
    "source/main/physics/Actor.cpp",
    "source/main/physics/Actor.h",
    "source/main/physics/ActorManager.cpp",
    "source/main/physics/ActorManager.h",
    "source/main/physics/ActorSpawner.cpp",
    "source/main/physics/ActorSpawner.h",
    "source/main/physics/ActorSpawnerFlow.cpp",
    "source/main/physics/collision/Collisions.cpp",
    "source/main/physics/flex/FlexBody.cpp",
    "source/main/physics/flex/FlexBody.h",
    "source/main/physics/flex/FlexFactory.cpp",
    "source/main/physics/flex/FlexFactory.h",
    "source/main/physics/flex/FlexMesh.cpp",
    "source/main/physics/flex/FlexMesh.h",
    "source/main/physics/flex/FlexMeshTopology.h",
    "source/main/physics/flex/FlexMeshWheel.cpp",
    "source/main/physics/flex/FlexMeshWheel.h",
    "source/main/physics/flex/FlexObj.cpp",
    "source/main/physics/flex/FlexObj.h",
    "source/main/physics/flex/Flexable.h",
    "source/main/terrain/ProceduralManager.cpp",
    "source/main/terrain/ProceduralManager.h",
    "source/main/terrain/ProceduralRoad.cpp",
    "source/main/terrain/ProceduralRoad.h",
    "source/main/terrain/TerrainObjectManager.cpp",
    "source/main/terrain/TerrainObjectManager.h",
    "source/main/terrain/Terrain.cpp",
    "source/main/terrain/Terrain.h",
    "source/main/main.cpp",
    "source/main/AppContext.cpp",
    "source/main/AppContext.h",
    "source/main/system/ApplicationFatalError.h",
    "source/main/system/RendererBridgeChannel.cpp",
    "source/main/system/RendererBridgeChannel.h",
    "source/main/system/RendererBridgeEndpoint.cpp",
    "source/main/system/RendererBridgeEndpoint.h",
    "source/main/system/RendererBridgeLaunchPlan.cpp",
    "source/main/system/RendererBridgeLaunchPlan.h",
    "source/main/system/RendererBridgeProcessSupervisor.cpp",
    "source/main/system/RendererBridgeProcessSupervisor.h",
    "source/main/system/RendererChildIntent.cpp",
    "source/main/system/RendererChildIntent.h",
    "source/main/system/RendererChildLauncher.cpp",
    "source/main/system/RendererChildLauncher.h",
    "source/main/system/RendererOgre14GameBridge.cpp",
    "source/main/system/RendererOgre14GameBridge.h",
    "source/main/system/RendererOgre14GameHostSession.cpp",
    "source/main/system/RendererOgre14GameHostSession.h",
    "source/main/system/RendererGameInputTarget.cpp",
    "source/main/system/RendererGameInputTarget.h",
    "source/main/system/RendererOgre14InputAdapter.cpp",
    "source/main/system/RendererOgre14InputAdapter.h",
    "source/main/system/RendererGameInputEngineTarget.cpp",
    "source/main/system/RendererGameInputEngineTarget.h",
    "source/main/system/RendererOgre14ProductSession.cpp",
    "source/main/system/RendererOgre14ProductSession.h",
    "source/main/system/RendererInProcessSession.cpp",
    "source/main/system/RendererInProcessSession.h",
    "source/main/system/detail/OgreNextDemoInProcessFramePolicy.cpp",
    "source/main/system/detail/OgreNextDemoInProcessFramePolicy.h",
    "source/main/utils/InputEngine.cpp",
    "source/main/utils/InputEngine.h",
    "doc/nextgen/OGRE14_PRODUCT_SESSION.md",
    "source/main/system/RendererSiblingPath.cpp",
    "source/main/system/RendererSiblingPath.h",
    "source/main/system/RendererPackagedMediaPath.cpp",
    "source/main/system/RendererPackagedMediaPath.h",
    "source/main/system/RendererPackageRuntimeProbe.cpp",
    "source/main/system/RendererPackageRuntimeProbe.h",
    "source/main/system/RendererOgreNextChild.cpp",
    "source/main/system/RendererOgreNextChild.h",
    "source/main/system/RendererOgreNextChildMain.cpp",
    "source/main/system/RendererOgreNextLiveSession.cpp",
    "source/main/system/RendererOgreNextLiveSession.h",
    "source/main/system/RendererOgreNextProductionSession.cpp",
    "source/main/system/RendererOgreNextProductionSession.h",
    "source/main/system/RendererOgreNextSdlWindowRuntime.cpp",
    "source/main/system/RendererOgreNextSdlWindowRuntime.h",
    "source/main/system/RendererOgreNextSdlWindowRuntimeCocoa.mm",
    "source/main/system/RendererOgreNextInProcessPresenter.cpp",
    "source/main/system/RendererOgreNextInProcessPresenter.h",
    "source/main/system/RendererOgreNextWindowHost.cpp",
    "source/main/system/RendererOgreNextWindowHost.h",
    "source/main/system/RendererLauncherMain.cpp",
    "source/main/system/RendererLauncherPackageConfig.h.in",
    "source/main/system/RendererPublicLauncher.cpp",
    "source/main/system/RendererPublicLauncher.h",
    "source/main/CMakeLists.txt",
    "source/main/resources/CacheSystem.cpp",
    "source/main/resources/ContentManager.cpp",
    "source/main/resources/ContentManager.h",
    "source/main/resources/LegacyMaterialCompatibilityPlan.cpp",
    "source/main/resources/LegacyMaterialCompatibilityPlan.h",
    "source/main/resources/LegacyMaterialScriptSanitizer.cpp",
    "source/main/resources/LegacyMaterialScriptSanitizer.h",
    "source/main/resources/terrn2_fileformat/TerrainBundleDependency.cpp",
    "source/main/resources/terrn2_fileformat/TerrainBundleDependency.h",
    "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.cpp",
    "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.h",
    "source/main/resources/tobj_fileformat/CityWorldNeoQ20Compatibility.cpp",
    "source/main/resources/tobj_fileformat/CityWorldNeoQ20Compatibility.h",
    "source/main/gfx/render/ogrenext/OgreNextDisplayDomainUnlit.cpp",
    "source/main/gfx/render/ogrenext/OgreNextDisplayDomainUnlit.h",
    "source/main/gfx/render",
    "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h",
    "source/main/gfx/ogre14/Ogre14LegacyNativeMaterialCaptureAuthority.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2.h",
    "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.h",
    "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.h",
    "source/main/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBinding.cpp",
    "source/main/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBinding.h",
    "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.cpp",
    "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h",
    "source/main/gfx/ogre14/Ogre14SelectedTextureSource.cpp",
    "source/main/gfx/ogre14/Ogre14SelectedTextureSource.h",
    "source/main/gfx/ogre14/Ogre14ManagedMaterialSourceAdapter.cpp",
    "source/main/gfx/ogre14/Ogre14ManagedMaterialSourceAdapter.h",
    "source/main/gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.cpp",
    "source/main/gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h",
    "source/main/gfx/ogre14/Ogre14TerrainCompositeNativeAdapter.cpp",
    "doc/nextgen/OGRE_NEXT_DEMO_PRIVATE_BRIDGE.md",
    "doc/nextgen/OGRE_NEXT_COMBINED_RUNTIME.md",
    "doc/nextgen/RENDERER_VISUAL_PARITY_ORACLE.md",
    "tools/compare_renderer_visual_parity.py",
    "tests/tools/test_compare_renderer_visual_parity.py",
    "source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.cpp",
    "source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.h",
    "source/main/gfx/ogre14/detail/OgreNextDemoMaterialSource.cpp",
    "source/main/gfx/ogre14/detail/OgreNextDemoMaterialSource.h",
    "source/main/gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.cpp",
    "source/main/gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.h",
    "source/main/system/detail/OgreNextDemoFrameNormalization.cpp",
    "source/main/system/detail/OgreNextDemoFrameNormalization.h",
    "source/main/physics/Savegame.cpp",
    "tests/tools/test_renderer_suite_packaging_contract.py",
    "source/main/gfx/ogre14/Ogre14AuthenticatedArchiveLocationClosure.h",
    "source/main/gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceipt.cpp",
    "source/main/gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceipt.h",
    "source/main/gfx/ogre14/Ogre14AuthenticatedResourceThreadGate.h",
    "tests/CMakeLists.txt",
    "tests/gfx/GfxActorCaptureInventoryTests.cpp",
    "tests/system/ApplicationFatalShutdownContractTests.cpp",
    "tests/tools/test_ogre14_native_workflow_contract.py",
    "tests/gfx/RendererBackendPolicyTests.cpp",
    "tests/gfx/RendererBridgeEndpointTests.cpp",
    "tests/gfx/RendererBridgeLaunchPlanTests.cpp",
    "tests/gfx/RendererBridgeChannelTests.cpp",
    "tests/gfx/RendererBridgeProcessFakeChild.cpp",
    "tests/gfx/RendererBridgeProcessSupervisorTests.cpp",
    "tests/gfx/RendererChildIntentTests.cpp",
    "tests/gfx/RendererChildLauncherFakeChild.cpp",
    "tests/gfx/RendererChildLauncherTests.cpp",
    "tests/gfx/RendererOgre14GameBridgeTests.cpp",
    "tests/gfx/RendererOgre14GameHostSessionTests.cpp",
    "tests/gfx/RendererInProcessSessionTests.cpp",
    "tests/gfx/RendererSiblingPathTests.cpp",
    "tests/gfx/RendererPackageRuntimeProbeTests.cpp",
    "tests/gfx/RendererOgreNextChildTests.cpp",
    "tests/gfx/RendererOgreNextLiveSessionTests.cpp",
    "tests/gfx/RendererOgreNextInProcessPresenterPolicyTests.cpp",
    "tests/gfx/RendererOgreNextWindowHostTests.cpp",
    "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
    "tests/gfx/RendererPublicLauncherTests.cpp",
    "tests/cmake/VerifyRendererPublicBridgeExit.cmake",
    "tests/gfx/RendererStartupHandoffTests.cpp",
    "tests/gfx/RendererStartupPlanTests.cpp",
    "tests/gfx/render/RenderBridgeControlTransportTests.cpp",
    "tests/gfx/render/RendererFrontendTransportDispatcherTests.cpp",
    "tests/gfx/render/GraphicsSceneSnapshotProducerTests.cpp",
    "tests/gfx/render/Ogre14GraphicsSceneSourceTests.cpp",
    "tests/gfx/render/Ogre14LegacyAssetTranslatorTests.cpp",
    "tests/gfx/render/Ogre14LegacyMaterialClosureTests.cpp",
    "tests/gfx/ogre14/Ogre14LegacyNativeAssetExtractorCompileTests.cpp",
    "tests/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2Tests.cpp",
    "tests/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistryTests.cpp",
    "tests/gfx/render/Ogre14SourceTextureDecoderTests.cpp",
    "tests/fixtures/gfx/ogre14/material-semantic-catalog-v2.synthetic.json",
    "tests/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinatorTests.cpp",
    "tests/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBindingTests.cpp",
    "tests/gfx/ogre14/Ogre14AuthenticatedTextureReceiptTests.cpp",
    "tests/gfx/ogre14/Ogre14SelectedTextureSourceTests.cpp",
    "tests/gfx/ogre14/Ogre14ManagedMaterialSourceAdapterTests.cpp",
    "tests/gfx/ogre14/Ogre14TerrainCompositeCaptureReceiptTests.cpp",
    "tests/gfx/ogre14/Ogre14TerrainCompositeNativeReadbackTests.cpp",
    "tests/gfx/ogre14/OgreNextDemoPrivatePolicyTests.cpp",
    "tests/gfx/ogre14/OgreNextDemoMaterialSourceNativeTests.cpp",
    "tests/gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceiptTests.cpp",
    "tests/gfx/ogre14/Ogre14AuthenticatedArchiveLocationClosureTests.cpp",
    "tests/gfx/ogre14/Ogre14AuthenticatedMaterialScriptNativeIntegrationTests.cpp",
    "tests/resources/LegacyMaterialCompatibilityPlanTests.cpp",
    "tests/resources/CityWorldNeoQ20CompatibilityTests.cpp",
    "tests/resources/LegacyMaterialScriptSanitizerTests.cpp",
    "tests/resources/TerrainBundleDependencyTests.cpp",
    "tests/resources/TerrainBundleArchiveVerifierTests.cpp",
    "tests/tools/assert_ogre_recipe_graph.py",
    "tests/tools/test_ogre14_exact_material_script_preopen_recipe_contract.py",
    "tests/gfx/render/Ogre14ParticleCaptureSourceTests.cpp",
    "tests/gfx/render/OgreNextN1ParticleRuntimeTests.cpp",
    "source/main/gfx/render/ogrenext/OgreNextN1ParticleRuntime.cpp",
    "source/main/gfx/render/ogrenext/OgreNextN1ParticleRuntime.h",
    "tests/gfx/render/Ogre14ProceduralRoadSourceTests.cpp",
    "tests/gfx/render/Ogre14RoadMaterialTransactionTests.cpp",
    "tests/physics/FlexMeshTopologyTests.cpp",
    "tests/physics/Ogre14FlexShadowLoadTests.cpp",
    "tests/physics/Ogre14MetalFlexShadowReadContractTests.cpp",
    "tests/gfx/render/Ogre14DynamicMaterialClosureTests.cpp",
    "tests/tools/test_ogre_next_child_runtime_contract.py",
    "tests/tools/test_ogre_next_embedded_namespace_contract.py",
    "tests/tools/test_ogre14_legacy_asset_translator_contract.py",
    "tests/tools/test_ogre14_legacy_material_closure_contract.py",
    "tests/tools/test_ogre14_particle_capture_contract.py",
    "tests/tools/test_ogre14_road_material_transaction_contract.py",
    "tests/tools/test_ogre_next_metal_n2_contract.py",
    "tests/tools/test_ogre14_dynamic_material_closure_contract.py",
    "tests/tools/test_ogre14_material_semantic_registry_contract.py",
    "tests/tools/test_ogre14_source_texture_decoder_contract.py",
    "tests/tools/test_ogre14_material_semantic_catalog_v2.py",
    "tests/tools/test_ogre14_live_material_coordinator_contract.py",
    "tests/tools/test_ogre14_graphics_scene_prepared_material_binding_contract.py",
    "tests/tools/test_ogre14_authenticated_texture_receipt_contract.py",
    "tests/tools/test_ogre14_authenticated_material_script_receipt_contract.py",
    "tests/tools/test_ogre14_authenticated_texture_capture_bridge_contract.py",
    "tests/tools/test_ogre14_terrain_composite_recipe_contract.py",
    "tests/tools/test_ogre14_terrain_composite_capture_receipt_contract.py",
    "tests/tools/test_ogre_next_product_packaging_contract.py",
    "tests/tools/test_ogre_next_probe_contract.py",
    "tests/tools/test_ogre_next_probe_workflow.py",
    "tests/tools/test_ogre_next_window_host_contract.py",
    "tests/tools/test_ogre_next_window_presentation_contract.py",
    "tests/tools/test_ogre_next_window_run_loop_contract.py",
    "tools/ogre_next_probe/media/Hlms/RoR/DisplayDomain/DisplayDomain_piece_ps.any",
    "tools/ogre_next_probe/audit_embedded_namespace.py",
    "tools/ogre_next_probe/embedded_namespace/RoROgreNextNamespaceRemap.h",
    "tools/ogre_next_probe/patches/0006-embedded-namespace-plugin-symbols.patch",
    "tools/ogre_next_probe/src/embedded_namespace/main.cpp",
    "tools/ogre_next_probe/src/embedded_namespace/metal_plugin_export_probe.mm",
    "tools/ogre_next_probe/src/embedded_namespace/n1_session_adapter.cpp",
    "tools/ogre_next_probe/src/embedded_namespace/presenter_link_adapter.cpp",
    "tools/ogre_next_probe/src/embedded_namespace/next_adapter.cpp",
    "tools/ogre_next_probe/src/embedded_namespace/ogre14_adapter.cpp",
    "tools/ogre_next_probe",
    "tools/ogre14_runtime_audit.py",
    "tools/compile_ogre14_material_semantic_catalog_v2.py",
    "tools/run_ogre_next_probe.py",
    "tools/validate_ogre_next_frame_probe.py",
    "tools/verify_ogre_next_artifact_set.py",
    "tools/schemas/ogre14-material-semantic-catalog-v2.schema.json",
)


class ProbeError(RuntimeError):
    """Raised when a capability or provenance contract fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def expected_build_shader_media(lock: dict[str, Any]) -> dict[str, Any]:
    source = DISPLAY_DOMAIN_MEDIA_PATH
    notice = DISPLAY_DOMAIN_NOTICE_SOURCE
    if source.is_symlink() or not source.is_file():
        raise ProbeError("legacy display-domain shader source is missing or indirect")
    if notice.is_symlink() or not notice.is_file():
        raise ProbeError("legacy display-domain GPL notice is missing or indirect")
    expected = dict(lock["shader_media"])
    expected["display_domain_unlit"] = {
        "base_color_transfer": "SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE",
        "relative_path": DISPLAY_DOMAIN_MEDIA_RELATIVE,
        "size": source.stat().st_size,
        "sha256": sha256_file(source),
        "license_expression": DISPLAY_DOMAIN_LICENSE_EXPRESSION,
        "notice_path": DISPLAY_DOMAIN_NOTICE_PATH,
        "notice_sha256": sha256_file(notice),
    }
    return expected


def relevant_source_manifest(
    repository_root: Path = REPOSITORY_ROOT,
    paths: tuple[str, ...] = RELEVANT_SOURCE_PATHS,
) -> dict[str, Any]:
    roots = tuple(repository_root / relative for relative in paths)
    selected_paths: set[Path] = set()
    for root in roots:
        if root.is_symlink():
            raise ProbeError(
                "RoR relevant source contains a symbolic link: "
                + root.relative_to(repository_root).as_posix()
            )
        if root.is_dir():
            selected_paths.update(root.rglob("*"))
        else:
            selected_paths.add(root)
    entries: list[tuple[str, int, str]] = []
    for path in sorted(selected_paths, key=lambda item: item.as_posix()):
        relative = path.relative_to(repository_root)
        if "__pycache__" in relative.parts or path.suffix in (".pyc", ".pyo"):
            continue
        if path.name == ".DS_Store":
            continue
        if path.is_symlink():
            raise ProbeError(
                "RoR relevant source contains a symbolic link: "
                + relative.as_posix()
            )
        if path.is_dir():
            continue
        if not path.is_file():
            raise ProbeError(
                "RoR relevant source is missing or irregular: "
                + relative.as_posix()
            )
        entries.append(
            (relative.as_posix(), path.stat().st_size, sha256_file(path))
        )
    if not entries:
        raise ProbeError("RoR relevant source manifest is empty")
    serialized = "".join(
        f"{relative}|{size}|{digest}\n"
        for relative, size, digest in entries
    ).encode("utf-8")
    return {
        "sha256": hashlib.sha256(serialized).hexdigest(),
        "file_count": len(entries),
    }


def ror_source_identity() -> dict[str, Any]:
    def git_output(*arguments: str) -> str:
        try:
            result = subprocess.run(
                ["git", "-C", str(REPOSITORY_ROOT), *arguments],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except OSError as error:
            raise ProbeError(f"could not execute Git for RoR provenance: {error}") from error
        value = result.stdout.strip()
        if result.returncode != 0 or not value:
            raise ProbeError("could not resolve RoR Git provenance")
        return value

    repository = (
        os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_REPOSITORY")
        or ROR_SOURCE_REPOSITORY
    )
    git_commit = git_output("rev-parse", "HEAD")
    commit = (
        os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT")
        or os.environ.get("GITHUB_SHA")
        or git_commit
    )
    ref = os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_REF") or git_output(
        "rev-parse", "--abbrev-ref", "HEAD"
    )
    if repository != ROR_SOURCE_REPOSITORY or re.fullmatch(
        r"[0-9a-f]{40}", commit
    ) is None or re.fullmatch(
        r"[A-Za-z0-9._/-]+", ref
    ) is None:
        raise ProbeError("RoR Git provenance is not canonical")
    if commit != git_commit:
        raise ProbeError("expected RoR commit differs from checked-out source")
    manifest = relevant_source_manifest()
    return {
        "repository": repository,
        "ref": ref,
        "commit": commit,
        "relevant_manifest_sha256": manifest["sha256"],
        "relevant_manifest_file_count": manifest["file_count"],
    }


def require_source_identity_unchanged(
    expected: dict[str, Any],
) -> None:
    if ror_source_identity() != expected:
        raise ProbeError(
            "RoR relevant source or Git identity changed during the probe"
        )


def require_relevant_source_clean(
    repository_root: Path = REPOSITORY_ROOT,
    paths: tuple[str, ...] = RELEVANT_SOURCE_PATHS,
) -> None:
    try:
        status_result = subprocess.run(
            [
                "git",
                "status",
                "--porcelain=v1",
                "--untracked-files=all",
                "--",
                *paths,
            ],
            cwd=repository_root,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise ProbeError(
            f"could not inspect Metal N2 relevant source state: {error}"
        ) from error
    if status_result.returncode != 0:
        detail = status_result.stderr.decode("utf-8", errors="replace").strip()
        raise ProbeError(
            "could not inspect Metal N2 relevant source state"
            + (f": {detail}" if detail else "")
        )
    dirty = status_result.stdout.decode("utf-8", errors="replace").strip()
    if dirty:
        raise ProbeError(
            "Metal N2 provenance requires a clean relevant source set:\n"
            + dirty
        )


def repository_identity(
    repository_root: Path = REPOSITORY_ROOT,
) -> tuple[str, str, str]:
    require_relevant_source_clean(repository_root)
    try:
        commit_result = subprocess.run(
            ["git", "rev-parse", "--verify", "HEAD"],
            cwd=repository_root,
            check=True,
            capture_output=True,
            text=True,
        )
        commit = commit_result.stdout.strip()
        ref_result = subprocess.run(
            ["git", "symbolic-ref", "--short", "HEAD"],
            cwd=repository_root,
            check=False,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise ProbeError(f"could not resolve RoR source provenance: {error}") from error
    if re.fullmatch(r"[0-9a-f]{40}", commit) is None:
        raise ProbeError("checked-out RoR commit is not a full lowercase Git SHA")
    ref = ref_result.stdout.strip() if ref_result.returncode == 0 else "detached"
    if not ref:
        raise ProbeError("checked-out RoR ref is empty")
    manifest = relevant_source_manifest(repository_root)
    return commit, ref, manifest["sha256"]


def _require_sha256(value: object, label: str) -> None:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ProbeError(f"{label} is not a lowercase SHA-256")


def _strict_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProbeError(f"normal-map source lock has duplicate key: {key}")
        result[key] = value
    return result


def load_normal_map_source_lock(
    path: Path = NORMAL_MAP_SOURCE_LOCK_PATH,
    source_root: Path | None = None,
) -> dict[str, Any]:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        raise ProbeError(
            f"could not read normal-map source lock: {error}"
        ) from error
    if sha256_file(path) != NORMAL_MAP_SOURCE_LOCK_SHA256:
        raise ProbeError("the reviewed normal-map source lock changed")
    try:
        lock = json.loads(source, object_pairs_hook=_strict_json_object)
    except json.JSONDecodeError as error:
        raise ProbeError(
            f"could not parse normal-map source lock: {error}"
        ) from error
    if type(lock) is not dict:
        raise ProbeError("normal-map source lock root must be an object")
    if source != json.dumps(lock, indent=2) + "\n":
        raise ProbeError("normal-map source lock is not canonical JSON")
    if set(lock) != {"schema", "ogre_next_commit", "contract", "sources"}:
        raise ProbeError("normal-map source lock root schema drifted")
    if lock.get("schema") != "ror.ogre_next_rt4_normal_map_source_lock.v1":
        raise ProbeError("normal-map source lock schema changed")
    if lock.get("ogre_next_commit") != (
        "37149a802de747f6806996fa3067b0748ecc1084"
    ):
        raise ProbeError("normal-map source lock moved to another Ogre commit")

    contract = lock.get("contract")
    if type(contract) is not dict or set(contract) != {
        "authored_texture",
        "native_texture",
        "pbs_slot",
        "uv_source",
        "normal_scale",
        "normal_map_weight",
        "decoded_b_tolerance",
        "positive_z_only",
        "occlusion_admitted",
    }:
        raise ProbeError("normal-map source lock contract schema drifted")
    tolerance = contract.get("decoded_b_tolerance")
    if (
        type(tolerance) is not dict
        or set(tolerance) != {"numerator", "denominator"}
        or type(tolerance.get("numerator")) is not int
        or type(tolerance.get("denominator")) is not int
        or tolerance != {"numerator": 1, "denominator": 255}
    ):
        raise ProbeError("normal-map quantization tolerance contract changed")
    if (
        type(contract.get("uv_source")) is not int
        or type(contract.get("normal_scale")) is not int
        or type(contract.get("normal_map_weight")) is not int
        or type(contract.get("positive_z_only")) is not bool
        or type(contract.get("occlusion_admitted")) is not bool
        or contract
        != {
            "authored_texture": "linear_RGBA8_UNORM",
            "native_texture": "RG8_UNORM",
            "pbs_slot": "PBSM_NORMAL",
            "uv_source": 0,
            "normal_scale": 1,
            "normal_map_weight": 1,
            "decoded_b_tolerance": {"numerator": 1, "denominator": 255},
            "positive_z_only": True,
            "occlusion_admitted": False,
        }
    ):
        raise ProbeError("normal-map source lock semantic contract changed")

    expected_owners = (
        ("normal_decode_shader", "Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any"),
        ("normal_vertex_tbn_shader", "Samples/Media/Hlms/Pbs/Any/Main/800.VertexShader_piece_vs.any"),
        ("normal_weight_shader_uniform", "Samples/Media/Hlms/Pbs/Any/Main/500.Structs_piece_vs_piece_ps.any"),
        ("normal_uv_modifier", "Samples/Media/Hlms/Pbs/Any/UvModifierMacros_piece_ps.any"),
        ("metal_vertex_input", "Samples/Media/Hlms/Pbs/Metal/VertexShader_vs.metal"),
        ("glsl_vertex_input", "Samples/Media/Hlms/Pbs/GLSL/VertexShader_vs.glsl"),
        ("hlsl_vertex_input", "Samples/Media/Hlms/Pbs/HLSL/VertexShader_vs.hlsl"),
        ("metal_sampling_precision", "Samples/Media/Hlms/Common/Metal/CrossPlatformSettings_piece_all.metal"),
        ("glsl_sampling_precision", "Samples/Media/Hlms/Common/GLSL/CrossPlatformSettings_piece_all.glsl"),
        ("hlsl_sampling_precision", "Samples/Media/Hlms/Common/HLSL/CrossPlatformSettings_piece_all.hlsl"),
        ("hlms_precision_default", "OgreMain/src/OgreHlms.cpp"),
        ("pbs_texture_slot", "Components/Hlms/Pbs/include/OgreHlmsPbsPrerequisites.h"),
        ("datablock_api", "Components/Hlms/Pbs/include/OgreHlmsPbsDatablock.h"),
        ("datablock_implementation", "Components/Hlms/Pbs/src/OgreHlmsPbsDatablock.cpp"),
        ("normal_format_selection", "Components/Hlms/Pbs/src/OgreHlmsPbs.cpp"),
        ("pixel_format_enum", "OgreMain/include/OgrePixelFormatGpu.h"),
        ("pixel_format_metadata", "OgreMain/src/OgrePixelFormatGpuUtils.cpp"),
        ("texture_box_row_layout", "OgreMain/include/OgreTextureBox.h"),
        ("image_api", "OgreMain/include/OgreImage2.h"),
        ("image_row_layout_implementation", "OgreMain/src/OgreImage2.cpp"),
        ("d3d11_rg8_mapping", "RenderSystems/Direct3D11/src/OgreD3D11Mappings.cpp"),
        ("metal_rg8_mapping", "RenderSystems/Metal/src/OgreMetalMappings.mm"),
        ("vulkan_rg8_mapping", "RenderSystems/Vulkan/src/OgreVulkanMappings.cpp"),
    )
    sources = lock.get("sources")
    if type(sources) is not list or len(sources) != len(expected_owners):
        raise ProbeError("normal-map source owner list changed")
    roles: set[str] = set()
    paths: set[str] = set()
    resolved_root: Path | None = None
    if source_root is not None:
        try:
            resolved_root = source_root.resolve(strict=True)
        except OSError as error:
            raise ProbeError(
                f"could not resolve pinned Ogre source root: {error}"
            ) from error
        if not resolved_root.is_dir():
            raise ProbeError("pinned Ogre source root is not a directory")
    for index, (entry, expected_owner) in enumerate(zip(sources, expected_owners)):
        if type(entry) is not dict or set(entry) != {"role", "path", "sha256"}:
            raise ProbeError(f"normal-map source owner {index} schema drifted")
        role = entry.get("role")
        relative = entry.get("path")
        digest = entry.get("sha256")
        if type(role) is not str or type(relative) is not str:
            raise ProbeError(f"normal-map source owner {index} has invalid types")
        posix_path = PurePosixPath(relative)
        if (
            not relative
            or posix_path.is_absolute()
            or posix_path.as_posix() != relative
            or any(part in {"", ".", ".."} for part in posix_path.parts)
            or "\\" in relative
        ):
            raise ProbeError(f"normal-map source owner {index} path is unsafe")
        _require_sha256(digest, f"normal-map source owner {role} hash")
        if role in roles or relative in paths:
            raise ProbeError("normal-map source owner role or path is duplicated")
        roles.add(role)
        paths.add(relative)
        if (role, relative) != expected_owner:
            raise ProbeError(f"normal-map source owner {index} identity drifted")
        if resolved_root is not None:
            candidate = resolved_root / relative
            try:
                resolved = candidate.resolve(strict=True)
                resolved.relative_to(resolved_root)
            except (OSError, ValueError) as error:
                raise ProbeError(
                    f"normal-map source owner escaped or is missing: {relative}"
                ) from error
            if candidate.is_symlink() or not resolved.is_file():
                raise ProbeError(
                    f"normal-map source owner is not a direct file: {relative}"
                )
            if sha256_file(resolved) != digest:
                raise ProbeError(
                    f"normal-map source owner hash mismatch: {relative}"
                )
    return lock


def load_lock(path: Path = LOCK_PATH) -> dict[str, Any]:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read OGRE-Next lock: {error}") from error

    expected_commit = "37149a802de747f6806996fa3067b0748ecc1084"
    expected_archive_sha256 = (
        "1c0be064474da512606d02543be2630b36cdf99f359a9f23edc97eeb410e25b2"
    )
    expected_ogre_license_sha256 = (
        "df6294031f26c4401ce713be0b0b3c5da27c2f1b7278a0d9833d111273174183"
    )
    expected_rapidjson_archive_sha256 = (
        "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e"
    )
    expected_rapidjson_license_sha256 = (
        "a140e5d46fe734a1c78f1a3c3ef207871dd75648be71fdda8e309b23ab8b1f32"
    )
    expected_freetype_archive_sha256 = (
        "36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f"
    )
    expected_freetype_license_sha256 = (
        "c4120c6752c910c299e3bd9cb3a46ff262c268303ca2069b61f92f10a5656c18"
    )
    expected_freetype_overview_sha256 = (
        "bd36c8b474855fa294c2ec5c184544478ef3720aad37d65a6296a4f264fd2d3b"
    )
    expected_patch_sha256 = (
        "84916d0d1abf61a15d19d2c89a7d9b1a445f1a37a5067a9f8b558395fe10ead1"
    )
    expected_ibl_patch_sha256 = (
        "2a4792a553a3911db197750ae6e4de2155f7b9604e9bc6d730cc19bba0b1075f"
    )
    expected_metal_anisotropy_patch_sha256 = (
        "f7c5356f5f2025bbc7daf5e0788b7820244ed1ad8c3d45dd5ac73f381d800a22"
    )
    if type(lock.get("schema_version")) is not int or lock.get("schema_version") != 6:
        raise ProbeError("unsupported OGRE-Next lock schema")
    if lock.get("repository") != "https://github.com/OGRECave/ogre-next":
        raise ProbeError("OGRE-Next repository contract changed")
    if lock.get("branch") != "v3-0":
        raise ProbeError("OGRE-Next branch contract changed")
    if lock.get("commit") != expected_commit:
        raise ProbeError("OGRE-Next lock moved without an integration review")
    if lock.get("archive_url") != (
        f"https://github.com/OGRECave/ogre-next/archive/{expected_commit}.tar.gz"
    ):
        raise ProbeError("OGRE-Next archive URL contract changed")
    if lock.get("archive_sha256") != expected_archive_sha256:
        raise ProbeError("OGRE-Next archive hash moved without review")
    if lock.get("license", {}).get("spdx") != "MIT":
        raise ProbeError("OGRE-Next license contract changed")
    _require_sha256(lock.get("archive_sha256"), "OGRE-Next archive hash")
    _require_sha256(
        lock.get("license", {}).get("sha256"), "OGRE-Next license hash"
    )
    if lock["license"]["sha256"] != expected_ogre_license_sha256:
        raise ProbeError("OGRE-Next license hash moved without review")

    expected_shader_media = {
        "root": "Samples/Media/Hlms",
        "license_expression": (
            "MIT AND LicenseRef-Heitz-LTC-Paper-Notice"
        ),
        "third_party_notice": {
            "license_ref": "LicenseRef-Heitz-LTC-Paper-Notice",
            "source_path": (
                "Samples/Media/Hlms/Pbs/Any/AreaLights_LTC_piece_ps.any"
            ),
            "source_sha256": (
                "44146bd7eee4bd6a3bb9428352e89dc20d7690b32c609e62c5f9330678f3a124"
            ),
            "notice_path": (
                "licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt"
            ),
            "notice_sha256": (
                "cc942875917be271c92fdc1fdec7a17da92b45dadf42a979b69583003f38bba6"
            ),
            "upstream_source": "https://github.com/selfshadow/ltc_code/",
            "paper_reference": (
                "Real-Time Polygonal-Light Shading with Linearly "
                "Transformed Cosines, ACM TOG 35(4), 2016"
            ),
            "source_and_binary_notice_required": True,
            "paper_reference_required": True,
        },
    }
    if lock.get("shader_media") != expected_shader_media:
        raise ProbeError(
            "OGRE-Next shader-media license contract changed without review"
        )
    shader_notice = expected_shader_media["third_party_notice"]
    _require_sha256(
        shader_notice["source_sha256"], "shader-media source hash"
    )
    _require_sha256(
        shader_notice["notice_sha256"], "shader-media notice hash"
    )
    notice_path = path.parent / shader_notice["notice_path"]
    if not notice_path.is_file():
        raise ProbeError(f"shader-media notice is missing: {notice_path}")
    if sha256_file(notice_path) != shader_notice["notice_sha256"]:
        raise ProbeError("shader-media notice SHA-256 mismatch")

    expected_reflection_shader_media = {
        "root": "Samples/Media/Compute/Algorithms/IBL",
        "license_expression": "LicenseRef-IBLBaker",
        "third_party_notice": {
            "license_ref": "LicenseRef-IBLBaker",
            "source_path": "Docs/licenses/IBLBaker.txt",
            "source_sha256": (
                "c66291524d9d111ed44349d4217dda31bdb33c6203a14b2d7682d805c9166a8e"
            ),
            "package_path": "licenses/IBLBaker.txt",
            "source_and_binary_notice_required": True,
        },
    }
    if lock.get("reflection_shader_media") != expected_reflection_shader_media:
        raise ProbeError(
            "OGRE-Next reflection-media license contract changed without review"
        )
    _require_sha256(
        expected_reflection_shader_media["third_party_notice"]["source_sha256"],
        "reflection-media notice hash",
    )

    rapidjson = lock.get("dependencies", {}).get("rapidjson", {})
    if (
        rapidjson.get("repository") != "https://github.com/Tencent/rapidjson"
        or rapidjson.get("tag") != "v1.1.0"
        or rapidjson.get("archive_url")
        != "https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz"
        or rapidjson.get("archive_sha256")
        != expected_rapidjson_archive_sha256
        or rapidjson.get("license_spdx")
        != "MIT AND BSD-3-Clause AND JSON"
        or rapidjson.get("compiled_headers_spdx") != "MIT"
        or rapidjson.get("non_mit_paths")
        != ["bin/jsonchecker", "include/rapidjson/msinttypes"]
        or rapidjson.get("license_sha256")
        != expected_rapidjson_license_sha256
    ):
        raise ProbeError("RapidJSON dependency contract changed")
    _require_sha256(rapidjson.get("archive_sha256"), "RapidJSON archive hash")
    _require_sha256(rapidjson.get("license_sha256"), "RapidJSON license hash")

    freetype = lock.get("dependencies", {}).get("freetype", {})
    expected_freetype = {
        "repository": "https://gitlab.freedesktop.org/freetype/freetype",
        "version": "2.14.3",
        "archive_url": (
            "https://download.savannah.gnu.org/releases/freetype/"
            "freetype-2.14.3.tar.xz"
        ),
        "archive_fallback_url": (
            "https://downloads.sourceforge.net/project/freetype/freetype2/"
            "2.14.3/freetype-2.14.3.tar.xz"
        ),
        "archive_sha256": expected_freetype_archive_sha256,
        "license_expression": "FTL OR GPL-2.0-or-later",
        "selected_license_spdx": "GPL-2.0-or-later",
        "license_path": "docs/GPLv2.TXT",
        "license_sha256": expected_freetype_license_sha256,
        "package_license_path": "licenses/FreeType-GPLv2.txt",
        "overview_path": "LICENSE.TXT",
        "overview_sha256": expected_freetype_overview_sha256,
        "package_overview_path": "licenses/FreeType-LICENSE.txt",
        "static_link": True,
        "disabled_optional_dependencies": [
            "BZip2",
            "Brotli",
            "HarfBuzz",
            "PNG",
            "ZLIB",
        ],
    }
    if freetype != expected_freetype:
        raise ProbeError("FreeType dependency contract changed")
    for field in ("archive_sha256", "license_sha256", "overview_sha256"):
        _require_sha256(freetype.get(field), f"FreeType {field}")

    expected_patches = [
        {
            "path": "patches/0001-macos-non-xcode-framework-path.patch",
            "sha256": expected_patch_sha256,
            "reason": (
                "Resolve the macOS SDK and framework staging path correctly "
                "for non-Xcode generators"
            ),
        },
        {
            "path": "patches/0005-metal-typed-ibl-uav-conversions.patch",
            "sha256": expected_ibl_patch_sha256,
            "reason": (
                "Use explicit Metal typed-UAV vector conversions for half and "
                "float IBL targets without changing GLSL or HLSL"
            ),
            "source_path": (
                "Samples/Media/Compute/Algorithms/IBL/"
                "SpecularIblIntegrator_piece_cs.any"
            ),
            "source_sha256": (
                "68884256ab318116833bf2efe19518833459cc461fb8dd4f8e2c253f8c352165"
            ),
            "patched_sha256": (
                "3ebebc1132c720ee8b741226d41e8638f747a0d5700222d7cb4c8f4e0663fa41"
            ),
        },
        {
            "path": "patches/0008-metal-report-anisotropy-limit.patch",
            "sha256": expected_metal_anisotropy_patch_sha256,
            "reason": (
                "Report Metal's sampler anisotropy limit so exact authored "
                "anisotropic samplers are admitted instead of rejected as "
                "unsupported"
            ),
            "source_path": "RenderSystems/Metal/src/OgreMetalRenderSystem.mm",
            "source_sha256": (
                "bebe97dd2cb318d6aa2331eaaf0f8b181e18ac66660b02d4160802e0ed8ed0eb"
            ),
            "patched_sha256": (
                "56bb59e7e8d7be5b9efe10e724e5385583618a12e2bb49482e0472d273dc1222"
            ),
        },
    ]
    patches = lock.get("patches")
    if patches != expected_patches:
        raise ProbeError("the reviewed OGRE-Next adaptation patch set changed")
    for patch in patches:
        _require_sha256(patch.get("sha256"), "adaptation patch hash")
        if "source_sha256" in patch:
            _require_sha256(patch["source_sha256"], "adaptation source hash")
            _require_sha256(patch["patched_sha256"], "adapted source hash")
        patch_path = path.parent / patch["path"]
        if not patch_path.is_file():
            raise ProbeError(f"pinned patch is missing: {patch_path}")
        actual_hash = sha256_file(patch_path)
        if actual_hash != patch.get("sha256"):
            raise ProbeError(
                f"pinned patch SHA-256 mismatch for {patch_path.name}: "
                f"expected {patch.get('sha256')}, got {actual_hash}"
            )
    expected_embedded_namespace = {
        "namespace": "RoROgreNext",
        "cmake_option": "ROR_OGRE_NEXT_EMBEDDED_NAMESPACE",
        "default_enabled": False,
        "patch": {
            "path": "patches/0006-embedded-namespace-plugin-symbols.patch",
            "sha256": (
                "0df3dfdd1d97848eddf04d5fe64fcd2e70f65cb9059a5d8f1dd78ff63c5d8fec"
            ),
            "reason": (
                "Prefix OgreNext plugin entry points and dynamic lookup names "
                "for private in-process ownership"
            ),
        },
        "remap_header": {
            "path": "embedded_namespace/RoROgreNextNamespaceRemap.h",
            "sha256": (
                "fa3abee1afe5d48f0117f7c2c3c218012c6ebde8fc84df55f0b48e261f0d7984"
            ),
        },
    }
    embedded_namespace = lock.get("embedded_namespace")
    if embedded_namespace != expected_embedded_namespace:
        raise ProbeError("the reviewed embedded namespace contract changed")
    for embedded_input in (
        embedded_namespace["patch"],
        embedded_namespace["remap_header"],
    ):
        embedded_path = path.parent / embedded_input["path"]
        if embedded_path.is_symlink() or not embedded_path.is_file():
            raise ProbeError(
                f"embedded namespace input is missing or indirect: {embedded_path}"
            )
        actual_hash = sha256_file(embedded_path)
        if actual_hash != embedded_input["sha256"]:
            raise ProbeError(
                f"embedded namespace SHA-256 mismatch for {embedded_path.name}: "
                f"expected {embedded_input['sha256']}, got {actual_hash}"
            )
    expected_abi = {
        "cxx_standard": 17,
        "static_link": True,
        "new_project_name": True,
        "debug_level_debug": 3,
        "debug_level_release": 0,
        "embed_debug_mode": "auto",
        "assert_mode": 0,
        "double_precision": False,
        "allocator": 0,
        "container_custom_allocator": False,
        "string_custom_allocator": False,
        "memory_tracker_debug": False,
        "memory_tracker_release": False,
        "thread_support": 0,
        "thread_provider": "none",
        "id_string_128": False,
        "id_string_always_readable": False,
        "node_inherit_transform": False,
        "restrict_aliasing": True,
        "flexibility_level": 0,
        "planar_reflections": False,
        "simd": {
            "enabled": True,
            "alignment": 16,
            "macos-arm64-metal": "neon",
            "windows-x64-d3d11": "sse2",
            "linux-x86_64-vulkan": "sse2",
        },
    }
    if lock.get("abi_contract") != expected_abi:
        raise ProbeError("OGRE-Next ABI contract changed without review")
    return lock


def load_linux_shader_toolchain_lock(
    path: Path = LINUX_SHADER_TOOLCHAIN_LOCK_PATH,
) -> dict[str, Any]:
    try:
        source = path.read_text(encoding="utf-8")
        lock = json.loads(source)
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(
            f"could not read Linux shader toolchain lock: {error}"
        ) from error
    if source != json.dumps(lock, indent=2) + "\n":
        raise ProbeError("Linux shader toolchain lock is not canonical JSON")
    if sha256_file(path) != LINUX_SHADER_TOOLCHAIN_LOCK_SHA256:
        raise ProbeError("Linux shader toolchain lock moved without review")
    if (
        lock.get("schema") != "ror.ogre_next_linux_shader_toolchain.v1"
        or lock.get("platform_policy") != "linux-x86_64-vulkan"
        or lock.get("provider") != "pinned-source"
    ):
        raise ProbeError("Linux shader source policy changed")

    expected_sources = {
        "shaderc": (
            lock.get("shaderc_release", {}),
            "https://github.com/google/shaderc",
            "v2025.3",
            "8c2e602ce440b7739c95ff3d69cecb1adf6becda",
            "1a17c01614debaacd5c3674a540368119e93bd299991e0f1c3554875c92ef5e2",
            "c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4",
            "licenses/Apache-2.0.txt",
        ),
        "glslang": (
            lock.get("dependencies", {}).get("glslang", {}),
            "https://github.com/KhronosGroup/glslang",
            "15.3.0+efd24d75",
            "efd24d75bcbc55620e759f6bf42c45a32abac5f8",
            "9427deccbdf4bde6a269938df38c6bd75247493786a310d8d733a2c82065ef47",
            "adb783e734e906d1f46db5df29991dbde84bdb0ceab502ac2febb44fe3c2b5f4",
            "licenses/glslang-LICENSE.txt",
        ),
        "spirv-tools": (
            lock.get("dependencies", {}).get("spirv_tools", {}),
            "https://github.com/KhronosGroup/SPIRV-Tools",
            "v2025.3",
            "33e02568181e3312f49a3cf33df470bf96ef293a",
            "44d1005880c583fc00a0fb41c839214c68214b000ea8dcb54d352732fee600ff",
            "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30",
            "licenses/SPIRV-Tools-LICENSE.txt",
        ),
        "spirv-headers": (
            lock.get("dependencies", {}).get("spirv_headers", {}),
            "https://github.com/KhronosGroup/SPIRV-Headers",
            "1.5.5+2a611a97",
            "2a611a970fdbc41ac2e3e328802aed9985352dca",
            "c2225a49c3d7efa5c4f4ce4a6b42081e6ea3daca376f3353d9d7c2722d77a28a",
            "ea43b1de38a6f90c488800d66dec1ed671e68cda530266bc96951fb5b6307613",
            "licenses/SPIRV-Headers-LICENSE.txt",
        ),
    }
    for component, expected in expected_sources.items():
        record, repository, version, commit, archive_hash, license_hash, notice = (
            expected
        )
        observed_version = record.get(
            "tag" if component == "shaderc" else "version"
        )
        checks = (
            record.get("repository") == repository,
            observed_version == version,
            record.get("commit") == commit,
            record.get("archive_url")
            == f"{repository}/archive/{commit}.tar.gz",
            record.get("archive_sha256") == archive_hash,
            record.get("license_sha256") == license_hash,
            record.get("package_notice_path") == notice,
            record.get("package_notice_sha256") == license_hash,
        )
        if not all(checks):
            raise ProbeError(f"Linux shader source pin changed for {component}")
        _require_sha256(record.get("archive_sha256"), f"{component} archive")
        _require_sha256(record.get("license_sha256"), f"{component} license")

    shaderc = lock["shaderc_release"]
    if (
        shaderc.get("dependency_manifest_path") != "DEPS"
        or shaderc.get("dependency_manifest_sha256")
        != "586935f05d12137e2aa587bd96a96b66e75320b1e311907e20567ab352b19a48"
    ):
        raise ProbeError("shaderc dependency manifest contract changed")
    shaderc_patch = shaderc.get("compatibility_patch", {})
    if (
        shaderc_patch.get("path")
        != "patches/0003-shaderc-disable-glslang-install.patch"
        or shaderc_patch.get("sha256")
        != "9742f2a9fcb5aef762298d823acb85352e536402ba7c4587173cc52130012b0b"
    ):
        raise ProbeError("shaderc CMake compatibility patch contract changed")
    shaderc_patch_path = path.parent / shaderc_patch["path"]
    if (
        not shaderc_patch_path.is_file()
        or sha256_file(shaderc_patch_path) != shaderc_patch["sha256"]
    ):
        raise ProbeError("shaderc CMake compatibility patch SHA-256 mismatch")

    expected_targets = [
        "shaderc_combined",
        "shaderc",
        "shaderc_util",
        "glslang",
        "SPIRV",
        "SPIRV-Tools-opt",
        "SPIRV-Tools-static",
    ]
    targets = lock.get("static_closure_targets")
    if not isinstance(targets, list) or [
        record.get("target") for record in targets
    ] != expected_targets:
        raise ProbeError("Linux shader static closure inventory changed")

    patch = lock.get("ogre_compatibility_patch", {})
    if (
        patch.get("path") != "patches/0002-vulkan-use-glslang-spv-options.patch"
        or patch.get("sha256")
        != "4242ad130cff4e70245d151d0b1a0a63959d3d9b25d11a5587a74f48b15b7897"
    ):
        raise ProbeError("OGRE/glslang compatibility patch contract changed")
    patch_path = path.parent / patch["path"]
    if not patch_path.is_file() or sha256_file(patch_path) != patch["sha256"]:
        raise ProbeError("OGRE/glslang compatibility patch SHA-256 mismatch")

    reflect = lock.get("ogre_embedded_components", {}).get("spirv_reflect", {})
    if (
        reflect.get("source_sha256")
        != "41394a0cfed351240dc811758d398117ec2cd13ba95dc9f1a1e346546ac7b4d2"
        or reflect.get("header_sha256")
        != "2f3823ea53c6c86902841b5bef3c0b604d56a1e18b97ca46498b6e764573ab03"
        or reflect.get("license_expression") != "Apache-2.0"
        or reflect.get("package_notice_path") != "licenses/Apache-2.0.txt"
    ):
        raise ProbeError("embedded SPIRV-Reflect contract changed")
    if lock.get("host_dynamic_boundary") != {
        "component": "Vulkan-Loader",
        "cmake_library": "Vulkan_LIBRARY",
        "policy": (
            "host-provided dynamic system API; never copied or statically "
            "linked into the N1 package"
        ),
    }:
        raise ProbeError("Linux Vulkan host dynamic boundary changed")
    return lock


def detect_policy(system: str, machine: str) -> dict[str, str]:
    normalized_system = system.strip().lower()
    normalized_machine = machine.strip().lower()
    if normalized_system == "darwin" and normalized_machine in {
        "arm64",
        "aarch64",
    }:
        return {
            "name": "macos-arm64-metal",
            "renderer_target": "RenderSystem_Metal",
            "renderer_name": "Metal Rendering Subsystem",
            "device_option_name": "Rendering Device",
            "shader_data_path": "Hlms/Pbs/Metal",
        }
    if normalized_system == "windows" and normalized_machine in {
        "amd64",
        "x86_64",
    }:
        return {
            "name": "windows-x64-d3d11",
            "renderer_target": "RenderSystem_Direct3D11",
            "renderer_name": "Direct3D11 Rendering Subsystem",
            "device_option_name": "Rendering Device",
            "shader_data_path": "Hlms/Pbs/HLSL",
        }
    if normalized_system == "linux" and normalized_machine in {
        "amd64",
        "x86_64",
    }:
        return {
            "name": "linux-x86_64-vulkan",
            "renderer_target": "RenderSystem_Vulkan",
            "renderer_name": "Vulkan Rendering Subsystem",
            "device_option_name": "Device",
            "shader_data_path": "Hlms/Pbs/GLSL",
        }
    raise ProbeError(f"no reviewed OGRE-Next policy for {system}/{machine}")


def verify_archive(path: Path, expected_sha256: str, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise ProbeError(f"{label} archive does not exist: {resolved}")
    actual_sha256 = sha256_file(resolved)
    if actual_sha256 != expected_sha256:
        raise ProbeError(
            f"{label} archive SHA-256 mismatch: expected {expected_sha256}, "
            f"got {actual_sha256}"
        )
    return resolved


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def default_build_dir() -> Path:
    platform_token = re.sub(
        r"[^a-z0-9]+",
        "-",
        f"{platform.system()}-{platform.machine()}".lower(),
    ).strip("-")
    return Path(tempfile.gettempdir()) / f"ror-ogre-next-probe-{platform_token}"


def prepare_build_dir(path: Path, clean: bool, reuse: bool = False) -> Path:
    requested = path.expanduser()
    if requested.is_symlink():
        raise ProbeError("--build-dir must not be a symbolic link")
    resolved = requested.resolve()
    repository_root = REPOSITORY_ROOT.resolve()
    home = Path.home().resolve()
    filesystem_root = Path(resolved.anchor).resolve()

    if (
        resolved == filesystem_root
        or resolved == home
        or _is_relative_to(repository_root, resolved)
        or _is_relative_to(resolved, repository_root)
    ):
        raise ProbeError(
            "--build-dir must be isolated from the filesystem root, home, "
            "and source checkout"
        )
    try:
        exists = resolved.exists()
        if exists and not resolved.is_dir():
            raise ProbeError(f"--build-dir is not a directory: {resolved}")
        has_entries = exists and any(resolved.iterdir())
    except OSError as error:
        raise ProbeError(f"could not inspect --build-dir: {error}") from error

    if reuse:
        if clean:
            raise ProbeError("--reuse-build-dir and --clean-build-dir conflict")
        if not has_entries:
            raise ProbeError("--reuse-build-dir requires a configured probe build")
        sentinel = resolved / BUILD_SENTINEL_NAME
        cache = resolved / "CMakeCache.txt"
        try:
            sentinel_content = sentinel.read_text(encoding="utf-8")
            cache_text = cache.read_text(encoding="utf-8")
        except OSError as error:
            raise ProbeError(
                "--reuse-build-dir requires an owned configured probe build"
            ) from error
        cache_prefix = "CMAKE_HOME_DIRECTORY:INTERNAL="
        cached_sources = [
            line[len(cache_prefix) :]
            for line in cache_text.splitlines()
            if line.startswith(cache_prefix)
        ]
        cached_source = cached_sources[0] if len(cached_sources) == 1 else None
        windows_paths = os.name == "nt"
        exact_source = (
            cached_source is not None
            and _normalize_cmake_source_path(cached_source, windows_paths)
            == _normalize_cmake_source_path(
                str(PROBE_SOURCE.resolve()), windows_paths
            )
        )
        if sentinel_content != BUILD_SENTINEL_CONTENT or not exact_source:
            raise ProbeError(
                "--reuse-build-dir does not identify this exact probe source"
            )
        return resolved

    if has_entries:
        sentinel = resolved / BUILD_SENTINEL_NAME
        if not clean:
            raise ProbeError(
                "--build-dir is not empty; use a new directory or explicitly "
                "pass --clean-build-dir"
            )
        try:
            sentinel_content = sentinel.read_text(encoding="utf-8")
        except OSError as error:
            raise ProbeError(
                "refusing to clean a directory not owned by the OGRE-Next probe"
            ) from error
        if sentinel_content != BUILD_SENTINEL_CONTENT:
            raise ProbeError(
                "refusing to clean a directory with an invalid probe sentinel"
            )
        try:
            shutil.rmtree(resolved)
        except OSError as error:
            raise ProbeError(f"could not clean --build-dir: {error}") from error

    try:
        resolved.mkdir(parents=True, exist_ok=True)
        (resolved / BUILD_SENTINEL_NAME).write_text(
            BUILD_SENTINEL_CONTENT, encoding="utf-8"
        )
    except OSError as error:
        raise ProbeError(f"could not prepare --build-dir: {error}") from error
    return resolved


def _normalize_cmake_source_path(value: str, windows: bool) -> str:
    """Normalize CMake cache paths without assuming its separator spelling."""

    if not value or "\0" in value or "\n" in value or "\r" in value:
        return ""
    if windows:
        return ntpath.normcase(ntpath.normpath(value))
    return os.path.normcase(os.path.realpath(value))


def validate_report(
    report: dict[str, Any], lock: dict[str, Any], policy: dict[str, str]
) -> None:
    provenance = report.get("provenance", {})
    build = report.get("build", {})
    capabilities = report.get("capabilities", {})
    renderer = capabilities.get("renderer", {})
    pbs = capabilities.get("hlms_pbs", {})
    compositor = capabilities.get("compositor2", {})
    rapidjson = lock["dependencies"]["rapidjson"]
    shader_media = lock["shader_media"]
    shader_notice = shader_media["third_party_notice"]
    abi = lock["abi_contract"]

    checks = {
        "schema_version": report.get("schema_version") == 2,
        "status": report.get("status") == "pass",
        "repository": provenance.get("repository") == lock["repository"],
        "branch": provenance.get("branch") == lock["branch"],
        "commit": provenance.get("commit") == lock["commit"],
        "archive_sha256": provenance.get("archive_sha256")
        == lock["archive_sha256"],
        "license": provenance.get("license_spdx")
        == lock["license"]["spdx"],
        "license_sha256": provenance.get("license_sha256")
        == lock["license"]["sha256"],
        "rapidjson": provenance.get("rapidjson_archive_sha256")
        == rapidjson["archive_sha256"],
        "rapidjson_tag": provenance.get("rapidjson_tag") == rapidjson["tag"],
        "rapidjson_license": provenance.get(
            "rapidjson_source_archive_license_spdx"
        )
        == rapidjson["license_spdx"],
        "rapidjson_compiled_headers_license": provenance.get(
            "rapidjson_compiled_headers_license_spdx"
        )
        == rapidjson["compiled_headers_spdx"],
        "rapidjson_license_sha256": provenance.get("rapidjson_license_sha256")
        == rapidjson["license_sha256"],
        "shader_media_root": provenance.get("shader_media_root")
        == shader_media["root"],
        "shader_media_license": provenance.get(
            "shader_media_license_expression"
        )
        == shader_media["license_expression"],
        "shader_media_source_path": provenance.get(
            "shader_media_third_party_source_path"
        )
        == shader_notice["source_path"],
        "shader_media_source_hash": provenance.get(
            "shader_media_third_party_source_sha256"
        )
        == shader_notice["source_sha256"],
        "shader_media_notice_path": provenance.get(
            "shader_media_notice_path"
        )
        == shader_notice["notice_path"],
        "shader_media_notice_hash": provenance.get(
            "shader_media_notice_sha256"
        )
        == shader_notice["notice_sha256"],
        "shader_media_upstream": provenance.get(
            "shader_media_upstream_source"
        )
        == shader_notice["upstream_source"],
        "shader_media_paper": provenance.get(
            "shader_media_paper_reference"
        )
        == shader_notice["paper_reference"],
        "shader_media_notice_required": provenance.get(
            "shader_media_source_and_binary_notice_required"
        )
        is True,
        "shader_media_paper_required": provenance.get(
            "shader_media_paper_reference_required"
        )
        is True,
        "ogre_version": build.get("ogre_version") == "3.0.0",
        "platform_policy": build.get("platform_policy") == policy["name"],
        "cxx_standard": build.get("cxx_standard") == 17,
        "pointer_bits": build.get("pointer_bits") == 64,
        "static_link": build.get("static_link") is True,
        "abi_cookie": isinstance(build.get("abi_cookie"), str)
        and re.fullmatch(r"[0-9a-f]{32}", build["abi_cookie"]) is not None,
        "debug_mode": build.get("debug_mode")
        == abi["debug_level_release"],
        "double_precision": build.get("double_precision")
        is abi["double_precision"],
        "memory_allocator": build.get("memory_allocator") == abi["allocator"],
        "container_custom_allocator": build.get("container_custom_allocator")
        is abi["container_custom_allocator"],
        "string_custom_allocator": build.get("string_custom_allocator")
        is abi["string_custom_allocator"],
        "thread_support": build.get("thread_support") == abi["thread_support"],
        "thread_provider": build.get("thread_provider") == 0,
        "id_string_bits": build.get("id_string_bits") == 32,
        "id_string_size": build.get("id_string_size") == 4,
        "flexibility_level": build.get("flexibility_level")
        == abi["flexibility_level"],
        "simd_alignment": build.get("simd_alignment")
        == abi["simd"]["alignment"],
        "use_simd": build.get("use_simd") == int(abi["simd"]["enabled"]),
        "restrict_aliasing": build.get("restrict_aliasing")
        == int(abi["restrict_aliasing"]),
        "assert_mode": build.get("assert_mode") == abi["assert_mode"],
        "renderer_target": renderer.get("target")
        == policy["renderer_target"],
        "renderer_name": renderer.get("name") == policy["renderer_name"],
        "renderer_registered": renderer.get("registered") is True,
        "renderer_count": renderer.get("registered_renderer_count") == 1,
        "renderer_options": isinstance(
            renderer.get("configuration_option_count"), int
        )
        and renderer["configuration_option_count"] > 0,
        "renderer_device_option": renderer.get("device_option_name")
        == policy["device_option_name"],
        "renderer_device_count": isinstance(
            renderer.get("reported_device_count"), int
        )
        and renderer["reported_device_count"] > 0,
        "renderer_device_name": isinstance(
            renderer.get("first_reported_device"), str
        )
        and bool(renderer["first_reported_device"]),
        "pbs_linked": pbs.get("compiled_and_linked") is True,
        "pbs_path": pbs.get("shader_data_path") == policy["shader_data_path"],
        "pbs_policy": pbs.get("shader_path_matches_policy") is True,
        "pbs_libraries": isinstance(pbs.get("library_path_count"), int)
        and pbs["library_path_count"] >= 4,
        "compositor_linked": compositor.get("compiled_and_linked") is True,
        "compositor_deferred": compositor.get("runtime_initialization")
        == "deferred_until_real_window",
        "compositor_deferred_observed": compositor.get(
            "deferred_contract_observed"
        )
        is True,
        "ray_tracing_scope": capabilities.get("native_ray_tracing")
        == "not_evaluated",
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next report failed closed: " + ", ".join(sorted(failed))
        )


def validate_build_contract(
    contract: dict[str, Any], lock: dict[str, Any], policy: dict[str, str],
    source_identity: dict[str, Any] | None = None,
) -> None:
    provenance = contract.get("provenance", {})
    rapidjson_contract = contract.get("dependencies", {}).get("rapidjson", {})
    freetype_contract = contract.get("dependencies", {}).get("freetype", {})
    platform_contract = contract.get("platform", {})
    contract_abi = contract.get("abi", {})
    components = contract.get("components", {})
    compiler = contract.get("compiler", {})
    rapidjson = lock["dependencies"]["rapidjson"]
    freetype = lock["dependencies"]["freetype"]
    shader_media = expected_build_shader_media(lock)
    reflection_shader_media = lock["reflection_shader_media"]
    abi = lock["abi_contract"]
    expected_simd_family = abi["simd"][policy["name"]]

    expected_abi = {
        key: value
        for key, value in abi.items()
        if key != "simd"
    }
    expected_abi.update(
        {
            "simd_enabled": abi["simd"]["enabled"],
            "simd_alignment": abi["simd"]["alignment"],
            "simd_family": expected_simd_family,
            "simd_neon": expected_simd_family == "neon",
            "simd_sse2": expected_simd_family == "sse2",
        }
    )
    schema_version = contract.get("schema_version")
    expected_components = {
        "hlms_pbs": True,
        "compositor2_core": True,
        "json_materials": True,
        "mesh_lod": True,
        "dds_codec": True,
        "native_ray_tracing": "not_evaluated",
    }
    if schema_version in (4, 5, 6, 7):
        expected_components.update(
            {
                "hlms_unlit": True,
                "overlay": True,
                "hdr_temporal_contract_version": 2,
                "hdr_history_validation_mode": (
                    "native_authoritative_conditioning_plus_one_r16_ulp_v2"
                ),
                "hdr_workspace": "RoRHdrWorkspaceUiFreeV2",
                "hdr_visual_evidence_version": 1,
            }
        )
    if schema_version in (5, 6, 7):
        expected_components.update(
            {
                "headless_child_bootstrap": True,
                "headless_child_output_name": "RoR-OgreNext",
                "headless_child_packaged": False,
                "headless_child_production_admitted": False,
            }
        )
    if schema_version in (6, 7):
        expected_components.update(
            {
                "headless_child_execution_receipt_schema": (
                    "ror.ogre_next_child_runtime_execution_receipt.v1"
                ),
                "headless_child_execution_receipt_required": True,
                "headless_child_binary_retained": True,
                "headless_child_logs_retained": True,
                "headless_child_process_model": (
                    "single-process-reviewed-source-closure-v1"
                ),
            }
        )
    checks = {
        # Schema 2 remains readable for the immutable checked-in M5 evidence,
        # schema 3 remains the reflection/IBL lineage contract, and schema 4
        # remains the original HDR contract. Every newly generated contract
        # with the pinned static FreeType/Overlay closure is schema 5; schema 6
        # adds upload-bound child evidence and schema 7 binds the optional
        # private embedded-namespace fork without changing its OFF default.
        "schema_version": type(schema_version) is int
        and schema_version in (2, 3, 4, 5, 6, 7),
        "repository": provenance.get("repository") == lock["repository"],
        "branch": provenance.get("branch") == lock["branch"],
        "commit": provenance.get("commit") == lock["commit"],
        "archive_sha256": provenance.get("archive_sha256")
        == lock["archive_sha256"],
        "license_spdx": provenance.get("license_spdx")
        == lock["license"]["spdx"],
        "license_sha256": provenance.get("license_sha256")
        == lock["license"]["sha256"],
        "rapidjson_tag": rapidjson_contract.get("tag") == rapidjson["tag"],
        "rapidjson_archive": rapidjson_contract.get("archive_sha256")
        == rapidjson["archive_sha256"],
        "rapidjson_source_license": rapidjson_contract.get(
            "source_archive_license_spdx"
        )
        == rapidjson["license_spdx"],
        "rapidjson_compiled_license": rapidjson_contract.get(
            "compiled_headers_license_spdx"
        )
        == rapidjson["compiled_headers_spdx"],
        "rapidjson_license_hash": rapidjson_contract.get("license_sha256")
        == rapidjson["license_sha256"],
        "freetype": freetype_contract
        == (
            {
                "repository": freetype["repository"],
                "version": freetype["version"],
                "archive_url": freetype["archive_url"],
                "archive_sha256": freetype["archive_sha256"],
                "license_expression": freetype["license_expression"],
                "selected_license_spdx": freetype[
                    "selected_license_spdx"
                ],
                "license_path": freetype["license_path"],
                "license_sha256": freetype["license_sha256"],
                "package_license_path": freetype[
                    "package_license_path"
                ],
                "overview_path": freetype["overview_path"],
                "overview_sha256": freetype["overview_sha256"],
                "package_overview_path": freetype[
                    "package_overview_path"
                ],
                "target": "freetype",
                "target_type": "STATIC_LIBRARY",
                "static_link": True,
                "overlay_link_target": True,
                "disabled_optional_dependencies": freetype[
                    "disabled_optional_dependencies"
                ],
            }
            if schema_version in (5, 6, 7)
            else {}
        ),
        "shader_media": contract.get("shader_media") == shader_media,
        "reflection_shader_media": contract.get("reflection_shader_media")
        == (
            reflection_shader_media
            if schema_version in (3, 4, 5, 6, 7)
            else None
        ),
        "patches": contract.get("patches")
        == (lock["patches"] if schema_version in (3, 4, 5, 6, 7) else None),
        "platform_policy": platform_contract.get("policy") == policy["name"],
        "renderer_target": platform_contract.get("renderer_target")
        == policy["renderer_target"],
        "device_option_name": platform_contract.get("device_option_name")
        == policy["device_option_name"],
        "system": isinstance(platform_contract.get("system"), str)
        and bool(platform_contract["system"]),
        "processor": isinstance(platform_contract.get("processor"), str)
        and bool(platform_contract["processor"]),
        "abi": contract_abi == expected_abi,
        "components": components == expected_components,
        "compiler_id": isinstance(compiler.get("id"), str)
        and bool(compiler["id"]),
        "compiler_version": isinstance(compiler.get("version"), str)
        and bool(compiler["version"]),
        "build_type": compiler.get("build_type") == REQUIRED_CONFIG,
    }
    if schema_version == 7:
        embedded = lock["embedded_namespace"]
        enabled = contract.get("embedded_namespace", {}).get("enabled")
        checks["embedded_namespace"] = type(enabled) is bool and contract.get(
            "embedded_namespace"
        ) == {
            "enabled": enabled,
            "namespace": embedded["namespace"],
            "cmake_option": embedded["cmake_option"],
            "default_enabled": embedded["default_enabled"],
            "patch": {
                **embedded["patch"],
                "applied": enabled,
            },
            "remap_header": {
                **embedded["remap_header"],
                "forced_include": enabled,
            },
            "full_n1_link_evidence": "not_evaluated",
        }
    else:
        checks["embedded_namespace"] = "embedded_namespace" not in contract
    if source_identity is not None:
        checks["ror_source"] = contract.get("ror_source") == source_identity
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next build contract failed closed: "
            + ", ".join(sorted(failed))
        )


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    try:
        subprocess.run(command, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise ProbeError(f"command failed: {command[0]}: {error}") from error


def run_frame_checkpoint(
    build_dir: Path,
    config: str,
    jobs: int,
    policy: dict[str, str],
    capability_report_path: Path,
) -> None:
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_frame_probe_report",
            "--config",
            config,
            "--parallel",
            str(jobs),
        ]
    )
    frame_report_path = build_dir / FRAME_REPORT_NAME
    frame_image_path = build_dir / FRAME_IMAGE_NAME
    missing = [
        path.name
        for path in (frame_report_path, frame_image_path, capability_report_path)
        if not path.is_file()
    ]
    if missing:
        raise ProbeError(
            "OGRE-Next frame checkpoint did not produce required artifacts: "
            + ", ".join(missing)
        )
    run(
        [
            sys.executable,
            str(FRAME_VALIDATOR),
            "--report",
            str(frame_report_path),
            "--image",
            str(frame_image_path),
            "--capability-report",
            str(capability_report_path),
            "--platform-policy",
            policy["name"],
        ]
    )


def _fnv1a64(data: bytes) -> str:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"


def _changed_pixels(
    baseline: bytes, variant: bytes, bytes_per_pixel: int
) -> int:
    if (
        bytes_per_pixel <= 0
        or len(baseline) != len(variant)
        or len(baseline) % bytes_per_pixel != 0
    ):
        raise ProbeError("RT4/V1 isolation attachment layout drifted")
    return sum(
        baseline[offset : offset + bytes_per_pixel]
        != variant[offset : offset + bytes_per_pixel]
        for offset in range(0, len(baseline), bytes_per_pixel)
    )


def validate_rt4_isolation_evidence(
    report: dict[str, Any], evidence_path: Path
) -> list[dict[str, Any]]:
    try:
        evidence = evidence_path.read_bytes()
    except OSError as error:
        raise ProbeError(
            f"could not read RT4/V1 isolation evidence: {error}"
        ) from error
    isolation = report.get("texture_isolation")
    if not isinstance(isolation, dict):
        raise ProbeError("RT4/V1 isolation report is missing")
    expected_variants = (
        ("baseline", "none"),
        ("base_color", "base_color_rgb"),
        ("roughness_g", "packed_green_roughness"),
        ("metallic_b", "packed_blue_metallic"),
        ("emissive", "emissive_rgb"),
        ("normal_rg", "canonical_positive_z_normal_rg"),
        ("sampler_uv", "sampler_address_over_uv0"),
    )
    variants = isolation.get("variants")
    common_checks = {
        "schema": isolation.get("schema")
        == "ror.ogre_next_rt4_texture_isolation.v1",
        "evidence_file": isolation.get("evidence_file")
        == evidence_path.name,
        "extent": isolation.get("width") == 192
        and isolation.get("height") == 128,
        "evidence_bytes": type(isolation.get("evidence_bytes")) is int
        and 0 < isolation["evidence_bytes"] <= len(evidence),
        "geometry_control": isolation.get("geometry_identical") is True,
        "factor_control": isolation.get(
            "material_factors_constants_identical"
        )
        is True,
        "camera_control": isolation.get("camera_identical") is True,
        "light_control": isolation.get("lights_identical") is True,
        "ui_free": isolation.get("ui_included") is False,
        "variant_count": isinstance(variants, list)
        and len(variants) == len(expected_variants),
    }
    failed = [name for name, passed in common_checks.items() if not passed]
    if failed:
        raise ProbeError(
            "RT4/V1 isolation evidence failed closed: "
            + ", ".join(sorted(failed))
        )
    if not isinstance(variants, list):
        raise ProbeError("RT4/V1 isolation variant list is invalid")

    offset = 0
    baseline_blocks: dict[str, bytes] = {}
    observed_hashes: dict[str, set[str]] = {"hdr": set(), "sdr": set()}
    slice_attestations: list[dict[str, Any]] = []
    for index, (entry, expected) in enumerate(
        zip(variants, expected_variants)
    ):
        if not isinstance(entry, dict):
            raise ProbeError("RT4/V1 isolation variant is not an object")
        if (
            entry.get("name") != expected[0]
            or entry.get("changed_input") != expected[1]
            or entry.get("asset_sequence") != index + 1
        ):
            raise ProbeError("RT4/V1 isolation variant identity drifted")
        for label, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
            attachment = entry.get(label)
            expected_bytes = 192 * 128 * bytes_per_pixel
            if not isinstance(attachment, dict):
                raise ProbeError(
                    f"RT4/V1 {entry.get('name')} {label} metadata is invalid"
                )
            if (
                attachment.get("offset") != offset
                or attachment.get("bytes") != expected_bytes
                or offset + expected_bytes > len(evidence)
            ):
                raise ProbeError(
                    f"RT4/V1 {entry.get('name')} {label} slice is invalid"
                )
            block = evidence[offset : offset + expected_bytes]
            exact_hash = _fnv1a64(block)
            if attachment.get("exact_fnv1a64") != exact_hash:
                raise ProbeError(
                    f"RT4/V1 {entry.get('name')} {label} hash mismatch"
                )
            observed_hashes[label].add(exact_hash)
            if index == 0:
                changed_pixels = 0
                baseline_blocks[label] = block
            else:
                changed_pixels = _changed_pixels(
                    baseline_blocks[label], block, bytes_per_pixel
                )
            if (
                attachment.get("changed_pixels_from_baseline")
                != changed_pixels
                or (index != 0 and changed_pixels < 64)
            ):
                raise ProbeError(
                    f"RT4/V1 {entry.get('name')} {label} delta is not isolated evidence"
                )
            slice_attestations.append(
                {
                    "variant": expected[0],
                    "attachment": label,
                    "offset": offset,
                    "bytes": expected_bytes,
                    "sha256": hashlib.sha256(block).hexdigest(),
                }
            )
            offset += expected_bytes
    if isolation.get("evidence_bytes") != offset:
        raise ProbeError("RT4/V1 texture-isolation byte extent drifted")
    if any(len(hashes) != len(expected_variants) for hashes in observed_hashes.values()):
        raise ProbeError(
            "RT4/V1 isolated inputs did not produce distinct HDR and SDR attachments"
        )
    if (
        report.get("hdr", {}).get("exact_attachment_fnv1a64")
        != _fnv1a64(baseline_blocks["hdr"])
        or report.get("sdr", {}).get("exact_attachment_fnv1a64")
        != _fnv1a64(baseline_blocks["sdr"])
    ):
        raise ProbeError(
            "RT4/V1 baseline isolation evidence differs from the primary report"
        )

    handedness = report.get("tangent_handedness")
    expected_handedness_keys = {
        "schema",
        "evidence_file",
        "evidence_offset",
        "evidence_bytes",
        "authored_tangent_format",
        "positive_tangent_w",
        "negative_tangent_w",
        "position_normal_tangent_xyz_uv0_identical",
        "material_camera_lights_identical",
        "ui_included",
        "positive",
        "negative",
        "hdr_changed_pixels",
        "sdr_changed_pixels",
    }
    if not isinstance(handedness, dict) or set(handedness) != expected_handedness_keys:
        raise ProbeError("RT4/V1 tangent-handedness evidence schema drifted")
    handedness_start = offset
    if (
        handedness.get("schema")
        != "ror.ogre_next_rt4_tangent_handedness.v1"
        or handedness.get("evidence_file") != evidence_path.name
        or handedness.get("evidence_offset") != handedness_start
        or handedness.get("authored_tangent_format") != "FLOAT4"
        or type(handedness.get("positive_tangent_w")) is not int
        or handedness.get("positive_tangent_w") != 1
        or type(handedness.get("negative_tangent_w")) is not int
        or handedness.get("negative_tangent_w") != -1
        or handedness.get("position_normal_tangent_xyz_uv0_identical") is not True
        or handedness.get("material_camera_lights_identical") is not True
        or handedness.get("ui_included") is not False
    ):
        raise ProbeError("RT4/V1 tangent-handedness controls failed closed")
    handedness_blocks: dict[str, dict[str, bytes]] = {
        "positive": {},
        "negative": {},
    }
    for sign in ("positive", "negative"):
        sign_report = handedness.get(sign)
        if not isinstance(sign_report, dict) or set(sign_report) != {"hdr", "sdr"}:
            raise ProbeError(f"RT4/V1 {sign} tangent evidence is invalid")
        for label, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
            attachment = sign_report.get(label)
            expected_bytes = 192 * 128 * bytes_per_pixel
            if not isinstance(attachment, dict) or set(attachment) != {
                "offset",
                "bytes",
                "exact_fnv1a64",
            }:
                raise ProbeError(
                    f"RT4/V1 {sign} tangent {label} metadata is invalid"
                )
            if (
                attachment.get("offset") != offset
                or attachment.get("bytes") != expected_bytes
                or offset + expected_bytes > len(evidence)
            ):
                raise ProbeError(
                    f"RT4/V1 {sign} tangent {label} slice is invalid"
                )
            block = evidence[offset : offset + expected_bytes]
            if attachment.get("exact_fnv1a64") != _fnv1a64(block):
                raise ProbeError(
                    f"RT4/V1 {sign} tangent {label} hash mismatch"
                )
            handedness_blocks[sign][label] = block
            slice_attestations.append(
                {
                    "variant": f"tangent_{sign}_w",
                    "attachment": label,
                    "offset": offset,
                    "bytes": expected_bytes,
                    "sha256": hashlib.sha256(block).hexdigest(),
                }
            )
            offset += expected_bytes
    if (
        handedness.get("evidence_bytes") != offset - handedness_start
        or offset != len(evidence)
    ):
        raise ProbeError("RT4/V1 tangent-handedness byte extent drifted")
    for label, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
        changed = _changed_pixels(
            handedness_blocks["positive"][label],
            handedness_blocks["negative"][label],
            bytes_per_pixel,
        )
        if (
            handedness.get(f"{label}_changed_pixels") != changed
            or changed < 64
        ):
            raise ProbeError(
                f"RT4/V1 tangent-w sign produced no exact {label.upper()} effect"
            )
    return slice_attestations


def _is_nonzero_u64_hex(value: object) -> bool:
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{16}", value) is not None
        and value != "0" * 16
    )


def _is_exact_int(value: object, expected: int) -> bool:
    return type(value) is int and value == expected


def _is_exact_int_list(value: object, expected: tuple[int, ...]) -> bool:
    return (
        isinstance(value, list)
        and len(value) == len(expected)
        and all(
            _is_exact_int(actual, wanted)
            for actual, wanted in zip(value, expected)
        )
    )


def _is_bounded_evidence_string(value: object) -> bool:
    return (
        isinstance(value, str)
        and 0 < len(value) <= 512
        and "\x00" not in value
    )


def _reflection_half_metrics(payload: bytes, label: str) -> dict[str, int | float]:
    if not payload or len(payload) % 8 != 0:
        raise ProbeError(f"RT4/V1 {label} RGBA16F layout is invalid")
    finite_components = 0
    nonzero_rgb_components = 0
    max_absolute_rgb = 0.0
    for channels in struct.iter_unpack("<4e", payload):
        if not all(math.isfinite(channel) for channel in channels):
            raise ProbeError(
                f"RT4/V1 {label} reflection evidence contains non-finite data"
            )
        finite_components += 4
        for channel in channels[:3]:
            magnitude = abs(channel)
            if magnitude > 0.0:
                nonzero_rgb_components += 1
            max_absolute_rgb = max(max_absolute_rgb, magnitude)
    return {
        "finite_component_count": finite_components,
        "nonzero_rgb_component_count": nonzero_rgb_components,
        "distinct_texel_count": len(
            {payload[offset : offset + 8] for offset in range(0, len(payload), 8)}
        ),
        "max_absolute_rgb": max_absolute_rgb,
    }


def _reported_reflection_metric_matches(
    reported: object, computed: float
) -> bool:
    return (
        isinstance(reported, (int, float))
        and not isinstance(reported, bool)
        and math.isfinite(float(reported))
        and math.isclose(
            float(reported), computed, rel_tol=2.0e-7, abs_tol=2.0e-7
        )
    )


def validate_rt4_reflection_evidence(
    report: dict[str, Any], evidence_path: Path, policy: dict[str, str]
) -> list[dict[str, Any]]:
    try:
        evidence = evidence_path.read_bytes()
    except OSError as error:
        raise ProbeError(
            f"could not read RT4/V1 reflection evidence: {error}"
        ) from error
    if len(evidence) != RT4_REFLECTION_EVIDENCE_BYTES:
        raise ProbeError(
            "RT4/V1 reflection evidence is truncated or has trailing bytes"
        )
    reflection = report.get("reflection_probes")
    expected_root_keys = {
        "schema",
        "evidence_file",
        "evidence_bytes",
        "backend",
        "render_system",
        "device_name",
        "driver_version",
        "pixel_format",
        "byte_order",
        "row_padding_included",
        "subresource_order",
        "ui_included",
        "same_device_exact_replay",
        "capture",
        "runtime_audit",
        "raw",
        "filtered",
    }
    if not isinstance(reflection, dict) or set(reflection) != expected_root_keys:
        raise ProbeError("RT4/V1 reflection report schema drifted")
    policy_name = policy.get("name")
    expected_backend = RT4_REFLECTION_BACKENDS.get(str(policy_name))
    common_checks = {
        "schema": reflection.get("schema") == RT4_REFLECTION_SCHEMA,
        "file": reflection.get("evidence_file") == evidence_path.name,
        "bytes": _is_exact_int(
            reflection.get("evidence_bytes"), RT4_REFLECTION_EVIDENCE_BYTES
        ),
        "backend": expected_backend is not None
        and reflection.get("backend") == expected_backend,
        "render_system": reflection.get("render_system")
        == policy.get("renderer_name")
        and reflection.get("render_system") == report.get("renderer"),
        "device": _is_bounded_evidence_string(reflection.get("device_name")),
        "driver": _is_bounded_evidence_string(
            reflection.get("driver_version")
        ),
        "format": reflection.get("pixel_format") == "RGBA16_FLOAT",
        "byte_order": reflection.get("byte_order") == "little_endian",
        "tight_rows": reflection.get("row_padding_included") is False,
        "order": reflection.get("subresource_order")
        == "raw_face_major_then_filtered_mip_major_face_major",
        "ui_free": reflection.get("ui_included") is False,
        "replay": reflection.get("same_device_exact_replay") is True,
    }
    failed = sorted(name for name, passed in common_checks.items() if not passed)
    if failed:
        raise ProbeError(
            "RT4/V1 reflection evidence controls failed closed: "
            + ", ".join(failed)
        )

    capture = reflection.get("capture")
    expected_capture_keys = {
        "render_frame_id",
        "simulation_tick",
        "probe_id",
        "content_revision",
        "candidate_generation",
        "deterministic_seed",
        "resolution",
    }
    if not isinstance(capture, dict) or set(capture) != expected_capture_keys:
        raise ProbeError("RT4/V1 reflection capture lineage schema drifted")
    if not (
        _is_exact_int(capture.get("render_frame_id"), 1)
        and _is_exact_int(capture.get("simulation_tick"), 1)
        and _is_exact_int(capture.get("probe_id"), 1)
        and _is_exact_int(capture.get("content_revision"), 1)
        and _is_exact_int(capture.get("candidate_generation"), 1)
        and _is_nonzero_u64_hex(capture.get("deterministic_seed"))
        and _is_exact_int(
            capture.get("resolution"), RT4_REFLECTION_RESOLUTION
        )
    ):
        raise ProbeError("RT4/V1 reflection capture lineage is invalid")

    runtime = reflection.get("runtime_audit")
    expected_runtime_keys = {
        "version",
        "successful_capture_count",
        "failed_capture_count",
        "live_probe_count",
        "blend_resolution",
        "blend_texture_ready",
        "committed_state_digest",
        "native_execution_evidence",
        "capture_digest",
        "canonical_filtered_payload_bytes",
        "completed_face_count",
        "completed_mip_count",
        "ui_free_capture",
        "reserved_render_queue_excluded",
    }
    if not isinstance(runtime, dict) or set(runtime) != expected_runtime_keys:
        raise ProbeError("RT4/V1 reflection runtime-audit schema drifted")
    if not (
        _is_exact_int(runtime.get("version"), 2)
        and _is_exact_int(runtime.get("successful_capture_count"), 1)
        and _is_exact_int(runtime.get("failed_capture_count"), 0)
        and _is_exact_int(runtime.get("live_probe_count"), 1)
        and _is_exact_int(runtime.get("blend_resolution"), 2048)
        and runtime.get("blend_texture_ready") is True
        and _is_nonzero_u64_hex(runtime.get("committed_state_digest"))
        and _is_nonzero_u64_hex(runtime.get("native_execution_evidence"))
        and _is_nonzero_u64_hex(runtime.get("capture_digest"))
        and _is_exact_int(
            runtime.get("canonical_filtered_payload_bytes"),
            RT4_REFLECTION_FILTERED_BYTES,
        )
        and _is_exact_int(
            runtime.get("completed_face_count"), RT4_REFLECTION_FACE_COUNT
        )
        and _is_exact_int(
            runtime.get("completed_mip_count"),
            len(RT4_REFLECTION_FILTERED_DIMENSIONS),
        )
        and runtime.get("ui_free_capture") is True
        and runtime.get("reserved_render_queue_excluded") is True
    ):
        raise ProbeError("RT4/V1 reflection runtime audit is invalid")

    section_specs = (
        (
            "raw",
            0,
            RT4_REFLECTION_RAW_BYTES,
            (RT4_REFLECTION_RESOLUTION,),
        ),
        (
            "filtered",
            RT4_REFLECTION_RAW_BYTES,
            RT4_REFLECTION_FILTERED_BYTES,
            RT4_REFLECTION_FILTERED_DIMENSIONS,
        ),
    )
    for name, offset, byte_count, dimensions in section_specs:
        section = reflection.get(name)
        expected_section_keys = {
            "offset",
            "bytes",
            "face_count",
            "mip_dimensions",
            "exact_fnv1a64",
            "finite_component_count",
            "nonzero_rgb_component_count",
            "distinct_texel_count",
            "max_absolute_rgb",
        }
        if not isinstance(section, dict) or set(section) != expected_section_keys:
            raise ProbeError(f"RT4/V1 reflection {name} schema drifted")
        payload = evidence[offset : offset + byte_count]
        metrics = _reflection_half_metrics(payload, name)
        if not (
            _is_exact_int(section.get("offset"), offset)
            and _is_exact_int(section.get("bytes"), byte_count)
            and _is_exact_int(
                section.get("face_count"), RT4_REFLECTION_FACE_COUNT
            )
            and _is_exact_int_list(section.get("mip_dimensions"), dimensions)
            and section.get("exact_fnv1a64") == _fnv1a64(payload)
            and _is_exact_int(
                section.get("finite_component_count"),
                int(metrics["finite_component_count"]),
            )
            and _is_exact_int(
                section.get("nonzero_rgb_component_count"),
                int(metrics["nonzero_rgb_component_count"]),
            )
            and _is_exact_int(
                section.get("distinct_texel_count"),
                int(metrics["distinct_texel_count"]),
            )
            and _reported_reflection_metric_matches(
                section.get("max_absolute_rgb"),
                float(metrics["max_absolute_rgb"]),
            )
            and int(metrics["nonzero_rgb_component_count"]) > 0
            and int(metrics["distinct_texel_count"]) >= 2
            and float(metrics["max_absolute_rgb"]) > 0.0
        ):
            raise ProbeError(f"RT4/V1 reflection {name} evidence is invalid")

    offset = 0
    slices: list[dict[str, Any]] = []
    for texture, dimensions in (
        ("raw", (RT4_REFLECTION_RESOLUTION,)),
        ("filtered", RT4_REFLECTION_FILTERED_DIMENSIONS),
    ):
        for mip, dimension in enumerate(dimensions):
            slice_bytes = dimension * dimension * 8
            for face in range(RT4_REFLECTION_FACE_COUNT):
                payload = evidence[offset : offset + slice_bytes]
                _reflection_half_metrics(
                    payload, f"{texture} mip {mip} face {face}"
                )
                slices.append(
                    {
                        "texture": texture,
                        "mip": mip,
                        "face": face,
                        "offset": offset,
                        "bytes": slice_bytes,
                        "sha256": hashlib.sha256(payload).hexdigest(),
                    }
                )
                offset += slice_bytes
    filtered_mip_one_offset = RT4_REFLECTION_RAW_BYTES * 2
    filtered_mip_one = _reflection_half_metrics(
        evidence[filtered_mip_one_offset:], "filtered mip one"
    )
    if (
        offset != len(evidence)
        or len(slices) != 18
        or int(filtered_mip_one["nonzero_rgb_component_count"]) == 0
        or int(filtered_mip_one["distinct_texel_count"]) < 2
        or float(filtered_mip_one["max_absolute_rgb"]) <= 0.0
    ):
        raise ProbeError("RT4/V1 reflection subresource coverage is incomplete")
    return slices


def _decode_positive_r16(bits: object) -> float | None:
    if type(bits) is not int or not 0 < bits <= 0x7BFF:
        return None
    decoded = struct.unpack("<e", struct.pack("<H", bits))[0]
    if not math.isfinite(decoded) or decoded <= 0.0:
        return None
    return decoded


def _positive_r16_storage_ulp(bits: object) -> float | None:
    decoded = _decode_positive_r16(bits)
    if decoded is None:
        return None
    spacings = []
    if bits > 1:
        lower = _decode_positive_r16(bits - 1)
        if lower is None:
            return None
        spacings.append(decoded - lower)
    else:
        spacings.append(2.0**-24)
    if bits < 0x7BFF:
        upper = _decode_positive_r16(bits + 1)
        if upper is None:
            return None
        spacings.append(upper - decoded)
    storage_ulp = max(spacings)
    return storage_ulp if math.isfinite(storage_ulp) and storage_ulp > 0.0 else None


def _binary32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _recompute_hdr_history_oracle(compositor: dict[str, Any]) -> dict[str, Any]:
    fields = (
        "history_ogre_exposure",
        "history_minimum_auto_exposure",
        "history_maximum_auto_exposure",
        "history_average_log_luminance",
        "history_delta_seconds",
    )
    if any(
        not isinstance(compositor.get(field), (int, float))
        or isinstance(compositor.get(field), bool)
        or not math.isfinite(float(compositor[field]))
        for field in fields
    ):
        raise ProbeError("HDR history oracle inputs are not finite numbers")
    previous_bits = compositor.get(
        "history_previous_inverse_luminance_r16_bits"
    )
    previous = _decode_positive_r16(previous_bits)
    if previous is None:
        raise ProbeError("HDR history oracle previous R16 input is invalid")

    exposure = _binary32(float(compositor["history_ogre_exposure"]))
    minimum = _binary32(
        float(compositor["history_minimum_auto_exposure"])
    )
    maximum = _binary32(
        float(compositor["history_maximum_auto_exposure"])
    )
    average = _binary32(
        float(compositor["history_average_log_luminance"])
    )
    delta = _binary32(float(compositor["history_delta_seconds"]))
    if not (-16.0 <= exposure <= 16.0 and -16.0 <= minimum <= maximum <= 16.0):
        raise ProbeError("HDR history oracle exposure inputs are out of range")
    if not (0.0 <= delta <= 60.0):
        raise ProbeError("HDR history oracle delta is out of range")

    analytic_numerator = 1024.0 * math.exp(exposure - 2.0)
    analytic_clamped = max(7.5 - maximum, min(7.5 - minimum, average))
    analytic_target = analytic_numerator / math.exp(analytic_clamped)
    analytic_weight = math.pow(0.25, delta)

    shader_exponent = _binary32(exposure - _binary32(2.0))
    shader_exp = _binary32(math.exp(shader_exponent))
    shader_numerator = _binary32(_binary32(1024.0) * shader_exp)
    shader_minimum = _binary32(_binary32(7.5) - maximum)
    shader_maximum = _binary32(_binary32(7.5) - minimum)
    shader_clamped = max(shader_minimum, min(shader_maximum, average))
    shader_denominator = _binary32(math.exp(shader_clamped))
    shader_target = _binary32(shader_numerator / shader_denominator)
    shader_weight = _binary32(math.pow(_binary32(0.25), delta))
    shader_new_weight = _binary32(_binary32(1.0) - shader_weight)
    shader_weighted_target = _binary32(shader_target * shader_new_weight)
    shader_weighted_previous = _binary32(previous * shader_weight)
    shader_adapted = _binary32(
        shader_weighted_target + shader_weighted_previous
    )
    try:
        reference_bits = int.from_bytes(struct.pack("<e", shader_adapted), "little")
    except (OverflowError, struct.error) as error:
        raise ProbeError("HDR history oracle is not R16 representable") from error
    storage_ulp = _positive_r16_storage_ulp(reference_bits)
    if storage_ulp is None:
        raise ProbeError("HDR history oracle has no positive storage ULP")

    conditioning = abs(1.0 - analytic_weight) * abs(
        analytic_target - shader_target
    ) + abs(shader_target - previous) * abs(
        analytic_weight - shader_weight
    )
    gamma5 = (5.0 * 2.0**-24) / (1.0 - 5.0 * 2.0**-24)
    rounding = gamma5 * (
        abs(shader_target * (1.0 - shader_weight))
        + abs(previous * shader_weight)
    ) + 4.0 * 2.0**-149
    return {
        "reference_bits": reference_bits,
        "conditioning_bound": conditioning,
        "rounding_bound": rounding,
        "storage_ulp": storage_ulp,
        "allowed_error": conditioning + rounding + storage_ulp,
    }


def _history_oracle_matches(reported: object, computed: float) -> bool:
    return (
        isinstance(reported, (int, float))
        and not isinstance(reported, bool)
        and math.isfinite(float(reported))
        and math.isclose(
            float(reported), computed, rel_tol=2.0e-6, abs_tol=1.0e-12
        )
    )


def validate_hdr_compositor_visual_evidence(
    report: dict[str, Any], evidence_path: Path, ppm_pixels: bytes
) -> list[dict[str, Any]]:
    try:
        payload = evidence_path.read_bytes()
    except OSError as error:
        raise ProbeError(f"could not read HDR compositor evidence: {error}") from error
    visual = report.get("hdr_compositor_visual")
    compositor = report.get("hdr_compositor")
    if not isinstance(visual, dict) or not isinstance(compositor, dict):
        raise ProbeError("HDR compositor visual evidence metadata is missing")
    attachments = visual.get("attachments")
    expected_names = ("first_ui_free", "final_ui_free", "ui_overlay_control")
    expected_bytes = 192 * 128 * 4
    if (
        visual.get("schema") != "ror.ogre_next_hdr_compositor_visual.v1"
        or visual.get("evidence_file") != evidence_path.name
        or visual.get("ppm_attachment") != "final_ui_free"
        or visual.get("width") != 192
        or visual.get("height") != 128
        or visual.get("bytes_per_pixel") != 4
        or visual.get("evidence_bytes") != len(payload)
        or not isinstance(attachments, list)
        or len(attachments) != len(expected_names)
        or len(payload) != expected_bytes * len(expected_names)
    ):
        raise ProbeError("HDR compositor visual evidence contract is invalid")
    baseline = payload[:expected_bytes]
    slices: list[dict[str, Any]] = []
    observed: dict[str, bytes] = {}
    for index, (entry, name) in enumerate(zip(attachments, expected_names)):
        if not isinstance(entry, dict) or set(entry) != {
            "name",
            "offset",
            "bytes",
            "exact_fnv1a64",
            "changed_pixels_from_first",
        }:
            raise ProbeError("HDR compositor attachment metadata is invalid")
        offset = index * expected_bytes
        block = payload[offset : offset + expected_bytes]
        changed = _changed_pixels(baseline, block, 4)
        if (
            entry.get("name") != name
            or entry.get("offset") != offset
            or entry.get("bytes") != expected_bytes
            or entry.get("exact_fnv1a64") != _fnv1a64(block)
            or entry.get("changed_pixels_from_first") != changed
            or any(block[pixel + 3] < 250 for pixel in range(0, len(block), 4))
        ):
            raise ProbeError("HDR compositor attachment evidence mismatch")
        observed[name] = block
        slices.append(
            {
                "attachment": name,
                "offset": offset,
                "bytes": expected_bytes,
                "sha256": hashlib.sha256(block).hexdigest(),
            }
        )
    final_rgb = bytes(
        channel
        for offset in range(0, len(observed["final_ui_free"]), 4)
        for channel in observed["final_ui_free"][offset : offset + 3]
    )
    magenta = sum(
        block[offset] >= 250
        and block[offset + 1] <= 5
        and block[offset + 2] >= 250
        for block in (observed["ui_overlay_control"],)
        for offset in range(0, len(block), 4)
    )
    exposure_changed = _changed_pixels(
        observed["first_ui_free"], observed["final_ui_free"], 4
    )
    overlay_changed = _changed_pixels(
        observed["first_ui_free"], observed["ui_overlay_control"], 4
    )
    if (
        final_rgb != ppm_pixels
        or exposure_changed < 512
        or overlay_changed < 18432
        or magenta < 18432
        or compositor.get("exposure_changed_pixels") != exposure_changed
        or compositor.get("ui_overlay_control_changed_pixels") != overlay_changed
        or compositor.get("ui_overlay_control_magenta_pixels") != magenta
        or compositor.get("first_attachment_fnv1a64")
        != _fnv1a64(observed["first_ui_free"])
        or compositor.get("final_attachment_fnv1a64")
        != _fnv1a64(observed["final_ui_free"])
        or compositor.get("ui_overlay_control_fnv1a64")
        != _fnv1a64(observed["ui_overlay_control"])
    ):
        raise ProbeError("HDR compositor visual evidence failed closed")
    return slices


def validate_n1_checkpoint(
    report: dict[str, Any],
    image_path: Path,
    lock: dict[str, Any],
    policy: dict[str, str],
    media_manifest: dict[str, Any],
    source_identity: dict[str, Any],
    modern_pbr: bool = False,
    isolation_evidence_path: Path | None = None,
    compositor_evidence_path: Path | None = None,
) -> list[dict[str, Any]] | None:
    try:
        image = image_path.read_bytes()
    except OSError as error:
        raise ProbeError(f"could not read N1 frame: {error}") from error
    header = b"P6\n192 128\n255\n"
    if not image.startswith(header) or len(image) != len(header) + 192 * 128 * 3:
        raise ProbeError("N1 frame is not the exact 192x128 RGB8 PPM contract")
    pixels = image[len(header) :]
    hash_value = 14695981039346656037
    for value in pixels:
        hash_value ^= value
        hash_value = (hash_value * 1099511628211) & ((1 << 64) - 1)
    colours = [
        bytes(pixels[offset : offset + 3])
        for offset in range(0, len(pixels), 3)
    ]
    counts: dict[bytes, int] = {}
    for colour in colours:
        counts[colour] = counts.get(colour, 0) + 1
    observed_non_background = len(colours) - max(counts.values())
    provenance = report.get("provenance", {})
    adapter = report.get("adapter", {})
    catalog = report.get("catalog", {})
    dynamic_meshes = report.get("dynamic_meshes", {})
    display_domain_unlit = report.get("display_domain_unlit", {})
    texture_allocations = report.get("texture_allocations", {})
    texture_upload_rollback = report.get("texture_upload_rollback", {})
    texture_retirement = report.get("texture_retirement", {})
    hdr_compositor = report.get("hdr_compositor", {})
    hdr = report.get("hdr", {})
    sdr = report.get("sdr", {})
    lifecycle = report.get("lifecycle", {})
    native_history = _decode_positive_r16(
        hdr_compositor.get("final_inverse_luminance_r16_bits")
    )
    reference_history = _decode_positive_r16(
        hdr_compositor.get("reference_inverse_luminance_r16_bits")
    )
    expected_storage_ulp = _positive_r16_storage_ulp(
        hdr_compositor.get("reference_inverse_luminance_r16_bits")
    )
    reported_absolute_error = hdr_compositor.get("history_absolute_error")
    reported_allowed_error = hdr_compositor.get("history_allowed_error")
    reported_conditioning_bound = hdr_compositor.get(
        "history_conditioning_bound"
    )
    reported_rounding_bound = hdr_compositor.get(
        "history_binary32_rounding_bound"
    )
    reported_storage_ulp = hdr_compositor.get("history_storage_ulp")
    history_oracle = (
        _recompute_hdr_history_oracle(hdr_compositor) if modern_pbr else None
    )
    if modern_pbr:
        if compositor_evidence_path is None:
            raise ProbeError("RT4/V1 HDR compositor evidence path is missing")
        validate_hdr_compositor_visual_evidence(
            report, compositor_evidence_path, pixels
        )
    shader_media = lock["shader_media"]
    checks = {
        "schema": report.get("schema")
        == (
            RT4_PBR_REPORT_SCHEMA
            if modern_pbr
            else "ror.ogre_next_frontend_n1_smoke.v1"
        ),
        "status": report.get("status") == "pass",
        "commit": provenance.get("ogre_next_commit") == lock["commit"],
        "archive": provenance.get("ogre_next_archive_sha256")
        == lock["archive_sha256"],
        "normal_map_source_lock": provenance.get(
            "normal_map_source_lock_sha256"
        )
        == NORMAL_MAP_SOURCE_LOCK_SHA256,
        "ror_repository": provenance.get("ror_repository")
        == source_identity["repository"],
        "ror_ref": provenance.get("ror_ref") == source_identity["ref"],
        "ror_commit": provenance.get("ror_commit")
        == source_identity["commit"],
        "ror_source_manifest": provenance.get(
            "ror_relevant_source_manifest_sha256"
        )
        == source_identity["relevant_manifest_sha256"],
        "ror_source_count": provenance.get(
            "ror_relevant_source_manifest_file_count"
        )
        == source_identity["relevant_manifest_file_count"],
        "shader_root": provenance.get("shader_media_root")
        == shader_media["root"],
        "shader_license": provenance.get("shader_media_license_expression")
        == shader_media["license_expression"],
        "shader_notice": provenance.get("shader_media_notice_sha256")
        == shader_media["third_party_notice"]["notice_sha256"],
        "shader_manifest_hash": provenance.get(
            "shader_media_manifest_sha256"
        )
        == media_manifest["sha256"],
        "shader_manifest_count": provenance.get(
            "shader_media_manifest_file_count"
        )
        == media_manifest["file_count"],
        "platform": report.get("platform_policy") == policy["name"],
        "renderer": report.get("renderer") == policy["renderer_name"],
        "v2_vao": adapter.get("native_mesh_path")
        == "Ogre v2 Mesh plus immutable VertexArrayObject",
        "pbs": adapter.get("material_path") == "HLMS PBS metallic-roughness",
        "brdf": adapter.get("brdf")
        == "PbsBrdf::Default height-correlated GGX",
        "pbr_readback": adapter.get("pbr_datablock_readback_verified") is True,
        "runtime_media_root": adapter.get("runtime_media_root")
        == "explicit_absolute",
        "package_media": adapter.get("package_media_relative_path")
        == "share/rigsofrods/ogre-next/Samples/Media",
        "relocated_executable": adapter.get("relocated_executable") is True,
        "compositor2": adapter.get("compositor2") is True,
        "ui_free": adapter.get("ui_included") is False,
        "readback": adapter.get("cpu_readback_completed") is True,
        "dynamic_capability": adapter.get("dynamic_mesh_updates")
        == "synchronous_full_frame_owned",
        "light_policy": (
            adapter.get("analytic_lights_calibrated") is True
            and adapter.get("directional_lux_to_native_power_scale")
            == 1.0 / 1024.0
            and adapter.get("maximum_directional_lights") == 1
            and adapter.get("constant_environment_only") is False
        )
        if modern_pbr
        else (
            adapter.get("analytic_lights_calibrated") is False
            and adapter.get("constant_environment_only") is True
        ),
        "interop_closed": adapter.get("native_interop") is False
        and adapter.get("ray_tracing") is False,
        "catalog": catalog.get("sequence") == (7 if modern_pbr else 1)
        and catalog.get("transactional_replay_after_restart") is True,
        "dynamic_meshes": dynamic_meshes.get("schema")
        == "ror.ogre_next_dynamic_mesh.v1"
        and dynamic_meshes.get("base_deformation_revision") == 1
        and dynamic_meshes.get("deformed_deformation_revision") == 2
        and dynamic_meshes.get("full_update_owned") is True
        and dynamic_meshes.get("solver_memory_aliased") is False
        and isinstance(dynamic_meshes.get("changed_pixels"), int)
        and dynamic_meshes["changed_pixels"] >= 256
        and isinstance(dynamic_meshes.get("base_attachment_fnv1a64"), str)
        and len(dynamic_meshes["base_attachment_fnv1a64"]) == 16
        and isinstance(
            dynamic_meshes.get("deformed_attachment_fnv1a64"), str
        )
        and len(dynamic_meshes["deformed_attachment_fnv1a64"]) == 16
        and dynamic_meshes["base_attachment_fnv1a64"]
        != dynamic_meshes["deformed_attachment_fnv1a64"]
        and dynamic_meshes.get("base_exact_replay") is True
        and dynamic_meshes.get("deformed_exact_replay") is True,
        "hdr_format": hdr.get("format") == "RGBA16_FLOAT",
        "hdr_energy": isinstance(hdr.get("maximum_luminance"), (int, float))
        and hdr["maximum_luminance"] > 1.05,
        "hdr_geometry": isinstance(hdr.get("non_background_pixels"), int)
        and hdr["non_background_pixels"] >= 512,
        "sdr_format": sdr.get("format") == "RGBA8_SRGB",
        "sdr_hash": modern_pbr
        or sdr.get("rgb8_fnv1a64") == f"{hash_value:016x}",
        "sdr_distinct": modern_pbr
        or sdr.get("distinct_rgb8_values") == len(counts),
        "sdr_geometry": (
            observed_non_background >= 512
            if modern_pbr
            else sdr.get("non_background_pixels") == observed_non_background
            and observed_non_background >= 512
        ),
        "lifecycle": all(
            lifecycle.get(field) is True
            for field in (
                "unsupported_depth_failed_before_submission",
                "double_sided_pbs_readback",
                "lifetime_snapshot_identity_replay",
                "lifetime_completed_frame_queries",
                "process_global_root_exclusion",
                "shutdown_reinitialize_render_shutdown",
            )
        ),
    }
    if modern_pbr:
        checks.update(
            {
                "rt4_tier": adapter.get("raster_feature_tier")
                == "MODERN_PBR_RT4_V1",
                "rt4_hdr_media_manifest": provenance.get(
                    "hdr_media_manifest_sha256"
                )
                == media_manifest.get("hdr_sha256")
                and provenance.get("hdr_media_manifest_file_count")
                == media_manifest.get("hdr_file_count"),
                "rt4_vertex_layout": adapter.get("vertex_layout")
                == "position_normal_tangent_uv0",
                "rt4_srgb": adapter.get("base_color_upload")
                == "RGBA8_UNORM_SRGB"
                and adapter.get("emissive_upload") == "RGBA8_UNORM_SRGB",
                "rt4_display_domain_unlit": display_domain_unlit.get("schema")
                == "ror.ogre_next_rt4_display_domain_unlit.v1"
                and display_domain_unlit.get("base_color_transfer")
                == "SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE"
                and display_domain_unlit.get("upload_format") == "RGBA8_UNORM"
                and display_domain_unlit.get("mip_policy")
                == "complete_base_to_1x1_nearest_mip"
                and display_domain_unlit.get("sampler")
                == "linear_min_mag_clamp_edge"
                and display_domain_unlit.get("shader_precision")
                == "PrecisionFull32"
                and all(
                    isinstance(display_domain_unlit.get(field), list)
                    and len(display_domain_unlit[field]) == 3
                    and all(
                        isinstance(value, (int, float))
                        for value in display_domain_unlit[field]
                    )
                    for field in (
                        "encoded_filtered",
                        "filter_then_eotf",
                        "decode_before_filter",
                    )
                )
                and isinstance(
                    display_domain_unlit.get("matching_foreground_pixels"), int
                )
                and display_domain_unlit["matching_foreground_pixels"] >= 512
                and display_domain_unlit.get("decode_before_filter_pixels") == 0
                and display_domain_unlit.get("complete_unorm_mips_uploaded")
                is True
                and display_domain_unlit.get(
                    "full32_after_filter_shader_executed"
                )
                is True
                and display_domain_unlit.get("alpha_untouched_opaque") is True
                and display_domain_unlit.get("no_cast_or_receive_shadow_flags")
                is True
                and display_domain_unlit.get("usage_transition_rollback_exact")
                is True
                and display_domain_unlit.get("usage_transition_commit_exact")
                is True,
                "rt4_orm": adapter.get("metallic_roughness_upload")
                == "linear_G_to_R8_roughness_B_to_R8_metallic",
                "rt4_sampler": adapter.get(
                    "portable_sampler_mapping_verified"
                )
                is True,
                "rt4_padded_rows": adapter.get(
                    "padded_source_rows_verified"
                )
                is True,
                "rt4_normal": adapter.get("normal_texture_admitted") is True
                and adapter.get("normal_upload")
                == "linear_RGBA8_positive_Z_to_RG8_UNORM"
                and adapter.get("normal_slot") == "PBSM_NORMAL"
                and adapter.get("normal_uv_source") == 0
                and adapter.get("normal_scale") == 1
                and adapter.get("normal_map_weight") == 1
                and adapter.get("normal_positive_z_tolerance_decoded")
                == "1/255",
                "rt4_occlusion_closed": adapter.get(
                    "occlusion_texture_admitted"
                )
                is False
                and adapter.get("occlusion_blocker")
                == "pinned_HLMS_PBS_has_no_ambient_only_AO_slot",
                "rt4_referenced_resources": catalog.get(
                    "referenced_texture_count"
                )
                == 4
                and catalog.get("referenced_sampler_count") == 1
                and catalog.get("unreferenced_assets_not_uploaded") is True,
                "rt4_replacement_sequence": catalog.get(
                    "baseline_sequence"
                )
                == 1
                and catalog.get("live_replacement_count") == 6,
                "rt4_exact_allocations": texture_allocations
                == {
                    "version": 1,
                    "live_source_textures": 4,
                    "sampled_rgba_allocations": 2,
                    "roughness_r8_allocations": 1,
                    "metallic_r8_allocations": 1,
                    "normal_rg8_allocations": 1,
                    "unused_packed_rgba_allocations": 0,
                    "exact_usage": True,
                },
                "rt4_live_replacement": lifecycle.get(
                    "live_texture_replacement_retirement"
                )
                is True
                and lifecycle.get("replacement_audit")
                == {
                    "creates": 17,
                    "destroys": 12,
                    "live": 5,
                    "retired_name_lookups": 12,
                    "retired_name_rejections": 12,
                    "exact_usage": True,
                },
                "rt4_non_uniform_scale_closed": lifecycle.get(
                    "non_uniform_scale_rejected_before_submission"
                )
                is True,
                "rt4_normal_upload_rollback": texture_upload_rollback.get(
                    "derived_allocation"
                )
                == "normal_RG8_UNORM"
                and texture_upload_rollback.get(
                    "clean_retry_replacement_shutdown"
                )
                is True,
                "rt4_retirement": texture_retirement
                == {
                    "schema": "ror.ogre_next_rt4_texture_retirement.v1",
                    "derived_allocation": "normal_RG8_UNORM",
                    "isolated_from_visual_variants": True,
                    "native_image_rg8_staging": {
                        "version": 1,
                        "verified_uploads": 2,
                        "verified_mip_levels": 3,
                        "verified_rows": 5,
                        "verified_texels": 14,
                        "verified_rg_bytes": 28,
                        "verified_padded_source_rows": 5,
                        "exact_source_rg_to_native_image": True,
                    },
                    "transitions": [
                        {
                            "revision": 1,
                            "width": 2,
                            "height": 2,
                            "mip_levels": 1,
                        },
                        {
                            "revision": 2,
                            "width": 4,
                            "height": 2,
                            "mip_levels": 2,
                            "padded_rows": True,
                        },
                        {
                            "revision": 3,
                            "width": 2,
                            "height": 2,
                            "mip_levels": 1,
                        },
                    ],
                    "exact_extent_and_mip_transitions": True,
                    "renders_through_transitions_and_restart": True,
                    "find_texture_no_throw_rejected_old_names": True,
                    "audits": {
                        "initial": {
                            "creates": 1,
                            "destroys": 0,
                            "live": 1,
                            "retired_name_lookups": 0,
                            "retired_name_rejections": 0,
                        },
                        "expanded": {
                            "creates": 2,
                            "destroys": 1,
                            "live": 1,
                            "retired_name_lookups": 1,
                            "retired_name_rejections": 1,
                        },
                        "restored": {
                            "creates": 3,
                            "destroys": 2,
                            "live": 1,
                            "retired_name_lookups": 2,
                            "retired_name_rejections": 2,
                        },
                        "first_shutdown": {
                            "creates": 3,
                            "destroys": 3,
                            "live": 0,
                            "retired_name_lookups": 3,
                            "retired_name_rejections": 3,
                        },
                        "restarted": {
                            "creates": 4,
                            "destroys": 3,
                            "live": 1,
                            "retired_name_lookups": 3,
                            "retired_name_rejections": 3,
                        },
                        "final_shutdown": {
                            "creates": 4,
                            "destroys": 4,
                            "live": 0,
                            "retired_name_lookups": 4,
                            "retired_name_rejections": 4,
                        },
                    },
                },
                "rt4_hdr_compositor": isinstance(hdr_compositor, dict)
                and set(hdr_compositor)
                == {
                    "schema",
                    "workspace",
                    "persistent_workspace",
                    "scene_format",
                    "history_format",
                    "output_format",
                    "ui_included",
                    "ui_free_workspace_verified",
                    "deterministic_simulation_delta",
                    "history_validation_mode",
                    "native_r16_history_validated",
                    "exact_current_to_old_copy_verified",
                    "warmup_frames",
                    "committed_frames",
                    "initial_inverse_luminance_r16_bits",
                    "final_inverse_luminance_r16_bits",
                    "reference_inverse_luminance_r16_bits",
                    "history_ogre_exposure",
                    "history_minimum_auto_exposure",
                    "history_maximum_auto_exposure",
                    "history_average_log_luminance",
                    "history_previous_inverse_luminance_r16_bits",
                    "history_delta_seconds",
                    "history_absolute_error",
                    "history_allowed_error",
                    "history_conditioning_bound",
                    "history_binary32_rounding_bound",
                    "history_storage_ulp",
                    "history_r16_ulp_distance",
                    "history_changed_from_initial",
                    "exposure_changed_pixels",
                    "ui_overlay_control_node",
                    "ui_overlay_control_kind",
                    "ui_overlay_control_changed_pixels",
                    "ui_overlay_control_magenta_pixels",
                    "ui_overlay_control_fnv1a64",
                    "initialization_failure_stages_verified",
                    "same_object_reinitialize_verified",
                    "frame_commit_prepare_failure_verified",
                    "aborted_hdr_audit_unchanged",
                    "aborted_reflection_audit_unchanged",
                    "aborted_submission_uncommitted",
                    "aborted_output_unchanged",
                    "post_render_failure_fault_latched",
                    "first_attachment_fnv1a64",
                    "final_attachment_fnv1a64",
                    "clean_shutdown",
                }
                and hdr_compositor.get("schema")
                == "ror.ogre_next_hdr_compositor.v4"
                and hdr_compositor.get("workspace") == "RoRHdrWorkspaceUiFreeV2"
                and hdr_compositor.get("persistent_workspace") is True
                and hdr_compositor.get("scene_format") == "RGBA16_FLOAT"
                and hdr_compositor.get("history_format") == "R16_FLOAT"
                and hdr_compositor.get("output_format") == "RGBA8_SRGB"
                and hdr_compositor.get("ui_included") is False
                and hdr_compositor.get("ui_free_workspace_verified") is True
                and hdr_compositor.get("deterministic_simulation_delta")
                is True
                and hdr_compositor.get("history_validation_mode")
                == "native_authoritative_conditioning_plus_one_r16_ulp_v2"
                and hdr_compositor.get("native_r16_history_validated") is True
                and hdr_compositor.get("exact_current_to_old_copy_verified")
                is True
                and type(hdr_compositor.get("warmup_frames")) is int
                and hdr_compositor.get("warmup_frames") == 2
                and type(hdr_compositor.get("committed_frames")) is int
                and hdr_compositor.get("committed_frames") == 2
                and type(
                    hdr_compositor.get("initial_inverse_luminance_r16_bits")
                )
                is int
                and hdr_compositor.get("initial_inverse_luminance_r16_bits")
                == int.from_bytes(struct.pack("<e", 0.01), "little")
                and type(
                    hdr_compositor.get("final_inverse_luminance_r16_bits")
                )
                is int
                and 0
                < hdr_compositor.get("final_inverse_luminance_r16_bits")
                <= 0x7BFF
                and hdr_compositor.get("final_inverse_luminance_r16_bits")
                != hdr_compositor.get("initial_inverse_luminance_r16_bits")
                and type(
                    hdr_compositor.get("reference_inverse_luminance_r16_bits")
                )
                is int
                and 0
                < hdr_compositor.get("reference_inverse_luminance_r16_bits")
                <= 0x7BFF
                and isinstance(
                    hdr_compositor.get("history_absolute_error"), (int, float)
                )
                and not isinstance(
                    hdr_compositor.get("history_absolute_error"), bool
                )
                and math.isfinite(
                    float(hdr_compositor.get("history_absolute_error"))
                )
                and 0.0 <= hdr_compositor.get("history_absolute_error")
                and isinstance(
                    hdr_compositor.get("history_allowed_error"), (int, float)
                )
                and not isinstance(
                    hdr_compositor.get("history_allowed_error"), bool
                )
                and math.isfinite(
                    float(hdr_compositor.get("history_allowed_error"))
                )
                and hdr_compositor.get("history_allowed_error")
                >= hdr_compositor.get("history_absolute_error")
                and all(
                    isinstance(hdr_compositor.get(field), (int, float))
                    and not isinstance(hdr_compositor.get(field), bool)
                    and math.isfinite(float(hdr_compositor.get(field)))
                    and hdr_compositor.get(field) >= 0.0
                    for field in (
                        "history_conditioning_bound",
                        "history_binary32_rounding_bound",
                    )
                )
                and isinstance(
                    hdr_compositor.get("history_storage_ulp"), (int, float)
                )
                and not isinstance(hdr_compositor.get("history_storage_ulp"), bool)
                and math.isfinite(float(hdr_compositor.get("history_storage_ulp")))
                and hdr_compositor.get("history_storage_ulp") > 0.0
                and type(hdr_compositor.get("history_r16_ulp_distance")) is int
                and hdr_compositor.get("history_r16_ulp_distance") >= 0
                and native_history is not None
                and reference_history is not None
                and expected_storage_ulp is not None
                and history_oracle is not None
                and _decode_positive_r16(
                    hdr_compositor.get(
                        "history_previous_inverse_luminance_r16_bits"
                    )
                )
                is not None
                and hdr_compositor.get("reference_inverse_luminance_r16_bits")
                == history_oracle["reference_bits"]
                and _history_oracle_matches(
                    reported_conditioning_bound,
                    history_oracle["conditioning_bound"],
                )
                and _history_oracle_matches(
                    reported_rounding_bound, history_oracle["rounding_bound"]
                )
                and _history_oracle_matches(
                    reported_storage_ulp, history_oracle["storage_ulp"]
                )
                and _history_oracle_matches(
                    reported_allowed_error, history_oracle["allowed_error"]
                )
                and math.isclose(
                    float(reported_absolute_error),
                    abs(native_history - reference_history),
                    rel_tol=0.0,
                    abs_tol=0.0,
                )
                and math.isclose(
                    float(reported_storage_ulp),
                    expected_storage_ulp,
                    rel_tol=0.0,
                    abs_tol=0.0,
                )
                and math.isclose(
                    float(reported_allowed_error),
                    float(reported_conditioning_bound)
                    + float(reported_rounding_bound)
                    + float(reported_storage_ulp),
                    rel_tol=2.0e-15,
                    abs_tol=1.0e-18,
                )
                and hdr_compositor.get("history_r16_ulp_distance")
                == abs(
                    hdr_compositor.get("final_inverse_luminance_r16_bits")
                    - hdr_compositor.get(
                        "reference_inverse_luminance_r16_bits"
                    )
                )
                and hdr_compositor.get("history_changed_from_initial") is True
                and type(hdr_compositor.get("exposure_changed_pixels")) is int
                and hdr_compositor.get("exposure_changed_pixels") >= 512
                and hdr_compositor.get("ui_overlay_control_node")
                == "HdrRenderUi"
                and hdr_compositor.get("ui_overlay_control_kind")
                == "Ogre::v1::Overlay"
                and type(
                    hdr_compositor.get("ui_overlay_control_changed_pixels")
                )
                is int
                and hdr_compositor.get("ui_overlay_control_changed_pixels")
                >= 18432
                and type(
                    hdr_compositor.get("ui_overlay_control_magenta_pixels")
                )
                is int
                and hdr_compositor.get("ui_overlay_control_magenta_pixels")
                >= 18432
                and isinstance(
                    hdr_compositor.get("ui_overlay_control_fnv1a64"),
                    str,
                )
                and re.fullmatch(
                    r"[0-9a-f]{16}",
                    hdr_compositor.get("ui_overlay_control_fnv1a64"),
                )
                is not None
                and type(
                    hdr_compositor.get("initialization_failure_stages_verified")
                )
                is int
                and hdr_compositor.get("initialization_failure_stages_verified")
                == 10
                and hdr_compositor.get("same_object_reinitialize_verified")
                is True
                and all(
                    hdr_compositor.get(field) is True
                    for field in (
                        "frame_commit_prepare_failure_verified",
                        "aborted_hdr_audit_unchanged",
                        "aborted_reflection_audit_unchanged",
                        "aborted_submission_uncommitted",
                        "aborted_output_unchanged",
                        "post_render_failure_fault_latched",
                    )
                )
                and isinstance(
                    hdr_compositor.get("first_attachment_fnv1a64"), str
                )
                and re.fullmatch(
                    r"[0-9a-f]{16}",
                    hdr_compositor.get("first_attachment_fnv1a64"),
                )
                is not None
                and isinstance(
                    hdr_compositor.get("final_attachment_fnv1a64"), str
                )
                and re.fullmatch(
                    r"[0-9a-f]{16}",
                    hdr_compositor.get("final_attachment_fnv1a64"),
                )
                is not None
                and hdr_compositor.get("first_attachment_fnv1a64")
                != hdr_compositor.get("final_attachment_fnv1a64")
                and hdr_compositor.get("first_attachment_fnv1a64")
                != hdr_compositor.get("ui_overlay_control_fnv1a64")
                and hdr_compositor.get("clean_shutdown") is True,
            }
        )
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next N1 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )
    if modern_pbr:
        if isolation_evidence_path is None:
            raise ProbeError("RT4/V1 isolation evidence path is required")
        return validate_rt4_isolation_evidence(report, isolation_evidence_path)
    return None


def _attest_regular_file(path: Path, build_dir: Path) -> dict[str, Any]:
    try:
        resolved_root = build_dir.resolve(strict=True)
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise ProbeError(f"could not resolve RT4 attestation input: {error}") from error
    if path.is_symlink() or not path.is_file() or resolved.parent == resolved:
        raise ProbeError(f"RT4 attestation input is not a regular file: {path}")
    try:
        relative = resolved.relative_to(resolved_root).as_posix()
    except ValueError as error:
        raise ProbeError(f"RT4 attestation input escaped the build root: {path}") from error
    size = resolved.stat().st_size
    if size <= 0:
        raise ProbeError(f"RT4 attestation input is empty: {relative}")
    return {"path": relative, "bytes": size, "sha256": sha256_file(resolved)}


def _packaged_n1_executable(
    build_dir: Path, policy: dict[str, str]
) -> Path:
    package_bin = build_dir / N1_PACKAGE_NAME / "bin"
    suffix = ".exe" if policy["name"] == "windows-x64-d3d11" else ""
    expected = package_bin / f"{N1_PACKAGE_EXECUTABLE_STEM}{suffix}"
    if expected.is_symlink() or not expected.is_file():
        raise ProbeError(f"packaged N1 executable is missing: {expected.name}")
    files = sorted(path for path in package_bin.iterdir() if path.is_file())
    if files != [expected]:
        raise ProbeError("N1 package must contain exactly the reviewed executable")
    return expected


def _fsync_parent_directory(path: Path) -> None:
    """Persist a directory entry on POSIX.

    Windows does not expose a portable directory-fsync primitive through
    Python. There, os.replace/unlink provide atomic namespace transitions but
    not the same power-loss durability guarantee as the POSIX fsync below.
    """
    if os.name == "nt":
        return
    descriptor: int | None = None
    try:
        descriptor = os.open(
            path.parent,
            os.O_RDONLY | getattr(os, "O_DIRECTORY", 0),
        )
        os.fsync(descriptor)
    except OSError as error:
        raise ProbeError(
            f"could not persist directory for {path.name}: {error}"
        ) from error
    finally:
        if descriptor is not None:
            os.close(descriptor)


def _atomic_write_json(path: Path, value: dict[str, Any]) -> None:
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as target:
            temporary = Path(target.name)
            json.dump(value, target, indent=2, sort_keys=True)
            target.write("\n")
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, path)
        temporary = None
        _fsync_parent_directory(path)
    except OSError as error:
        raise ProbeError(f"could not atomically write {path.name}: {error}") from error
    finally:
        if temporary is not None:
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass


def _durable_unlink(path: Path) -> None:
    existed = path.exists() or path.is_symlink()
    try:
        path.unlink(missing_ok=True)
        if existed:
            _fsync_parent_directory(path)
    except OSError as error:
        raise ProbeError(f"could not invalidate stale {path.name}: {error}") from error


def write_rt4_attestation(
    build_dir: Path,
    report: dict[str, Any],
    report_path: Path,
    image_path: Path,
    evidence_path: Path,
    reflection_evidence_path: Path,
    compositor_evidence_path: Path,
    repeat_report_path: Path,
    repeat_image_path: Path,
    repeat_evidence_path: Path,
    repeat_reflection_evidence_path: Path,
    repeat_compositor_evidence_path: Path,
    executable_path: Path,
    build_contract_path: Path,
    source_identity: dict[str, Any],
    lock: dict[str, Any],
    media_manifest: dict[str, Any],
    isolation_slices: list[dict[str, Any]],
    reflection_slices: list[dict[str, Any]],
    compositor_slices: list[dict[str, Any]],
) -> dict[str, Any]:
    provenance = report.get("provenance")
    if not isinstance(provenance, dict):
        raise ProbeError("RT4 report provenance is missing before attestation")
    shader_media = lock["shader_media"]
    notice = shader_media["third_party_notice"]
    attestation = {
        "schema": RT4_PBR_ATTESTATION_SCHEMA,
        "status": "pass",
        "integrity_model": (
            "self-contained-checksums-plus-independent-semantics; "
            "not-a-cryptographic-signature"
        ),
        "source": {
            "repository": source_identity["repository"],
            "ref": source_identity["ref"],
            "commit": source_identity["commit"],
            "relevant_manifest_sha256": source_identity[
                "relevant_manifest_sha256"
            ],
            "relevant_manifest_file_count": source_identity[
                "relevant_manifest_file_count"
            ],
        },
        "ogre_next": {
            "repository": lock["repository"],
            "branch": lock["branch"],
            "commit": lock["commit"],
            "archive_sha256": lock["archive_sha256"],
            "license_spdx": lock["license"]["spdx"],
            "license_sha256": lock["license"]["sha256"],
            "normal_map_source_lock_sha256": NORMAL_MAP_SOURCE_LOCK_SHA256,
        },
        "shader_media": {
            "root": shader_media["root"],
            "license_expression": shader_media["license_expression"],
            "source_path": notice["source_path"],
            "source_sha256": notice["source_sha256"],
            "notice_path": notice["notice_path"],
            "notice_sha256": notice["notice_sha256"],
            "manifest_sha256": media_manifest["sha256"],
            "manifest_file_count": media_manifest["file_count"],
            "hdr_manifest_sha256": media_manifest["hdr_sha256"],
            "hdr_manifest_file_count": media_manifest["hdr_file_count"],
        },
        "files": {
            "build_contract": _attest_regular_file(build_contract_path, build_dir),
            "report": _attest_regular_file(report_path, build_dir),
            "ppm": _attest_regular_file(image_path, build_dir),
            "isolation": _attest_regular_file(evidence_path, build_dir),
            "reflection": _attest_regular_file(
                reflection_evidence_path, build_dir
            ),
            "compositor": _attest_regular_file(
                compositor_evidence_path, build_dir
            ),
            "repeat_report": _attest_regular_file(
                repeat_report_path, build_dir
            ),
            "repeat_ppm": _attest_regular_file(repeat_image_path, build_dir),
            "repeat_isolation": _attest_regular_file(
                repeat_evidence_path, build_dir
            ),
            "repeat_reflection": _attest_regular_file(
                repeat_reflection_evidence_path, build_dir
            ),
            "repeat_compositor": _attest_regular_file(
                repeat_compositor_evidence_path, build_dir
            ),
            "executable": _attest_regular_file(executable_path, build_dir),
        },
        "isolation_slices": isolation_slices,
        "reflection_slices": reflection_slices,
        "compositor_slices": compositor_slices,
    }
    attestation_path = build_dir / RT4_PBR_ATTESTATION_NAME
    _atomic_write_json(attestation_path, attestation)
    try:
        persisted = json.loads(attestation_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not verify RT4 attestation: {error}") from error
    if persisted != attestation:
        raise ProbeError("persisted RT4 attestation differs from verified data")
    return attestation


def shader_media_manifest(root: Path) -> dict[str, Any]:
    if not root.is_dir():
        raise ProbeError(f"OGRE-Next N1 HLMS tree is missing: {root}")
    entries: list[tuple[str, int, str]] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        if path.is_symlink():
            raise ProbeError(
                "OGRE-Next N1 HLMS tree contains a symbolic link: "
                + path.relative_to(root).as_posix()
            )
        if path.is_dir():
            continue
        if not path.is_file():
            raise ProbeError(
                "OGRE-Next N1 HLMS tree contains a non-file entry: "
                + path.relative_to(root).as_posix()
            )
        entries.append(
            (
                path.relative_to(root).as_posix(),
                path.stat().st_size,
                sha256_file(path),
            )
        )
    if not entries:
        raise ProbeError("OGRE-Next N1 HLMS tree is empty")
    serialized = "".join(
        f"{relative}|{size}|{digest}\n"
        for relative, size, digest in entries
    ).encode("utf-8")
    return {
        "sha256": hashlib.sha256(serialized).hexdigest(),
        "file_count": len(entries),
        "entries": entries,
    }


def hdr_media_manifest(media_root: Path) -> dict[str, Any]:
    roots = (
        Path("2.0/scripts/Compositors"),
        Path("2.0/scripts/materials/Common"),
        Path("2.0/scripts/materials/HDR"),
    )
    entries_by_path: dict[str, tuple[str, int, str]] = {}
    for relative_root in roots:
        root = media_root / relative_root
        if root.is_symlink() or not root.is_dir():
            raise ProbeError(
                "OGRE-Next HDR media root is missing or symbolic: "
                + relative_root.as_posix()
            )
        for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
            relative = path.relative_to(media_root).as_posix()
            if path.is_symlink():
                raise ProbeError(
                    "OGRE-Next HDR media contains a symbolic link: " + relative
                )
            if path.is_dir():
                continue
            if not path.is_file():
                raise ProbeError(
                    "OGRE-Next HDR media contains a non-file entry: " + relative
                )
            entries_by_path[relative] = (
                relative,
                path.stat().st_size,
                sha256_file(path),
            )
    entries = [entries_by_path[key] for key in sorted(entries_by_path)]
    if not entries:
        raise ProbeError("OGRE-Next HDR media closure is empty")
    serialized = "".join(
        f"{relative}|{size}|{digest}\n"
        for relative, size, digest in entries
    ).encode("utf-8")
    return {
        "sha256": hashlib.sha256(serialized).hexdigest(),
        "file_count": len(entries),
        "entries": entries,
    }


def validate_n1_package(
    build_dir: Path, lock: dict[str, Any]
) -> dict[str, Any]:
    package_root = build_dir / N1_PACKAGE_NAME
    shader_notice = lock["shader_media"]["third_party_notice"]
    reflection_notice = lock["reflection_shader_media"]["third_party_notice"]
    expected_hashes = {
        Path("licenses/Rigs-of-Rods-GPL-3.0.txt"): sha256_file(
            REPOSITORY_ROOT / "COPYING"
        ),
        Path("licenses/Ogre-Next-MIT.txt"): lock["license"]["sha256"],
        Path("licenses/RapidJSON-license.txt"): lock["dependencies"][
            "rapidjson"
        ]["license_sha256"],
        Path(lock["dependencies"]["freetype"]["package_license_path"]): lock[
            "dependencies"
        ]["freetype"]["license_sha256"],
        Path(lock["dependencies"]["freetype"]["package_overview_path"]): lock[
            "dependencies"
        ]["freetype"]["overview_sha256"],
        Path(shader_notice["notice_path"]): shader_notice["notice_sha256"],
        Path(reflection_notice["package_path"]): reflection_notice[
            "source_sha256"
        ],
    }
    failures: list[str] = []
    for relative_path, expected_hash in expected_hashes.items():
        staged_path = package_root / relative_path
        if not staged_path.is_file():
            failures.append(f"missing {relative_path.as_posix()}")
            continue
        if sha256_file(staged_path) != expected_hash:
            failures.append(f"hash mismatch {relative_path.as_posix()}")
    if failures:
        raise ProbeError(
            "OGRE-Next N1 package license validation failed closed: "
            + ", ".join(failures)
        )
    source_media_root = (
        build_dir / "_deps" / "ogre_next-src" / "Samples" / "Media"
    )
    package_media_root = (
        package_root
        / "share"
        / "rigsofrods"
        / "ogre-next"
        / "Samples"
        / "Media"
    )
    source_manifest = shader_media_manifest(source_media_root / "Hlms")
    package_manifest = shader_media_manifest(package_media_root / "Hlms")
    if package_manifest != source_manifest:
        raise ProbeError(
            "OGRE-Next N1 staged HLMS tree differs from the pinned source manifest"
        )
    source_hdr_manifest = hdr_media_manifest(source_media_root)
    package_hdr_manifest = hdr_media_manifest(package_media_root)
    if package_hdr_manifest != source_hdr_manifest:
        raise ProbeError(
            "OGRE-Next N1 staged HDR media differs from the pinned source manifest"
        )
    combined = dict(source_manifest)
    combined["hdr_sha256"] = source_hdr_manifest["sha256"]
    combined["hdr_file_count"] = source_hdr_manifest["file_count"]
    return combined


def run_n1_checkpoint(
    build_dir: Path,
    config: str,
    jobs: int,
    lock: dict[str, Any],
    policy: dict[str, str],
    source_identity: dict[str, Any],
) -> None:
    attestation_path = build_dir / RT4_PBR_ATTESTATION_NAME
    _durable_unlink(attestation_path)
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_frontend_n1_report",
            "--config",
            config,
            "--parallel",
            str(jobs),
        ]
    )
    require_source_identity_unchanged(source_identity)
    report_path = build_dir / N1_REPORT_NAME
    image_path = build_dir / N1_IMAGE_NAME
    rt4_report_path = build_dir / RT4_PBR_REPORT_NAME
    rt4_image_path = build_dir / RT4_PBR_IMAGE_NAME
    rt4_evidence_path = build_dir / RT4_PBR_EVIDENCE_NAME
    rt4_reflection_evidence_path = (
        build_dir / RT4_PBR_REFLECTION_EVIDENCE_NAME
    )
    rt4_compositor_evidence_path = (
        build_dir / RT4_PBR_COMPOSITOR_EVIDENCE_NAME
    )
    repeat_root = build_dir / RT4_PBR_REPEAT_DIRECTORY
    repeat_report_path = repeat_root / RT4_PBR_REPORT_NAME
    repeat_image_path = repeat_root / RT4_PBR_IMAGE_NAME
    repeat_evidence_path = repeat_root / RT4_PBR_EVIDENCE_NAME
    repeat_reflection_evidence_path = (
        repeat_root / RT4_PBR_REFLECTION_EVIDENCE_NAME
    )
    repeat_compositor_evidence_path = (
        repeat_root / RT4_PBR_COMPOSITOR_EVIDENCE_NAME
    )
    missing = [
        path.name
        for path in (
            report_path,
            image_path,
            rt4_report_path,
            rt4_image_path,
            rt4_evidence_path,
            rt4_reflection_evidence_path,
            rt4_compositor_evidence_path,
            repeat_report_path,
            repeat_image_path,
            repeat_evidence_path,
            repeat_reflection_evidence_path,
            repeat_compositor_evidence_path,
        )
        if not path.is_file()
    ]
    if missing:
        raise ProbeError(
            "OGRE-Next N1 checkpoint did not produce required artifacts: "
            + ", ".join(missing)
        )
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
        rt4_report = json.loads(rt4_report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read N1 report: {error}") from error
    media_manifest = validate_n1_package(build_dir, lock)
    validate_n1_checkpoint(
        report, image_path, lock, policy, media_manifest, source_identity
    )
    isolation_slices = validate_n1_checkpoint(
        rt4_report,
        rt4_image_path,
        lock,
        policy,
        media_manifest,
        source_identity,
        modern_pbr=True,
        isolation_evidence_path=rt4_evidence_path,
        compositor_evidence_path=rt4_compositor_evidence_path,
    )
    if isolation_slices is None:
        raise ProbeError("RT4/V1 isolation slice attestation is missing")
    reflection_slices = validate_rt4_reflection_evidence(
        rt4_report, rt4_reflection_evidence_path, policy
    )
    compositor_slices = validate_hdr_compositor_visual_evidence(
        rt4_report,
        rt4_compositor_evidence_path,
        rt4_image_path.read_bytes()[len(b"P6\n192 128\n255\n") :],
    )
    try:
        repeat_report = json.loads(
            repeat_report_path.read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read RT4 repeat report: {error}") from error
    repeat_slices = validate_n1_checkpoint(
        repeat_report,
        repeat_image_path,
        lock,
        policy,
        media_manifest,
        source_identity,
        modern_pbr=True,
        isolation_evidence_path=repeat_evidence_path,
        compositor_evidence_path=repeat_compositor_evidence_path,
    )
    if repeat_slices != isolation_slices:
        raise ProbeError("RT4 deterministic repeat isolation slices differ")
    repeat_reflection_slices = validate_rt4_reflection_evidence(
        repeat_report, repeat_reflection_evidence_path, policy
    )
    repeat_compositor_slices = validate_hdr_compositor_visual_evidence(
        repeat_report,
        repeat_compositor_evidence_path,
        repeat_image_path.read_bytes()[len(b"P6\n192 128\n255\n") :],
    )
    if repeat_reflection_slices != reflection_slices:
        raise ProbeError("RT4 deterministic repeat reflection slices differ")
    if repeat_compositor_slices != compositor_slices:
        raise ProbeError("RT4 deterministic repeat compositor slices differ")
    for primary, repeat in (
        (rt4_report_path, repeat_report_path),
        (rt4_image_path, repeat_image_path),
        (rt4_evidence_path, repeat_evidence_path),
        (rt4_reflection_evidence_path, repeat_reflection_evidence_path),
        (rt4_compositor_evidence_path, repeat_compositor_evidence_path),
    ):
        if sha256_file(primary) != sha256_file(repeat):
            raise ProbeError(
                f"RT4 deterministic repeat differs: {primary.name}"
            )
    if rt4_report["sdr"]["rgb8_fnv1a64"] == report["sdr"]["rgb8_fnv1a64"]:
        raise ProbeError(
            "RT4/V1 texture-backed evidence is indistinguishable from static N1"
        )
    require_source_identity_unchanged(source_identity)
    require_relevant_source_clean()
    write_rt4_attestation(
        build_dir,
        rt4_report,
        rt4_report_path,
        rt4_image_path,
        rt4_evidence_path,
        rt4_reflection_evidence_path,
        rt4_compositor_evidence_path,
        repeat_report_path,
        repeat_image_path,
        repeat_evidence_path,
        repeat_reflection_evidence_path,
        repeat_compositor_evidence_path,
        _packaged_n1_executable(build_dir, policy),
        build_dir / BUILD_CONTRACT_NAME,
        source_identity,
        lock,
        media_manifest,
        isolation_slices,
        reflection_slices,
        compositor_slices,
    )


def validate_n2_checkpoint(
    report: dict[str, Any],
    probe_path: Path | None,
    executable_path: Path,
    lock: dict[str, Any],
    policy: dict[str, str],
    expected_source_commit: str,
    expected_source_ref: str,
    expected_source_manifest_sha256: str,
) -> None:
    if policy["name"] != "macos-arm64-metal":
        raise ProbeError("Metal N2 validation is Apple-only")
    if re.fullmatch(r"[0-9a-f]{40}", expected_source_commit) is None:
        raise ProbeError("expected RoR source commit is not a full Git SHA")
    _require_sha256(
        expected_source_manifest_sha256,
        "expected Metal N2 relevant-source manifest",
    )
    try:
        executable_bytes = executable_path.stat().st_size
        executable_sha256 = sha256_file(executable_path)
    except OSError as error:
        raise ProbeError(f"could not attest Metal N2 executable: {error}") from error

    def object_field(name: str) -> dict[str, Any]:
        value = report.get(name, {})
        return value if isinstance(value, dict) else {}

    def nonnegative_int(mapping: dict[str, Any], name: str) -> int | None:
        value = mapping.get(name)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            return None
        return value

    provenance = object_field("provenance")
    device = object_field("device")
    admission = object_field("admission")
    geometry = object_field("geometry")
    synchronization = object_field("synchronization")
    acceleration = object_field("acceleration_structures")
    probe = object_field("probe")
    lifecycle = object_field("lifecycle")
    status = report.get("status")
    common_checks = {
        "schema": report.get("schema") == "ror.ogre_next_metal_rt_n2.v3",
        "status": status in ("pass", "skip"),
        "scope": report.get("scope")
        == (
            "same-device single-ray geometry interop capability probe; no "
            "rendered image, view-dependent result, GPU timing, material, "
            "lighting, denoising, or compositing claim"
        ),
        "ror_repository": provenance.get("ror_repository")
        == ROR_SOURCE_REPOSITORY,
        "ror_ref": provenance.get("ror_ref") == expected_source_ref,
        "ror_commit": provenance.get("ror_commit") == expected_source_commit,
        "relevant_source_clean": provenance.get("relevant_source_clean")
        is True,
        "relevant_source_manifest": provenance.get(
            "relevant_source_manifest_sha256"
        )
        == expected_source_manifest_sha256,
        "ogre_repository": provenance.get("ogre_next_repository")
        == lock["repository"],
        "ogre_commit": provenance.get("ogre_next_commit") == lock["commit"],
        "ogre_archive": provenance.get("ogre_next_archive_sha256")
        == lock["archive_sha256"],
        "build_artifact": provenance.get("build_artifact")
        == executable_path.name,
        "build_artifact_bytes": provenance.get("build_artifact_bytes")
        == executable_bytes
        and executable_bytes > 0,
        "build_artifact_sha256": provenance.get("build_artifact_sha256")
        == executable_sha256,
        "backend_compiled": admission.get("backend_compiled") is True,
        "no_render_claim": probe.get("rendered_image_produced") is False
        and probe.get("view_dependent") is False
        and probe.get("gpu_timestamp_measured") is False,
        "legacy_dispatch_absent": "dispatch" not in report,
    }
    failed = [name for name, passed in common_checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next Metal N2 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )

    if status == "skip":
        skip = object_field("skip")
        skip_checks = {
            "probe_absent": probe_path is None or not probe_path.exists(),
            "device": isinstance(device.get("name"), str)
            and bool(device["name"])
            and nonnegative_int(device, "context_id") not in (None, 0),
            "same_device_queue": device.get("same_ogre_device") is True
            and device.get("same_ogre_queue") is True,
            "interop_context": admission.get("interop_context_exported") is True,
            "hardware_unavailable": admission.get("hardware_floor_met") is False
            and isinstance(admission.get("supports_raytracing"), bool)
            and isinstance(admission.get("supports_family_apple9"), bool)
            and (
                admission.get("supports_raytracing") is False
                or admission.get("supports_family_apple9") is False
            ),
            "skip_code": skip.get("initialization_code") == "UNSUPPORTED",
            "skip_reason": isinstance(skip.get("reason"), str)
            and bool(skip["reason"]),
            "hardware_floor": skip.get("required_metal_ray_tracing") is True
            and skip.get("required_apple_gpu_family") == 9,
            "probe_not_executed": probe.get("executed") is False
            and probe.get("probe_readback_bytes") == 0,
            "pass_evidence_absent": not geometry
            and not synchronization
            and not acceleration
            and not lifecycle,
        }
        failed = [name for name, passed in skip_checks.items() if not passed]
        if failed:
            raise ProbeError(
                "OGRE-Next Metal N2 capability skip failed closed: "
                + ", ".join(sorted(failed))
            )
        return

    if probe_path is None:
        raise ProbeError("passed Metal N2 checkpoint has no probe artifact")
    try:
        probe_bytes = probe_path.read_bytes()
    except OSError as error:
        raise ProbeError(f"could not read Metal N2 probe: {error}") from error
    if len(probe_bytes) != 8:
        raise ProbeError("Metal N2 artifact is not the exact eight-byte probe")
    hit_magic, hit_distance = struct.unpack("<If", probe_bytes)
    if hit_magic != 0x52545254 or abs(hit_distance - 1.0) > 0.0001:
        raise ProbeError("Metal N2 probe does not encode the proven unit hit")

    vertex_offset = nonnegative_int(geometry, "vertex_pool_offset_bytes")
    vertex_size = nonnegative_int(geometry, "vertex_slice_bytes")
    index_offset = nonnegative_int(geometry, "index_pool_offset_bytes")
    index_size = nonnegative_int(geometry, "index_slice_bytes")
    vertex_length = nonnegative_int(geometry, "vertex_buffer_length_bytes")
    index_length = nonnegative_int(geometry, "index_buffer_length_bytes")
    vertex_end = (
        vertex_offset + vertex_size
        if vertex_offset is not None and vertex_size is not None
        else None
    )
    index_end = (
        index_offset + index_size
        if index_offset is not None and index_size is not None
        else None
    )
    context_id = nonnegative_int(device, "context_id")
    frontend_complete = nonnegative_int(
        synchronization, "frontend_complete_value"
    )
    external_complete = nonnegative_int(
        synchronization, "external_complete_value"
    )
    checks = {
        "device": isinstance(device.get("name"), str)
        and bool(device["name"])
        and context_id is not None
        and context_id > 0,
        "same_device_queue": device.get("same_ogre_device") is True
        and device.get("same_ogre_queue") is True,
        "admission": all(
            admission.get(field) is True
            for field in (
                "frontend_api_reported",
                "backend_compiled",
                "api_supported",
                "supports_raytracing",
                "supports_family_apple9",
                "hardware_accelerated",
                "dispatch_readback_probe_passed",
                "geometry_interop_ready",
            )
        ),
        "deformed_triangle": geometry.get("frame_id") == 1
        and geometry.get("snapshot_id") == 1
        and geometry.get("instance_id") == 1
        and geometry.get("topology_revision") == 1
        and geometry.get("deformation_revision") == 2
        and geometry.get("vertex_count") == 3
        and geometry.get("index_count") == 3,
        "vertex_generation": geometry.get("vertex_buffer_generation")
        == geometry.get("frame_id"),
        "index_generation": geometry.get("index_buffer_generation")
        == geometry.get("frame_id"),
        "vertex_slice": vertex_size == 60
        and geometry.get("vertex_stride_bytes") == 24
        and vertex_end is not None
        and vertex_length is not None
        and vertex_end <= vertex_length,
        "index_slice": index_size == 6
        and geometry.get("index_stride_bytes") == 2
        and index_end is not None
        and index_length is not None
        and index_end <= index_length,
        "exact_slices": geometry.get("exact_exported_vertex_slice_used") is True
        and geometry.get("exact_exported_index_slice_used") is True,
        "timeline_values": frontend_complete is not None
        and external_complete is not None
        and frontend_complete > 0
        and external_complete > frontend_complete,
        "timeline_order": all(
            synchronization.get(field) is True
            for field in (
                "same_shared_event",
                "external_encoders_ended_before_signal",
                "cpu_wait_after_commit_only",
            )
        ),
        "blas_tlas": all(
            isinstance(acceleration.get(field), int)
            and acceleration[field] > 0
            for field in (
                "blas_bytes",
                "blas_scratch_bytes",
                "tlas_bytes",
                "tlas_scratch_bytes",
            )
        ),
        "ray_hit": probe.get("kind") == "single_ray_geometry_interop"
        and probe.get("rays") == 1
        and probe.get("hit_magic") == hit_magic
        and isinstance(probe.get("hit_distance"), (int, float))
        and abs(probe["hit_distance"] - hit_distance) <= 0.0001,
        "probe_readback": probe.get("probe_readback_bytes") == len(probe_bytes)
        and probe.get("probe_readback_sha256")
        == hashlib.sha256(probe_bytes).hexdigest(),
        "lifecycle": all(
            lifecycle.get(field) is True
            for field in (
                "stale_generation_rejected",
                "revision_n_plus_one_blocked_while_n_live",
                "frontend_shutdown_blocked_before_backend",
                "backend_shutdown_before_frontend",
                "frontend_revoke_clears_backend_readiness",
                "frontend_destructor_before_backend_safe",
                "backend_destructor_before_frontend_safe",
                "native_submission_precedes_injected_observation",
                "injected_device_lost_abandon_allows_frontend_shutdown",
                "injected_timeout_abandon_allows_frontend_shutdown",
                "post_release_revision_n_plus_one_rendered",
                "interop_report_geometry_proven",
            )
        ),
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next Metal N2 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )


def run_n2_checkpoint(
    build_dir: Path,
    config: str,
    jobs: int,
    lock: dict[str, Any],
    policy: dict[str, str],
) -> None:
    if policy["name"] != "macos-arm64-metal":
        return
    source_commit, source_ref, source_manifest_sha256 = repository_identity()
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_metal_n2_report",
            "--config",
            config,
            "--parallel",
            str(jobs),
        ]
    )
    report_path = build_dir / N2_REPORT_NAME
    probe_candidate = build_dir / N2_PROBE_NAME
    probe_path = probe_candidate if probe_candidate.is_file() else None
    executable_candidates = (
        build_dir / "bin" / "ror_ogre_next_metal_n2_smoke",
        build_dir / "bin" / config / "ror_ogre_next_metal_n2_smoke",
    )
    executable_path = next(
        (candidate for candidate in executable_candidates if candidate.is_file()),
        executable_candidates[0],
    )
    missing = [path.name for path in (report_path, executable_path) if not path.is_file()]
    if missing:
        raise ProbeError(
            "OGRE-Next Metal N2 checkpoint did not produce required artifacts: "
            + ", ".join(missing)
        )
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read Metal N2 report: {error}") from error
    rebuilt_identity = repository_identity()
    if rebuilt_identity != (source_commit, source_ref, source_manifest_sha256):
        raise ProbeError(
            "RoR source provenance changed while building the Metal N2 proof"
        )
    validate_n2_checkpoint(
        report,
        probe_path,
        executable_path,
        lock,
        policy,
        source_commit,
        source_ref,
        source_manifest_sha256,
    )

    attestation = {
        "schema": "ror.ogre_next_metal_rt_n2.attestation.v2",
        "status": report.get("status"),
        "source": {
            "ror_commit": source_commit,
            "ror_ref": source_ref,
            "relevant_source_clean": True,
            "relevant_source_manifest_sha256": source_manifest_sha256,
        },
        "executable": {
            "path": executable_path.name,
            "bytes": executable_path.stat().st_size,
            "sha256": sha256_file(executable_path),
        },
        "report": {
            "path": report_path.name,
            "bytes": report_path.stat().st_size,
            "sha256": sha256_file(report_path),
        },
        "probe": (
            {
                "path": probe_path.name,
                "bytes": probe_path.stat().st_size,
                "sha256": sha256_file(probe_path),
            }
            if probe_path is not None
            else None
        ),
    }
    attestation_path = build_dir / N2_ATTESTATION_NAME
    temporary_attestation = attestation_path.with_suffix(".json.tmp")
    try:
        temporary_attestation.write_text(
            json.dumps(attestation, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary_attestation.replace(attestation_path)
        persisted_attestation = json.loads(
            attestation_path.read_text(encoding="utf-8")
        )
    except OSError as error:
        raise ProbeError(f"could not write Metal N2 attestation: {error}") from error
    except json.JSONDecodeError as error:
        raise ProbeError(f"could not verify Metal N2 attestation: {error}") from error
    if persisted_attestation != attestation:
        raise ProbeError("persisted Metal N2 attestation differs from verified data")


def validate_linux_static_closure_manifest(
    manifest: dict[str, Any], linux_lock: dict[str, Any]
) -> None:
    expected_source_records = []
    for component, record in (
        ("shaderc", linux_lock["shaderc_release"]),
        ("glslang", linux_lock["dependencies"]["glslang"]),
        ("spirv-tools", linux_lock["dependencies"]["spirv_tools"]),
        ("spirv-headers", linux_lock["dependencies"]["spirv_headers"]),
    ):
        expected_source_records.append(
            {
                "component": component,
                "repository": record["repository"],
                "version": record[
                    "tag" if component == "shaderc" else "version"
                ],
                "commit": record["commit"],
                "archive_sha256": record["archive_sha256"],
                "license_expression": record["license_expression"],
                "license_sha256": record["license_sha256"],
                "package_notice_path": record["package_notice_path"],
                "package_notice_sha256": record["package_notice_sha256"],
            }
        )
    artifacts = manifest.get("artifacts")
    expected_artifacts = (
        ("shaderc_combined", "libshaderc_combined.a"),
        ("shaderc", "libshaderc.a"),
        ("shaderc_util", "libshaderc_util.a"),
        ("glslang", "libglslang.a"),
        ("SPIRV", "libSPIRV.a"),
        ("SPIRV-Tools-opt", "libSPIRV-Tools-opt.a"),
        ("SPIRV-Tools-static", "libSPIRV-Tools.a"),
    )
    artifact_inventory_valid = isinstance(artifacts, list) and len(artifacts) == 7
    if artifact_inventory_valid:
        artifact_inventory_valid = all(
            artifact.get("target") == target
            and artifact.get("file") == filename
            and isinstance(artifact.get("sha256"), str)
            and re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]) is not None
            for artifact, (target, filename) in zip(artifacts, expected_artifacts)
        )
    source_lock = manifest.get("source_lock", {})
    compiler = manifest.get("compiler", {})
    host = manifest.get("host", {})
    checks = {
        "schema": manifest.get("schema")
        == "ror.ogre_next_linux_static_closure.v1",
        "status": manifest.get("status") == "pass",
        "provider": manifest.get("provider") == "pinned-source",
        "platform": manifest.get("platform_policy")
        == "linux-x86_64-vulkan",
        "source_lock": source_lock
        == {
            "schema": "ror.ogre_next_linux_shader_toolchain.v1",
            "sha256": LINUX_SHADER_TOOLCHAIN_LOCK_SHA256,
            "package_path": (
                "provenance/ogre-next-linux-shader-toolchain.lock.json"
            ),
        },
        "compiler": isinstance(compiler.get("id"), str)
        and bool(compiler["id"])
        and isinstance(compiler.get("version"), str)
        and bool(compiler["version"])
        and compiler.get("build_type") == REQUIRED_CONFIG,
        "host": host.get("system") == "Linux"
        and host.get("processor") in {"AMD64", "amd64", "x86_64"},
        "sources": manifest.get("sources") == expected_source_records,
        "artifacts": artifact_inventory_valid,
        "dynamic_boundary": manifest.get("host_dynamic_boundary")
        == "Vulkan-Loader",
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "Linux shader static closure manifest failed closed: "
            + ", ".join(sorted(failed))
        )


def _n3_image_metrics(
    payload: bytes, width: int, height: int
) -> dict[str, int | float | str]:
    expected_bytes = width * height * 8
    if width <= 0 or height <= 0 or len(payload) != expected_bytes:
        raise ProbeError("Metal N3 RGBA16F artifact extent/byte count differs")
    nontrivial = 0
    luminance_sum = 0.0
    for offset in range(0, len(payload), 8):
        channels = struct.unpack_from("<4e", payload, offset)
        if not all(math.isfinite(channel) for channel in channels):
            raise ProbeError("Metal N3 RGBA16F artifact contains non-finite data")
        luminance_sum += (
            0.2126 * channels[0]
            + 0.7152 * channels[1]
            + 0.0722 * channels[2]
        )
        if any(abs(channel) > 1.0e-6 for channel in channels[:3]):
            nontrivial += 1
    return {
        "width": width,
        "height": height,
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "mean_luminance": luminance_sum / (width * height),
        "nontrivial_pixels": nontrivial,
    }


def _validate_n3_reported_metrics(
    reported: dict[str, Any], computed: dict[str, int | float | str], label: str
) -> None:
    checks = {
        "width": reported.get("width") == computed["width"],
        "height": reported.get("height") == computed["height"],
        "format": reported.get("format") == "RGBA16_FLOAT",
        "bytes": reported.get("bytes") == computed["bytes"],
        "sha256": reported.get("sha256") == computed["sha256"],
        "nontrivial_pixels": reported.get("nontrivial_pixels")
        == computed["nontrivial_pixels"],
        "mean_luminance": isinstance(reported.get("mean_luminance"), (int, float))
        and math.isclose(
            float(reported["mean_luminance"]),
            float(computed["mean_luminance"]),
            rel_tol=1.0e-9,
            abs_tol=1.0e-12,
        ),
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            f"Metal N3 {label} metrics failed closed: "
            + ", ".join(sorted(failed))
        )


def validate_n1_package_provenance(
    build_dir: Path,
    lock: dict[str, Any],
    linux_lock: dict[str, Any],
    policy: dict[str, str],
) -> None:
    package_root = build_dir / N1_PACKAGE_NAME
    expected_common = {
        "licenses/Rigs-of-Rods-GPL-3.0.txt": sha256_file(
            REPOSITORY_ROOT / "COPYING"
        ),
        "licenses/Ogre-Next-MIT.txt": lock["license"]["sha256"],
        "licenses/RapidJSON-license.txt": lock["dependencies"]["rapidjson"][
            "license_sha256"
        ],
        lock["dependencies"]["freetype"]["package_license_path"]: lock[
            "dependencies"
        ]["freetype"]["license_sha256"],
        lock["dependencies"]["freetype"]["package_overview_path"]: lock[
            "dependencies"
        ]["freetype"]["overview_sha256"],
        lock["shader_media"]["third_party_notice"]["notice_path"]: lock[
            "shader_media"
        ]["third_party_notice"]["notice_sha256"],
        lock["reflection_shader_media"]["third_party_notice"][
            "package_path"
        ]: lock["reflection_shader_media"]["third_party_notice"][
            "source_sha256"
        ],
    }
    expected = dict(expected_common)
    if policy["name"] == "linux-x86_64-vulkan":
        for record in (
            linux_lock["shaderc_release"],
            linux_lock["dependencies"]["glslang"],
            linux_lock["dependencies"]["spirv_tools"],
            linux_lock["dependencies"]["spirv_headers"],
        ):
            expected[record["package_notice_path"]] = record[
                "package_notice_sha256"
            ]
    for relative_path, expected_hash in expected.items():
        staged_path = package_root / relative_path
        if not staged_path.is_file():
            raise ProbeError(f"N1 package notice is missing: {relative_path}")
        if sha256_file(staged_path) != expected_hash:
            raise ProbeError(f"N1 package notice SHA-256 mismatch: {relative_path}")

    if policy["name"] != "linux-x86_64-vulkan":
        return
    packaged_lock = (
        package_root
        / "provenance"
        / "ogre-next-linux-shader-toolchain.lock.json"
    )
    if (
        not packaged_lock.is_file()
        or sha256_file(packaged_lock) != LINUX_SHADER_TOOLCHAIN_LOCK_SHA256
        or packaged_lock.read_bytes()
        != LINUX_SHADER_TOOLCHAIN_LOCK_PATH.read_bytes()
    ):
        raise ProbeError("N1 package Linux shader source lock is missing or changed")

    source_manifest = build_dir / LINUX_STATIC_CLOSURE_MANIFEST_NAME
    packaged_manifest = (
        package_root / "provenance" / LINUX_STATIC_CLOSURE_MANIFEST_NAME
    )
    if not source_manifest.is_file() or not packaged_manifest.is_file():
        raise ProbeError("N1 package Linux static closure manifest is missing")
    if source_manifest.read_bytes() != packaged_manifest.read_bytes():
        raise ProbeError("N1 package Linux static closure manifest changed in staging")
    try:
        manifest = json.loads(packaged_manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(
            f"could not read Linux static closure manifest: {error}"
        ) from error
    validate_linux_static_closure_manifest(manifest, linux_lock)


def validate_linux_dynamic_boundary(
    build_dir: Path,
    *,
    require_frame_probe: bool,
    require_packaged_frontend: bool,
) -> None:
    package_bin = build_dir / N1_PACKAGE_NAME / "bin"
    executables: list[Path] = []
    if require_frame_probe:
        executables.append(build_dir / "bin" / "ror_ogre_next_frame_probe")
    if require_packaged_frontend:
        if not package_bin.is_dir():
            raise ProbeError("N1 package Linux executable directory is missing")
        packaged_executables = sorted(
            path for path in package_bin.iterdir() if path.is_file()
        )
        if len(packaged_executables) != 1:
            raise ProbeError("N1 package must contain exactly one Linux executable")
        executables.append(packaged_executables[0])
    if not executables:
        raise ProbeError("Linux linkage audit has no required executable")
    forbidden = re.compile(
        r"lib(?:freetype|shaderc|glslang|SPIRV(?:-Tools(?:-opt)?)?|"
        r"MachineIndependent|GenericCodeGen|OSDependent)",
        re.IGNORECASE,
    )
    for executable in executables:
        if not executable.is_file():
            raise ProbeError(f"Linux linkage input is missing: {executable}")
        try:
            result = subprocess.run(
                ["ldd", str(executable)],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
        except (OSError, subprocess.CalledProcessError) as error:
            raise ProbeError(f"could not audit Linux linkage: {executable}") from error
        match = forbidden.search(result.stdout)
        if match:
            raise ProbeError(
                "Linux executable crossed a pinned static dependency "
                f"boundary: {match.group(0)}"
            )


def validate_n3_checkpoint(
    report: dict[str, Any],
    raster_path: Path | None,
    contribution_path: Path | None,
    hybrid_path: Path | None,
    executable_path: Path,
    lock: dict[str, Any],
    policy: dict[str, str],
    expected_source_commit: str,
    expected_source_ref: str,
    expected_source_manifest_sha256: str,
) -> None:
    if policy["name"] != "macos-arm64-metal":
        raise ProbeError("Metal N3 validation is Apple-only")
    if re.fullmatch(r"[0-9a-f]{40}", expected_source_commit) is None:
        raise ProbeError("expected N3 RoR source commit is not a full Git SHA")
    _require_sha256(
        expected_source_manifest_sha256,
        "expected Metal N3 relevant-source manifest",
    )
    try:
        executable_bytes = executable_path.stat().st_size
        executable_sha256 = sha256_file(executable_path)
    except OSError as error:
        raise ProbeError(f"could not attest Metal N3 executable: {error}") from error

    def object_field(name: str) -> dict[str, Any]:
        value = report.get(name, {})
        return value if isinstance(value, dict) else {}

    provenance = object_field("provenance")
    status = report.get("status")
    common_checks = {
        "schema": report.get("schema") == "ror.ogre_next_metal_rt_n3.v2",
        "status": status in ("pass", "skip"),
        "scope": report.get("scope")
        == (
            "same-device Metal primary-ray hit contribution composited into "
            "exact UI-free Ogre-Next HDR target; no GI, reflection, denoising, "
            "multi-bounce, or material parity claim"
        )
        if status == "pass"
        else isinstance(report.get("scope"), str),
        "ror_repository": provenance.get("ror_repository")
        == ROR_SOURCE_REPOSITORY,
        "ror_ref": provenance.get("ror_ref") == expected_source_ref,
        "ror_commit": provenance.get("ror_commit") == expected_source_commit,
        "relevant_source_clean": provenance.get("relevant_source_clean") is True,
        "relevant_source_manifest": provenance.get(
            "relevant_source_manifest_sha256"
        )
        == expected_source_manifest_sha256,
        "ogre_commit": provenance.get("ogre_next_commit") == lock["commit"],
        "build_artifact": provenance.get("build_artifact")
        == executable_path.name,
        "build_artifact_bytes": provenance.get("build_artifact_bytes")
        == executable_bytes
        and executable_bytes > 0,
        "build_artifact_sha256": provenance.get("build_artifact_sha256")
        == executable_sha256,
    }
    failed = [name for name, passed in common_checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next Metal N3 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )
    if status == "skip":
        if any(
            path is not None and path.exists()
            for path in (raster_path, contribution_path, hybrid_path)
        ):
            raise ProbeError("skipped Metal N3 checkpoint emitted image artifacts")
        if not isinstance(report.get("reason"), str) or not report["reason"]:
            raise ProbeError("skipped Metal N3 checkpoint has no reason")
        return

    if raster_path is None or contribution_path is None or hybrid_path is None:
        raise ProbeError("passed Metal N3 checkpoint is missing an image artifact")
    try:
        raster = raster_path.read_bytes()
        contribution = contribution_path.read_bytes()
        hybrid = hybrid_path.read_bytes()
    except OSError as error:
        raise ProbeError(f"could not read Metal N3 image artifacts: {error}") from error
    raster_metrics = _n3_image_metrics(raster, 96, 64)
    contribution_metrics = _n3_image_metrics(contribution, 96, 64)
    hybrid_metrics = _n3_image_metrics(hybrid, 96, 64)
    _validate_n3_reported_metrics(
        object_field("raster_only_hdr"), raster_metrics, "raster"
    )
    _validate_n3_reported_metrics(
        object_field("rt_contribution"), contribution_metrics, "contribution"
    )
    _validate_n3_reported_metrics(
        object_field("hybrid_hdr"), hybrid_metrics, "hybrid"
    )

    applied = 0
    untouched = 0
    for offset in range(0, len(raster), 8):
        raster_values = struct.unpack_from("<4e", raster, offset)
        contribution_values = struct.unpack_from("<4e", contribution, offset)
        hybrid_values = struct.unpack_from("<4e", hybrid, offset)
        contribution_channels = struct.unpack_from("<4H", contribution, offset)
        applies = any((channel & 0x7FFF) != 0 for channel in contribution_channels[:3])
        if contribution_channels[3] != 0:
            raise ProbeError("Metal N3 contribution changed straight alpha")
        if hybrid[offset + 6 : offset + 8] != raster[offset + 6 : offset + 8]:
            raise ProbeError("Metal N3 hybrid changed raster alpha")
        if applies:
            applied += 1
            if hybrid[offset : offset + 6] == raster[offset : offset + 6]:
                raise ProbeError("Metal N3 contribution did not change hybrid RGB")
            for channel in range(3):
                expected = max(
                    -65504.0,
                    min(65504.0, raster_values[channel] + contribution_values[channel]),
                )
                if not math.isclose(
                    hybrid_values[channel],
                    expected,
                    rel_tol=2.0e-3,
                    abs_tol=5.0e-4,
                ):
                    raise ProbeError(
                        "Metal N3 hybrid RGB is not the reported GPU contribution"
                    )
        else:
            untouched += 1
            if hybrid[offset : offset + 8] != raster[offset : offset + 8]:
                raise ProbeError("Metal N3 changed a pixel outside its contribution")

    device = object_field("device")
    contract = object_field("contract")
    raster_contract = object_field("raster_contract")
    proof = object_field("proof")
    second = object_field("second_view_contribution")
    resized = object_field("resized_hybrid")
    second_sha256 = second.get("sha256")
    resized_sha256 = resized.get("sha256")
    if not isinstance(second_sha256, str) or not isinstance(resized_sha256, str):
        raise ProbeError("Metal N3 follow-up image hashes are missing")
    _require_sha256(second_sha256, "Metal N3 second-view contribution")
    _require_sha256(resized_sha256, "Metal N3 resized hybrid")
    texture_allocations = raster_contract.get("texture_allocations")
    live_allocations = (
        texture_allocations.get("live")
        if isinstance(texture_allocations, dict)
        else None
    )
    shutdown_allocations = (
        texture_allocations.get("after_shutdown")
        if isinstance(texture_allocations, dict)
        else None
    )
    pass_checks = {
        "device": isinstance(device.get("name"), str)
        and bool(device["name"])
        and device.get("same_ogre_device") is True
        and device.get("same_ogre_queue") is True
        and device.get("apple_family_9") is True,
        "image_contract": type(contract.get("image_version")) is int
        and contract.get("image_version") == 2
        and type(contract.get("image_generation")) is int
        and contract["image_generation"] > 0
        and contract.get("usage")
        == "COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE"
        and contract.get("release_state") == "GENERAL_READ_WRITE"
        and contract.get("return_state") == "GENERAL_READ_WRITE",
        "simultaneous_raster_contract": raster_contract.get(
            "raster_feature_tier"
        )
        == "MODERN_PBR_RT4_V1"
        and raster_contract.get("native_feature_tier")
        == "METAL_RAY_TRACING_N3"
        and raster_contract.get("vertex_layout")
        == "POSITION_NORMAL_TANGENT_UV0_FLOAT32_48"
        and type(raster_contract.get("vertex_stride_bytes")) is int
        and raster_contract.get("vertex_stride_bytes") == 48
        and raster_contract.get("authored_tangent_uv0") is True
        and raster_contract.get("base_color_texture") == "RGBA8_UNORM_SRGB"
        and type(raster_contract.get("directional_light_lux")) is int
        and raster_contract.get("directional_light_lux") == 1024
        and raster_contract.get("ray_material_parity_claimed") is False,
        "texture_allocation_contract": isinstance(live_allocations, dict)
        and live_allocations
        == {
            "source_textures": 1,
            "sampled_rgba": 1,
            "roughness_r8": 0,
            "metallic_r8": 0,
            "creates": 1,
            "destroys": 0,
            "live": 1,
            "exact_usage": True,
        }
        and live_allocations.get("exact_usage") is True
        and isinstance(shutdown_allocations, dict)
        and shutdown_allocations
        == {
            "creates": 1,
            "destroys": 1,
            "live": 0,
            "retired_name_lookups": 1,
            "retired_name_rejections": 1,
        },
        "distinct_artifacts": len(
            {
                raster_metrics["sha256"],
                contribution_metrics["sha256"],
                hybrid_metrics["sha256"],
            }
        )
        == 3
        and int(raster_metrics["nontrivial_pixels"]) > 0
        and int(contribution_metrics["nontrivial_pixels"]) > 0
        and int(hybrid_metrics["nontrivial_pixels"]) > 0,
        "mapping": applied > 0
        and untouched > 0
        and type(proof.get("contribution_pixels")) is int
        and proof.get("contribution_pixels") == applied,
        "far_plane_edge": type(
            proof.get("off_axis_far_plane_contribution_pixels")
        )
        is int
        and proof["off_axis_far_plane_contribution_pixels"] > 0,
        "second_view": second.get("width") == 96
        and second.get("height") == 64
        and second.get("format") == "RGBA16_FLOAT"
        and second.get("bytes") == 96 * 64 * 8
        and second_sha256 != contribution_metrics["sha256"]
        and type(second.get("nontrivial_pixels")) is int
        and second["nontrivial_pixels"] > 0,
        "resize": resized.get("width") == 80
        and resized.get("height") == 48
        and resized.get("format") == "RGBA16_FLOAT"
        and resized.get("bytes") == 80 * 48 * 8
        and type(resized.get("nontrivial_pixels")) is int
        and resized["nontrivial_pixels"] > 0,
        "proof": all(
            proof.get(field) is True
            for field in (
                "exact_exported_vertex_slice_used",
                "exact_exported_index_slice_used",
                "exact_exported_color_image_used",
                "gpu_composite_not_cpu_postprocess",
                "hybrid_changes_only_on_contribution",
                "all_channels_finite",
                "second_camera_changes_contribution_hash",
                "camera_mismatch_rejected",
                "snapshot_transform_mismatch_rejected",
                "off_axis_far_plane_hit_passed",
                "released_frame_allows_extent_change",
                "submitted_device_loss_and_timeout_paths_tested",
                "simultaneous_rt4_n3",
                "textured_rt4_geometry_rendered",
                "calibrated_directional_light_applied",
                "exact_48_byte_vertex_layout_exported",
                "texture_allocation_audit_exact",
                "texture_teardown_audit_exact",
                "view_dependent_output_ready",
                "hybrid_composite_ready",
            )
        ),
    }
    failed = [name for name, passed in pass_checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next Metal N3 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )


def run_n3_checkpoint(
    build_dir: Path,
    config: str,
    jobs: int,
    lock: dict[str, Any],
    policy: dict[str, str],
) -> None:
    if policy["name"] != "macos-arm64-metal":
        return
    source_commit, source_ref, source_manifest_sha256 = repository_identity()
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_metal_n3_report",
            "--config",
            config,
            "--parallel",
            str(jobs),
        ]
    )
    report_path = build_dir / N3_REPORT_NAME
    candidates = {
        "raster": build_dir / N3_RASTER_NAME,
        "contribution": build_dir / N3_CONTRIBUTION_NAME,
        "hybrid": build_dir / N3_HYBRID_NAME,
    }
    artifacts = {
        name: path if path.is_file() else None for name, path in candidates.items()
    }
    executable_candidates = (
        build_dir / "bin" / "ror_ogre_next_metal_n3_smoke",
        build_dir / "bin" / config / "ror_ogre_next_metal_n3_smoke",
    )
    executable_path = next(
        (candidate for candidate in executable_candidates if candidate.is_file()),
        executable_candidates[0],
    )
    missing = [path.name for path in (report_path, executable_path) if not path.is_file()]
    if missing:
        raise ProbeError(
            "OGRE-Next Metal N3 checkpoint did not produce required artifacts: "
            + ", ".join(missing)
        )
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read Metal N3 report: {error}") from error
    if repository_identity() != (
        source_commit,
        source_ref,
        source_manifest_sha256,
    ):
        raise ProbeError("RoR source provenance changed while building Metal N3")
    validate_n3_checkpoint(
        report,
        artifacts["raster"],
        artifacts["contribution"],
        artifacts["hybrid"],
        executable_path,
        lock,
        policy,
        source_commit,
        source_ref,
        source_manifest_sha256,
    )

    def attest(path: Path | None) -> dict[str, Any] | None:
        if path is None:
            return None
        return {
            "path": path.name,
            "bytes": path.stat().st_size,
            "sha256": sha256_file(path),
        }

    attestation = {
        "schema": "ror.ogre_next_metal_rt_n3.attestation.v1",
        "status": report.get("status"),
        "source": {
            "ror_commit": source_commit,
            "ror_ref": source_ref,
            "relevant_source_clean": True,
            "relevant_source_manifest_sha256": source_manifest_sha256,
        },
        "executable": attest(executable_path),
        "report": attest(report_path),
        "raster_only_hdr": attest(artifacts["raster"]),
        "rt_contribution": attest(artifacts["contribution"]),
        "hybrid_hdr": attest(artifacts["hybrid"]),
    }
    attestation_path = build_dir / N3_ATTESTATION_NAME
    temporary = attestation_path.with_suffix(".json.tmp")
    try:
        temporary.write_text(
            json.dumps(attestation, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temporary.replace(attestation_path)
        persisted = json.loads(attestation_path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ProbeError(f"could not write Metal N3 attestation: {error}") from error
    except json.JSONDecodeError as error:
        raise ProbeError(f"could not verify Metal N3 attestation: {error}") from error
    if persisted != attestation:
        raise ProbeError("persisted Metal N3 attestation differs from verified data")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=default_build_dir(),
        help="fresh standalone CMake build directory outside the source checkout",
    )
    parser.add_argument(
        "--clean-build-dir",
        action="store_true",
        help="clean a non-empty build directory only when its probe sentinel matches",
    )
    parser.add_argument(
        "--reuse-build-dir",
        action="store_true",
        help="reuse an owned configured build for a later independent checkpoint",
    )
    parser.add_argument(
        "--checkpoint",
        choices=("all", "n1", "n2", "n3", "legacy"),
        default="all",
        help=(
            "run all gates, an independent N1/N2/N3 gate, or the legacy probes"
        ),
    )
    parser.add_argument(
        "--ogre-archive",
        type=Path,
        help="optional already-downloaded pinned OGRE-Next archive",
    )
    parser.add_argument(
        "--rapidjson-archive",
        type=Path,
        help="optional already-downloaded pinned RapidJSON archive",
    )
    parser.add_argument(
        "--freetype-archive",
        type=Path,
        help="optional already-downloaded pinned FreeType archive",
    )
    parser.add_argument("--generator", help="optional CMake generator")
    parser.add_argument(
        "--config",
        choices=(REQUIRED_CONFIG,),
        default=REQUIRED_CONFIG,
        help="reviewed build configuration (Release only)",
    )
    parser.add_argument(
        "--jobs", type=int, default=max(1, min(os.cpu_count() or 1, 8))
    )
    parser.add_argument(
        "--validate-contract-only",
        action="store_true",
        help="validate checked-in pins and platform policy without network/build",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.jobs <= 0:
            raise ProbeError("--jobs must be positive")
        lock = load_lock()
        normal_map_source_lock = load_normal_map_source_lock()
        linux_shader_lock = load_linux_shader_toolchain_lock()
        policy = detect_policy(platform.system(), platform.machine())
        if args.validate_contract_only:
            print(
                json.dumps(
                    {
                        "schema_version": 2,
                        "status": "pass",
                        "commit": lock["commit"],
                        "normal_map_source_lock_sha256": (
                            NORMAL_MAP_SOURCE_LOCK_SHA256
                        ),
                        "normal_map_source_owner_count": len(
                            normal_map_source_lock["sources"]
                        ),
                        "linux_shader_source_lock_sha256": (
                            LINUX_SHADER_TOOLCHAIN_LOCK_SHA256
                        ),
                        "linux_shader_provider": linux_shader_lock["provider"],
                        "platform_policy": policy["name"],
                        "configuration": REQUIRED_CONFIG,
                        "network_used": False,
                    },
                    indent=2,
                    sort_keys=True,
                )
            )
            return 0

        source_identity = ror_source_identity()

        if args.reuse_build_dir and args.checkpoint == "all":
            raise ProbeError(
                "--reuse-build-dir requires --checkpoint n1, n2, n3, or legacy"
            )
        if args.reuse_build_dir and (
            args.ogre_archive
            or args.rapidjson_archive
            or args.freetype_archive
            or args.generator
        ):
            raise ProbeError(
                "reused checkpoints cannot change archives or the generator"
            )
        build_dir = prepare_build_dir(
            args.build_dir, args.clean_build_dir, args.reuse_build_dir
        )
        configure = [
            "cmake",
            "-S",
            str(PROBE_SOURCE),
            "-B",
            str(build_dir),
            "-DROR_OGRE_NEXT_PROBE=ON",
            f"-DCMAKE_BUILD_TYPE={args.config}",
        ]
        if args.generator:
            configure.extend(["-G", args.generator])
        elif shutil.which("ninja"):
            configure.extend(["-G", "Ninja"])

        if policy["name"] == "macos-arm64-metal":
            configure.extend(
                [
                    "-DCMAKE_OSX_ARCHITECTURES=arm64",
                    "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0",
                ]
            )
        if args.ogre_archive:
            archive = verify_archive(
                args.ogre_archive, lock["archive_sha256"], "OGRE-Next"
            )
            configure.append(f"-DROR_OGRE_NEXT_ARCHIVE={archive}")
        if args.rapidjson_archive:
            rapidjson_lock = lock["dependencies"]["rapidjson"]
            archive = verify_archive(
                args.rapidjson_archive,
                rapidjson_lock["archive_sha256"],
                "RapidJSON",
            )
            configure.append(f"-DROR_RAPIDJSON_ARCHIVE={archive}")
        if args.freetype_archive:
            freetype_lock = lock["dependencies"]["freetype"]
            archive = verify_archive(
                args.freetype_archive,
                freetype_lock["archive_sha256"],
                "FreeType",
            )
            configure.append(f"-DROR_FREETYPE_ARCHIVE={archive}")

        if not args.reuse_build_dir:
            run(configure)

        build_contract_path = build_dir / BUILD_CONTRACT_NAME
        try:
            build_contract = json.loads(
                build_contract_path.read_text(encoding="utf-8")
            )
        except (OSError, json.JSONDecodeError) as error:
            raise ProbeError(f"could not read build contract: {error}") from error
        validate_build_contract(build_contract, lock, policy, source_identity)

        if args.checkpoint in ("all", "n1"):
            run_n1_checkpoint(
                build_dir,
                args.config,
                args.jobs,
                lock,
                policy,
                source_identity,
            )
            if policy["name"] == "linux-x86_64-vulkan":
                run(
                    [
                        "cmake",
                        "--build",
                        str(build_dir),
                        "--target",
                        "ror_ogre_next_linux_static_closure_verify",
                        "--config",
                        args.config,
                        "--parallel",
                        str(args.jobs),
                    ]
                )
                require_source_identity_unchanged(source_identity)
            validate_n1_package_provenance(
                build_dir, lock, linux_shader_lock, policy
            )
            if policy["name"] == "linux-x86_64-vulkan":
                validate_linux_dynamic_boundary(
                    build_dir,
                    require_frame_probe=False,
                    require_packaged_frontend=True,
                )

        if args.checkpoint in ("all", "n2"):
            run_n2_checkpoint(
                build_dir,
                args.config,
                args.jobs,
                lock,
                policy,
            )
            require_source_identity_unchanged(source_identity)

        if args.checkpoint in ("all", "n3"):
            run_n3_checkpoint(
                build_dir,
                args.config,
                args.jobs,
                lock,
                policy,
            )
            require_source_identity_unchanged(source_identity)

        report: dict[str, Any] = {
            "schema_version": 2,
            "status": "pass",
            "checkpoint": args.checkpoint,
            "platform_policy": policy["name"],
        }
        if args.checkpoint in ("all", "legacy"):
            run(
                [
                    "cmake",
                    "--build",
                    str(build_dir),
                    "--target",
                    "ror_ogre_next_probe_report",
                    "--config",
                    args.config,
                    "--parallel",
                    str(args.jobs),
                ]
            )
            require_source_identity_unchanged(source_identity)
            report_path = build_dir / REPORT_NAME
            try:
                report = json.loads(report_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as error:
                raise ProbeError(f"could not read probe report: {error}") from error
            validate_report(report, lock, policy)
            run_frame_checkpoint(
                build_dir,
                args.config,
                args.jobs,
                policy,
                report_path,
            )
            require_source_identity_unchanged(source_identity)
            if policy["name"] == "linux-x86_64-vulkan":
                run(
                    [
                        "cmake",
                        "--build",
                        str(build_dir),
                        "--target",
                        "ror_ogre_next_linux_static_closure_verify",
                        "--config",
                        args.config,
                        "--parallel",
                        str(args.jobs),
                    ]
                )
                require_source_identity_unchanged(source_identity)
                closure_path = build_dir / LINUX_STATIC_CLOSURE_MANIFEST_NAME
                try:
                    closure = json.loads(closure_path.read_text(encoding="utf-8"))
                except (OSError, json.JSONDecodeError) as error:
                    raise ProbeError(
                        f"could not read Linux static closure manifest: {error}"
                    ) from error
                validate_linux_static_closure_manifest(
                    closure, linux_shader_lock
                )
                validate_linux_dynamic_boundary(
                    build_dir,
                    require_frame_probe=True,
                    require_packaged_frontend=(
                        args.checkpoint == "all" or args.reuse_build_dir
                    ),
                )
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except ProbeError as error:
        print(f"OGRE-Next probe failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
