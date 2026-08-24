#!/usr/bin/env python3
"""Fail unless every required OGRE-Next CI artifact exists and is nonempty."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import struct
import sys


TOOLS_ROOT = Path(__file__).resolve().parent
if str(TOOLS_ROOT) not in sys.path:
    sys.path.insert(0, str(TOOLS_ROOT))

from ogre_next_probe.validate_child_runtime_receipt import (  # noqa: E402
    RECEIPT_NAME as CHILD_RUNTIME_RECEIPT_ARTIFACT,
    STDERR_LOG_NAME as CHILD_RUNTIME_STDERR_ARTIFACT,
    STDOUT_LOG_NAME as CHILD_RUNTIME_STDOUT_ARTIFACT,
    ReceiptValidationError as ChildReceiptValidationError,
    expected_child_relative,
    validate_receipt as validate_child_runtime_receipt,
)


REQUIRED_ARTIFACTS = (
    "ogre-next-build-contract.json",
    "ror-ogre-next-probe-report.json",
    "ror-ogre-next-frame-probe-report.json",
    "ror-ogre-next-frame-probe.ppm",
    "ror-ogre-next-frontend-n1-report.json",
    "ror-ogre-next-frontend-n1.ppm",
    "ror-ogre-next-frontend-rt4-pbr-v1-report.json",
    "ror-ogre-next-frontend-rt4-pbr-v1.ppm",
    "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-analytic-sky.ppm",
    "ror-ogre-next-frontend-rt4-pbr-v1-analytic-sky.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-repeat/ror-ogre-next-frontend-rt4-pbr-v1-report.json",
    "ror-ogre-next-frontend-rt4-pbr-v1-repeat/ror-ogre-next-frontend-rt4-pbr-v1.ppm",
    "ror-ogre-next-frontend-rt4-pbr-v1-repeat/ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-repeat/ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-repeat/ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-repeat/ror-ogre-next-frontend-rt4-pbr-v1-analytic-sky.ppm",
    "ror-ogre-next-frontend-rt4-pbr-v1-repeat/ror-ogre-next-frontend-rt4-pbr-v1-analytic-sky.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json",
    "ror-ogre-next-pssm-shadow-report.json",
)
PSSM_REPORT_ARTIFACT = "ror-ogre-next-pssm-shadow-report.json"
PSSM_EVIDENCE_ARTIFACT = "ror-ogre-next-pssm-shadow-isolation.bin"
PSSM_EXECUTION_RECEIPT_ARTIFACT = (
    "ror-ogre-next-pssm-shadow-execution-receipt.json"
)
PSSM_ATTESTATION_ARTIFACT = "ror-ogre-next-pssm-shadow-attestation.json"
PSSM_ARTIFACT_MANIFEST_ARTIFACT = (
    "ror-ogre-next-pssm-shadow-artifact-manifest.json"
)
PSSM_EXECUTABLE_STEM = "ror_ogre_next_pssm_shadow_smoke"
PSSM_REPORT_SCHEMA = "ror.ogre_next_pssm_shadow_smoke.v5"
PSSM_EXECUTION_SCHEMA = "ror.ogre_next_pssm_shadow_execution_challenge.v1"
PSSM_EXECUTION_RECEIPT_SCHEMA = (
    "ror.ogre_next_pssm_shadow_execution_receipt.v1"
)
PSSM_ATTESTATION_SCHEMA = "ror.ogre_next_pssm_shadow_attestation.v1"
PSSM_ARTIFACT_MANIFEST_SCHEMA = (
    "ror.ogre_next_pssm_shadow_artifact_manifest.v1"
)
PSSM_OFFLINE_EXECUTION_LIMITATION = (
    "hashes, binary structure, report semantics, and a fresh challenge can be "
    "verified offline, but the receipt is not a cryptographic proof that its "
    "executable ran; require the GitHub artifact attestation for that receipt"
)
PSSM_UNSUPPORTED_DETAIL = (
    "PSSM_3_CASCADE_V1 native capability gate rejected the required atlas "
    "dimensions or PCF4 texture-gather support"
)
RT4_REPORT_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1-report.json"
RT4_PPM_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1.ppm"
RT4_ISOLATION_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin"
RT4_REFLECTION_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin"
RT4_COMPOSITOR_ARTIFACT = (
    "ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin"
)
RT4_ANALYTIC_SKY_PPM_ARTIFACT = (
    "ror-ogre-next-frontend-rt4-pbr-v1-analytic-sky.ppm"
)
RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT = (
    "ror-ogre-next-frontend-rt4-pbr-v1-analytic-sky.bin"
)
RT4_REPEAT_DIRECTORY = "ror-ogre-next-frontend-rt4-pbr-v1-repeat"
RT4_REPEAT_REPORT_ARTIFACT = f"{RT4_REPEAT_DIRECTORY}/{RT4_REPORT_ARTIFACT}"
RT4_REPEAT_PPM_ARTIFACT = f"{RT4_REPEAT_DIRECTORY}/{RT4_PPM_ARTIFACT}"
RT4_REPEAT_ISOLATION_ARTIFACT = (
    f"{RT4_REPEAT_DIRECTORY}/{RT4_ISOLATION_ARTIFACT}"
)
RT4_REPEAT_REFLECTION_ARTIFACT = (
    f"{RT4_REPEAT_DIRECTORY}/{RT4_REFLECTION_ARTIFACT}"
)
RT4_REPEAT_COMPOSITOR_ARTIFACT = (
    f"{RT4_REPEAT_DIRECTORY}/{RT4_COMPOSITOR_ARTIFACT}"
)
RT4_REPEAT_ANALYTIC_SKY_PPM_ARTIFACT = (
    f"{RT4_REPEAT_DIRECTORY}/{RT4_ANALYTIC_SKY_PPM_ARTIFACT}"
)
RT4_REPEAT_ANALYTIC_SKY_EVIDENCE_ARTIFACT = (
    f"{RT4_REPEAT_DIRECTORY}/{RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT}"
)
RT4_ATTESTATION_ARTIFACT = (
    "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json"
)
RT4_PACKAGE_EXECUTABLE_STEM = "ror_ogre_next_frontend_n1_smoke"
FREETYPE_PACKAGE_LICENSE_CONTRACT = (
    (
        "licenses/FreeType-GPLv2.txt",
        "c4120c6752c910c299e3bd9cb3a46ff262c268303ca2069b61f92f10a5656c18",
    ),
    (
        "licenses/FreeType-LICENSE.txt",
        "bd36c8b474855fa294c2ec5c184544478ef3720aad37d65a6296a4f264fd2d3b",
    ),
)
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
PINNED_LOCK_PATH = REPOSITORY_ROOT / "tools/ogre_next_probe/ogre-next.lock.json"
DISPLAY_DOMAIN_MEDIA_PATH = (
    REPOSITORY_ROOT
    / "tools/ogre_next_probe/media/Hlms/RoR/DisplayDomain/DisplayDomain_piece_ps.any"
)
DISPLAY_DOMAIN_MEDIA_RELATIVE = (
    "Hlms/RoR/DisplayDomain/DisplayDomain_piece_ps.any"
)
DISPLAY_DOMAIN_NOTICE_PATH = "licenses/Rigs-of-Rods-GPL-3.0.txt"
DISPLAY_DOMAIN_LICENSE_EXPRESSION = "GPL-3.0-or-later"
DISPLAY_DOMAIN_NOTICE_SOURCE = REPOSITORY_ROOT / "COPYING"
NORMAL_MAP_SOURCE_LOCK_PATH = (
    REPOSITORY_ROOT
    / "tools/ogre_next_probe/ogre-next-normal-map-source.lock.json"
)
NORMAL_MAP_SOURCE_LOCK_SHA256 = (
    "7d180c54c54e7cc26b0081753c621b7164551d2b631c1127f818fbb22645f682"
)
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
    "cmake/conan/recipes/ogre3d/patches/14.5.2/allow-hidden-offline-cocoa-gl-context.patch",
    "cmake/conan/recipes/ogre3d/patches/14.5.2/always-lock-log-output.patch",
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
    "source/main/system/CVar.cpp",
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
    "source/main/gfx/render/ogrenext/OgreNextUvAffinePbs.cpp",
    "source/main/gfx/render/ogrenext/OgreNextUvAffinePbs.h",
    "tools/ogre_next_probe/media/Hlms/RoR/UvAffinePbs/UvAffinePbs_piece_ps.any",
    "tools/ogre_next_probe/ogre-next-uv0-affine-pbs-v1.lock.json",
    "tools/ogre_next_probe/verify_uv0_affine_pbs_shader.py",
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
RT4_ATTESTATION_SCHEMA = (
    "ror.ogre_next_frontend_rt4_pbr_v1.attestation.v4"
)
RT4_INTEGRITY_MODEL = (
    "self-contained-checksums-plus-independent-semantics; "
    "not-a-cryptographic-signature"
)
RT4_REPORT_SCHEMA = "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v4"
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
BASE_EXECUTABLE_IDENTITY_SCHEMA = (
    "ror.ogre_next_frontend_n1.build_identity.v1"
)
RT4_EXECUTABLE_IDENTITY_SCHEMA = (
    "ror.ogre_next_frontend_n1.build_identity.v2"
)
RT4_EXPECTED_VARIANTS = (
    ("baseline", "none"),
    ("base_color", "base_color_rgb"),
    ("roughness_g", "packed_green_roughness"),
    ("metallic_b", "packed_blue_metallic"),
    ("emissive", "emissive_rgb"),
    ("normal_rg", "canonical_positive_z_normal_rg"),
    ("uv0_affine", "shared_uv0_scale_offset"),
    ("sampler_uv", "sampler_address_over_uv0"),
)
RT4_EXPECTED_RETIREMENT = {
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
        {"revision": 1, "width": 2, "height": 2, "mip_levels": 1},
        {
            "revision": 2,
            "width": 4,
            "height": 2,
            "mip_levels": 2,
            "padded_rows": True,
        },
        {"revision": 3, "width": 2, "height": 2, "mip_levels": 1},
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
}
RT4_EXPECTED_TEXTURE_ALLOCATIONS = {
    "version": 2,
    "live_source_textures": 4,
    "sampled_rgba_allocations": 2,
    "linear_rgba_allocations": 0,
    "roughness_r8_allocations": 1,
    "metallic_r8_allocations": 1,
    "normal_rg8_allocations": 1,
    "unused_packed_rgba_allocations": 0,
    "exact_usage": True,
}
RT4_EXPECTED_LIFECYCLE = {
    "unsupported_depth_failed_before_submission": True,
    "non_uniform_scale_skips_instance_not_frame": True,
    "double_sided_pbs_readback": True,
    "lifetime_snapshot_identity_replay": True,
    "lifetime_completed_frame_queries": True,
    "process_global_root_exclusion": True,
    "live_texture_replacement_retirement": True,
    "replacement_audit": {
        "creates": 17,
        "destroys": 12,
        "live": 5,
        "retired_name_lookups": 12,
        "retired_name_rejections": 12,
        "exact_usage": True,
    },
    "shutdown_reinitialize_render_shutdown": True,
}
RT4_ROLLBACK_STAGES = (
    "after_create",
    "after_set_resolution",
    "after_set_mipmaps",
    "after_set_pixel_format",
    "after_schedule_transition",
)
RT4_EXPECTED_TEXTURE_UPLOAD_ROLLBACK = {
    "schema": "ror.ogre_next_rt4_texture_upload_rollback.v1",
    "derived_allocation": "normal_RG8_UNORM",
    "injected_post_create_stage_count": len(RT4_ROLLBACK_STAGES),
    "stages": [
        {
            "name": name,
            "audits": {
                "after_failure": {
                    "creates": 1,
                    "destroys": 1,
                    "live": 0,
                    "retired_name_lookups": 1,
                    "retired_name_rejections": 1,
                    "exact_usage": True,
                },
                "after_retry": {
                    "creates": 2,
                    "destroys": 1,
                    "live": 1,
                    "retired_name_lookups": 1,
                    "retired_name_rejections": 1,
                    "exact_usage": True,
                },
                "after_replacement": {
                    "creates": 3,
                    "destroys": 2,
                    "live": 1,
                    "retired_name_lookups": 2,
                    "retired_name_rejections": 2,
                    "exact_usage": True,
                },
                "after_shutdown": {
                    "creates": 3,
                    "destroys": 3,
                    "live": 0,
                    "retired_name_lookups": 3,
                    "retired_name_rejections": 3,
                    "exact_usage": False,
                },
            },
        }
        for name in RT4_ROLLBACK_STAGES
    ],
    "clean_retry_replacement_shutdown": True,
}
PLATFORM_CONTRACTS = {
    "macos-arm64-metal": {
        "systems": {"Darwin"},
        "processors": {"arm64", "aarch64"},
        "renderer_target": "RenderSystem_Metal",
        "renderer_name": "Metal Rendering Subsystem",
        "device_option_name": "Rendering Device",
        "compiler_ids": {"AppleClang"},
        "binary_format": "mach-o-64",
        "binary_architecture": "arm64",
    },
    "windows-x64-d3d11": {
        "systems": {"Windows"},
        "processors": {"AMD64", "amd64", "x86_64"},
        "renderer_target": "RenderSystem_Direct3D11",
        "renderer_name": "Direct3D11 Rendering Subsystem",
        "device_option_name": "Rendering Device",
        "compiler_ids": {"MSVC"},
        "binary_format": "pe32+",
        "binary_architecture": "x86_64",
    },
    "linux-x86_64-vulkan": {
        "systems": {"Linux"},
        "processors": {"AMD64", "amd64", "x86_64"},
        "renderer_target": "RenderSystem_Vulkan",
        "renderer_name": "Vulkan Rendering Subsystem",
        "device_option_name": "Device",
        "compiler_ids": {"GNU", "Clang"},
        "binary_format": "elf64",
        "binary_architecture": "x86_64",
    },
}
METAL_N2_REQUIRED_ARTIFACTS = (
    "ror-ogre-next-metal-n2-report.json",
    "ror-ogre-next-metal-n2-attestation.json",
    "bin/ror_ogre_next_metal_n2_smoke",
)
METAL_N2_PROBE_ARTIFACT = "ror-ogre-next-metal-n2-probe.bin"
METAL_N3_REQUIRED_ARTIFACTS = (
    "ror-ogre-next-metal-n3-report.json",
    "ror-ogre-next-metal-n3-attestation.json",
    "bin/ror_ogre_next_metal_n3_smoke",
)
METAL_N3_IMAGE_ARTIFACTS = (
    ("raster_only_hdr", "ror-ogre-next-metal-n3-raster.bin"),
    ("rt_contribution", "ror-ogre-next-metal-n3-contribution.bin"),
    ("hybrid_hdr", "ror-ogre-next-metal-n3-hybrid.bin"),
)
METAL_N3_SCOPE = (
    "same-device Metal primary-ray hit contribution composited into exact "
    "UI-free Ogre-Next HDR target; no GI, reflection, denoising, multi-bounce, "
    "or material parity claim"
)
METAL_N3_REQUIRED_PROOF_BOOLEANS = (
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
METAL_N4_REQUIRED_ARTIFACTS = (
    "ror-ogre-next-metal-n4-directional-shadow-report.json",
    "bin/ror_ogre_next_metal_n4_directional_shadow_smoke",
)
METAL_N4_IMAGE_ARTIFACTS = (
    ("raster", "ror-ogre-next-metal-n4-raster.bin", "RGBA16_FLOAT", 8),
    ("visibility", "ror-ogre-next-metal-n4-visibility-r16.bin", "R16_FLOAT", 2),
    (
        "ray_lineage",
        "ror-ogre-next-metal-n4-ray-lineage-r32.bin",
        "R32_UINT",
        4,
    ),
    ("hybrid", "ror-ogre-next-metal-n4-hybrid.bin", "RGBA16_FLOAT", 8),
)
METAL_N4_PASS_SCOPE = (
    "same-device Metal two-BLAS hard directional visibility applied to the "
    "exact UI-free Ogre-Next HDR target; no GI, reflection, denoising, "
    "multi-bounce, soft-shadow, or material-parity claim"
)
METAL_N4_SKIP_SCOPE = (
    "same-device Metal two-BLAS directional hard shadow; no GI, reflection, "
    "denoising, multi-bounce, soft-shadow, or material-parity claim"
)
METAL_N4_REQUIRED_PROOF_BOOLEANS = (
    "full_view_receiver",
    "partial_distinct_occluder",
    "every_visibility_texel_canonical_r16",
    "visible_preserves_exact_rgba16",
    "occluded_zeros_rgb_preserves_alpha",
    "visible_and_occluded_sample_contracts_validated",
    "exact_exported_dual_geometry_used",
    "exact_exported_color_image_used",
    "gpu_composite_not_cpu_postprocess",
    "consecutive_identical_scene_bytes_stable",
    "occluder_motion_changes_only_shadow_outputs",
    "released_frame_allows_extent_change",
    "submitted_device_loss_and_timeout_paths_tested",
    "view_dependent_output_ready",
    "hybrid_composite_ready",
)


class ArtifactSetError(RuntimeError):
    """Raised when a required artifact is missing, empty, or indirect."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _expected_build_shader_media(lock: dict[str, object]) -> dict[str, object]:
    source = DISPLAY_DOMAIN_MEDIA_PATH
    notice = DISPLAY_DOMAIN_NOTICE_SOURCE
    if source.is_symlink() or not source.is_file():
        raise ArtifactSetError(
            "legacy display-domain shader source is missing or indirect"
        )
    if notice.is_symlink() or not notice.is_file():
        raise ArtifactSetError(
            "legacy display-domain GPL notice is missing or indirect"
        )
    locked = lock.get("shader_media")
    if not isinstance(locked, dict):
        raise ArtifactSetError("pinned shader-media contract is invalid")
    expected = dict(locked)
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


def _read_json_object(path: Path, label: str) -> dict[str, object]:
    def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                raise ArtifactSetError(
                    f"invalid {label}: duplicate JSON object key {key!r}"
                )
            result[key] = value
        return result

    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ArtifactSetError(f"invalid {label}: {error}") from error
    if not isinstance(value, dict):
        raise ArtifactSetError(f"invalid {label}: root is not an object")
    return value


def _verify_attested_file(
    entry: object,
    path: Path,
    expected_name: str,
    checkpoint: str,
    label: str,
) -> None:
    if not isinstance(entry, dict):
        raise ArtifactSetError(f"invalid {checkpoint} {label} attestation")
    expected = {
        "path": expected_name,
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }
    if not _json_exact(entry, expected):
        raise ArtifactSetError(f"{checkpoint} {label} attestation mismatch")


def _json_exact(actual: object, expected: object) -> bool:
    """Compare JSON values without Python's bool/int equality aliasing."""
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return set(actual) == set(expected) and all(
            _json_exact(actual[key], value) for key, value in expected.items()
        )
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(
            _json_exact(left, right) for left, right in zip(actual, expected)
        )
    return actual == expected


def _require_exact_keys(
    value: object, expected: set[str], label: str
) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != expected:
        raise ArtifactSetError(f"{label} fields are incomplete or unexpected")
    return value


def _relevant_source_manifest(
    repository_root: Path = REPOSITORY_ROOT,
) -> dict[str, int | str]:
    selected: set[Path] = set()
    for relative in RELEVANT_SOURCE_PATHS:
        path = repository_root / relative
        if path.is_symlink():
            raise ArtifactSetError(
                f"RoR relevant source is indirect: {relative}"
            )
        if path.is_dir():
            selected.update(path.rglob("*"))
        else:
            selected.add(path)
    entries: list[tuple[str, int, str]] = []
    for path in sorted(selected, key=lambda item: item.as_posix()):
        try:
            relative = path.relative_to(repository_root)
        except ValueError as error:
            raise ArtifactSetError("RoR relevant source escaped repository") from error
        if "__pycache__" in relative.parts or path.suffix in (".pyc", ".pyo"):
            continue
        if path.name == ".DS_Store":
            continue
        if path.is_symlink():
            raise ArtifactSetError(
                "RoR relevant source is indirect: " + relative.as_posix()
            )
        if path.is_dir():
            continue
        if not path.is_file():
            raise ArtifactSetError(
                "RoR relevant source is missing or irregular: "
                + relative.as_posix()
            )
        entries.append(
            (relative.as_posix(), path.stat().st_size, sha256_file(path))
        )
    if not entries:
        raise ArtifactSetError("RoR relevant source manifest is empty")
    serialized = "".join(
        f"{relative}|{size}|{digest}\n"
        for relative, size, digest in entries
    ).encode("utf-8")
    return {
        "sha256": hashlib.sha256(serialized).hexdigest(),
        "file_count": len(entries),
    }


def _git_output(repository_root: Path, *arguments: str) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(repository_root), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except OSError as error:
        raise ArtifactSetError(
            f"could not execute Git for RoR provenance: {error}"
        ) from error
    value = result.stdout.strip()
    if result.returncode != 0 or not value:
        raise ArtifactSetError("could not resolve RoR Git provenance")
    return value


def _current_source_identity(
    repository_root: Path = REPOSITORY_ROOT,
    expected_repository: str | None = None,
    expected_ref: str | None = None,
    expected_commit: str | None = None,
) -> dict[str, object]:
    repository = (
        expected_repository
        or os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_REPOSITORY")
        or ROR_SOURCE_REPOSITORY
    )
    commit = (
        expected_commit
        or os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT")
        or os.environ.get("GITHUB_SHA")
        or _git_output(repository_root, "rev-parse", "HEAD")
    )
    ref = (
        expected_ref
        or os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_REF")
        or _git_output(repository_root, "rev-parse", "--abbrev-ref", "HEAD")
    )
    if re.fullmatch(r"[0-9a-f]{40}", commit) is None or re.fullmatch(
        r"[A-Za-z0-9._/-]+", ref
    ) is None or repository != ROR_SOURCE_REPOSITORY:
        raise ArtifactSetError("RoR Git provenance is not canonical")
    git_commit = _git_output(repository_root, "rev-parse", "HEAD")
    if commit != git_commit:
        raise ArtifactSetError("expected RoR commit differs from checked-out source")
    manifest = _relevant_source_manifest(repository_root)
    return {
        "repository": repository,
        "ref": ref,
        "commit": commit,
        "relevant_manifest_sha256": manifest["sha256"],
        "relevant_manifest_file_count": manifest["file_count"],
    }


def _read_pinned_lock() -> dict[str, object]:
    lock = _read_json_object(PINNED_LOCK_PATH, "pinned OGRE-Next lock")
    if (
        type(lock.get("schema_version")) is not int
        or lock.get("schema_version") != 6
        or lock.get("name") != "OGRE-Next"
    ):
        raise ArtifactSetError("pinned OGRE-Next lock identity is invalid")
    expected_embedded = {
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
                "bbb329c68e98a9a8e8c61783601d219d6f5ac2545fe8f4f346be0445b302d47d"
            ),
        },
    }
    embedded = lock.get("embedded_namespace")
    if embedded != expected_embedded:
        raise ArtifactSetError("pinned embedded namespace identity is invalid")
    for key in ("patch", "remap_header"):
        value = embedded.get(key)
        if not isinstance(value, dict) or not isinstance(value.get("path"), str):
            raise ArtifactSetError("pinned embedded namespace input is invalid")
        path = PINNED_LOCK_PATH.parent / value["path"]
        if path.is_symlink() or not path.is_file():
            raise ArtifactSetError("pinned embedded namespace input is indirect")
        if not _is_sha256(value.get("sha256")) or sha256_file(path) != value["sha256"]:
            raise ArtifactSetError("pinned embedded namespace input hash mismatch")
    return lock


def _read_build_contract(
    root: Path, expected_source: dict[str, object]
) -> dict[str, object]:
    contract = _read_json_object(
        root / REQUIRED_ARTIFACTS[0], "OGRE-Next build contract"
    )
    schema_version = contract.get("schema_version")
    expected_contract_keys = {
        "schema_version",
        "ror_source",
        "provenance",
        "patches",
        "dependencies",
        "shader_media",
        "reflection_shader_media",
        "platform",
        "abi",
        "components",
        "compiler",
    }
    if schema_version == 7:
        expected_contract_keys.add("embedded_namespace")
    _require_exact_keys(
        contract,
        expected_contract_keys,
        "OGRE-Next build contract",
    )
    ror_source = contract.get("ror_source")
    ogre_source = contract.get("provenance")
    shader_media = contract.get("shader_media")
    reflection_shader_media = contract.get("reflection_shader_media")
    patches = contract.get("patches")
    platform = contract.get("platform")
    dependencies = contract.get("dependencies")
    abi = contract.get("abi")
    components = contract.get("components")
    compiler = contract.get("compiler")
    notice = (
        shader_media.get("third_party_notice")
        if isinstance(shader_media, dict)
        else None
    )
    if (
        type(contract.get("schema_version")) is not int
        or contract.get("schema_version") not in (5, 6, 7)
        or not isinstance(ror_source, dict)
        or not isinstance(ogre_source, dict)
        or not isinstance(ror_source.get("repository"), str)
        or not ror_source["repository"]
        or not isinstance(ror_source.get("ref"), str)
        or not ror_source["ref"]
        or not isinstance(ror_source.get("commit"), str)
        or re.fullmatch(r"[0-9a-f]{40}", ror_source["commit"]) is None
        or not _is_sha256(ror_source.get("relevant_manifest_sha256"))
        or not _is_positive_int(ror_source.get("relevant_manifest_file_count"))
        or not isinstance(ogre_source.get("repository"), str)
        or not ogre_source["repository"]
        or not isinstance(ogre_source.get("branch"), str)
        or not ogre_source["branch"]
        or not isinstance(ogre_source.get("commit"), str)
        or re.fullmatch(r"[0-9a-f]{40}", ogre_source["commit"]) is None
        or not _is_sha256(ogre_source.get("archive_sha256"))
        or not isinstance(ogre_source.get("license_spdx"), str)
        or not ogre_source["license_spdx"]
        or not _is_sha256(ogre_source.get("license_sha256"))
        or not isinstance(shader_media, dict)
        or not isinstance(shader_media.get("root"), str)
        or not shader_media["root"]
        or not isinstance(shader_media.get("license_expression"), str)
        or not shader_media["license_expression"]
        or not isinstance(notice, dict)
        or not isinstance(notice.get("source_path"), str)
        or not notice["source_path"]
        or not _is_sha256(notice.get("source_sha256"))
        or not isinstance(notice.get("notice_path"), str)
        or not notice["notice_path"]
        or not _is_sha256(notice.get("notice_sha256"))
        or not isinstance(reflection_shader_media, dict)
        or not isinstance(patches, list)
        or not isinstance(platform, dict)
        or platform.get("policy")
        not in PLATFORM_CONTRACTS
    ):
        raise ArtifactSetError("OGRE-Next build contract source identity is invalid")

    lock = _read_pinned_lock()
    policy = PLATFORM_CONTRACTS[platform["policy"]]
    rapidjson = lock.get("dependencies", {}).get("rapidjson", {})
    freetype = lock.get("dependencies", {}).get("freetype", {})
    lock_abi = lock.get("abi_contract", {})
    expected_abi = {
        key: value for key, value in lock_abi.items() if key != "simd"
    }
    expected_simd = lock_abi.get("simd", {}).get(platform["policy"])
    expected_abi.update(
        {
            "simd_enabled": lock_abi.get("simd", {}).get("enabled"),
            "simd_alignment": lock_abi.get("simd", {}).get("alignment"),
            "simd_family": expected_simd,
            "simd_neon": expected_simd == "neon",
            "simd_sse2": expected_simd == "sse2",
        }
    )
    expected_dependencies = {
        "freetype": {
            "repository": freetype.get("repository"),
            "version": freetype.get("version"),
            "archive_url": freetype.get("archive_url"),
            "archive_sha256": freetype.get("archive_sha256"),
            "license_expression": freetype.get("license_expression"),
            "selected_license_spdx": freetype.get("selected_license_spdx"),
            "license_path": freetype.get("license_path"),
            "license_sha256": freetype.get("license_sha256"),
            "package_license_path": freetype.get("package_license_path"),
            "overview_path": freetype.get("overview_path"),
            "overview_sha256": freetype.get("overview_sha256"),
            "package_overview_path": freetype.get("package_overview_path"),
            "target": "freetype",
            "target_type": "STATIC_LIBRARY",
            "static_link": True,
            "overlay_link_target": True,
            "disabled_optional_dependencies": freetype.get(
                "disabled_optional_dependencies"
            ),
        },
        "rapidjson": {
            "tag": rapidjson.get("tag"),
            "archive_sha256": rapidjson.get("archive_sha256"),
            "source_archive_license_spdx": rapidjson.get("license_spdx"),
            "compiled_headers_license_spdx": rapidjson.get(
                "compiled_headers_spdx"
            ),
            "license_sha256": rapidjson.get("license_sha256"),
        }
    }
    expected_components = {
        "hlms_pbs": True,
        "hlms_unlit": True,
        "overlay": True,
        "compositor2_core": True,
        "json_materials": True,
        "mesh_lod": True,
        "dds_codec": True,
        "hdr_temporal_contract_version": 2,
        "hdr_history_validation_mode": (
            "native_authoritative_conditioning_plus_one_r16_ulp_v2"
        ),
        "hdr_workspace": "RoRHdrWorkspaceHudV1",
        "hdr_visual_evidence_version": 1,
        "headless_child_bootstrap": True,
        "headless_child_output_name": "RoR-OgreNext",
        "headless_child_packaged": False,
        "headless_child_production_admitted": False,
        "native_ray_tracing": "not_evaluated",
    }
    if contract.get("schema_version") in (6, 7):
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
    expected_platform = {
        "policy": platform["policy"],
        "system": platform.get("system"),
        "processor": platform.get("processor"),
        "renderer_target": policy["renderer_target"],
        "device_option_name": policy["device_option_name"],
    }
    expected_ogre = {
        "repository": lock.get("repository"),
        "branch": lock.get("branch"),
        "commit": lock.get("commit"),
        "archive_sha256": lock.get("archive_sha256"),
        "license_spdx": lock.get("license", {}).get("spdx"),
        "license_sha256": lock.get("license", {}).get("sha256"),
    }
    compiler_valid = (
        isinstance(compiler, dict)
        and set(compiler) == {"id", "version", "build_type"}
        and compiler.get("id") in policy["compiler_ids"]
        and isinstance(compiler.get("version"), str)
        and re.fullmatch(r"[A-Za-z0-9.+_-]+", compiler["version"]) is not None
        and compiler.get("build_type") == "Release"
    )
    exact_checks = {
        "source": _json_exact(ror_source, expected_source),
        "ogre": _json_exact(ogre_source, expected_ogre),
        "dependencies": _json_exact(dependencies, expected_dependencies),
        "shader_media": _json_exact(
            shader_media, _expected_build_shader_media(lock)
        ),
        "reflection_shader_media": _json_exact(
            reflection_shader_media, lock.get("reflection_shader_media")
        ),
        "patches": _json_exact(patches, lock.get("patches")),
        "platform": _json_exact(platform, expected_platform)
        and platform.get("system") in policy["systems"]
        and platform.get("processor") in policy["processors"],
        "abi": _json_exact(abi, expected_abi),
        "components": _json_exact(components, expected_components),
        "compiler": compiler_valid,
    }
    if contract.get("schema_version") == 7:
        embedded = lock.get("embedded_namespace", {})
        embedded_contract = contract.get("embedded_namespace")
        enabled = (
            embedded_contract.get("enabled")
            if isinstance(embedded_contract, dict)
            else None
        )
        exact_checks["embedded_namespace"] = (
            type(enabled) is bool
            and _json_exact(
                embedded_contract,
                {
                    "enabled": enabled,
                    "namespace": embedded.get("namespace"),
                    "cmake_option": embedded.get("cmake_option"),
                    "default_enabled": embedded.get("default_enabled"),
                    "patch": {
                        **embedded.get("patch", {}),
                        "applied": enabled,
                    },
                    "remap_header": {
                        **embedded.get("remap_header", {}),
                        "forced_include": enabled,
                    },
                    "full_n1_link_evidence": "not_evaluated",
                },
            )
        )
    failed = sorted(name for name, passed in exact_checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            "OGRE-Next build contract identity mismatch: " + ", ".join(failed)
        )
    return contract


def _verify_freetype_package_licenses(
    root: Path, manifest: list[dict[str, object]]
) -> None:
    package_root = root / "ror-ogre-next-n1-package"
    for relative, expected_sha256 in FREETYPE_PACKAGE_LICENSE_CONTRACT:
        path = package_root / relative
        artifact_relative = f"ror-ogre-next-n1-package/{relative}"
        if path.is_symlink() or not path.is_file():
            raise ArtifactSetError(
                f"FreeType package license is missing or indirect: {relative}"
            )
        size = path.stat().st_size
        if size <= 0:
            raise ArtifactSetError(f"FreeType package license is empty: {relative}")
        if sha256_file(path) != expected_sha256:
            raise ArtifactSetError(
                f"FreeType package license hash mismatch: {relative}"
            )
        manifest.append(
            {
                "path": artifact_relative,
                "bytes": size,
                "sha256": expected_sha256,
            }
        )


def _is_positive_int(value: object) -> bool:
    return type(value) is int and value > 0


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{64}", value) is not None
    )


def _packaged_media_manifest(
    base: Path, scan_roots: tuple[Path, ...], label: str
) -> dict[str, object]:
    entries_by_path: dict[str, tuple[str, int, str, Path]] = {}
    for relative_root in scan_roots:
        tree = base / relative_root
        if tree.is_symlink() or not tree.is_dir():
            raise ArtifactSetError(
                f"{label} media root is missing or symbolic: "
                + relative_root.as_posix()
            )
        for path in sorted(tree.rglob("*"), key=lambda item: item.as_posix()):
            relative = path.relative_to(base).as_posix()
            if path.is_symlink():
                raise ArtifactSetError(
                    f"{label} media contains a symbolic link: {relative}"
                )
            if path.is_dir():
                continue
            if not path.is_file():
                raise ArtifactSetError(
                    f"{label} media contains a non-file entry: {relative}"
                )
            entries_by_path[relative] = (
                relative,
                path.stat().st_size,
                sha256_file(path),
                path,
            )
    entries = [entries_by_path[key] for key in sorted(entries_by_path)]
    if not entries:
        raise ArtifactSetError(f"{label} media closure is empty")
    serialized = "".join(
        f"{relative}|{size}|{digest}\n"
        for relative, size, digest, _ in entries
    ).encode("utf-8")
    return {
        "sha256": hashlib.sha256(serialized).hexdigest(),
        "file_count": len(entries),
        "entries": entries,
    }


def _fnv1a64(payload: bytes) -> str:
    value = 14695981039346656037
    for byte in payload:
        value ^= byte
        value = (value * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"


def _expected_build_identity(
    schema: str,
    build_contract: dict[str, object], report: dict[str, object]
) -> str:
    platform = build_contract["platform"]
    compiler = build_contract["compiler"]
    source = build_contract["ror_source"]
    ogre = build_contract["provenance"]
    provenance = report.get("provenance")
    if not all(
        isinstance(value, dict)
        for value in (platform, compiler, source, ogre, provenance)
    ):
        raise ArtifactSetError("executable build identity inputs are missing")
    return (
        f"{schema}"
        f"|platform={platform['policy']}"
        f"|compiler={compiler['id']}-{compiler['version']}-{compiler['build_type']}"
        f"|ror_commit={source['commit']}"
        f"|ror_manifest={source['relevant_manifest_sha256']}"
        f"|ogre_commit={ogre['commit']}"
        f"|ogre_archive={ogre['archive_sha256']}"
        "|shader_manifest="
        f"{provenance.get('shader_media_manifest_sha256')}"
    )


def _expected_base_build_identity(
    build_contract: dict[str, object], report: dict[str, object]
) -> str:
    return _expected_build_identity(
        BASE_EXECUTABLE_IDENTITY_SCHEMA, build_contract, report
    )


def _expected_rt4_build_identity(
    build_contract: dict[str, object], report: dict[str, object]
) -> str:
    provenance = report.get("provenance")
    if not isinstance(provenance, dict):
        raise ArtifactSetError("RT4 executable build identity inputs are missing")
    return (
        _expected_build_identity(
            RT4_EXECUTABLE_IDENTITY_SCHEMA, build_contract, report
        )
        + "|hdr_media_manifest="
        + str(provenance.get("hdr_media_manifest_sha256"))
        + "|hdr_temporal_contract=2"
        + "|hdr_history_validation="
        + "native_authoritative_conditioning_plus_one_r16_ulp_v2"
        + "|hdr_workspace=RoRHdrWorkspaceHudV1"
    )


def _verify_mach_o_64(payload: bytes) -> dict[str, str]:
    if len(payload) < 32:
        raise ArtifactSetError("RT4 executable has a truncated Mach-O header")
    magic, cpu, _, file_type, command_count, command_bytes, _, _ = (
        struct.unpack_from("<IiiIIIII", payload, 0)
    )
    if (
        magic != 0xFEEDFACF
        or cpu != 0x0100000C
        or file_type != 2
        or command_count < 2
        or command_bytes < 96
        or 32 + command_bytes > len(payload)
    ):
        raise ArtifactSetError("RT4 executable is not an arm64 Mach-O executable")
    offset = 32
    has_executable_text = False
    has_entrypoint = False
    for _ in range(command_count):
        if offset + 8 > 32 + command_bytes:
            raise ArtifactSetError("RT4 Mach-O load commands are truncated")
        command, command_size = struct.unpack_from("<II", payload, offset)
        if command_size < 8 or offset + command_size > 32 + command_bytes:
            raise ArtifactSetError("RT4 Mach-O load command layout is invalid")
        if command == 0x19 and command_size >= 72:  # LC_SEGMENT_64
            values = struct.unpack_from("<II16sQQQQiiII", payload, offset)
            segment = values[2].split(b"\0", 1)[0]
            file_offset, file_size = values[5], values[6]
            initial_protection = values[8]
            if (
                segment == b"__TEXT"
                and initial_protection & 0x4
                and file_size > 0
                and file_offset + file_size <= len(payload)
            ):
                has_executable_text = True
        if command == 0x80000028 and command_size >= 24:  # LC_MAIN
            entry_offset = struct.unpack_from("<Q", payload, offset + 8)[0]
            if 0 < entry_offset < len(payload):
                has_entrypoint = True
        offset += command_size
    if offset != 32 + command_bytes or not has_executable_text or not has_entrypoint:
        raise ArtifactSetError("RT4 Mach-O executable structure is incomplete")
    return {"format": "mach-o-64", "architecture": "arm64"}


def _verify_pe32_plus(payload: bytes) -> dict[str, str]:
    if len(payload) < 0x100 or payload[:2] != b"MZ":
        raise ArtifactSetError("RT4 executable has an invalid PE DOS header")
    pe_offset = struct.unpack_from("<I", payload, 0x3C)[0]
    if pe_offset + 24 > len(payload) or payload[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ArtifactSetError("RT4 executable has an invalid PE signature")
    machine, section_count, _, _, _, optional_size, characteristics = (
        struct.unpack_from("<HHIIIHH", payload, pe_offset + 4)
    )
    optional_offset = pe_offset + 24
    if (
        machine != 0x8664
        or not 1 <= section_count <= 96
        or optional_size < 112
        or optional_offset + optional_size > len(payload)
        or characteristics & 0x0002 == 0
        or characteristics & 0x2000 != 0
        or struct.unpack_from("<H", payload, optional_offset)[0] != 0x20B
        or struct.unpack_from("<I", payload, optional_offset + 16)[0] == 0
    ):
        raise ArtifactSetError("RT4 executable is not an x64 PE32+ executable")
    section_offset = optional_offset + optional_size
    has_executable_code = False
    for index in range(section_count):
        offset = section_offset + index * 40
        if offset + 40 > len(payload):
            raise ArtifactSetError("RT4 PE section table is truncated")
        raw_size, raw_offset = struct.unpack_from("<II", payload, offset + 16)
        flags = struct.unpack_from("<I", payload, offset + 36)[0]
        if (
            flags & 0x20
            and flags & 0x20000000
            and raw_size > 0
            and raw_offset + raw_size <= len(payload)
        ):
            has_executable_code = True
    if not has_executable_code:
        raise ArtifactSetError("RT4 PE executable has no executable code section")
    return {"format": "pe32+", "architecture": "x86_64"}


def _verify_elf64(payload: bytes) -> dict[str, str]:
    if (
        len(payload) < 64
        or payload[:4] != b"\x7fELF"
        or payload[4] != 2
        or payload[5] != 1
        or payload[6] != 1
    ):
        raise ArtifactSetError("RT4 executable has an invalid ELF64 header")
    file_type, machine = struct.unpack_from("<HH", payload, 16)
    entrypoint, program_offset = struct.unpack_from("<QQ", payload, 24)
    program_entry_size, program_count = struct.unpack_from("<HH", payload, 54)
    if (
        file_type not in (2, 3)
        or machine != 62
        or entrypoint == 0
        or program_entry_size < 56
        or program_count == 0
        or program_offset + program_entry_size * program_count > len(payload)
    ):
        raise ArtifactSetError("RT4 executable is not an x86_64 ELF executable")
    has_executable_load = False
    for index in range(program_count):
        offset = program_offset + index * program_entry_size
        segment_type, flags = struct.unpack_from("<II", payload, offset)
        file_offset = struct.unpack_from("<Q", payload, offset + 8)[0]
        file_size = struct.unpack_from("<Q", payload, offset + 32)[0]
        if (
            segment_type == 1
            and flags & 0x1
            and file_size > 0
            and file_offset + file_size <= len(payload)
        ):
            has_executable_load = True
    if not has_executable_load:
        raise ArtifactSetError("RT4 ELF executable has no executable load segment")
    return {"format": "elf64", "architecture": "x86_64"}


def _requires_posix_executable_permission(
    binary_format: str, host_os_name: str | None = None
) -> bool:
    """Return whether this host can meaningfully enforce Unix execute bits."""
    effective_os_name = os.name if host_os_name is None else host_os_name
    return effective_os_name == "posix" and binary_format in (
        "mach-o-64",
        "elf64",
    )


def _verify_rt4_executable(
    path: Path,
    build_contract: dict[str, object],
    report: dict[str, object],
) -> None:
    size = path.stat().st_size
    if size < 64 * 1024 or size > 512 * 1024 * 1024:
        raise ArtifactSetError("RT4 executable byte count is structurally implausible")
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(f"could not read RT4 executable: {error}") from error
    policy_name = build_contract["platform"]["policy"]
    policy = PLATFORM_CONTRACTS[policy_name]
    verifier = {
        "mach-o-64": _verify_mach_o_64,
        "pe32+": _verify_pe32_plus,
        "elf64": _verify_elf64,
    }[policy["binary_format"]]
    structure = verifier(payload)
    expected_structure = {
        "format": policy["binary_format"],
        "architecture": policy["binary_architecture"],
    }
    if structure != expected_structure:
        raise ArtifactSetError("RT4 executable platform structure mismatch")
    # Windows filesystems do not carry POSIX executable mode bits.  The PE
    # policy is already validated structurally above, and a Windows-hosted
    # unit test may intentionally exercise a synthetic Mach-O/ELF fixture.
    # Enforce Unix execute permission only on hosts where that metadata exists;
    # never reinterpret a missing Windows mode bit as evidence about the
    # packaged foreign binary.
    if _requires_posix_executable_permission(policy["binary_format"]) and (
        path.stat().st_mode & 0o111 == 0
    ):
        raise ArtifactSetError("RT4 packaged executable has no execute permission")
    identity = _expected_rt4_build_identity(build_contract, report)
    if payload.count(identity.encode()) != 1:
        raise ArtifactSetError(
            "RT4 executable build identity is missing or ambiguous"
        )
    required_tokens = (
        RT4_REPORT_SCHEMA,
        "--modern-pbr",
        policy["renderer_name"],
        '\"raster_feature_tier\": \"MODERN_PBR_RT4_V1\"',
        "linear_RGBA8_positive_Z_to_RG8_UNORM",
    )
    missing = [token for token in required_tokens if token.encode() not in payload]
    if missing:
        raise ArtifactSetError(
            "RT4 executable build identity is missing or ambiguous"
        )


def _changed_pixels(
    baseline: bytes, variant: bytes, bytes_per_pixel: int
) -> int:
    if (
        bytes_per_pixel <= 0
        or len(baseline) != len(variant)
        or len(baseline) % bytes_per_pixel != 0
    ):
        raise ArtifactSetError("RT4 isolation attachment layout is invalid")
    return sum(
        baseline[offset : offset + bytes_per_pixel]
        != variant[offset : offset + bytes_per_pixel]
        for offset in range(0, len(baseline), bytes_per_pixel)
    )


def _quantize_unit_float(value: float) -> int:
    return int(math.floor(max(0.0, min(1.0, value)) * 255.0 + 0.5))


def _attachment_metrics(payload: bytes, hdr: bool) -> dict[str, object]:
    bytes_per_pixel = 8 if hdr else 4
    if len(payload) == 0 or len(payload) % bytes_per_pixel != 0:
        raise ArtifactSetError("RT4 attachment byte layout is invalid")
    rgb = bytearray()
    colour_counts: dict[bytes, int] = {}
    minimum_luminance = math.inf
    maximum_luminance = -math.inf
    if hdr:
        pixels = struct.iter_unpack("<4e", payload)
    else:
        pixels = (
            tuple(payload[offset : offset + 4])
            for offset in range(0, len(payload), 4)
        )
    for channels in pixels:
        if hdr:
            if not all(math.isfinite(channel) for channel in channels):
                raise ArtifactSetError("RT4 HDR isolation contains non-finite data")
            if any(channel < 0.0 for channel in channels[:3]):
                raise ArtifactSetError("RT4 HDR isolation contains negative RGB energy")
            if not 0.99 <= channels[3] <= 1.01:
                raise ArtifactSetError("RT4 HDR isolation alpha is not opaque")
            linear = channels[:3]
            quantized = bytes(_quantize_unit_float(value) for value in linear)
        else:
            if channels[3] < 250:
                raise ArtifactSetError("RT4 SDR isolation alpha is not opaque")
            linear = tuple(value / 255.0 for value in channels[:3])
            quantized = bytes(channels[:3])
        rgb.extend(quantized)
        colour_counts[quantized] = colour_counts.get(quantized, 0) + 1
        luminance = (
            0.2126 * linear[0]
            + 0.7152 * linear[1]
            + 0.0722 * linear[2]
        )
        minimum_luminance = min(minimum_luminance, luminance)
        maximum_luminance = max(maximum_luminance, luminance)
    pixel_count = len(payload) // bytes_per_pixel
    return {
        "exact_attachment_fnv1a64": _fnv1a64(payload),
        "rgb8_fnv1a64": _fnv1a64(bytes(rgb)),
        "distinct_rgb8_values": len(colour_counts),
        "non_background_pixels": pixel_count - max(colour_counts.values()),
        "minimum_luminance": minimum_luminance,
        "maximum_luminance": maximum_luminance,
        "rgb": bytes(rgb),
    }


def _reported_metric_matches(reported: object, computed: float) -> bool:
    return (
        isinstance(reported, (int, float))
        and not isinstance(reported, bool)
        and math.isfinite(float(reported))
        and math.isclose(
            float(reported), computed, rel_tol=2.0e-7, abs_tol=2.0e-7
        )
    )


def _is_nonzero_u64_hex(value: object) -> bool:
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{16}", value) is not None
        and value != "0" * 16
    )


def _is_bounded_evidence_string(value: object) -> bool:
    return (
        isinstance(value, str)
        and 0 < len(value) <= 512
        and "\x00" not in value
    )


def _reflection_half_metrics(
    payload: bytes, label: str
) -> dict[str, int | float]:
    if not payload or len(payload) % 8 != 0:
        raise ArtifactSetError(f"RT4 {label} RGBA16F layout is invalid")
    finite_components = 0
    nonzero_rgb_components = 0
    max_absolute_rgb = 0.0
    for channels in struct.iter_unpack("<4e", payload):
        if not all(math.isfinite(channel) for channel in channels):
            raise ArtifactSetError(
                f"RT4 {label} reflection evidence contains non-finite data"
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


def _verify_rt4_reflection_semantics(
    report: dict[str, object],
    reflection_path: Path,
    build_contract: dict[str, object],
) -> list[dict[str, object]]:
    try:
        evidence = reflection_path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(
            f"could not read RT4 reflection evidence: {error}"
        ) from error
    if len(evidence) != RT4_REFLECTION_EVIDENCE_BYTES:
        raise ArtifactSetError(
            "RT4 reflection evidence is truncated or has trailing bytes"
        )
    reflection = report.get("reflection_probes")
    if not isinstance(reflection, dict):
        raise ArtifactSetError("RT4 reflection report is missing")
    _require_exact_keys(
        reflection,
        {
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
        },
        "RT4 reflection report",
    )
    platform = build_contract.get("platform")
    if not isinstance(platform, dict):
        raise ArtifactSetError("RT4 reflection platform contract is missing")
    policy_name = platform.get("policy")
    policy = PLATFORM_CONTRACTS.get(str(policy_name))
    expected_backend = RT4_REFLECTION_BACKENDS.get(str(policy_name))
    controls = {
        "schema": reflection.get("schema") == RT4_REFLECTION_SCHEMA,
        "file": reflection.get("evidence_file") == reflection_path.name,
        "bytes": _json_exact(
            reflection.get("evidence_bytes"), RT4_REFLECTION_EVIDENCE_BYTES
        ),
        "backend": expected_backend is not None
        and reflection.get("backend") == expected_backend,
        "render_system": policy is not None
        and reflection.get("render_system") == policy["renderer_name"]
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
    failed_controls = sorted(
        name for name, passed in controls.items() if not passed
    )
    if failed_controls:
        raise ArtifactSetError(
            "RT4 reflection controls failed: " + ", ".join(failed_controls)
        )

    capture = reflection.get("capture")
    if not isinstance(capture, dict):
        raise ArtifactSetError("RT4 reflection capture lineage is missing")
    _require_exact_keys(
        capture,
        {
            "render_frame_id",
            "simulation_tick",
            "probe_id",
            "content_revision",
            "candidate_generation",
            "deterministic_seed",
            "resolution",
        },
        "RT4 reflection capture lineage",
    )
    capture_checks = {
        "frame": _json_exact(capture.get("render_frame_id"), 1),
        "tick": _json_exact(capture.get("simulation_tick"), 1),
        "probe": _json_exact(capture.get("probe_id"), 1),
        "revision": _json_exact(capture.get("content_revision"), 1),
        "generation": _json_exact(capture.get("candidate_generation"), 1),
        "seed": _is_nonzero_u64_hex(capture.get("deterministic_seed")),
        "resolution": _json_exact(
            capture.get("resolution"), RT4_REFLECTION_RESOLUTION
        ),
    }
    failed_capture = sorted(
        name for name, passed in capture_checks.items() if not passed
    )
    if failed_capture:
        raise ArtifactSetError(
            "RT4 reflection capture lineage failed: "
            + ", ".join(failed_capture)
        )

    runtime = reflection.get("runtime_audit")
    if not isinstance(runtime, dict):
        raise ArtifactSetError("RT4 reflection runtime audit is missing")
    _require_exact_keys(
        runtime,
        {
            "version",
            "successful_capture_count",
            "failed_capture_count",
            "live_probe_count",
            "probe_resolution",
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
        },
        "RT4 reflection runtime audit",
    )
    runtime_checks = {
        "version": _json_exact(runtime.get("version"), 4),
        "success": _json_exact(runtime.get("successful_capture_count"), 1),
        "failure": _json_exact(runtime.get("failed_capture_count"), 0),
        "live": _json_exact(runtime.get("live_probe_count"), 1),
        "probe_resolution": _json_exact(
            runtime.get("probe_resolution"), RT4_REFLECTION_RESOLUTION
        ),
        "blend_resolution": _json_exact(runtime.get("blend_resolution"), 2048),
        "blend_ready": runtime.get("blend_texture_ready") is True,
        "state_digest": _is_nonzero_u64_hex(
            runtime.get("committed_state_digest")
        ),
        "native_evidence": _is_nonzero_u64_hex(
            runtime.get("native_execution_evidence")
        ),
        "capture_digest": _is_nonzero_u64_hex(runtime.get("capture_digest")),
        "payload": _json_exact(
            runtime.get("canonical_filtered_payload_bytes"),
            RT4_REFLECTION_FILTERED_BYTES,
        ),
        "faces": _json_exact(
            runtime.get("completed_face_count"), RT4_REFLECTION_FACE_COUNT
        ),
        "mips": _json_exact(
            runtime.get("completed_mip_count"),
            len(RT4_REFLECTION_FILTERED_DIMENSIONS),
        ),
        "ui_free": runtime.get("ui_free_capture") is True,
        "queue_excluded": runtime.get("reserved_render_queue_excluded") is True,
    }
    failed_runtime = sorted(
        name for name, passed in runtime_checks.items() if not passed
    )
    if failed_runtime:
        raise ArtifactSetError(
            "RT4 reflection runtime audit failed: "
            + ", ".join(failed_runtime)
        )

    for name, offset, byte_count, dimensions in (
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
    ):
        section = reflection.get(name)
        if not isinstance(section, dict):
            raise ArtifactSetError(f"RT4 reflection {name} report is missing")
        _require_exact_keys(
            section,
            {
                "offset",
                "bytes",
                "face_count",
                "mip_dimensions",
                "exact_fnv1a64",
                "finite_component_count",
                "nonzero_rgb_component_count",
                "distinct_texel_count",
                "max_absolute_rgb",
            },
            f"RT4 reflection {name} report",
        )
        payload = evidence[offset : offset + byte_count]
        metrics = _reflection_half_metrics(payload, name)
        checks = {
            "offset": _json_exact(section.get("offset"), offset),
            "bytes": _json_exact(section.get("bytes"), byte_count),
            "faces": _json_exact(
                section.get("face_count"), RT4_REFLECTION_FACE_COUNT
            ),
            "dimensions": _json_exact(
                section.get("mip_dimensions"), list(dimensions)
            ),
            "hash": section.get("exact_fnv1a64") == _fnv1a64(payload),
            "finite": _json_exact(
                section.get("finite_component_count"),
                metrics["finite_component_count"],
            ),
            "nonzero": _json_exact(
                section.get("nonzero_rgb_component_count"),
                metrics["nonzero_rgb_component_count"],
            )
            and int(metrics["nonzero_rgb_component_count"]) > 0,
            "distinct": _json_exact(
                section.get("distinct_texel_count"),
                metrics["distinct_texel_count"],
            )
            and int(metrics["distinct_texel_count"]) >= 2,
            "maximum": _reported_metric_matches(
                section.get("max_absolute_rgb"),
                float(metrics["max_absolute_rgb"]),
            )
            and float(metrics["max_absolute_rgb"]) > 0.0,
        }
        failed = sorted(key for key, passed in checks.items() if not passed)
        if failed:
            raise ArtifactSetError(
                f"RT4 reflection {name} evidence failed: "
                + ", ".join(failed)
            )

    offset = 0
    reflection_slices: list[dict[str, object]] = []
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
                reflection_slices.append(
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
        or len(reflection_slices) != 18
        or int(filtered_mip_one["nonzero_rgb_component_count"]) == 0
        or int(filtered_mip_one["distinct_texel_count"]) < 2
        or float(filtered_mip_one["max_absolute_rgb"]) <= 0.0
    ):
        raise ArtifactSetError("RT4 reflection subresource coverage is incomplete")
    return reflection_slices


def _binary32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _decode_positive_r16(bits: object) -> float | None:
    if type(bits) is not int or not 0 < bits <= 0x7BFF:
        return None
    decoded = struct.unpack("<e", struct.pack("<H", bits))[0]
    return decoded if math.isfinite(decoded) and decoded > 0.0 else None


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
    result = max(spacings)
    return result if math.isfinite(result) and result > 0.0 else None


def _recompute_hdr_history_oracle(compositor: dict[str, object]) -> dict[str, object]:
    input_fields = (
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
        for field in input_fields
    ):
        raise ArtifactSetError("RT4 HDR history oracle inputs are invalid")
    previous = _decode_positive_r16(
        compositor.get("history_previous_inverse_luminance_r16_bits")
    )
    if previous is None:
        raise ArtifactSetError("RT4 HDR history previous R16 input is invalid")

    exposure = _binary32(float(compositor["history_ogre_exposure"]))
    minimum = _binary32(float(compositor["history_minimum_auto_exposure"]))
    maximum = _binary32(float(compositor["history_maximum_auto_exposure"]))
    average = _binary32(float(compositor["history_average_log_luminance"]))
    delta = _binary32(float(compositor["history_delta_seconds"]))
    if not (-16.0 <= exposure <= 16.0 and -16.0 <= minimum <= maximum <= 16.0):
        raise ArtifactSetError("RT4 HDR history exposure inputs are out of range")
    if not 0.0 <= delta <= 60.0:
        raise ArtifactSetError("RT4 HDR history delta is out of range")

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
    new_weight = _binary32(_binary32(1.0) - shader_weight)
    weighted_target = _binary32(shader_target * new_weight)
    weighted_previous = _binary32(previous * shader_weight)
    adapted = _binary32(weighted_target + weighted_previous)
    try:
        reference_bits = int.from_bytes(struct.pack("<e", adapted), "little")
    except (OverflowError, struct.error) as error:
        raise ArtifactSetError("RT4 HDR history oracle is not R16 representable") from error
    storage_ulp = _positive_r16_storage_ulp(reference_bits)
    if storage_ulp is None:
        raise ArtifactSetError("RT4 HDR history oracle storage ULP is invalid")

    conditioning = abs(1.0 - analytic_weight) * abs(
        analytic_target - shader_target
    ) + abs(shader_target - previous) * abs(analytic_weight - shader_weight)
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


def _verify_hdr_compositor(value: object) -> None:
    compositor = _require_exact_keys(
        value,
        {
            "schema",
            "workspace",
            "persistent_workspace",
            "scene_format",
            "history_format",
            "output_format",
            "ui_included",
            "hud_workspace_verified",
            "deterministic_simulation_delta",
            "history_validation_mode",
            "native_r16_history_validated",
            "exact_current_to_old_copy_verified",
            "warmup_frames",
            "committed_frames",
            "split_lighting",
            "split_content",
            "native_lighting_state_verifications",
            "lighting_test_content_readbacks",
            "lighting_production_content_readbacks",
            "lighting_production_framebuffer_readbacks",
            "ogre14_lighting_passes",
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
            "suspend_restore_preserved_graph",
            "invalid_resize_rollback_verified",
            "resize_rebuild_verified",
            "resized_frame_verified",
            "first_attachment_fnv1a64",
            "final_attachment_fnv1a64",
            "clean_shutdown",
        },
        "RT4 HDR compositor report",
    )
    initial_bits = compositor.get("initial_inverse_luminance_r16_bits")
    final_bits = compositor.get("final_inverse_luminance_r16_bits")
    reference_bits = compositor.get("reference_inverse_luminance_r16_bits")
    absolute_error = compositor.get("history_absolute_error")
    allowed_error = compositor.get("history_allowed_error")
    conditioning_bound = compositor.get("history_conditioning_bound")
    rounding_bound = compositor.get("history_binary32_rounding_bound")
    storage_ulp = compositor.get("history_storage_ulp")
    first_hash = compositor.get("first_attachment_fnv1a64")
    final_hash = compositor.get("final_attachment_fnv1a64")
    overlay_hash = compositor.get("ui_overlay_control_fnv1a64")

    def finite_nonnegative(metric: object) -> bool:
        return (
            isinstance(metric, (int, float))
            and not isinstance(metric, bool)
            and math.isfinite(float(metric))
            and metric >= 0.0
        )

    native_history = _decode_positive_r16(final_bits)
    reference_history = _decode_positive_r16(reference_bits)
    expected_storage_ulp = _positive_r16_storage_ulp(reference_bits)
    oracle = _recompute_hdr_history_oracle(compositor)
    checks = {
        "schema": compositor.get("schema")
        == "ror.ogre_next_hdr_compositor.v6",
        "workspace": compositor.get("workspace") == "RoRHdrWorkspaceHudV1",
        "persistence": compositor.get("persistent_workspace") is True,
        "formats": compositor.get("scene_format") == "RGBA16_FLOAT"
        and compositor.get("history_format") == "R16_FLOAT"
        and compositor.get("output_format") == "RGBA8_SRGB",
        "hud_ui": compositor.get("ui_included") is True
        and compositor.get("hud_workspace_verified") is True,
        "deterministic_delta": compositor.get(
            "deterministic_simulation_delta"
        )
        is True,
        "native_history": compositor.get("history_validation_mode")
        == "native_authoritative_conditioning_plus_one_r16_ulp_v2"
        and compositor.get("native_r16_history_validated") is True,
        "history_copy": compositor.get("exact_current_to_old_copy_verified")
        is True,
        "frame_lineage": _json_exact(compositor.get("warmup_frames"), 2)
        and _json_exact(compositor.get("committed_frames"), 2),
        "split_lighting": compositor.get("split_lighting")
        == {
            "base_hdr_rgba16": True,
            "sun_full_unoccluded_rgba16": True,
            "sun_direct_rgba16": True,
            "gpu_max_full_minus_base": True,
            "transactional_sun_toggle": True,
            "raster_lit_rgba16": True,
            "scene_evaluations": 3,
            "single_history_step": True,
        },
        "split_content": isinstance(compositor.get("split_content"), dict)
        and set(compositor["split_content"])
        == {
            "rgb_channels_verified",
            "positive_sun_direct_pixels",
            "canonical_base_full_raster_alpha_one_direct_alpha_zero",
            "base_fnv1a64",
            "sun_full_fnv1a64",
            "sun_direct_fnv1a64",
            "raster_lit_fnv1a64",
        }
        and _json_exact(
            compositor["split_content"].get("rgb_channels_verified"),
            192 * 128 * 3,
        )
        and type(
            compositor["split_content"].get("positive_sun_direct_pixels")
        )
        is int
        and compositor["split_content"].get("positive_sun_direct_pixels") >= 128
        and compositor["split_content"].get(
            "canonical_base_full_raster_alpha_one_direct_alpha_zero"
        )
        is True
        and all(
            isinstance(compositor["split_content"].get(field), str)
            and re.fullmatch(
                r"[0-9a-f]{16}", compositor["split_content"].get(field)
            )
            is not None
            for field in (
                "base_fnv1a64",
                "sun_full_fnv1a64",
                "sun_direct_fnv1a64",
                "raster_lit_fnv1a64",
            )
        )
        and compositor["split_content"].get("base_fnv1a64")
        != compositor["split_content"].get("sun_full_fnv1a64"),
        "native_lighting": type(
            compositor.get("native_lighting_state_verifications")
        )
        is int
        and compositor.get("native_lighting_state_verifications") == 6
        and type(compositor.get("lighting_test_content_readbacks")) is int
        and compositor.get("lighting_test_content_readbacks") == 13
        and _json_exact(compositor.get("lighting_production_content_readbacks"), 0)
        and _json_exact(
            compositor.get("lighting_production_framebuffer_readbacks"), 0
        )
        and _json_exact(compositor.get("ogre14_lighting_passes"), 0),
        "initial_history": _json_exact(
            initial_bits, int.from_bytes(struct.pack("<e", 0.01), "little")
        ),
        "final_history": type(final_bits) is int
        and 0 < final_bits <= 0x7BFF
        and final_bits != initial_bits
        and compositor.get("history_changed_from_initial") is True,
        "reference_history": type(reference_bits) is int
        and 0 < reference_bits <= 0x7BFF,
        "comparison": finite_nonnegative(absolute_error)
        and finite_nonnegative(allowed_error)
        and allowed_error >= absolute_error
        and finite_nonnegative(conditioning_bound)
        and finite_nonnegative(rounding_bound)
        and finite_nonnegative(storage_ulp)
        and storage_ulp > 0.0
        and type(compositor.get("history_r16_ulp_distance")) is int
        and compositor.get("history_r16_ulp_distance") >= 0
        and native_history is not None
        and reference_history is not None
        and expected_storage_ulp is not None
        and _decode_positive_r16(
            compositor.get("history_previous_inverse_luminance_r16_bits")
        )
        is not None
        and reference_bits == oracle["reference_bits"]
        and _history_oracle_matches(
            conditioning_bound, float(oracle["conditioning_bound"])
        )
        and _history_oracle_matches(
            rounding_bound, float(oracle["rounding_bound"])
        )
        and _history_oracle_matches(storage_ulp, float(oracle["storage_ulp"]))
        and _history_oracle_matches(
            allowed_error, float(oracle["allowed_error"])
        )
        and math.isclose(
            float(absolute_error),
            abs(native_history - reference_history),
            rel_tol=0.0,
            abs_tol=0.0,
        )
        and math.isclose(
            float(storage_ulp),
            expected_storage_ulp,
            rel_tol=0.0,
            abs_tol=0.0,
        )
        and math.isclose(
            float(allowed_error),
            float(conditioning_bound)
            + float(rounding_bound)
            + float(storage_ulp),
            rel_tol=2.0e-15,
            abs_tol=1.0e-18,
        )
        and compositor.get("history_r16_ulp_distance")
        == abs(final_bits - reference_bits),
        "visual_response": type(compositor.get("exposure_changed_pixels"))
        is int
        and compositor.get("exposure_changed_pixels") >= 512,
        "ui_overlay_control": compositor.get("ui_overlay_control_node")
        == "HdrRenderUi"
        and compositor.get("ui_overlay_control_kind") == "Ogre::v1::Overlay"
        and type(compositor.get("ui_overlay_control_changed_pixels"))
        is int
        and compositor.get("ui_overlay_control_changed_pixels")
        >= 18432
        and type(compositor.get("ui_overlay_control_magenta_pixels")) is int
        and compositor.get("ui_overlay_control_magenta_pixels") >= 18432
        and isinstance(overlay_hash, str)
        and re.fullmatch(r"[0-9a-f]{16}", overlay_hash) is not None,
        "recovery": _json_exact(
            compositor.get("initialization_failure_stages_verified"), 10
        )
        and compositor.get("same_object_reinitialize_verified") is True,
        "atomic_publication": all(
            compositor.get(field) is True
            for field in (
                "frame_commit_prepare_failure_verified",
                "aborted_hdr_audit_unchanged",
                "aborted_reflection_audit_unchanged",
                "aborted_submission_uncommitted",
                "aborted_output_unchanged",
                "post_render_failure_fault_latched",
                "suspend_restore_preserved_graph",
                "invalid_resize_rollback_verified",
                "resize_rebuild_verified",
                "resized_frame_verified",
            )
        ),
        "hashes": isinstance(first_hash, str)
        and re.fullmatch(r"[0-9a-f]{16}", first_hash) is not None
        and isinstance(final_hash, str)
        and re.fullmatch(r"[0-9a-f]{16}", final_hash) is not None
        and first_hash != final_hash
        and first_hash != overlay_hash,
        "shutdown": compositor.get("clean_shutdown") is True,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            "RT4 HDR compositor evidence failed: " + ", ".join(failed)
        )


def _verify_hdr_compositor_visual(
    report: dict[str, object], ppm_pixels: bytes, evidence_path: Path
) -> list[dict[str, object]]:
    try:
        payload = evidence_path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(
            f"could not read RT4 HDR compositor evidence: {error}"
        ) from error
    visual = report.get("hdr_compositor_visual")
    compositor = report.get("hdr_compositor")
    visual = _require_exact_keys(
        visual,
        {
            "schema",
            "evidence_file",
            "ppm_attachment",
            "width",
            "height",
            "bytes_per_pixel",
            "attachments",
            "linear_split_attachments",
            "evidence_bytes",
        },
        "RT4 HDR compositor visual evidence",
    )
    if not isinstance(compositor, dict):
        raise ArtifactSetError("RT4 HDR compositor report is missing")
    attachments = visual.get("attachments")
    split_attachments = visual.get("linear_split_attachments")
    names = ("first_ui_free", "final_ui_free", "ui_overlay_control")
    split_names = (
        "base_hdr",
        "sun_full_unoccluded_hdr",
        "sun_direct_hdr",
        "raster_lit_hdr",
    )
    split_hash_fields = (
        "base_fnv1a64",
        "sun_full_fnv1a64",
        "sun_direct_fnv1a64",
        "raster_lit_fnv1a64",
    )
    width = 192
    height = 128
    attachment_bytes = width * height * 4
    split_attachment_bytes = width * height * 8
    expected_evidence_bytes = (
        attachment_bytes * len(names)
        + split_attachment_bytes * len(split_names)
    )
    if (
        visual.get("schema") != "ror.ogre_next_hdr_compositor_visual.v2"
        or visual.get("evidence_file") != evidence_path.name
        or visual.get("ppm_attachment") != "final_ui_free"
        or not _json_exact(visual.get("width"), width)
        or not _json_exact(visual.get("height"), height)
        or not _json_exact(visual.get("bytes_per_pixel"), 4)
        or not _json_exact(visual.get("evidence_bytes"), len(payload))
        or not isinstance(attachments, list)
        or len(attachments) != len(names)
        or not isinstance(split_attachments, list)
        or len(split_attachments) != len(split_names)
        or len(payload) != expected_evidence_bytes
    ):
        raise ArtifactSetError("RT4 HDR compositor visual contract mismatch")

    baseline = payload[:attachment_bytes]
    observed: dict[str, bytes] = {}
    slices: list[dict[str, object]] = []
    for index, (entry, name) in enumerate(zip(attachments, names)):
        entry = _require_exact_keys(
            entry,
            {
                "name",
                "offset",
                "bytes",
                "exact_fnv1a64",
                "changed_pixels_from_first",
            },
            f"RT4 HDR compositor {name} attachment",
        )
        offset = index * attachment_bytes
        block = payload[offset : offset + attachment_bytes]
        changed = _changed_pixels(baseline, block, 4)
        if (
            entry.get("name") != name
            or not _json_exact(entry.get("offset"), offset)
            or not _json_exact(entry.get("bytes"), attachment_bytes)
            or entry.get("exact_fnv1a64") != _fnv1a64(block)
            or not _json_exact(entry.get("changed_pixels_from_first"), changed)
            or any(block[pixel + 3] < 250 for pixel in range(0, len(block), 4))
        ):
            raise ArtifactSetError(
                f"RT4 HDR compositor {name} attachment mismatch"
            )
        observed[name] = block
        slices.append(
            {
                "attachment": name,
                "offset": offset,
                "bytes": attachment_bytes,
                "sha256": hashlib.sha256(block).hexdigest(),
            }
        )

    split_report = compositor.get("split_content")
    if not isinstance(split_report, dict):
        raise ArtifactSetError("RT4 HDR split-content report is missing")
    split_blocks: list[bytes] = []
    split_offset = attachment_bytes * len(names)
    for index, (entry, name, hash_field) in enumerate(
        zip(split_attachments, split_names, split_hash_fields, strict=True)
    ):
        entry = _require_exact_keys(
            entry,
            {"name", "offset", "bytes", "format", "exact_fnv1a64"},
            f"RT4 HDR compositor {name} attachment",
        )
        offset = split_offset + index * split_attachment_bytes
        block = payload[offset : offset + split_attachment_bytes]
        exact_hash = _fnv1a64(block)
        if (
            entry.get("name") != name
            or not _json_exact(entry.get("offset"), offset)
            or not _json_exact(entry.get("bytes"), split_attachment_bytes)
            or entry.get("format") != "RGBA16_FLOAT"
            or entry.get("exact_fnv1a64") != exact_hash
            or split_report.get(hash_field) != exact_hash
        ):
            raise ArtifactSetError(
                f"RT4 HDR compositor {name} attachment mismatch"
            )
        split_blocks.append(block)
        slices.append(
            {
                "attachment": name,
                "offset": offset,
                "bytes": split_attachment_bytes,
                "sha256": hashlib.sha256(block).hexdigest(),
            }
        )

    base, sun_full, sun_direct, raster_lit = split_blocks
    rgb_channels_verified = 0
    positive_sun_direct_pixels = 0
    for pixel in range(width * height):
        word = pixel * 8
        positive = False
        for channel in range(3):
            channel_offset = word + channel * 2
            base_value = struct.unpack_from("<e", base, channel_offset)[0]
            full_value = struct.unpack_from("<e", sun_full, channel_offset)[0]
            raster_value = struct.unpack_from("<e", raster_lit, channel_offset)[0]
            direct_value = struct.unpack_from("<e", sun_direct, channel_offset)[0]
            half_words = (
                int.from_bytes(base[channel_offset : channel_offset + 2], "little"),
                int.from_bytes(
                    sun_full[channel_offset : channel_offset + 2], "little"
                ),
                int.from_bytes(
                    raster_lit[channel_offset : channel_offset + 2], "little"
                ),
                int.from_bytes(
                    sun_direct[channel_offset : channel_offset + 2], "little"
                ),
            )
            if not all(
                math.isfinite(value) and value >= 0.0
                for value in (base_value, full_value, raster_value, direct_value)
            ):
                raise ArtifactSetError(
                    "RT4 HDR split attachment contains negative or non-finite radiance"
                )
            if any(word & 0x8000 for word in half_words):
                raise ArtifactSetError(
                    "RT4 HDR split attachment contains noncanonical negative-zero radiance"
                )
            try:
                expected_direct = struct.pack(
                    "<e", max(full_value - base_value, 0.0)
                )
            except (OverflowError, struct.error) as error:
                raise ArtifactSetError(
                    "RT4 HDR split directional radiance is not binary16 representable"
                ) from error
            if sun_direct[channel_offset : channel_offset + 2] != expected_direct:
                raise ArtifactSetError(
                    "RT4 HDR split directional radiance oracle mismatch"
                )
            rgb_channels_verified += 1
            positive = positive or expected_direct != b"\x00\x00"
        positive_sun_direct_pixels += int(positive)
        alpha_offset = word + 6
        if (
            base[alpha_offset : alpha_offset + 2] != b"\x00\x3c"
            or sun_full[alpha_offset : alpha_offset + 2] != b"\x00\x3c"
            or sun_direct[alpha_offset : alpha_offset + 2] != b"\x00\x00"
            or raster_lit[alpha_offset : alpha_offset + 2] != b"\x00\x3c"
        ):
            raise ArtifactSetError("RT4 HDR split attachment alpha mismatch")
    if (
        not _json_exact(
            split_report.get("rgb_channels_verified"), rgb_channels_verified
        )
        or not _json_exact(
            split_report.get("positive_sun_direct_pixels"),
            positive_sun_direct_pixels,
        )
        or split_report.get(
            "canonical_base_full_raster_alpha_one_direct_alpha_zero"
        )
        is not True
        or positive_sun_direct_pixels < 128
        or split_report.get("base_fnv1a64") == split_report.get("sun_full_fnv1a64")
        or split_report.get("sun_direct_fnv1a64") == "0000000000000000"
        or raster_lit != sun_full
    ):
        raise ArtifactSetError("RT4 HDR split-content evidence failed closed")

    final_rgb = bytes(
        channel
        for offset in range(0, len(observed["final_ui_free"]), 4)
        for channel in observed["final_ui_free"][offset : offset + 3]
    )
    exposure_changed = _changed_pixels(
        observed["first_ui_free"], observed["final_ui_free"], 4
    )
    overlay_changed = _changed_pixels(
        observed["first_ui_free"], observed["ui_overlay_control"], 4
    )
    overlay = observed["ui_overlay_control"]
    magenta = sum(
        overlay[offset] >= 250
        and overlay[offset + 1] <= 5
        and overlay[offset + 2] >= 250
        for offset in range(0, len(overlay), 4)
    )
    checks = {
        "ppm": final_rgb == ppm_pixels,
        "exposure": exposure_changed >= 512
        and compositor.get("exposure_changed_pixels") == exposure_changed,
        "overlay_delta": overlay_changed >= 18432
        and compositor.get("ui_overlay_control_changed_pixels")
        == overlay_changed,
        "overlay_magenta": magenta >= 18432
        and compositor.get("ui_overlay_control_magenta_pixels") == magenta,
        "first_hash": compositor.get("first_attachment_fnv1a64")
        == _fnv1a64(observed["first_ui_free"]),
        "final_hash": compositor.get("final_attachment_fnv1a64")
        == _fnv1a64(observed["final_ui_free"]),
        "overlay_hash": compositor.get("ui_overlay_control_fnv1a64")
        == _fnv1a64(overlay),
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            "RT4 HDR compositor visual evidence failed: " + ", ".join(failed)
        )
    return slices


def _verify_analytic_sky_visual(
    visual: dict[str, object], ppm_path: Path, evidence_path: Path
) -> list[dict[str, object]]:
    width = visual.get("width")
    height = visual.get("height")
    if not _json_exact(width, 768) or not _json_exact(height, 512):
        raise ArtifactSetError("RT4 analytic-sky visual extent drifted")
    width_int = int(width)
    height_int = int(height)
    attachment_bytes = width_int * height_int * 8
    try:
        evidence = evidence_path.read_bytes()
        ppm = ppm_path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(
            f"could not read analytic-sky visual evidence: {error}"
        ) from error
    header = f"P6\n{width_int} {height_int}\n255\n".encode("ascii")
    if (
        not ppm.startswith(header)
        or len(ppm) != len(header) + width_int * height_int * 3
        or len(evidence) != attachment_bytes * 2
        or visual.get("sky_only") is not True
        or visual.get("camera_facing_sun") is not True
        or visual.get("hdr_pixel_format") != "RGBA16_FLOAT"
        or not _json_exact(visual.get("evidence_bytes"), len(evidence))
        or not _json_exact(visual.get("sunless_hdr_offset"), 0)
        or not _json_exact(
            visual.get("sunless_hdr_bytes"), attachment_bytes
        )
        or not _json_exact(visual.get("sun_hdr_offset"), attachment_bytes)
        or not _json_exact(visual.get("sun_hdr_bytes"), attachment_bytes)
    ):
        raise ArtifactSetError("RT4 analytic-sky artifact layout is invalid")

    sunless = evidence[:attachment_bytes]
    sun = evidence[attachment_bytes:]
    ppm_rgb = ppm[len(header) :]
    pixel_count = width_int * height_int
    changed = 0
    changed_alpha_one = 0
    opaque_alpha = 0
    covered = 0
    row_luminance = [0.0] * height_int
    sunless_maximum_luminance = 0.0
    sun_maximum_luminance = 0.0
    distinct_sunless_rgb: set[bytes] = set()
    for pixel in range(pixel_count):
        offset = pixel * 8
        try:
            sunless_channels = struct.unpack_from("<4e", sunless, offset)
            sun_channels = struct.unpack_from("<4e", sun, offset)
        except struct.error as error:
            raise ArtifactSetError(
                "RT4 analytic-sky half-float evidence is malformed"
            ) from error
        if not all(
            math.isfinite(float(value))
            for value in (*sunless_channels, *sun_channels)
        ):
            raise ArtifactSetError(
                "RT4 analytic-sky evidence contains non-finite pixels"
            )
        if sunless[offset + 6 : offset + 8] != b"\x00\x3c":
            raise ArtifactSetError(
                "RT4 analytic-sky sunless HDR alpha is not exact one"
            )
        alpha_one = sun[offset + 6 : offset + 8] == b"\x00\x3c"
        opaque_alpha += int(alpha_one)
        rgb_changed = sunless[offset : offset + 6] != sun[offset : offset + 6]
        changed += int(rgb_changed)
        changed_alpha_one += int(rgb_changed and alpha_one)
        maximum = max(float(value) for value in sunless_channels[:3])
        covered += int(maximum > 0.0001)
        luminance = (
            0.2126 * float(sunless_channels[0])
            + 0.7152 * float(sunless_channels[1])
            + 0.0722 * float(sunless_channels[2])
        )
        row_luminance[pixel // width_int] += luminance
        sunless_maximum_luminance = max(
            sunless_maximum_luminance, luminance
        )
        sun_maximum_luminance = max(
            sun_maximum_luminance,
            0.2126 * float(sun_channels[0])
            + 0.7152 * float(sun_channels[1])
            + 0.0722 * float(sun_channels[2]),
        )
        distinct_sunless_rgb.add(sunless[offset : offset + 6])
    row_luminance = [value / width_int for value in row_luminance]
    gradient_rows = sum(
        abs(row_luminance[row] - row_luminance[row - 1]) > 1.0e-6
        for row in range(1, height_int)
    )
    checks = {
        "sunless_hash": visual.get("sunless_hdr_fnv1a64")
        == _fnv1a64(sunless),
        "sun_hash": visual.get("sun_hdr_fnv1a64") == _fnv1a64(sun),
        "ppm_hash": visual.get("visual_rgb_fnv1a64")
        == _fnv1a64(ppm_rgb),
        "coverage": _json_exact(
            visual.get("hemisphere_covered_pixels"), covered
        )
        and covered >= pixel_count * 95 // 100
        and visual.get("broad_hemisphere_coverage") is True,
        "gradient": _json_exact(
            visual.get("hemisphere_gradient_rows"), gradient_rows
        )
        and gradient_rows >= height_int // 4
        and len(distinct_sunless_rgb) >= 4,
        "visible_sun": _json_exact(
            visual.get("sun_changed_pixels"), changed
        )
        and changed > 0
        and sun_maximum_luminance > sunless_maximum_luminance
        and visual.get("visible_sun_effect") is True,
        "opaque_sun": _json_exact(
            visual.get("sun_changed_pixels_alpha_exact_one"),
            changed_alpha_one,
        )
        and _json_exact(
            visual.get("sun_hdr_opaque_alpha_pixels"), opaque_alpha
        )
        and changed_alpha_one == changed
        and opaque_alpha == pixel_count
        and visual.get("visible_sun_alpha_exact_one") is True,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            "RT4 analytic-sky visual proof failed: " + ", ".join(failed)
        )
    return [
        {
            "attachment": "camera_facing_sunless_hdr",
            "offset": 0,
            "bytes": attachment_bytes,
            "sha256": hashlib.sha256(sunless).hexdigest(),
        },
        {
            "attachment": "camera_facing_sun_hdr",
            "offset": attachment_bytes,
            "bytes": attachment_bytes,
            "sha256": hashlib.sha256(sun).hexdigest(),
        },
        {
            "attachment": "camera_facing_sun_sdr",
            "offset": 0,
            "bytes": len(ppm_rgb),
            "sha256": hashlib.sha256(ppm_rgb).hexdigest(),
        },
    ]


def _verify_rt4_semantics(
    report: dict[str, object],
    ppm_path: Path,
    isolation_path: Path,
    compositor_path: Path,
    analytic_sky_ppm_path: Path,
    analytic_sky_evidence_path: Path,
    build_contract: dict[str, object],
) -> tuple[
    list[dict[str, object]],
    list[dict[str, object]],
    list[dict[str, object]],
]:
    try:
        ppm = ppm_path.read_bytes()
        isolation_payload = isolation_path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(f"could not read RT4 raster evidence: {error}") from error
    width = 192
    height = 128
    header = b"P6\n192 128\n255\n"
    if not ppm.startswith(header) or len(ppm) != len(header) + width * height * 3:
        raise ArtifactSetError("RT4 PPM is not the exact 192x128 RGB8 contract")
    ppm_pixels = ppm[len(header) :]
    colours = [
        ppm_pixels[offset : offset + 3]
        for offset in range(0, len(ppm_pixels), 3)
    ]
    colour_counts: dict[bytes, int] = {}
    for colour in colours:
        colour_counts[colour] = colour_counts.get(colour, 0) + 1
    ppm_non_background = len(colours) - max(colour_counts.values())

    _require_exact_keys(
        report,
        {
            "schema",
            "status",
            "executable_build_identity",
            "provenance",
            "platform_policy",
            "renderer",
            "adapter",
            "catalog",
            "dynamic_meshes",
            "analytic_sky",
            "display_domain_unlit",
            "texture_allocations",
            "texture_upload_rollback",
            "texture_retirement",
            "texture_isolation",
            "tangent_handedness",
            "reflection_probes",
            "hdr_compositor",
            "hdr_compositor_visual",
            "hdr",
            "sdr",
            "lifecycle",
        },
        "RT4 report",
    )
    if report.get("schema") != RT4_REPORT_SCHEMA or report.get("status") != "pass":
        raise ArtifactSetError("RT4 report schema or status is invalid")
    if report.get("executable_build_identity") != _expected_rt4_build_identity(
        build_contract, report
    ):
        raise ArtifactSetError("RT4 report executable build identity mismatch")
    hdr = report.get("hdr")
    sdr = report.get("sdr")
    isolation = report.get("texture_isolation")
    if not isinstance(hdr, dict) or not isinstance(sdr, dict):
        raise ArtifactSetError("RT4 HDR/SDR report metrics are missing")
    if not isinstance(isolation, dict):
        raise ArtifactSetError("RT4 isolation report is missing")
    display_domain_unlit = report.get("display_domain_unlit")
    if not isinstance(display_domain_unlit, dict):
        raise ArtifactSetError("RT4 display-domain Unlit report is missing")
    dynamic_meshes = _require_exact_keys(
        report.get("dynamic_meshes"),
        {
            "schema",
            "base_deformation_revision",
            "deformed_deformation_revision",
            "full_update_owned",
            "solver_memory_aliased",
            "changed_pixels",
            "base_attachment_fnv1a64",
            "deformed_attachment_fnv1a64",
            "base_exact_replay",
            "deformed_exact_replay",
            "persistent_vertex_storage_exact",
            "persistent_buffer_updates",
            "native_mesh_rebuilds_through_persistent_update",
            "uploaded_vertex_bytes_through_persistent_update",
        },
        "RT4 dynamic-mesh report",
    )
    dynamic_mesh_checks = {
        "schema": dynamic_meshes.get("schema")
        == "ror.ogre_next_dynamic_mesh.v2",
        "revisions": _json_exact(
            dynamic_meshes.get("base_deformation_revision"), 1
        )
        and _json_exact(
            dynamic_meshes.get("deformed_deformation_revision"), 2
        ),
        "ownership": dynamic_meshes.get("full_update_owned") is True
        and dynamic_meshes.get("solver_memory_aliased") is False,
        "visible_change": _is_positive_int(dynamic_meshes.get("changed_pixels"))
        and int(dynamic_meshes["changed_pixels"]) >= 256,
        "hashes": _is_nonzero_u64_hex(
            dynamic_meshes.get("base_attachment_fnv1a64")
        )
        and _is_nonzero_u64_hex(
            dynamic_meshes.get("deformed_attachment_fnv1a64")
        )
        and dynamic_meshes.get("base_attachment_fnv1a64")
        != dynamic_meshes.get("deformed_attachment_fnv1a64"),
        "replay": dynamic_meshes.get("base_exact_replay") is True
        and dynamic_meshes.get("deformed_exact_replay") is True,
        "stable_storage": (
            dynamic_meshes.get("persistent_vertex_storage_exact") is True
            and _json_exact(
                dynamic_meshes.get("persistent_buffer_updates"), 1
            )
            and _json_exact(
                dynamic_meshes.get(
                    "native_mesh_rebuilds_through_persistent_update"
                ),
                1,
            )
            and _is_positive_int(
                dynamic_meshes.get(
                    "uploaded_vertex_bytes_through_persistent_update"
                )
            )
        ),
    }
    failed_dynamic_meshes = sorted(
        name for name, passed in dynamic_mesh_checks.items() if not passed
    )
    if failed_dynamic_meshes:
        raise ArtifactSetError(
            "RT4 dynamic-mesh controls failed: "
            + ", ".join(failed_dynamic_meshes)
        )
    analytic_sky = _require_exact_keys(
        report.get("analytic_sky"),
        {
            "schema",
            "evidence_file",
            "visual_file",
            "capture_policy_version",
            "native_render_policy_version",
            "authoritative_inputs",
            "exact_skyx_pixel_capture",
            "skyx_capture_boundary",
            "sun_light_id",
            "descriptor",
            "native_geometry",
            "runtime_audit",
            "visual_proof",
            "transactional_rollback",
        },
        "RT4 analytic-sky report",
    )
    descriptor = _require_exact_keys(
        analytic_sky.get("descriptor"),
        {
            "zenith_radiance",
            "horizon_radiance",
            "ground_radiance",
            "sun_disk_radiance",
            "sun_angular_radius_radians",
        },
        "RT4 analytic-sky descriptor",
    )
    native_geometry = _require_exact_keys(
        analytic_sky.get("native_geometry"),
        {
            "resource_model",
            "background_vertex_count",
            "background_index_count",
            "sun_vertex_count",
            "sun_index_count",
            "native_content_bytes",
            "cpu_geometry_fnv1a64",
            "native_geometry_metadata_verified",
            "production_default_gpu_content_readbacks_zero",
            "exact_gpu_buffer_content_readback",
            "camera_centered",
            "rendered_first",
            "depth_check_disabled",
            "depth_write_disabled",
            "additive_sun_disk",
            "separate_sun_alpha_replace",
            "casts_shadows",
            "portable_scene_identity_absent",
        },
        "RT4 analytic-sky native geometry",
    )
    sky_audit = _require_exact_keys(
        analytic_sky.get("runtime_audit"),
        {
            "version",
            "completed_frames",
            "native_mesh_creates",
            "native_mesh_destroys",
            "native_vertex_buffer_creates",
            "native_vertex_buffer_destroys",
            "native_index_buffer_creates",
            "native_index_buffer_destroys",
            "native_vao_creates",
            "native_vao_destroys",
            "native_item_creates",
            "native_item_destroys",
            "native_scene_node_creates",
            "native_scene_node_destroys",
            "native_datablock_creates",
            "native_datablock_destroys",
            "native_mesh_absence_checks",
            "native_item_absence_checks",
            "native_scene_node_absence_checks",
            "native_datablock_absence_checks",
            "native_gpu_content_readbacks",
            "native_state_verifications",
        },
        "RT4 analytic-sky runtime audit",
    )
    sky_rollback = _require_exact_keys(
        analytic_sky.get("transactional_rollback"),
        {
            "injected_stage_count",
            "publication_unchanged_on_failure",
            "native_lifetimes_balanced_on_failure",
            "clean_retry",
        },
        "RT4 analytic-sky rollback audit",
    )
    sky_visual = _require_exact_keys(
        analytic_sky.get("visual_proof"),
        {
            "sky_only",
            "camera_facing_sun",
            "width",
            "height",
            "hdr_pixel_format",
            "evidence_bytes",
            "sunless_hdr_offset",
            "sunless_hdr_bytes",
            "sun_hdr_offset",
            "sun_hdr_bytes",
            "sunless_hdr_fnv1a64",
            "sun_hdr_fnv1a64",
            "visual_rgb_fnv1a64",
            "hemisphere_covered_pixels",
            "hemisphere_gradient_rows",
            "broad_hemisphere_coverage",
            "sun_changed_pixels",
            "sun_changed_pixels_alpha_exact_one",
            "sun_hdr_opaque_alpha_pixels",
            "visible_sun_effect",
            "visible_sun_alpha_exact_one",
        },
        "RT4 analytic-sky visual proof",
    )
    radiance_ok = True
    for field in (
        "zenith_radiance",
        "horizon_radiance",
        "ground_radiance",
        "sun_disk_radiance",
    ):
        value = descriptor.get(field)
        radiance_ok = radiance_ok and isinstance(value, list) and len(value) == 3
        if not isinstance(value, list) or len(value) != 3:
            continue
        radiance_ok = radiance_ok and all(
            isinstance(component, (int, float))
            and not isinstance(component, bool)
            and math.isfinite(float(component))
            and float(component) >= 0.0
            for component in value
        )
    expected_sky_radiance = {
        "zenith_radiance": (
            0.022859251126646996,
            0.047378916293382645,
            0.09662292897701263,
        ),
        "horizon_radiance": (
            0.06477569788694382,
            0.07451573759317398,
            0.08912292867898941,
        ),
        "ground_radiance": (
            0.001500000013038516,
            0.0017999999690800905,
            0.0022499999031424522,
        ),
        "sun_disk_radiance": (
            25.812335968017578,
            23.74734878540039,
            21.166114807128906,
        ),
    }
    policy_radiance_ok = radiance_ok and all(
        all(
            _reported_metric_matches(actual, expected)
            for actual, expected in zip(
                descriptor[field], expected_components, strict=True
            )
        )
        for field, expected_components in expected_sky_radiance.items()
    )
    completed_frames = sky_audit.get("completed_frames")
    analytic_sky_checks = {
        "schema": analytic_sky.get("schema")
        == "ror.ogre_next_analytic_sky.v2",
        "files": analytic_sky.get("evidence_file")
        == analytic_sky_evidence_path.name
        and analytic_sky.get("visual_file") == analytic_sky_ppm_path.name,
        "capture_policy": _json_exact(
            analytic_sky.get("capture_policy_version"), 1
        ),
        "native_policy": _json_exact(
            analytic_sky.get("native_render_policy_version"), 1
        ),
        "authority": analytic_sky.get("authoritative_inputs")
        == "joined_live_ambient_and_exact_converted_main_light",
        "honest_boundary": analytic_sky.get("exact_skyx_pixel_capture") is False
        and analytic_sky.get("skyx_capture_boundary")
        == "SkyX_shader_is_azimuth_dependent_and_may_apply_LDR_exposure",
        "sun_identity": _json_exact(analytic_sky.get("sun_light_id"), 1),
        "radiance": policy_radiance_ok,
        "sun_radius": isinstance(
            descriptor.get("sun_angular_radius_radians"), (int, float)
        )
        and not isinstance(descriptor.get("sun_angular_radius_radians"), bool)
        and math.isclose(
            float(descriptor["sun_angular_radius_radians"]),
            0.00465047,
            rel_tol=0.0,
            abs_tol=1.0e-8,
        ),
        "topology": _json_exact(
            {
                "background_vertex_count": native_geometry.get(
                    "background_vertex_count"
                ),
                "background_index_count": native_geometry.get(
                    "background_index_count"
                ),
                "sun_vertex_count": native_geometry.get("sun_vertex_count"),
                "sun_index_count": native_geometry.get("sun_index_count"),
            },
            {
                "background_vertex_count": 2082,
                "background_index_count": 11904,
                "sun_vertex_count": 34,
                "sun_index_count": 96,
            },
        )
        and native_geometry.get("resource_model")
        == "frontend_owned_v2_mesh_item"
        and _json_exact(native_geometry.get("native_content_bytes"), 107248)
        and _is_positive_int(native_geometry.get("cpu_geometry_fnv1a64"))
        and int(native_geometry["cpu_geometry_fnv1a64"]) <= (1 << 64) - 1
        and native_geometry.get("native_geometry_metadata_verified") is True
        and native_geometry.get(
            "production_default_gpu_content_readbacks_zero"
        ) is True
        and native_geometry.get("exact_gpu_buffer_content_readback") is True,
        "native_state": all(
            native_geometry.get(field) is True
            for field in (
                "camera_centered",
                "rendered_first",
                "depth_check_disabled",
                "depth_write_disabled",
                "additive_sun_disk",
                "separate_sun_alpha_replace",
                "portable_scene_identity_absent",
            )
        )
        and native_geometry.get("casts_shadows") is False,
        "balanced_lifetime": _is_positive_int(completed_frames)
        and int(completed_frames) >= 4
        and _json_exact(sky_audit.get("version"), 2)
        and all(
            _json_exact(sky_audit.get(field), int(completed_frames) * 2)
            for field in (
                "native_mesh_creates",
                "native_mesh_destroys",
                "native_vertex_buffer_creates",
                "native_vertex_buffer_destroys",
                "native_index_buffer_creates",
                "native_index_buffer_destroys",
                "native_vao_creates",
                "native_vao_destroys",
                "native_item_creates",
                "native_item_destroys",
                "native_datablock_creates",
                "native_datablock_destroys",
                "native_mesh_absence_checks",
                "native_item_absence_checks",
                "native_datablock_absence_checks",
            )
        )
        and all(
            _json_exact(sky_audit.get(field), completed_frames)
            for field in (
                "native_scene_node_creates",
                "native_scene_node_destroys",
                "native_scene_node_absence_checks",
                "native_state_verifications",
            )
        )
        and _json_exact(
            sky_audit.get("native_gpu_content_readbacks"),
            int(completed_frames) * 4,
        ),
        "rollback": _json_exact(sky_rollback.get("injected_stage_count"), 20)
        and sky_rollback.get("publication_unchanged_on_failure") is True
        and sky_rollback.get("native_lifetimes_balanced_on_failure") is True
        and sky_rollback.get("clean_retry") is True,
    }
    failed_sky = sorted(
        name for name, passed in analytic_sky_checks.items() if not passed
    )
    if failed_sky:
        raise ArtifactSetError(
            "RT4 analytic-sky controls failed: " + ", ".join(failed_sky)
        )
    sky_slices = _verify_analytic_sky_visual(
        sky_visual, analytic_sky_ppm_path, analytic_sky_evidence_path
    )
    _require_exact_keys(
        display_domain_unlit,
        {
            "schema",
            "base_color_transfer",
            "upload_format",
            "mip_policy",
            "sampler",
            "shader_precision",
            "encoded_filtered",
            "filter_then_eotf",
            "decode_before_filter",
            "matching_foreground_pixels",
            "decode_before_filter_pixels",
            "complete_unorm_mips_uploaded",
            "full32_after_filter_shader_executed",
            "alpha_untouched_opaque",
            "no_cast_or_receive_shadow_flags",
            "usage_transition_rollback_exact",
            "usage_transition_commit_exact",
        },
        "RT4 display-domain Unlit report",
    )
    _verify_hdr_compositor(report.get("hdr_compositor"))
    compositor_slices = _verify_hdr_compositor_visual(
        report, ppm_pixels, compositor_path
    )
    _require_exact_keys(
        hdr,
        {
            "format",
            "width",
            "height",
            "distinct_rgb8_values",
            "non_background_pixels",
            "minimum_luminance",
            "maximum_luminance",
            "exact_attachment_fnv1a64",
            "rgb8_fnv1a64",
        },
        "RT4 HDR metrics",
    )
    _require_exact_keys(
        sdr,
        {
            "format",
            "width",
            "height",
            "distinct_rgb8_values",
            "non_background_pixels",
            "minimum_luminance",
            "maximum_luminance",
            "exact_attachment_fnv1a64",
            "rgb8_fnv1a64",
        },
        "RT4 SDR metrics",
    )
    _require_exact_keys(
        isolation,
        {
            "schema",
            "evidence_file",
            "width",
            "height",
            "geometry_identical",
            "material_factors_constants_identical",
            "camera_identical",
            "lights_identical",
            "ui_included",
            "variants",
            "evidence_bytes",
        },
        "RT4 isolation report",
    )
    variants = isolation.get("variants")
    controls = {
        "schema": isolation.get("schema")
        == "ror.ogre_next_rt4_texture_isolation.v2",
        "file": isolation.get("evidence_file") == isolation_path.name,
        "extent": _json_exact(isolation.get("width"), width)
        and _json_exact(isolation.get("height"), height),
        "bytes": _is_positive_int(isolation.get("evidence_bytes"))
        and isolation["evidence_bytes"] <= len(isolation_payload),
        "geometry": isolation.get("geometry_identical") is True,
        "factors": isolation.get("material_factors_constants_identical") is True,
        "camera": isolation.get("camera_identical") is True,
        "lights": isolation.get("lights_identical") is True,
        "ui_free": isolation.get("ui_included") is False,
        "variant_count": isinstance(variants, list)
        and len(variants) == len(RT4_EXPECTED_VARIANTS),
    }
    failed_controls = sorted(
        name for name, passed in controls.items() if not passed
    )
    if failed_controls:
        raise ArtifactSetError(
            "RT4 isolation controls failed: " + ", ".join(failed_controls)
        )
    if not isinstance(variants, list):
        raise ArtifactSetError("RT4 isolation variants are invalid")

    offset = 0
    baseline: dict[str, bytes] = {}
    exact_hashes: dict[str, set[str]] = {"hdr": set(), "sdr": set()}
    slice_attestations: list[dict[str, object]] = []
    for index, (entry, expected) in enumerate(zip(variants, RT4_EXPECTED_VARIANTS)):
        if not isinstance(entry, dict):
            raise ArtifactSetError("RT4 isolation variant is not an object")
        _require_exact_keys(
            entry,
            {
                "name",
                "changed_input",
                "asset_sequence",
                "uv0_affine",
                "hdr",
                "sdr",
            },
            f"RT4 {expected[0]} isolation variant",
        )
        if (
            entry.get("name") != expected[0]
            or entry.get("changed_input") != expected[1]
            or not _json_exact(entry.get("asset_sequence"), index + 1)
        ):
            raise ArtifactSetError("RT4 isolation variant identity mismatch")
        uv0_affine = entry.get("uv0_affine")
        transformed = expected[0] == "uv0_affine"
        expected_scale = [2, 4] if transformed else [1, 1]
        expected_offset = [0.125, -0.25] if transformed else [0, 0]
        if not isinstance(uv0_affine, dict):
            raise ArtifactSetError(
                f"RT4 {expected[0]} native UV0 affine receipt is missing"
            )
        _require_exact_keys(
            uv0_affine,
            {
                "version",
                "scale",
                "offset",
                "portable_binding_count",
                "native_slot_count",
                "native_slot_readbacks",
                "native_user_value_readbacks",
                "transformed",
                "uv0_only",
                "positive_scale",
                "rotation_zero",
                "shared_across_bound_slots",
                "shader_piece_selected",
                "exact_native_state",
            },
            f"RT4 {expected[0]} native UV0 affine receipt",
        )
        if (
            not _json_exact(uv0_affine.get("version"), 1)
            or not _json_exact(uv0_affine.get("scale"), expected_scale)
            or not _json_exact(uv0_affine.get("offset"), expected_offset)
            or not _json_exact(
                uv0_affine.get("portable_binding_count"), 4
            )
            or not _json_exact(uv0_affine.get("native_slot_count"), 5)
            or not _json_exact(
                uv0_affine.get("native_slot_readbacks"), 5
            )
            or not _json_exact(
                uv0_affine.get("native_user_value_readbacks"), 3
            )
            or uv0_affine.get("transformed") is not transformed
            or any(
                uv0_affine.get(field) is not True
                for field in (
                    "uv0_only",
                    "positive_scale",
                    "rotation_zero",
                    "shared_across_bound_slots",
                    "shader_piece_selected",
                    "exact_native_state",
                )
            )
        ):
            raise ArtifactSetError(
                f"RT4 {expected[0]} native UV0 affine receipt changed"
            )
        for attachment, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
            reported = entry.get(attachment)
            expected_bytes = width * height * bytes_per_pixel
            if not isinstance(reported, dict):
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} metadata is missing"
                )
            _require_exact_keys(
                reported,
                {
                    "offset",
                    "bytes",
                    "exact_fnv1a64",
                    "changed_pixels_from_baseline",
                },
                f"RT4 {expected[0]} {attachment} metadata",
            )
            if (
                not _json_exact(reported.get("offset"), offset)
                or not _json_exact(reported.get("bytes"), expected_bytes)
                or offset + expected_bytes > len(isolation_payload)
            ):
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} slice layout mismatch"
                )
            payload = isolation_payload[offset : offset + expected_bytes]
            _attachment_metrics(payload, attachment == "hdr")
            fnv = _fnv1a64(payload)
            if reported.get("exact_fnv1a64") != fnv:
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} exact hash mismatch"
                )
            exact_hashes[attachment].add(fnv)
            if index == 0:
                changed = 0
                baseline[attachment] = payload
            else:
                changed = _changed_pixels(
                    baseline[attachment], payload, bytes_per_pixel
                )
            if (
                not _json_exact(reported.get("changed_pixels_from_baseline"), changed)
                or (index != 0 and changed < 64)
            ):
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} semantic delta mismatch"
                )
            slice_attestations.append(
                {
                    "variant": expected[0],
                    "attachment": attachment,
                    "offset": offset,
                    "bytes": expected_bytes,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            )
            offset += expected_bytes

    if not _json_exact(isolation.get("evidence_bytes"), offset):
        raise ArtifactSetError("RT4 texture-isolation byte extent drifted")
    if any(
        len(hashes) != len(RT4_EXPECTED_VARIANTS)
        for hashes in exact_hashes.values()
    ):
        raise ArtifactSetError("RT4 isolation variants are not all distinct")

    handedness = report.get("tangent_handedness")
    if not isinstance(handedness, dict):
        raise ArtifactSetError("RT4 tangent-handedness report is missing")
    _require_exact_keys(
        handedness,
        {
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
        },
        "RT4 tangent-handedness report",
    )
    handedness_start = offset
    if (
        handedness.get("schema")
        != "ror.ogre_next_rt4_tangent_handedness.v1"
        or handedness.get("evidence_file") != isolation_path.name
        or not _json_exact(handedness.get("evidence_offset"), handedness_start)
        or handedness.get("authored_tangent_format") != "FLOAT4"
        or not _json_exact(handedness.get("positive_tangent_w"), 1)
        or not _json_exact(handedness.get("negative_tangent_w"), -1)
        or handedness.get("position_normal_tangent_xyz_uv0_identical") is not True
        or handedness.get("material_camera_lights_identical") is not True
        or handedness.get("ui_included") is not False
    ):
        raise ArtifactSetError("RT4 tangent-handedness controls failed")
    handedness_blocks: dict[str, dict[str, bytes]] = {
        "positive": {},
        "negative": {},
    }
    for sign in ("positive", "negative"):
        sign_report = handedness.get(sign)
        if not isinstance(sign_report, dict):
            raise ArtifactSetError(f"RT4 {sign} tangent evidence is missing")
        _require_exact_keys(
            sign_report, {"hdr", "sdr"}, f"RT4 {sign} tangent evidence"
        )
        for attachment, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
            reported = sign_report.get(attachment)
            expected_bytes = width * height * bytes_per_pixel
            if not isinstance(reported, dict):
                raise ArtifactSetError(
                    f"RT4 {sign} tangent {attachment} metadata is missing"
                )
            _require_exact_keys(
                reported,
                {"offset", "bytes", "exact_fnv1a64"},
                f"RT4 {sign} tangent {attachment} metadata",
            )
            if (
                not _json_exact(reported.get("offset"), offset)
                or not _json_exact(reported.get("bytes"), expected_bytes)
                or offset + expected_bytes > len(isolation_payload)
            ):
                raise ArtifactSetError(
                    f"RT4 {sign} tangent {attachment} slice layout mismatch"
                )
            payload = isolation_payload[offset : offset + expected_bytes]
            _attachment_metrics(payload, attachment == "hdr")
            if reported.get("exact_fnv1a64") != _fnv1a64(payload):
                raise ArtifactSetError(
                    f"RT4 {sign} tangent {attachment} exact hash mismatch"
                )
            handedness_blocks[sign][attachment] = payload
            slice_attestations.append(
                {
                    "variant": f"tangent_{sign}_w",
                    "attachment": attachment,
                    "offset": offset,
                    "bytes": expected_bytes,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            )
            offset += expected_bytes
    if (
        not _json_exact(
            handedness.get("evidence_bytes"), offset - handedness_start
        )
        or offset != len(isolation_payload)
    ):
        raise ArtifactSetError("RT4 tangent-handedness byte extent drifted")
    for attachment, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
        changed = _changed_pixels(
            handedness_blocks["positive"][attachment],
            handedness_blocks["negative"][attachment],
            bytes_per_pixel,
        )
        if (
            not _json_exact(
                handedness.get(f"{attachment}_changed_pixels"), changed
            )
            or changed < 64
        ):
            raise ArtifactSetError(
                f"RT4 tangent-w sign produced no exact {attachment.upper()} effect"
            )
    baseline_sdr_rgb = bytes(
        channel
        for pixel_offset in range(0, len(baseline["sdr"]), 4)
        for channel in baseline["sdr"][pixel_offset : pixel_offset + 3]
    )
    baseline_colour_counts: dict[bytes, int] = {}
    for rgb_offset in range(0, len(baseline_sdr_rgb), 3):
        colour = baseline_sdr_rgb[rgb_offset : rgb_offset + 3]
        baseline_colour_counts[colour] = baseline_colour_counts.get(colour, 0) + 1
    baseline_non_background = (
        width * height - max(baseline_colour_counts.values())
    )
    hdr_metrics = _attachment_metrics(baseline["hdr"], True)
    sdr_metrics = _attachment_metrics(baseline["sdr"], False)
    if hdr_metrics["rgb"] != bytes(
        _quantize_unit_float(channel)
        for channels in struct.iter_unpack("<4e", baseline["hdr"])
        for channel in channels[:3]
    ):
        raise ArtifactSetError("RT4 HDR RGB derivation is inconsistent")
    hdr_energy = hdr_metrics["maximum_luminance"]
    hdr_minimum = hdr_metrics["minimum_luminance"]
    sdr_maximum = sdr_metrics["maximum_luminance"]
    sdr_minimum = sdr_metrics["minimum_luminance"]
    report_checks = {
        "ppm_is_compositor": ppm_pixels != baseline_sdr_rgb,
        "ppm_geometry": len(colour_counts) >= 2 and ppm_non_background >= 512,
        "retirement": _json_exact(
            report.get("texture_retirement"), RT4_EXPECTED_RETIREMENT
        ),
        "texture_allocations": _json_exact(
            report.get("texture_allocations"),
            RT4_EXPECTED_TEXTURE_ALLOCATIONS,
        ),
        "texture_upload_rollback": _json_exact(
            report.get("texture_upload_rollback"),
            RT4_EXPECTED_TEXTURE_UPLOAD_ROLLBACK,
        ),
        "display_domain_unlit": display_domain_unlit.get("schema")
        == "ror.ogre_next_rt4_display_domain_unlit.v1"
        and display_domain_unlit.get("base_color_transfer")
        == "SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE"
        and display_domain_unlit.get("upload_format") == "RGBA8_UNORM"
        and display_domain_unlit.get("mip_policy")
        == "complete_base_to_1x1_nearest_mip"
        and display_domain_unlit.get("sampler")
        == "linear_min_mag_clamp_edge"
        and display_domain_unlit.get("shader_precision") == "PrecisionFull32"
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
        and _is_positive_int(
            display_domain_unlit.get("matching_foreground_pixels")
        )
        and display_domain_unlit["matching_foreground_pixels"] >= 512
        and _json_exact(display_domain_unlit.get("decode_before_filter_pixels"), 0)
        and display_domain_unlit.get("complete_unorm_mips_uploaded") is True
        and display_domain_unlit.get("full32_after_filter_shader_executed")
        is True
        and display_domain_unlit.get("alpha_untouched_opaque") is True
        and display_domain_unlit.get("no_cast_or_receive_shadow_flags") is True
        and display_domain_unlit.get("usage_transition_rollback_exact") is True
        and display_domain_unlit.get("usage_transition_commit_exact") is True,
        "lifecycle": _json_exact(
            report.get("lifecycle"), RT4_EXPECTED_LIFECYCLE
        ),
        "hdr_format": hdr.get("format") == "RGBA16_FLOAT",
        "hdr_extent": _json_exact(hdr.get("width"), width)
        and _json_exact(hdr.get("height"), height),
        "hdr_exact": all(
            _json_exact(hdr.get(field), hdr_metrics[field])
            for field in (
                "exact_attachment_fnv1a64",
                "rgb8_fnv1a64",
                "distinct_rgb8_values",
                "non_background_pixels",
            )
        ),
        "hdr_minimum": _reported_metric_matches(
            hdr.get("minimum_luminance"), hdr_minimum
        )
        and hdr_minimum >= 0.0,
        "hdr_maximum": _reported_metric_matches(
            hdr.get("maximum_luminance"), hdr_energy
        )
        and hdr_energy > 1.05,
        "hdr_geometry": hdr_metrics["distinct_rgb8_values"] >= 2
        and hdr_metrics["non_background_pixels"] >= 512,
        "sdr_format": sdr.get("format") == "RGBA8_SRGB",
        "sdr_extent": _json_exact(sdr.get("width"), width)
        and _json_exact(sdr.get("height"), height),
        "sdr_exact": all(
            _json_exact(sdr.get(field), sdr_metrics[field])
            for field in (
                "exact_attachment_fnv1a64",
                "rgb8_fnv1a64",
                "distinct_rgb8_values",
                "non_background_pixels",
            )
        ),
        "sdr_minimum": _reported_metric_matches(
            sdr.get("minimum_luminance"), sdr_minimum
        ),
        "sdr_maximum": _reported_metric_matches(
            sdr.get("maximum_luminance"), sdr_maximum
        ),
        "sdr_isolation_hash": sdr_metrics["rgb8_fnv1a64"]
        == _fnv1a64(baseline_sdr_rgb),
        "sdr_distinct": sdr_metrics["distinct_rgb8_values"]
        == len(baseline_colour_counts),
        "sdr_geometry": sdr_metrics["non_background_pixels"]
        == baseline_non_background
        and baseline_non_background >= 512
        and sdr_maximum - sdr_minimum > 0.05,
    }
    failed_report = sorted(
        name for name, passed in report_checks.items() if not passed
    )
    if failed_report:
        raise ArtifactSetError(
            "RT4 PPM/isolation report mismatch: " + ", ".join(failed_report)
        )
    return slice_attestations, compositor_slices, sky_slices


def _verify_rt4(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    if (
        not NORMAL_MAP_SOURCE_LOCK_PATH.is_file()
        or sha256_file(NORMAL_MAP_SOURCE_LOCK_PATH)
        != NORMAL_MAP_SOURCE_LOCK_SHA256
    ):
        raise ArtifactSetError("reviewed normal-map source lock is missing or changed")
    report_path = root / RT4_REPORT_ARTIFACT
    ppm_path = root / RT4_PPM_ARTIFACT
    isolation_path = root / RT4_ISOLATION_ARTIFACT
    reflection_path = root / RT4_REFLECTION_ARTIFACT
    compositor_path = root / RT4_COMPOSITOR_ARTIFACT
    analytic_sky_ppm_path = root / RT4_ANALYTIC_SKY_PPM_ARTIFACT
    analytic_sky_evidence_path = root / RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT
    repeat_report_path = root / RT4_REPEAT_REPORT_ARTIFACT
    repeat_ppm_path = root / RT4_REPEAT_PPM_ARTIFACT
    repeat_isolation_path = root / RT4_REPEAT_ISOLATION_ARTIFACT
    repeat_reflection_path = root / RT4_REPEAT_REFLECTION_ARTIFACT
    repeat_compositor_path = root / RT4_REPEAT_COMPOSITOR_ARTIFACT
    repeat_analytic_sky_ppm_path = root / RT4_REPEAT_ANALYTIC_SKY_PPM_ARTIFACT
    repeat_analytic_sky_evidence_path = (
        root / RT4_REPEAT_ANALYTIC_SKY_EVIDENCE_ARTIFACT
    )
    attestation_path = root / RT4_ATTESTATION_ARTIFACT
    report = _read_json_object(report_path, "RT4 report")
    attestation = _read_json_object(attestation_path, "RT4 attestation")
    _require_exact_keys(
        attestation,
        {
            "schema",
            "status",
            "integrity_model",
            "source",
            "ogre_next",
            "shader_media",
            "files",
            "isolation_slices",
            "reflection_slices",
            "compositor_slices",
            "analytic_sky_slices",
        },
        "RT4 attestation",
    )
    if (
        attestation.get("schema")
        != RT4_ATTESTATION_SCHEMA
        or attestation.get("status") != "pass"
        or attestation.get("integrity_model") != RT4_INTEGRITY_MODEL
    ):
        raise ArtifactSetError("RT4 attestation schema or status is invalid")

    platform_contract = build_contract["platform"]
    executable_suffix = (
        ".exe" if platform_contract.get("policy") == "windows-x64-d3d11" else ""
    )
    executable_relative = (
        "ror-ogre-next-n1-package/bin/"
        f"{RT4_PACKAGE_EXECUTABLE_STEM}{executable_suffix}"
    )
    executable_path = root / executable_relative
    if executable_path.is_symlink() or not executable_path.is_file():
        raise ArtifactSetError(f"missing: {executable_relative}")
    if executable_path.stat().st_size <= 0:
        raise ArtifactSetError(f"empty: {executable_relative}")
    _verify_rt4_executable(executable_path, build_contract, report)

    files = attestation.get("files")
    if not isinstance(files, dict) or set(files) != {
        "build_contract",
        "report",
        "ppm",
        "isolation",
        "reflection",
        "compositor",
        "analytic_sky_evidence",
        "analytic_sky_ppm",
        "repeat_report",
        "repeat_ppm",
        "repeat_isolation",
        "repeat_reflection",
        "repeat_compositor",
        "repeat_analytic_sky_evidence",
        "repeat_analytic_sky_ppm",
        "executable",
    }:
        raise ArtifactSetError("RT4 attested file set is invalid")
    for key, path, relative in (
        ("build_contract", root / REQUIRED_ARTIFACTS[0], REQUIRED_ARTIFACTS[0]),
        ("report", report_path, RT4_REPORT_ARTIFACT),
        ("ppm", ppm_path, RT4_PPM_ARTIFACT),
        ("isolation", isolation_path, RT4_ISOLATION_ARTIFACT),
        ("reflection", reflection_path, RT4_REFLECTION_ARTIFACT),
        ("compositor", compositor_path, RT4_COMPOSITOR_ARTIFACT),
        (
            "analytic_sky_evidence",
            analytic_sky_evidence_path,
            RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT,
        ),
        (
            "analytic_sky_ppm",
            analytic_sky_ppm_path,
            RT4_ANALYTIC_SKY_PPM_ARTIFACT,
        ),
        ("repeat_report", repeat_report_path, RT4_REPEAT_REPORT_ARTIFACT),
        ("repeat_ppm", repeat_ppm_path, RT4_REPEAT_PPM_ARTIFACT),
        (
            "repeat_isolation",
            repeat_isolation_path,
            RT4_REPEAT_ISOLATION_ARTIFACT,
        ),
        (
            "repeat_reflection",
            repeat_reflection_path,
            RT4_REPEAT_REFLECTION_ARTIFACT,
        ),
        (
            "repeat_compositor",
            repeat_compositor_path,
            RT4_REPEAT_COMPOSITOR_ARTIFACT,
        ),
        (
            "repeat_analytic_sky_evidence",
            repeat_analytic_sky_evidence_path,
            RT4_REPEAT_ANALYTIC_SKY_EVIDENCE_ARTIFACT,
        ),
        (
            "repeat_analytic_sky_ppm",
            repeat_analytic_sky_ppm_path,
            RT4_REPEAT_ANALYTIC_SKY_PPM_ARTIFACT,
        ),
        ("executable", executable_path, executable_relative),
    ):
        _verify_attested_file(files.get(key), path, relative, "RT4", key)

    ror_source = build_contract["ror_source"]
    ogre_source = build_contract["provenance"]
    shader_contract = build_contract["shader_media"]
    notice = shader_contract["third_party_notice"]
    provenance = report.get("provenance")
    if not isinstance(provenance, dict):
        raise ArtifactSetError("RT4 report provenance is missing")
    expected_source = {
        "repository": ror_source.get("repository"),
        "ref": ror_source.get("ref"),
        "commit": ror_source.get("commit"),
        "relevant_manifest_sha256": ror_source.get("relevant_manifest_sha256"),
        "relevant_manifest_file_count": ror_source.get(
            "relevant_manifest_file_count"
        ),
    }
    expected_ogre = {
        key: ogre_source.get(key)
        for key in (
            "repository",
            "branch",
            "commit",
            "archive_sha256",
            "license_spdx",
            "license_sha256",
        )
    }
    expected_ogre["normal_map_source_lock_sha256"] = (
        NORMAL_MAP_SOURCE_LOCK_SHA256
    )
    expected_shader = {
        "root": shader_contract.get("root"),
        "license_expression": shader_contract.get("license_expression"),
        "source_path": notice.get("source_path"),
        "source_sha256": notice.get("source_sha256"),
        "notice_path": notice.get("notice_path"),
        "notice_sha256": notice.get("notice_sha256"),
        "manifest_sha256": provenance.get("shader_media_manifest_sha256"),
        "manifest_file_count": provenance.get("shader_media_manifest_file_count"),
        "hdr_manifest_sha256": provenance.get("hdr_media_manifest_sha256"),
        "hdr_manifest_file_count": provenance.get(
            "hdr_media_manifest_file_count"
        ),
    }
    expected_provenance = {
        "ror_repository": expected_source["repository"],
        "ror_ref": expected_source["ref"],
        "ror_commit": expected_source["commit"],
        "ror_relevant_source_manifest_sha256": expected_source[
            "relevant_manifest_sha256"
        ],
        "ror_relevant_source_manifest_file_count": expected_source[
            "relevant_manifest_file_count"
        ],
        "ogre_next_commit": expected_ogre["commit"],
        "ogre_next_archive_sha256": expected_ogre["archive_sha256"],
        "normal_map_source_lock_sha256": NORMAL_MAP_SOURCE_LOCK_SHA256,
        "shader_media_root": expected_shader["root"],
        "shader_media_license_expression": expected_shader[
            "license_expression"
        ],
        "shader_media_notice_path": expected_shader["notice_path"],
        "shader_media_notice_sha256": expected_shader["notice_sha256"],
        "shader_media_manifest_sha256": expected_shader["manifest_sha256"],
        "shader_media_manifest_file_count": expected_shader[
            "manifest_file_count"
        ],
        "hdr_media_manifest_sha256": expected_shader[
            "hdr_manifest_sha256"
        ],
        "hdr_media_manifest_file_count": expected_shader[
            "hdr_manifest_file_count"
        ],
    }
    if not _is_sha256(expected_shader["manifest_sha256"]) or not _is_positive_int(
        expected_shader["manifest_file_count"]
    ) or not _is_sha256(
        expected_shader["hdr_manifest_sha256"]
    ) or not _is_positive_int(expected_shader["hdr_manifest_file_count"]):
        raise ArtifactSetError("RT4 shader-media manifest identity is invalid")
    package_media_root = (
        root
        / "ror-ogre-next-n1-package"
        / "share"
        / "rigsofrods"
        / "ogre-next"
        / "Samples"
        / "Media"
    )
    packaged_hlms = _packaged_media_manifest(
        package_media_root / "Hlms", (Path("."),), "RT4 HLMS"
    )
    packaged_hdr = _packaged_media_manifest(
        package_media_root,
        (
            # Identical to the CMake HDR manifest roots and to
            # VerifyOgreNextN1HdrMedia's scan roots. RoRHaze carries the
            # RoR-owned aerial-haze material and shader siblings.
            Path("2.0/scripts/Compositors"),
            Path("2.0/scripts/materials/Common"),
            Path("2.0/scripts/materials/HDR"),
            Path("2.0/scripts/materials/RoRHaze"),
        ),
        "RT4 HDR",
    )
    packaged_reflection = _packaged_media_manifest(
        package_media_root,
        (
            Path("2.0/scripts/materials/LocalCubemaps"),
            Path("Compute/Algorithms/IBL"),
            Path("Compute/Tools/Any"),
        ),
        "RT4 reflection",
    )
    if (
        not _json_exact(
            packaged_hlms["sha256"], expected_shader["manifest_sha256"]
        )
        or not _json_exact(
            packaged_hlms["file_count"],
            expected_shader["manifest_file_count"],
        )
        or not _json_exact(
            packaged_hdr["sha256"], expected_shader["hdr_manifest_sha256"]
        )
        or not _json_exact(
            packaged_hdr["file_count"],
            expected_shader["hdr_manifest_file_count"],
        )
    ):
        raise ArtifactSetError("RT4 packaged shader-media manifest mismatch")
    if not _json_exact(attestation.get("source"), expected_source):
        raise ArtifactSetError("RT4 source attestation mismatch")
    if not _json_exact(attestation.get("ogre_next"), expected_ogre):
        raise ArtifactSetError("RT4 Ogre attestation mismatch")
    if not _json_exact(attestation.get("shader_media"), expected_shader):
        raise ArtifactSetError("RT4 shader-media attestation mismatch")
    if not _json_exact(provenance, expected_provenance):
        raise ArtifactSetError("RT4 build-contract provenance mismatch")

    policy = PLATFORM_CONTRACTS[platform_contract["policy"]]
    expected_adapter = {
        "frontend_version": "n1-ogre-3.0-" + expected_ogre["commit"],
        "native_mesh_path": "Ogre v2 Mesh plus immutable VertexArrayObject",
        "material_path": "HLMS PBS metallic-roughness",
        "brdf": "PbsBrdf::Default height-correlated GGX",
        "pbr_datablock_readback_verified": True,
        "raster_feature_tier": "MODERN_PBR_RT4_V1",
        "vertex_layout": "position_normal_tangent_uv0",
        "base_color_upload": "RGBA8_UNORM_SRGB",
        "metallic_roughness_upload": (
            "linear_G_to_R8_roughness_B_to_R8_metallic"
        ),
        "emissive_upload": "RGBA8_UNORM_SRGB",
        "normal_upload": "linear_RGBA8_positive_Z_to_RG8_UNORM",
        "padded_source_rows_verified": True,
        "portable_sampler_mapping_verified": True,
        "normal_texture_admitted": True,
        "normal_slot": "PBSM_NORMAL",
        "normal_uv_source": 0,
        "normal_scale": 1,
        "normal_map_weight": 1,
        "normal_positive_z_tolerance_decoded": "1/255",
        "occlusion_texture_admitted": False,
        "occlusion_blocker": "pinned_HLMS_PBS_has_no_ambient_only_AO_slot",
        "runtime_media_root": "explicit_absolute",
        "package_media_relative_path": (
            "share/rigsofrods/ogre-next/Samples/Media"
        ),
        "relocated_executable": True,
        "compositor2": True,
        "ui_included": False,
        "cpu_readback_completed": True,
        "analytic_lights_calibrated": True,
        "directional_lux_to_native_power_scale": 1.0 / 1024.0,
        "maximum_directional_lights": 1,
        "analytic_sky_capture_policy_version": 1,
        "analytic_sky_native_render_policy_version": 1,
        "analytic_sky_path": (
            "camera_centered_gradient_ground_additive_sun"
        ),
        "analytic_sky_exact_skyx_pixel_capture": False,
        "constant_environment_only": False,
        "native_interop": False,
        "ray_tracing": False,
    }
    expected_catalog = {
        "registry_id": 0x4E315F534D4F4B45,
        "sequence": 7,
        "baseline_sequence": 1,
        "live_replacement_count": 6,
        "referenced_texture_count": 4,
        "referenced_sampler_count": 1,
        "unreferenced_assets_not_uploaded": True,
        "transactional_replay_after_restart": True,
    }
    report_contract_checks = {
        "platform": report.get("platform_policy") == platform_contract["policy"],
        "renderer": report.get("renderer") == policy["renderer_name"],
        "adapter": _json_exact(report.get("adapter"), expected_adapter),
        "catalog": _json_exact(report.get("catalog"), expected_catalog),
    }
    failed_report_contract = sorted(
        name for name, passed in report_contract_checks.items() if not passed
    )
    if failed_report_contract:
        raise ArtifactSetError(
            "RT4 report contract mismatch: " + ", ".join(failed_report_contract)
        )

    computed_slices, compositor_slices, analytic_sky_slices = (
        _verify_rt4_semantics(
            report,
            ppm_path,
            isolation_path,
            compositor_path,
            analytic_sky_ppm_path,
            analytic_sky_evidence_path,
            build_contract,
        )
    )
    if not _json_exact(attestation.get("isolation_slices"), computed_slices):
        raise ArtifactSetError("RT4 SHA-256 slice attestation mismatch")
    reflection_slices = _verify_rt4_reflection_semantics(
        report, reflection_path, build_contract
    )
    if not _json_exact(
        attestation.get("reflection_slices"), reflection_slices
    ):
        raise ArtifactSetError("RT4 reflection SHA-256 slice attestation mismatch")
    if not _json_exact(attestation.get("compositor_slices"), compositor_slices):
        raise ArtifactSetError("RT4 compositor slice attestation mismatch")
    if not _json_exact(
        attestation.get("analytic_sky_slices"), analytic_sky_slices
    ):
        raise ArtifactSetError("RT4 analytic-sky slice attestation mismatch")
    repeat_report = _read_json_object(repeat_report_path, "RT4 repeat report")
    (
        repeat_slices,
        repeat_compositor_slices,
        repeat_analytic_sky_slices,
    ) = _verify_rt4_semantics(
        repeat_report,
        repeat_ppm_path,
        repeat_isolation_path,
        repeat_compositor_path,
        repeat_analytic_sky_ppm_path,
        repeat_analytic_sky_evidence_path,
        build_contract,
    )
    repeat_reflection_slices = _verify_rt4_reflection_semantics(
        repeat_report, repeat_reflection_path, build_contract
    )
    if (
        not _json_exact(repeat_slices, computed_slices)
        or not _json_exact(repeat_compositor_slices, compositor_slices)
        or not _json_exact(
            repeat_analytic_sky_slices, analytic_sky_slices
        )
        or not _json_exact(repeat_reflection_slices, reflection_slices)
    ):
        raise ArtifactSetError("RT4 deterministic repeat semantics differ")
    for primary, repeat in (
        (report_path, repeat_report_path),
        (ppm_path, repeat_ppm_path),
        (isolation_path, repeat_isolation_path),
        (reflection_path, repeat_reflection_path),
        (compositor_path, repeat_compositor_path),
        (analytic_sky_ppm_path, repeat_analytic_sky_ppm_path),
        (analytic_sky_evidence_path, repeat_analytic_sky_evidence_path),
    ):
        if sha256_file(primary) != sha256_file(repeat):
            raise ArtifactSetError(
                f"RT4 deterministic repeat bytes differ: {primary.name}"
            )
    packaged_paths = {
        entry[3]
        for media in (packaged_hlms, packaged_hdr, packaged_reflection)
        for entry in media["entries"]
    }
    for path in sorted(packaged_paths, key=lambda item: item.as_posix()):
        manifest.append(
            {
                "path": path.relative_to(root).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    manifest.append(
        {
            "path": executable_relative,
            "bytes": executable_path.stat().st_size,
            "sha256": sha256_file(executable_path),
        }
    )


def _number_matches(value: object, expected: float) -> bool:
    return (
        type(value) in (int, float)
        and math.isfinite(float(value))
        and math.isclose(float(value), expected, rel_tol=1.0e-7, abs_tol=1.0e-7)
    )


def _expected_pssm_provenance(
    build_contract: dict[str, object], report: dict[str, object]
) -> dict[str, object]:
    source = build_contract["ror_source"]
    ogre = build_contract["provenance"]
    provenance = report.get("provenance")
    if not all(isinstance(value, dict) for value in (source, ogre, provenance)):
        raise ArtifactSetError("PSSM provenance inputs are missing")
    return {
        "ror_repository": source.get("repository"),
        "ror_ref": source.get("ref"),
        "ror_commit": source.get("commit"),
        "ror_relevant_source_manifest_sha256": source.get(
            "relevant_manifest_sha256"
        ),
        "ogre_next_commit": ogre.get("commit"),
        "ogre_next_archive_sha256": ogre.get("archive_sha256"),
        "shader_media_manifest_sha256": provenance.get(
            "shader_media_manifest_sha256"
        ),
        "executable_build_identity": _expected_base_build_identity(
            build_contract, report
        ),
    }


def _pssm_entrypoint_bytes(payload: bytes, binary_format: str) -> bytes:
    file_offset: int | None = None
    if binary_format == "mach-o-64":
        _, _, _, _, command_count, command_bytes, _, _ = struct.unpack_from(
            "<IiiIIIII", payload, 0
        )
        offset = 32
        for _ in range(command_count):
            command, command_size = struct.unpack_from("<II", payload, offset)
            if command == 0x80000028 and command_size >= 24:
                file_offset = struct.unpack_from("<Q", payload, offset + 8)[0]
                break
            offset += command_size
    elif binary_format == "pe32+":
        pe_offset = struct.unpack_from("<I", payload, 0x3C)[0]
        _, section_count, _, _, _, optional_size, _ = struct.unpack_from(
            "<HHIIIHH", payload, pe_offset + 4
        )
        optional_offset = pe_offset + 24
        entry_rva = struct.unpack_from("<I", payload, optional_offset + 16)[0]
        section_offset = optional_offset + optional_size
        for index in range(section_count):
            offset = section_offset + index * 40
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", payload, offset + 8
            )
            extent = max(virtual_size, raw_size)
            if virtual_address <= entry_rva < virtual_address + extent:
                file_offset = raw_offset + entry_rva - virtual_address
                break
    elif binary_format == "elf64":
        entrypoint, program_offset = struct.unpack_from("<QQ", payload, 24)
        program_entry_size, program_count = struct.unpack_from("<HH", payload, 54)
        for index in range(program_count):
            offset = program_offset + index * program_entry_size
            segment_type, flags = struct.unpack_from("<II", payload, offset)
            segment_offset, virtual_address = struct.unpack_from("<QQ", payload, offset + 8)
            file_size = struct.unpack_from("<Q", payload, offset + 32)[0]
            if (
                segment_type == 1
                and flags & 0x1
                and virtual_address <= entrypoint < virtual_address + file_size
            ):
                file_offset = segment_offset + entrypoint - virtual_address
                break
    if file_offset is None or file_offset < 0 or file_offset + 16 > len(payload):
        raise ArtifactSetError("PSSM executable entrypoint is not file-backed")
    return payload[file_offset : file_offset + 32]


def _verify_pssm_executable(
    path: Path,
    build_contract: dict[str, object],
    report: dict[str, object],
) -> None:
    size = path.stat().st_size
    if size < 64 * 1024 or size > 512 * 1024 * 1024:
        raise ArtifactSetError("PSSM executable byte count is structurally implausible")
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(f"could not read PSSM executable: {error}") from error
    policy_name = build_contract["platform"]["policy"]
    policy = PLATFORM_CONTRACTS[policy_name]
    verifier = {
        "mach-o-64": _verify_mach_o_64,
        "pe32+": _verify_pe32_plus,
        "elf64": _verify_elf64,
    }[policy["binary_format"]]
    expected_structure = {
        "format": policy["binary_format"],
        "architecture": policy["binary_architecture"],
    }
    if verifier(payload) != expected_structure:
        raise ArtifactSetError("PSSM executable platform structure mismatch")
    entrypoint = _pssm_entrypoint_bytes(payload, policy["binary_format"])
    if (
        not any(byte != 0 for byte in entrypoint)
        or len(set(entrypoint)) < 4
        or not any(byte < 0x20 or byte > 0x7E for byte in entrypoint)
    ):
        raise ArtifactSetError(
            "PSSM executable entrypoint contains no plausible machine code"
        )
    if _requires_posix_executable_permission(policy["binary_format"]) and (
        path.stat().st_mode & 0o111 == 0
    ):
        raise ArtifactSetError("PSSM packaged executable has no execute permission")
    identity = _expected_base_build_identity(build_contract, report)
    if payload.count(identity.encode()) != 1:
        raise ArtifactSetError("PSSM executable build identity is missing or ambiguous")
    required_tokens = (
        PSSM_REPORT_SCHEMA,
        "--media-root",
        "--execution-challenge",
        "PSSM_3_CASCADE_V1",
        PSSM_UNSUPPORTED_DETAIL,
        policy["renderer_name"],
    )
    missing = [token for token in required_tokens if token.encode() not in payload]
    if missing:
        raise ArtifactSetError("PSSM executable contract strings are incomplete")


def _pssm_pair_metrics(
    baseline: bytes,
    shadowed: bytes,
    *,
    hdr: bool,
    receiver: tuple[int, int, int, int],
    occluder: tuple[int, int, int, int],
) -> tuple[int, int]:
    width = 192
    height = 128
    bytes_per_pixel = 8 if hdr else 4
    expected_bytes = width * height * bytes_per_pixel
    if len(baseline) != expected_bytes or len(shadowed) != expected_bytes:
        raise ArtifactSetError("PSSM evidence pair extent is invalid")
    changed = 0
    darkened = 0
    for pixel_index in range(width * height):
        offset = pixel_index * bytes_per_pixel
        left = baseline[offset : offset + bytes_per_pixel]
        right = shadowed[offset : offset + bytes_per_pixel]
        if hdr:
            left_channels = struct.unpack("<4e", left)
            right_channels = struct.unpack("<4e", right)
            if not all(
                math.isfinite(channel)
                for channel in left_channels + right_channels
            ):
                raise ArtifactSetError("PSSM HDR evidence contains non-finite data")
            left_luminance = (
                0.2126 * left_channels[0]
                + 0.7152 * left_channels[1]
                + 0.0722 * left_channels[2]
            )
            right_luminance = (
                0.2126 * right_channels[0]
                + 0.7152 * right_channels[1]
                + 0.0722 * right_channels[2]
            )
        else:
            left_luminance = 0.2126 * left[0] + 0.7152 * left[1] + 0.0722 * left[2]
            right_luminance = (
                0.2126 * right[0] + 0.7152 * right[1] + 0.0722 * right[2]
            )
        if left == right:
            continue
        changed += 1
        x = pixel_index % width
        y = pixel_index // width
        in_receiver = receiver[0] <= x <= receiver[1] and receiver[2] <= y <= receiver[3]
        in_occluder = occluder[0] <= x <= occluder[1] and occluder[2] <= y <= occluder[3]
        if not in_receiver or in_occluder:
            raise ArtifactSetError("PSSM visual change escaped its reviewed receiver region")
        if right_luminance < left_luminance:
            darkened += 1
    if changed < 16 or darkened * 10 < changed * 9:
        raise ArtifactSetError("PSSM evidence has no isolated receiver-local shadow")
    return changed, darkened


def _require_pssm_aabb(
    value: object,
    expected_minimum: tuple[float, float, float],
    expected_maximum: tuple[float, float, float],
    context: str,
) -> None:
    aabb = _require_exact_keys(value, {"minimum", "maximum"}, context)
    minimum = aabb.get("minimum")
    maximum = aabb.get("maximum")
    if not (
        isinstance(minimum, list)
        and isinstance(maximum, list)
        and len(minimum) == 3
        and len(maximum) == 3
        and all(
            _number_matches(observed, expected)
            for observed, expected in zip(minimum, expected_minimum)
        )
        and all(
            _number_matches(observed, expected)
            for observed, expected in zip(maximum, expected_maximum)
        )
    ):
        raise ArtifactSetError(f"{context} differs from the exact native AABB")


def _verify_pssm_pass(
    root: Path,
    report: dict[str, object],
    manifest: list[dict[str, object]],
) -> None:
    _require_exact_keys(
        report,
        {
            "schema",
            "status",
            "execution",
            "provenance",
            "platform_policy",
            "renderer",
            "shadow_contract",
            "isolation",
            "distant_cascade_proof",
            "projection_and_bounds_fixture",
            "lifecycle",
            "evidence",
        },
        "PSSM pass report",
    )
    shadow_contract = _require_exact_keys(
        report.get("shadow_contract"),
        {
            "version",
            "mode",
            "cascade_count",
            "split_points_m",
            "blend_points_m",
            "fade_point_m",
            "atlas",
            "filter",
            "programmatic_compositor2",
            "ui_included",
            "backend_substitution",
            "split_stable_tangent_projection",
            "native_definition_split_and_runtime_bias_readback",
            "native_d32_probe_attempted",
            "native_d32_atlas_allocation_use_readback_verified",
            "native_d32_atlas_cleanup_verified",
            "native_d32_atlas_cleanup_absence_checks",
            "runtime_normal_offset_bias",
        },
        "PSSM shadow contract",
    )
    expected_splits = (0.5, 7.81633186, 45.2411156, 350.0)
    expected_blends = (6.90179062, 40.5630188)
    splits = shadow_contract.get("split_points_m")
    blends = shadow_contract.get("blend_points_m")
    biases = shadow_contract.get("runtime_normal_offset_bias")
    contract_checks = {
        "version": _json_exact(shadow_contract.get("version"), 1),
        "mode": shadow_contract.get("mode") == "PSSM_3_CASCADE_V1",
        "cascade_count": _json_exact(shadow_contract.get("cascade_count"), 3),
        "splits": isinstance(splits, list)
        and len(splits) == 4
        and all(_number_matches(value, expected) for value, expected in zip(splits, expected_splits)),
        "blends": isinstance(blends, list)
        and len(blends) == 2
        and all(_number_matches(value, expected) for value, expected in zip(blends, expected_blends)),
        "fade": _number_matches(shadow_contract.get("fade_point_m"), 254.610474),
        "atlas": _json_exact(
            shadow_contract.get("atlas"),
            {"format": "D32_FLOAT", "width": 2048, "height": 3072},
        ),
        "filter": shadow_contract.get("filter") == "PCF_4x4",
        "compositor": shadow_contract.get("programmatic_compositor2") is True,
        "ui_free": shadow_contract.get("ui_included") is False,
        "native": shadow_contract.get("backend_substitution") is False
        and shadow_contract.get("split_stable_tangent_projection") is True
        and shadow_contract.get(
            "native_definition_split_and_runtime_bias_readback"
        )
        is True
        and shadow_contract.get("native_d32_probe_attempted") is True
        and shadow_contract.get(
            "native_d32_atlas_allocation_use_readback_verified"
        )
        is True
        and shadow_contract.get("native_d32_atlas_cleanup_verified") is True
        and _json_exact(
            shadow_contract.get("native_d32_atlas_cleanup_absence_checks"), 1
        ),
        "biases": isinstance(biases, list)
        and len(biases) == 3
        and all(
            type(value) in (int, float)
            and math.isfinite(float(value))
            and float(value) >= 168.0
            for value in biases
        ),
    }
    failed_contract = sorted(
        name for name, passed in contract_checks.items() if not passed
    )
    if failed_contract:
        raise ArtifactSetError(
            "PSSM shadow contract mismatch: " + ", ".join(failed_contract)
        )

    isolation = _require_exact_keys(
        report.get("isolation"),
        {
            "controlled_visual_change",
            "nonvisual_snapshot_identity_changed",
            "changed_pixels_outside_reviewed_receiver_region",
            "changed_pixels_inside_reviewed_occluder_region",
            "hdr_changed_receiver_pixels",
            "hdr_darkened_receiver_pixels",
            "sdr_changed_receiver_pixels",
            "sdr_darkened_receiver_pixels",
            "normalized_visibility_mask_0x1_verified",
            "shadow_disabled_default_equals_explicit",
            "shadow_disabled_exact_fnv1a64",
        },
        "PSSM isolation",
    )
    isolation_controls = {
        "controlled_change": isolation.get("controlled_visual_change")
        == "occluder_instance_casts_shadow",
        "snapshot_identity_disclosed": isolation.get(
            "nonvisual_snapshot_identity_changed"
        )
        is True,
        "receiver_only": _json_exact(
            isolation.get("changed_pixels_outside_reviewed_receiver_region"), 0
        )
        and _json_exact(
            isolation.get("changed_pixels_inside_reviewed_occluder_region"), 0
        ),
        "mask": isolation.get("normalized_visibility_mask_0x1_verified") is True,
        "disabled": isolation.get("shadow_disabled_default_equals_explicit")
        is True,
        "disabled_hash": isinstance(
            isolation.get("shadow_disabled_exact_fnv1a64"), str
        )
        and re.fullmatch(
            r"[0-9a-f]{16}", isolation["shadow_disabled_exact_fnv1a64"]
        )
        is not None,
    }
    failed_isolation = sorted(
        name for name, passed in isolation_controls.items() if not passed
    )
    if failed_isolation:
        raise ArtifactSetError(
            "PSSM isolation controls failed: " + ", ".join(failed_isolation)
        )

    lifecycle = _require_exact_keys(
        report.get("lifecycle"),
        {
            "shadow_frames_completed",
            "shadow_node_creates",
            "shadow_node_destroys",
            "workspace_node_definition_creates",
            "workspace_node_definition_destroys",
            "receiver_datablock_creates",
            "receiver_datablock_destroys",
            "d32_atlas_cleanup_absence_checks",
            "workspace_definition_cleanup_absence_checks",
            "workspace_node_cleanup_absence_checks",
            "shadow_node_cleanup_absence_checks",
            "receiver_datablock_cleanup_absence_checks",
            "target_texture_cleanup_absence_checks",
            "d32_post_create_same_instance_retry_verified",
            "d32_cleanup_lookup_failure_closed_retry_verified",
            "receiver_clone_same_frame_retry_verified",
            "workspace_node_same_frame_retry_verified",
            "receiver_cleanup_lookup_failure_closed_retry_verified",
            "workspace_definition_cleanup_lookup_failure_closed_retry_verified",
            "workspace_cleanup_lookup_failure_closed_retry_verified",
            "shadow_cleanup_lookup_failure_closed_retry_verified",
            "target_cleanup_lookup_failure_closed_retry_verified",
        },
        "PSSM lifecycle",
    )
    if not _json_exact(
        lifecycle,
        {
            "shadow_frames_completed": 10,
            "shadow_node_creates": 10,
            "shadow_node_destroys": 10,
            "workspace_node_definition_creates": 10,
            "workspace_node_definition_destroys": 10,
            # v5: the PSSM non-receiver clone lives for its retained
            # instance's lifetime — one create at admission, one destroy
            # (with absence proof) at shutdown — instead of one per frame.
            "receiver_datablock_creates": 1,
            "receiver_datablock_destroys": 1,
            "d32_atlas_cleanup_absence_checks": 1,
            "workspace_definition_cleanup_absence_checks": 10,
            "workspace_node_cleanup_absence_checks": 10,
            "shadow_node_cleanup_absence_checks": 10,
            "receiver_datablock_cleanup_absence_checks": 1,
            "target_texture_cleanup_absence_checks": 10,
            "d32_post_create_same_instance_retry_verified": True,
            "d32_cleanup_lookup_failure_closed_retry_verified": True,
            "receiver_clone_same_frame_retry_verified": True,
            "workspace_node_same_frame_retry_verified": True,
            "receiver_cleanup_lookup_failure_closed_retry_verified": True,
            "workspace_definition_cleanup_lookup_failure_closed_retry_verified": True,
            "workspace_cleanup_lookup_failure_closed_retry_verified": True,
            "shadow_cleanup_lookup_failure_closed_retry_verified": True,
            "target_cleanup_lookup_failure_closed_retry_verified": True,
        },
    ):
        raise ArtifactSetError("PSSM lifecycle and transactional retry proof is invalid")

    evidence = _require_exact_keys(
        report.get("evidence"),
        {
            "file",
            "bytes",
            "hdr_no_occluder_fnv1a64",
            "hdr_occluder_fnv1a64",
            "sdr_no_occluder_fnv1a64",
            "sdr_occluder_fnv1a64",
            "cascade_2_sdr_no_occluder_fnv1a64",
            "cascade_2_sdr_occluder_fnv1a64",
            "cascade_3_sdr_no_occluder_fnv1a64",
            "cascade_3_sdr_occluder_fnv1a64",
            "off_center_tight_bounds_sdr_no_occluder_fnv1a64",
            "off_center_tight_bounds_sdr_occluder_fnv1a64",
        },
        "PSSM evidence metadata",
    )
    evidence_path = root / PSSM_EVIDENCE_ARTIFACT
    if evidence_path.is_symlink() or not evidence_path.is_file():
        raise ArtifactSetError(f"missing: {PSSM_EVIDENCE_ARTIFACT}")
    try:
        payload = evidence_path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(f"could not read PSSM evidence: {error}") from error
    if evidence.get("file") != PSSM_EVIDENCE_ARTIFACT or not _json_exact(
        evidence.get("bytes"), len(payload)
    ):
        raise ArtifactSetError("PSSM evidence identity or byte count is invalid")
    segment_specs = (
        ("hdr_no_occluder_fnv1a64", 192 * 128 * 8),
        ("hdr_occluder_fnv1a64", 192 * 128 * 8),
        ("sdr_no_occluder_fnv1a64", 192 * 128 * 4),
        ("sdr_occluder_fnv1a64", 192 * 128 * 4),
        ("cascade_2_sdr_no_occluder_fnv1a64", 192 * 128 * 4),
        ("cascade_2_sdr_occluder_fnv1a64", 192 * 128 * 4),
        ("cascade_3_sdr_no_occluder_fnv1a64", 192 * 128 * 4),
        ("cascade_3_sdr_occluder_fnv1a64", 192 * 128 * 4),
        ("off_center_tight_bounds_sdr_no_occluder_fnv1a64", 192 * 128 * 4),
        ("off_center_tight_bounds_sdr_occluder_fnv1a64", 192 * 128 * 4),
    )
    slices: list[bytes] = []
    offset = 0
    for field, size in segment_specs:
        segment = payload[offset : offset + size]
        if len(segment) != size or evidence.get(field) != _fnv1a64(segment):
            raise ArtifactSetError(f"PSSM evidence slice mismatch: {field}")
        slices.append(segment)
        offset += size
    if offset != len(payload):
        raise ArtifactSetError("PSSM evidence has trailing bytes")
    near_region = (34, 158, 18, 110)
    near_occluder = (80, 111, 48, 79)
    off_axis_region = (68, 191, 18, 110)
    off_axis_occluder = (192, 192, 128, 128)
    hdr_metrics = _pssm_pair_metrics(
        slices[0], slices[1], hdr=True, receiver=near_region, occluder=near_occluder
    )
    sdr_metrics = _pssm_pair_metrics(
        slices[2], slices[3], hdr=False, receiver=near_region, occluder=near_occluder
    )
    reported_near = (
        isolation.get("hdr_changed_receiver_pixels"),
        isolation.get("hdr_darkened_receiver_pixels"),
        isolation.get("sdr_changed_receiver_pixels"),
        isolation.get("sdr_darkened_receiver_pixels"),
    )
    if not all(
        _json_exact(reported, computed)
        for reported, computed in zip(reported_near, hdr_metrics + sdr_metrics)
    ):
        raise ArtifactSetError("PSSM near-cascade report differs from evidence")

    distant = report.get("distant_cascade_proof")
    if not isinstance(distant, list) or len(distant) != 2:
        raise ArtifactSetError("PSSM distant-cascade proof is missing")
    expected_distant = ((1, 20.0, 12.5), (2, 100.0, 62.5))
    for index, (entry, expected) in enumerate(zip(distant, expected_distant)):
        entry = _require_exact_keys(
            entry,
            {
                "cascade_index",
                "receiver_depth_m",
                "occluder_depth_m",
                "off_axis",
                "sdr_changed_receiver_pixels",
                "sdr_darkened_receiver_pixels",
            },
            f"PSSM distant cascade {index + 2}",
        )
        computed = _pssm_pair_metrics(
            slices[4 + index * 2],
            slices[5 + index * 2],
            hdr=False,
            receiver=off_axis_region,
            occluder=off_axis_occluder,
        )
        if not (
            _json_exact(entry.get("cascade_index"), expected[0])
            and _number_matches(entry.get("receiver_depth_m"), expected[1])
            and _number_matches(entry.get("occluder_depth_m"), expected[2])
            and entry.get("off_axis") is True
            and _json_exact(entry.get("sdr_changed_receiver_pixels"), computed[0])
            and _json_exact(entry.get("sdr_darkened_receiver_pixels"), computed[1])
        ):
            raise ArtifactSetError("PSSM distant-cascade report differs from evidence")

    fixture = _require_exact_keys(
        report.get("projection_and_bounds_fixture"),
        {
            "horizontal_lens_offset",
            "vertical_lens_offset",
            "expected_tangent_extents",
            "off_center_projection_verified",
            "receiver_bounds_min_z",
            "receiver_bounds_max_z",
            "caster_bounds_min_z",
            "caster_bounds_max_z",
            "tight_caster_bounds_verified",
            "native_bounds_readback_verified",
            "native_aabb_observations",
            "sdr_changed_pixels",
            "sdr_darkened_pixels",
        },
        "PSSM projection and bounds fixture",
    )
    fixture_metrics = _pssm_pair_metrics(
        slices[8],
        slices[9],
        hdr=False,
        receiver=(0, 191, 0, 127),
        occluder=(192, 192, 128, 128),
    )
    extents = fixture.get("expected_tangent_extents")
    observations = fixture.get("native_aabb_observations")
    if not isinstance(observations, list) or len(observations) != 2:
        raise ArtifactSetError("PSSM native AABB observation set is incomplete")
    expected_observations = (
        {
            "instance_id": 1,
            "casts_shadow": True,
            "receives_shadow": True,
            "local_minimum": (-2.5, -1.8, 0.0),
            "local_maximum": (2.5, 1.8, 0.0),
            "world_minimum": (-2.5, -1.8, 0.0),
            "world_maximum": (2.5, 1.8, 0.0),
        },
        {
            "instance_id": 2,
            "casts_shadow": True,
            "receives_shadow": False,
            "local_minimum": (-0.45, -0.45, 0.0),
            "local_maximum": (0.45, 0.45, 0.0),
            "world_minimum": (-0.45, -0.45, 1.5),
            "world_maximum": (0.45, 0.45, 1.5),
        },
    )
    for index, (observation, expected) in enumerate(
        zip(observations, expected_observations)
    ):
        observation = _require_exact_keys(
            observation,
            {
                "instance_id",
                "casts_shadow",
                "receives_shadow",
                "expected_local",
                "ogre_mesh_local",
                "ogre_item_local",
                "expected_world",
                "ogre_item_world",
            },
            f"PSSM native AABB observation {index}",
        )
        if not (
            _json_exact(observation.get("instance_id"), expected["instance_id"])
            and observation.get("casts_shadow") is expected["casts_shadow"]
            and observation.get("receives_shadow")
            is expected["receives_shadow"]
        ):
            raise ArtifactSetError("PSSM native AABB role classification changed")
        for field in ("expected_local", "ogre_mesh_local", "ogre_item_local"):
            _require_pssm_aabb(
                observation.get(field),
                expected["local_minimum"],
                expected["local_maximum"],
                f"PSSM native AABB observation {index} {field}",
            )
        for field in ("expected_world", "ogre_item_world"):
            _require_pssm_aabb(
                observation.get(field),
                expected["world_minimum"],
                expected["world_maximum"],
                f"PSSM native AABB observation {index} {field}",
            )
    if not (
        _number_matches(fixture.get("horizontal_lens_offset"), 0.25)
        and _number_matches(fixture.get("vertical_lens_offset"), -0.125)
        and isinstance(extents, list)
        and len(extents) == 4
        and all(
            _number_matches(value, expected)
            for value, expected in zip(
                extents, (-0.75, 1.25, 0.875 / 1.5, -0.75)
            )
        )
        and fixture.get("off_center_projection_verified") is True
        and _number_matches(fixture.get("receiver_bounds_min_z"), 0.0)
        and _number_matches(fixture.get("receiver_bounds_max_z"), 0.0)
        and _number_matches(fixture.get("caster_bounds_min_z"), 0.0)
        and _number_matches(fixture.get("caster_bounds_max_z"), 0.0)
        and fixture.get("tight_caster_bounds_verified") is True
        and fixture.get("native_bounds_readback_verified") is True
        and _json_exact(fixture.get("sdr_changed_pixels"), fixture_metrics[0])
        and _json_exact(fixture.get("sdr_darkened_pixels"), fixture_metrics[1])
    ):
        raise ArtifactSetError(
            "PSSM off-center projection or exact-bounds fixture differs from evidence"
        )
    manifest.append(
        {
            "path": PSSM_EVIDENCE_ARTIFACT,
            "bytes": evidence_path.stat().st_size,
            "sha256": sha256_file(evidence_path),
        }
    )


def _verify_pssm_workflow_identity(
    workflow: object, source: dict[str, object]
) -> dict[str, object]:
    value = _require_exact_keys(
        workflow,
        {
            "provider",
            "repository",
            "workflow_ref",
            "run_id",
            "run_attempt",
            "sha",
            "ref",
            "job",
            "external_dsse_required",
        },
        "PSSM workflow identity",
    )
    provider = value.get("provider")
    if provider == "local":
        if not _json_exact(
            value,
            {
                "provider": "local",
                "repository": "",
                "workflow_ref": "",
                "run_id": "",
                "run_attempt": "",
                "sha": "",
                "ref": "",
                "job": "",
                "external_dsse_required": False,
            },
        ):
            raise ArtifactSetError("local PSSM workflow identity is not exact")
        return value
    fields = ("repository", "workflow_ref", "run_id", "run_attempt", "sha", "ref", "job")
    if not (
        provider == "github-actions"
        and all(isinstance(value.get(field), str) and value[field] for field in fields)
        and value.get("repository") == "oasiz-ai/rigs-of-rods"
        and value.get("workflow_ref", "").startswith(
            "oasiz-ai/rigs-of-rods/.github/workflows/ogre-next-probe.yml@"
        )
        and value.get("sha") == source.get("commit")
        and value.get("external_dsse_required") is True
    ):
        raise ArtifactSetError("GitHub PSSM workflow identity is not exact")
    return value


def _verify_pssm_integrity(
    root: Path,
    report: dict[str, object],
    build_contract: dict[str, object],
    executable_path: Path,
    executable_relative: str,
    manifest: list[dict[str, object]],
) -> None:
    receipt_path = root / PSSM_EXECUTION_RECEIPT_ARTIFACT
    attestation_path = root / PSSM_ATTESTATION_ARTIFACT
    artifact_manifest_path = root / PSSM_ARTIFACT_MANIFEST_ARTIFACT
    for path, label in (
        (receipt_path, "execution receipt"),
        (attestation_path, "attestation"),
        (artifact_manifest_path, "artifact manifest"),
    ):
        if path.is_symlink() or not path.is_file() or path.stat().st_size <= 0:
            raise ArtifactSetError(f"PSSM {label} is missing, empty, or indirect")

    def record(path: Path, relative: str) -> dict[str, object]:
        return {
            "path": relative,
            "bytes": path.stat().st_size,
            "sha256": sha256_file(path),
        }

    status = report.get("status")
    source = build_contract["ror_source"]
    build_identity = report["provenance"]["executable_build_identity"]
    evidence_path = root / PSSM_EVIDENCE_ARTIFACT
    subjects = {
        "build_contract": record(root / REQUIRED_ARTIFACTS[0], REQUIRED_ARTIFACTS[0]),
        "executable": record(executable_path, executable_relative),
        "report": record(root / PSSM_REPORT_ARTIFACT, PSSM_REPORT_ARTIFACT),
        "evidence": (
            record(evidence_path, PSSM_EVIDENCE_ARTIFACT)
            if status == "pass"
            else None
        ),
    }

    receipt = _read_json_object(receipt_path, "PSSM execution receipt")
    _require_exact_keys(
        receipt,
        {
            "schema",
            "status",
            "observation",
            "subjects",
            "build_identity",
            "source",
            "workflow",
            "complete",
        },
        "PSSM execution receipt",
    )
    observation = _require_exact_keys(
        receipt.get("observation"),
        {
            "mode",
            "challenge_nonce",
            "observed_process_exit_code",
            "offline_cryptographic_execution_proof",
            "limitation",
        },
        "PSSM execution observation",
    )
    execution = report["execution"]
    expected_exit = 0 if status == "pass" else 77
    if not (
        receipt.get("schema") == PSSM_EXECUTION_RECEIPT_SCHEMA
        and receipt.get("status") == status
        and observation.get("mode") == "fresh_child_process_challenge"
        and observation.get("challenge_nonce") == execution["challenge_nonce"]
        and _json_exact(observation.get("observed_process_exit_code"), expected_exit)
        and observation.get("offline_cryptographic_execution_proof") is False
        and observation.get("limitation") == PSSM_OFFLINE_EXECUTION_LIMITATION
        and _json_exact(receipt.get("subjects"), subjects)
        and receipt.get("build_identity") == build_identity
        and _json_exact(receipt.get("source"), source)
        and receipt.get("complete") is True
    ):
        raise ArtifactSetError("PSSM challenged execution receipt is invalid")
    workflow = _verify_pssm_workflow_identity(receipt.get("workflow"), source)

    receipt_record = record(receipt_path, PSSM_EXECUTION_RECEIPT_ARTIFACT)
    attestation = _read_json_object(attestation_path, "PSSM attestation")
    _require_exact_keys(
        attestation,
        {
            "schema",
            "status",
            "integrity_model",
            "source",
            "workflow",
            "build_identity",
            "files",
            "complete",
        },
        "PSSM attestation",
    )
    expected_files = {**subjects, "execution_receipt": receipt_record}
    if not (
        attestation.get("schema") == PSSM_ATTESTATION_SCHEMA
        and attestation.get("status") == status
        and attestation.get("integrity_model")
        == (
            "atomic-self-contained-sha256-plus-challenged-execution-receipt; "
            "external-github-dsse-required-in-ci"
        )
        and _json_exact(attestation.get("source"), source)
        and _json_exact(attestation.get("workflow"), workflow)
        and attestation.get("build_identity") == build_identity
        and _json_exact(attestation.get("files"), expected_files)
        and attestation.get("complete") is True
    ):
        raise ArtifactSetError("PSSM atomic attestation is invalid")

    attestation_record = record(attestation_path, PSSM_ATTESTATION_ARTIFACT)
    expected_artifacts = sorted(
        [entry for entry in subjects.values() if entry is not None]
        + [receipt_record, attestation_record],
        key=lambda entry: entry["path"],
    )
    persisted_manifest = _read_json_object(
        artifact_manifest_path, "PSSM artifact manifest"
    )
    _require_exact_keys(
        persisted_manifest,
        {
            "schema",
            "status",
            "source",
            "workflow",
            "build_identity",
            "artifacts",
            "complete",
        },
        "PSSM artifact manifest",
    )
    if not (
        persisted_manifest.get("schema") == PSSM_ARTIFACT_MANIFEST_SCHEMA
        and persisted_manifest.get("status") == status
        and _json_exact(persisted_manifest.get("source"), source)
        and _json_exact(persisted_manifest.get("workflow"), workflow)
        and persisted_manifest.get("build_identity") == build_identity
        and _json_exact(persisted_manifest.get("artifacts"), expected_artifacts)
        and persisted_manifest.get("complete") is True
    ):
        raise ArtifactSetError("PSSM persisted artifact manifest is invalid")

    manifest.extend(
        (
            receipt_record,
            attestation_record,
            record(artifact_manifest_path, PSSM_ARTIFACT_MANIFEST_ARTIFACT),
        )
    )


def _verify_pssm(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
    *,
    require_pass: bool = False,
) -> None:
    report_path = root / PSSM_REPORT_ARTIFACT
    report = _read_json_object(report_path, "PSSM report")
    if report.get("schema") != PSSM_REPORT_SCHEMA:
        raise ArtifactSetError("PSSM report schema is invalid")
    execution = _require_exact_keys(
        report.get("execution"),
        {"schema", "challenge_nonce"},
        "PSSM execution challenge",
    )
    if (
        execution.get("schema") != PSSM_EXECUTION_SCHEMA
        or not isinstance(execution.get("challenge_nonce"), str)
        or re.fullmatch(r"[0-9a-f]{64}", execution["challenge_nonce"]) is None
    ):
        raise ArtifactSetError("PSSM execution challenge is invalid")
    provenance = _require_exact_keys(
        report.get("provenance"),
        {
            "ror_repository",
            "ror_ref",
            "ror_commit",
            "ror_relevant_source_manifest_sha256",
            "ogre_next_commit",
            "ogre_next_archive_sha256",
            "shader_media_manifest_sha256",
            "executable_build_identity",
        },
        "PSSM provenance",
    )
    if not _is_sha256(provenance.get("shader_media_manifest_sha256")):
        raise ArtifactSetError("PSSM shader-media manifest identity is invalid")
    if not _json_exact(
        provenance, _expected_pssm_provenance(build_contract, report)
    ):
        raise ArtifactSetError("PSSM report provenance mismatch")
    platform_name = build_contract["platform"]["policy"]
    policy = PLATFORM_CONTRACTS[platform_name]
    if (
        report.get("platform_policy") != platform_name
        or report.get("renderer") != policy["renderer_name"]
    ):
        raise ArtifactSetError("PSSM platform or renderer identity mismatch")
    executable_suffix = ".exe" if platform_name == "windows-x64-d3d11" else ""
    executable_relative = f"bin/{PSSM_EXECUTABLE_STEM}{executable_suffix}"
    executable_path = root / executable_relative
    if executable_path.is_symlink() or not executable_path.is_file():
        raise ArtifactSetError(f"missing: {executable_relative}")
    _verify_pssm_executable(executable_path, build_contract, report)
    manifest.append(
        {
            "path": executable_relative,
            "bytes": executable_path.stat().st_size,
            "sha256": sha256_file(executable_path),
        }
    )
    status = report.get("status")
    if require_pass and status != "pass":
        raise ArtifactSetError(
            "declared platform job requires an actual PSSM native pass"
        )
    if status == "pass":
        _verify_pssm_pass(root, report, manifest)
    elif status != "unsupported":
        raise ArtifactSetError("PSSM report did not pass or fail closed")
    else:
        _require_exact_keys(
            report,
            {
                "schema",
                "status",
                "execution",
                "provenance",
                "platform_policy",
                "renderer",
                "capability_evidence",
                "backend_substitution",
            },
            "PSSM unsupported report",
        )
        capability = _require_exact_keys(
            report.get("capability_evidence"),
            {
                "code",
                "reason",
                "required_atlas_width",
                "required_atlas_height",
                "required_format",
                "required_filter",
                "observed_maximum_texture_dimension",
                "atlas_dimensions_supported",
                "texture_gather_supported",
                "d32_probe_attempted",
                "d32_render_target_supported",
                "d32_atlas_allocation_verified",
                "d32_atlas_readback_verified",
                "d32_atlas_cleanup_verified",
                "d32_atlas_cleanup_absence_checks",
            },
            "PSSM unsupported capability evidence",
        )
        maximum = capability.get("observed_maximum_texture_dimension")
        booleans = (
            capability.get("atlas_dimensions_supported"),
            capability.get("texture_gather_supported"),
            capability.get("d32_probe_attempted"),
            capability.get("d32_render_target_supported"),
            capability.get("d32_atlas_allocation_verified"),
            capability.get("d32_atlas_readback_verified"),
            capability.get("d32_atlas_cleanup_verified"),
        )
        unsupported_valid = (
            capability.get("code") == "PSSM_REQUIRED_NATIVE_CAPABILITY_MISSING"
            and capability.get("reason") == PSSM_UNSUPPORTED_DETAIL
            and _json_exact(capability.get("required_atlas_width"), 2048)
            and _json_exact(capability.get("required_atlas_height"), 3072)
            and capability.get("required_format") == "D32_FLOAT"
            and capability.get("required_filter") == "PCF_4x4_TEXTURE_GATHER"
            and type(maximum) is int
            and maximum > 0
            and all(type(value) is bool for value in booleans)
            and booleans[0] is (maximum >= 3072)
            and booleans[2] is False
            and booleans[3] is False
            and booleans[4] is False
            and booleans[5] is False
            and booleans[6] is True
            and _json_exact(
                capability.get("d32_atlas_cleanup_absence_checks"), 1
            )
            and not (booleans[0] and booleans[1])
            and report.get("backend_substitution") is False
        )
        if not unsupported_valid:
            raise ArtifactSetError("PSSM unsupported capability evidence is not exact")
        if (root / PSSM_EVIDENCE_ARTIFACT).exists():
            raise ArtifactSetError("unsupported PSSM report retained stale pass evidence")

    _verify_pssm_integrity(
        root,
        report,
        build_contract,
        executable_path,
        executable_relative,
        manifest,
    )


def _metal_n3_image_metrics(payload: bytes) -> dict[str, int | float | str]:
    width = 96
    height = 64
    if len(payload) != width * height * 8:
        raise ArtifactSetError("Metal N3 image extent/byte count mismatch")
    luminance_sum = 0.0
    nontrivial_pixels = 0
    for offset in range(0, len(payload), 8):
        channels = struct.unpack_from("<4e", payload, offset)
        if not all(math.isfinite(channel) for channel in channels):
            raise ArtifactSetError("Metal N3 image contains non-finite data")
        luminance_sum += (
            0.2126 * channels[0]
            + 0.7152 * channels[1]
            + 0.0722 * channels[2]
        )
        if any(abs(channel) > 1.0e-6 for channel in channels[:3]):
            nontrivial_pixels += 1
    return {
        "width": width,
        "height": height,
        "format": "RGBA16_FLOAT",
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "mean_luminance": luminance_sum / (width * height),
        "nontrivial_pixels": nontrivial_pixels,
    }


def _verify_metal_n3_reported_metrics(
    reported: object,
    computed: dict[str, int | float | str],
    label: str,
) -> None:
    if not isinstance(reported, dict):
        raise ArtifactSetError(f"Metal N3 {label} report metrics are missing")
    mean = reported.get("mean_luminance")
    checks = {
        field: reported.get(field) == computed[field]
        for field in ("width", "height", "format", "bytes", "sha256",
                      "nontrivial_pixels")
    }
    checks["mean_luminance"] = (
        isinstance(mean, (int, float))
        and not isinstance(mean, bool)
        and math.isfinite(float(mean))
        and math.isclose(
            float(mean),
            float(computed["mean_luminance"]),
            rel_tol=1.0e-9,
            abs_tol=1.0e-12,
        )
    )
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            f"Metal N3 {label} report metrics mismatch: {', '.join(failed)}"
        )


def _valid_metal_n3_followup(
    record: object, width: int, height: int
) -> bool:
    if not isinstance(record, dict):
        return False
    mean = record.get("mean_luminance")
    return (
        record.get("width") == width
        and record.get("height") == height
        and record.get("format") == "RGBA16_FLOAT"
        and record.get("bytes") == width * height * 8
        and _is_sha256(record.get("sha256"))
        and _is_positive_int(record.get("nontrivial_pixels"))
        and isinstance(mean, (int, float))
        and not isinstance(mean, bool)
        and math.isfinite(float(mean))
    )


def _verify_metal_n3_pass_semantics(
    report: dict[str, object], payloads: dict[str, bytes]
) -> None:
    metrics = {
        key: _metal_n3_image_metrics(payloads[key])
        for key, _ in METAL_N3_IMAGE_ARTIFACTS
    }
    for key, _ in METAL_N3_IMAGE_ARTIFACTS:
        _verify_metal_n3_reported_metrics(report.get(key), metrics[key], key)

    raster = payloads["raster_only_hdr"]
    contribution = payloads["rt_contribution"]
    hybrid = payloads["hybrid_hdr"]
    applied = 0
    untouched = 0
    for offset in range(0, len(raster), 8):
        raster_values = struct.unpack_from("<4e", raster, offset)
        contribution_values = struct.unpack_from("<4e", contribution, offset)
        hybrid_values = struct.unpack_from("<4e", hybrid, offset)
        contribution_channels = struct.unpack_from("<4H", contribution, offset)
        applies = any((channel & 0x7FFF) != 0 for channel in contribution_channels[:3])
        if contribution_channels[3] != 0:
            raise ArtifactSetError("Metal N3 contribution changed straight alpha")
        if hybrid[offset + 6 : offset + 8] != raster[offset + 6 : offset + 8]:
            raise ArtifactSetError("Metal N3 hybrid changed raster alpha")
        if applies:
            applied += 1
            if hybrid[offset : offset + 6] == raster[offset : offset + 6]:
                raise ArtifactSetError("Metal N3 contribution did not change hybrid RGB")
            for channel in range(3):
                expected = max(
                    -65504.0,
                    min(
                        65504.0,
                        raster_values[channel] + contribution_values[channel],
                    ),
                )
                if not math.isclose(
                    hybrid_values[channel],
                    expected,
                    rel_tol=2.0e-3,
                    abs_tol=5.0e-4,
                ):
                    raise ArtifactSetError(
                        "Metal N3 hybrid RGB is not the attested GPU contribution"
                    )
        else:
            untouched += 1
            if hybrid[offset : offset + 8] != raster[offset : offset + 8]:
                raise ArtifactSetError(
                    "Metal N3 changed a pixel outside its contribution"
                )

    contract = report.get("contract")
    raster_contract = report.get("raster_contract")
    proof = report.get("proof")
    device = report.get("device")
    second = report.get("second_view_contribution")
    resized = report.get("resized_hybrid")
    if (
        not isinstance(contract, dict)
        or not isinstance(raster_contract, dict)
        or not isinstance(proof, dict)
    ):
        raise ArtifactSetError("Metal N3 contract or proof is missing")
    if not isinstance(device, dict):
        raise ArtifactSetError("Metal N3 device proof is missing")
    second_valid = _valid_metal_n3_followup(second, 96, 64)
    resized_valid = _valid_metal_n3_followup(resized, 80, 48)
    second_hash = second.get("sha256") if isinstance(second, dict) else None
    allocations = raster_contract.get("texture_allocations")
    live_allocations = (
        allocations.get("live") if isinstance(allocations, dict) else None
    )
    shutdown_allocations = (
        allocations.get("after_shutdown")
        if isinstance(allocations, dict)
        else None
    )
    checks = {
        "scope": report.get("scope") == METAL_N3_SCOPE,
        "device": isinstance(device.get("name"), str)
        and bool(device["name"])
        and device.get("same_ogre_device") is True
        and device.get("same_ogre_queue") is True
        and device.get("apple_family_9") is True,
        "image_contract": type(contract.get("image_version")) is int
        and contract.get("image_version") == 2
        and _is_positive_int(contract.get("image_generation"))
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
        and _json_exact(
            live_allocations,
            {
                "version": 2,
                "source_textures": 1,
                "sampled_rgba": 1,
                "linear_rgba": 0,
                "roughness_r8": 0,
                "metallic_r8": 0,
                "normal_rg8": 0,
                "creates": 1,
                "destroys": 0,
                "live": 1,
                "exact_usage": True,
            },
        )
        and isinstance(shutdown_allocations, dict)
        and _json_exact(
            shutdown_allocations,
            {
                "version": 2,
                "creates": 1,
                "destroys": 1,
                "live": 0,
                "retired_name_lookups": 1,
                "retired_name_rejections": 1,
            },
        ),
        "distinct_nonempty_images": len(
            {metrics[key]["sha256"] for key, _ in METAL_N3_IMAGE_ARTIFACTS}
        )
        == 3
        and all(
            _is_positive_int(metrics[key]["nontrivial_pixels"])
            for key, _ in METAL_N3_IMAGE_ARTIFACTS
        ),
        "contribution_mapping": applied > 0
        and untouched > 0
        and type(proof.get("contribution_pixels")) is int
        and proof.get("contribution_pixels") == applied,
        "required_proofs": all(
            proof.get(field) is True
            for field in METAL_N3_REQUIRED_PROOF_BOOLEANS
        ),
        "far_plane_count": _is_positive_int(
            proof.get("off_axis_far_plane_contribution_pixels")
        ),
        "second_view": second_valid
        and second_hash != metrics["rt_contribution"]["sha256"],
        "resize": resized_valid,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            "Metal N3 pass evidence failed closed: " + ", ".join(failed)
        )


def _verify_metal_n2(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    report_path = root / METAL_N2_REQUIRED_ARTIFACTS[0]
    attestation_path = root / METAL_N2_REQUIRED_ARTIFACTS[1]
    executable_path = root / METAL_N2_REQUIRED_ARTIFACTS[2]
    probe_path = root / METAL_N2_PROBE_ARTIFACT
    report = _read_json_object(report_path, "Metal N2 report")
    attestation = _read_json_object(attestation_path, "Metal N2 attestation")
    status = report.get("status")
    if report.get("schema") != "ror.ogre_next_metal_rt_n2.v3" or status not in (
        "pass",
        "skip",
    ):
        raise ArtifactSetError("Metal N2 report schema or status is invalid")
    if (
        attestation.get("schema")
        != "ror.ogre_next_metal_rt_n2.attestation.v2"
        or attestation.get("status") != status
    ):
        raise ArtifactSetError("Metal N2 attestation schema or status mismatch")
    provenance = report.get("provenance")
    source = attestation.get("source")
    if not isinstance(provenance, dict) or not isinstance(source, dict):
        raise ArtifactSetError("Metal N2 source provenance is missing")
    expected_source = {
        "ror_commit": provenance.get("ror_commit"),
        "ror_ref": provenance.get("ror_ref"),
        "relevant_source_clean": provenance.get("relevant_source_clean"),
        "relevant_source_manifest_sha256": provenance.get(
            "relevant_source_manifest_sha256"
        ),
    }
    if source != expected_source or source.get("relevant_source_clean") is not True:
        raise ArtifactSetError("Metal N2 source attestation mismatch")
    contract_ror = build_contract["ror_source"]
    contract_ogre = build_contract["provenance"]
    expected_provenance = {
        "ror_repository": contract_ror.get("repository"),
        "ror_ref": contract_ror.get("ref"),
        "ror_commit": contract_ror.get("commit"),
        "relevant_source_manifest_sha256": contract_ror.get(
            "relevant_manifest_sha256"
        ),
        "ogre_next_repository": contract_ogre.get("repository"),
        "ogre_next_commit": contract_ogre.get("commit"),
        "ogre_next_archive_sha256": contract_ogre.get("archive_sha256"),
    }
    if any(
        provenance.get(field) != expected
        for field, expected in expected_provenance.items()
    ):
        raise ArtifactSetError("Metal N2 build-contract provenance mismatch")
    _verify_attested_file(
        attestation.get("report"),
        report_path,
        report_path.name,
        "Metal N2",
        "report",
    )
    _verify_attested_file(
        attestation.get("executable"),
        executable_path,
        executable_path.name,
        "Metal N2",
        "executable",
    )
    if (
        provenance.get("build_artifact") != executable_path.name
        or provenance.get("build_artifact_bytes") != executable_path.stat().st_size
        or provenance.get("build_artifact_sha256") != sha256_file(executable_path)
    ):
        raise ArtifactSetError("Metal N2 executable provenance mismatch")
    if status == "pass":
        if probe_path.is_symlink() or not probe_path.is_file():
            raise ArtifactSetError(f"missing: {METAL_N2_PROBE_ARTIFACT}")
        if probe_path.stat().st_size == 0:
            raise ArtifactSetError(f"empty: {METAL_N2_PROBE_ARTIFACT}")
        _verify_attested_file(
            attestation.get("probe"),
            probe_path,
            probe_path.name,
            "Metal N2",
            "probe",
        )
        manifest.append(
            {
                "path": METAL_N2_PROBE_ARTIFACT,
                "bytes": probe_path.stat().st_size,
                "sha256": sha256_file(probe_path),
            }
        )
    elif attestation.get("probe") is not None or probe_path.exists():
        raise ArtifactSetError("skipped Metal N2 evidence retained a stale probe")


def _verify_metal_n3(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    report_path = root / METAL_N3_REQUIRED_ARTIFACTS[0]
    attestation_path = root / METAL_N3_REQUIRED_ARTIFACTS[1]
    executable_path = root / METAL_N3_REQUIRED_ARTIFACTS[2]
    report = _read_json_object(report_path, "Metal N3 report")
    attestation = _read_json_object(attestation_path, "Metal N3 attestation")
    status = report.get("status")
    if report.get("schema") != "ror.ogre_next_metal_rt_n3.v3" or status not in (
        "pass",
        "skip",
    ):
        raise ArtifactSetError("Metal N3 report schema or status is invalid")
    if (
        attestation.get("schema")
        != "ror.ogre_next_metal_rt_n3.attestation.v1"
        or attestation.get("status") != status
    ):
        raise ArtifactSetError("Metal N3 attestation schema or status mismatch")
    provenance = report.get("provenance")
    source = attestation.get("source")
    if not isinstance(provenance, dict) or not isinstance(source, dict):
        raise ArtifactSetError("Metal N3 source provenance is missing")
    expected_source = {
        "ror_commit": provenance.get("ror_commit"),
        "ror_ref": provenance.get("ror_ref"),
        "relevant_source_clean": provenance.get("relevant_source_clean"),
        "relevant_source_manifest_sha256": provenance.get(
            "relevant_source_manifest_sha256"
        ),
    }
    if source != expected_source or source.get("relevant_source_clean") is not True:
        raise ArtifactSetError("Metal N3 source attestation mismatch")
    contract_ror = build_contract["ror_source"]
    contract_ogre = build_contract["provenance"]
    expected_provenance = {
        "ror_repository": contract_ror.get("repository"),
        "ror_ref": contract_ror.get("ref"),
        "ror_commit": contract_ror.get("commit"),
        "relevant_source_manifest_sha256": contract_ror.get(
            "relevant_manifest_sha256"
        ),
        "ogre_next_commit": contract_ogre.get("commit"),
    }
    if any(
        provenance.get(field) != expected
        for field, expected in expected_provenance.items()
    ):
        raise ArtifactSetError("Metal N3 build-contract provenance mismatch")
    _verify_attested_file(
        attestation.get("report"),
        report_path,
        report_path.name,
        "Metal N3",
        "report",
    )
    _verify_attested_file(
        attestation.get("executable"),
        executable_path,
        executable_path.name,
        "Metal N3",
        "executable",
    )
    if (
        provenance.get("build_artifact") != executable_path.name
        or provenance.get("build_artifact_bytes") != executable_path.stat().st_size
        or provenance.get("build_artifact_sha256") != sha256_file(executable_path)
    ):
        raise ArtifactSetError("Metal N3 executable provenance mismatch")
    if status == "skip" and (
        not isinstance(report.get("reason"), str) or not report["reason"]
    ):
        raise ArtifactSetError("skipped Metal N3 evidence has no reason")

    payloads: dict[str, bytes] = {}
    for key, name in METAL_N3_IMAGE_ARTIFACTS:
        path = root / name
        attested = attestation.get(key)
        if status == "skip":
            if attested is not None or path.exists():
                raise ArtifactSetError(
                    f"skipped Metal N3 evidence retained a stale {key} image"
                )
            continue
        if path.is_symlink() or not path.is_file():
            raise ArtifactSetError(f"missing: {name}")
        if path.stat().st_size == 0:
            raise ArtifactSetError(f"empty: {name}")
        _verify_attested_file(attested, path, path.name, "Metal N3", key)
        try:
            payloads[key] = path.read_bytes()
        except OSError as error:
            raise ArtifactSetError(f"could not read Metal N3 {key}: {error}") from error
        manifest.append(
            {
                "path": name,
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    if status == "pass":
        _verify_metal_n3_pass_semantics(report, payloads)


def _verify_metal_n4_provenance(
    report: dict[str, object],
    executable_path: Path,
    build_contract: dict[str, object],
) -> None:
    provenance = _require_exact_keys(
        report.get("provenance"),
        {
            "ror_repository",
            "ror_ref",
            "ror_commit",
            "relevant_source_clean",
            "relevant_source_manifest_sha256",
            "ogre_next_commit",
            "build_artifact",
            "build_artifact_bytes",
            "build_artifact_sha256",
        },
        "Metal N4 provenance",
    )
    source = build_contract["ror_source"]
    ogre = build_contract["provenance"]
    expected = {
        "ror_repository": source.get("repository"),
        "ror_ref": source.get("ref"),
        "ror_commit": source.get("commit"),
        "relevant_source_clean": True,
        "relevant_source_manifest_sha256": source.get(
            "relevant_manifest_sha256"
        ),
        "ogre_next_commit": ogre.get("commit"),
        "build_artifact": executable_path.name,
        "build_artifact_bytes": executable_path.stat().st_size,
        "build_artifact_sha256": sha256_file(executable_path),
    }
    if not _json_exact(provenance, expected):
        raise ArtifactSetError("Metal N4 build-contract provenance mismatch")
    platform = build_contract.get("platform")
    if not isinstance(platform, dict) or platform.get("policy") != (
        "macos-arm64-metal"
    ):
        raise ArtifactSetError("Metal N4 evidence requires the macOS Metal policy")


def _metal_n4_rgba16_strings(payload: bytes, offset: int) -> list[str]:
    return [
        f"0x{struct.unpack_from('<H', payload, offset + channel * 2)[0]:04x}"
        for channel in range(4)
    ]


def _validate_metal_n4_rgba16_pixel(
    payload: bytes, offset: int, label: str
) -> None:
    channels = struct.unpack_from("<4H", payload, offset)
    for channel, bits in enumerate(channels):
        if (bits & 0x8000) != 0 or (bits & 0x7C00) == 0x7C00:
            raise ArtifactSetError(
                f"Metal N4 {label} channel {channel} is not a canonical "
                "finite nonnegative binary16 value"
            )
    if channels[3] > 0x3C00:
        raise ArtifactSetError(
            f"Metal N4 {label} alpha exceeds the straight-alpha [0, 1] envelope"
        )


def _verify_metal_n4_pass_semantics(
    report: dict[str, object], payloads: dict[str, bytes]
) -> None:
    _require_exact_keys(
        report,
        {
            "schema",
            "status",
            "scope",
            "provenance",
            "device",
            "raster_contract",
            "native_contract",
            "artifacts",
            "coverage",
            "samples",
            "runtime_sequence",
            "proof",
        },
        "Metal N4 pass report",
    )
    if report.get("scope") != METAL_N4_PASS_SCOPE:
        raise ArtifactSetError("Metal N4 pass scope is invalid")
    device = _require_exact_keys(
        report.get("device"),
        {"name", "same_ogre_device", "same_ogre_queue", "apple_family_9"},
        "Metal N4 device",
    )
    if not (
        isinstance(device.get("name"), str)
        and bool(device["name"])
        and device.get("same_ogre_device") is True
        and device.get("same_ogre_queue") is True
        and device.get("apple_family_9") is True
    ):
        raise ArtifactSetError("Metal N4 same-device capability proof is invalid")
    raster_contract = _require_exact_keys(
        report.get("raster_contract"),
        {
            "raster_feature_tier",
            "native_feature_tier",
            "directional_shadow_mode",
            "pssm_enabled",
            "vertex_layout",
            "vertex_stride_bytes",
        },
        "Metal N4 raster contract",
    )
    if not _json_exact(
        raster_contract,
        {
            "raster_feature_tier": "MODERN_PBR_RT4_V1",
            "native_feature_tier": (
                "METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW"
            ),
            "directional_shadow_mode": "DISABLED",
            "pssm_enabled": False,
            "vertex_layout": "POSITION_NORMAL_TANGENT_UV0_FLOAT32_48",
            "vertex_stride_bytes": 48,
        },
    ):
        raise ArtifactSetError("Metal N4 raster contract is invalid")
    native_contract = _require_exact_keys(
        report.get("native_contract"),
        {
            "version",
            "backend",
            "tier",
            "blas_count",
            "tlas_instance_count",
            "primary_camera_rays_per_sample",
            "secondary_directional_visibility_rays_per_sample",
            "receiver_instance_id",
            "occluder_instance_id",
        },
        "Metal N4 native contract",
    )
    if not _json_exact(
        native_contract,
        {
            "version": 1,
            "backend": "METAL",
            "tier": "NATIVE_DIRECTIONAL_HARD_SHADOW_V1",
            "blas_count": 2,
            "tlas_instance_count": 2,
            "primary_camera_rays_per_sample": 1,
            "secondary_directional_visibility_rays_per_sample": 1,
            "receiver_instance_id": 1,
            "occluder_instance_id": 2,
        },
    ):
        raise ArtifactSetError("Metal N4 ray-lineage contract is invalid")

    width = 96
    height = 64
    pixel_count = width * height
    artifacts = _require_exact_keys(
        report.get("artifacts"),
        {key for key, _, _, _ in METAL_N4_IMAGE_ARTIFACTS},
        "Metal N4 artifact metrics",
    )
    for key, _, pixel_format, bytes_per_pixel in METAL_N4_IMAGE_ARTIFACTS:
        expected_keys = {"format", "bytes", "sha256"}
        if key == "visibility":
            expected_keys.update(("visible_r16_bits", "occluded_r16_bits"))
        metrics = _require_exact_keys(
            artifacts.get(key), expected_keys, f"Metal N4 {key} metrics"
        )
        payload = payloads[key]
        expected_metrics: dict[str, object] = {
            "format": pixel_format,
            "bytes": pixel_count * bytes_per_pixel,
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
        if key == "visibility":
            expected_metrics.update(
                {
                    "visible_r16_bits": "0x3c00",
                    "occluded_r16_bits": "0x0000",
                }
            )
        if len(payload) != pixel_count * bytes_per_pixel or not _json_exact(
            metrics, expected_metrics
        ):
            raise ArtifactSetError(f"Metal N4 {key} artifact metrics mismatch")

    raster = payloads["raster"]
    visibility = payloads["visibility"]
    lineage = payloads["ray_lineage"]
    hybrid = payloads["hybrid"]
    visible_count = 0
    occluded_count = 0
    for pixel in range(pixel_count):
        rgba_offset = pixel * 8
        visibility_bits = struct.unpack_from("<H", visibility, pixel * 2)[0]
        ray_lineage = struct.unpack_from("<I", lineage, pixel * 4)[0]
        _validate_metal_n4_rgba16_pixel(raster, rgba_offset, "raster")
        _validate_metal_n4_rgba16_pixel(hybrid, rgba_offset, "hybrid")
        if visibility_bits == 0x3C00:
            visible_count += 1
            if ray_lineage != 1 or hybrid[rgba_offset : rgba_offset + 8] != (
                raster[rgba_offset : rgba_offset + 8]
            ):
                raise ArtifactSetError(
                    "Metal N4 visible pixel mapping or lineage is invalid"
                )
        elif visibility_bits == 0x0000:
            occluded_count += 1
            if not (
                ray_lineage == 3
                and hybrid[rgba_offset : rgba_offset + 6] == b"\x00" * 6
                and hybrid[rgba_offset + 6 : rgba_offset + 8]
                == raster[rgba_offset + 6 : rgba_offset + 8]
            ):
                raise ArtifactSetError(
                    "Metal N4 occluded pixel mapping or lineage is invalid"
                )
        else:
            raise ArtifactSetError(
                "Metal N4 visibility contains a noncanonical R16 value"
            )

    coverage = _require_exact_keys(
        report.get("coverage"),
        {
            "width",
            "height",
            "pixels",
            "receiver_visible_pixels",
            "occluded_pixels",
            "primary_miss_pixels",
        },
        "Metal N4 coverage",
    )
    if not _json_exact(
        coverage,
        {
            "width": width,
            "height": height,
            "pixels": pixel_count,
            "receiver_visible_pixels": visible_count,
            "occluded_pixels": occluded_count,
            "primary_miss_pixels": 0,
        },
    ) or visible_count <= 0 or occluded_count <= 0:
        raise ArtifactSetError("Metal N4 coverage differs from the evidence")

    samples = report.get("samples")
    if not isinstance(samples, list) or len(samples) != 2:
        raise ArtifactSetError("Metal N4 visible/occluded samples are incomplete")
    expected_samples = (("VISIBLE", 0x3C00, 1, 0), ("OCCLUDED", 0, 3, 2))
    for index, (sample, expected) in enumerate(zip(samples, expected_samples)):
        sample = _require_exact_keys(
            sample,
            {
                "x",
                "y",
                "visibility",
                "visibility_r16_bits",
                "secondary_blocker_instance_id",
                "raster_rgba16_bits",
                "hybrid_rgba16_bits",
                "portable_contract_validated",
            },
            f"Metal N4 sample {index}",
        )
        x = sample.get("x")
        y = sample.get("y")
        if not (
            type(x) is int
            and type(y) is int
            and 0 <= x < width
            and 0 <= y < height
        ):
            raise ArtifactSetError("Metal N4 sample coordinate is invalid")
        pixel = y * width + x
        rgba_offset = pixel * 8
        observed_visibility = struct.unpack_from("<H", visibility, pixel * 2)[0]
        observed_lineage = struct.unpack_from("<I", lineage, pixel * 4)[0]
        if not (
            sample.get("visibility") == expected[0]
            and sample.get("visibility_r16_bits") == f"0x{expected[1]:04x}"
            and observed_visibility == expected[1]
            and observed_lineage == expected[2]
            and sample.get("secondary_blocker_instance_id") == expected[3]
            and sample.get("raster_rgba16_bits")
            == _metal_n4_rgba16_strings(raster, rgba_offset)
            and sample.get("hybrid_rgba16_bits")
            == _metal_n4_rgba16_strings(hybrid, rgba_offset)
            and sample.get("portable_contract_validated") is True
        ):
            raise ArtifactSetError(
                "Metal N4 reported sample differs from exact readback bytes"
            )

    sequence = _require_exact_keys(
        report.get("runtime_sequence"),
        {"exact_repeat", "moved_occluder", "resized_extent"},
        "Metal N4 runtime sequence",
    )
    metric_keys = {
        "frame_id",
        "width",
        "height",
        "pixels",
        "receiver_visible_pixels",
        "occluded_pixels",
        "primary_miss_pixels",
        "raster_sha256",
        "visibility_sha256",
        "ray_lineage_sha256",
        "hybrid_sha256",
    }
    sequence_metrics: dict[str, dict[str, object]] = {}
    for key in ("exact_repeat", "moved_occluder", "resized_extent"):
        observed = _require_exact_keys(
            sequence.get(key), metric_keys, f"Metal N4 {key} metrics"
        )
        observed_width = observed.get("width")
        observed_height = observed.get("height")
        observed_pixels = observed.get("pixels")
        visible_pixels = observed.get("receiver_visible_pixels")
        blocked_pixels = observed.get("occluded_pixels")
        if not (
            type(observed_width) is int
            and type(observed_height) is int
            and type(observed_pixels) is int
            and type(visible_pixels) is int
            and type(blocked_pixels) is int
            and observed_width > 0
            and observed_height > 0
            and observed_pixels == observed_width * observed_height
            and visible_pixels > 0
            and blocked_pixels > 0
            and visible_pixels + blocked_pixels == observed_pixels
            and observed.get("primary_miss_pixels") == 0
        ):
            raise ArtifactSetError(f"Metal N4 {key} coverage is invalid")
        for hash_key in (
            "raster_sha256",
            "visibility_sha256",
            "ray_lineage_sha256",
            "hybrid_sha256",
        ):
            digest = observed.get(hash_key)
            if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
                raise ArtifactSetError(f"Metal N4 {key} {hash_key} is invalid")
        sequence_metrics[key] = observed

    repeat = sequence_metrics["exact_repeat"]
    if not _json_exact(
        repeat,
        {
            "frame_id": 2,
            "width": width,
            "height": height,
            "pixels": pixel_count,
            "receiver_visible_pixels": visible_count,
            "occluded_pixels": occluded_count,
            "primary_miss_pixels": 0,
            "raster_sha256": artifacts["raster"]["sha256"],
            "visibility_sha256": artifacts["visibility"]["sha256"],
            "ray_lineage_sha256": artifacts["ray_lineage"]["sha256"],
            "hybrid_sha256": artifacts["hybrid"]["sha256"],
        },
    ):
        raise ArtifactSetError(
            "Metal N4 exact repeat differs from the retained baseline"
        )

    moved = sequence_metrics["moved_occluder"]
    if not (
        moved.get("frame_id") == 3
        and moved.get("width") == width
        and moved.get("height") == height
        and moved.get("raster_sha256") == artifacts["raster"]["sha256"]
        and moved.get("visibility_sha256") != artifacts["visibility"]["sha256"]
        and moved.get("ray_lineage_sha256") != artifacts["ray_lineage"]["sha256"]
        and moved.get("hybrid_sha256") != artifacts["hybrid"]["sha256"]
    ):
        raise ArtifactSetError(
            "Metal N4 moved occluder did not isolate shadow-output changes"
        )

    resized = sequence_metrics["resized_extent"]
    if not (
        resized.get("frame_id") == 4
        and resized.get("width") == 80
        and resized.get("height") == 48
        and resized.get("pixels") == 80 * 48
    ):
        raise ArtifactSetError("Metal N4 resized extent evidence is invalid")

    proof = _require_exact_keys(
        report.get("proof"),
        set(METAL_N4_REQUIRED_PROOF_BOOLEANS),
        "Metal N4 proof",
    )
    if not all(proof.get(field) is True for field in METAL_N4_REQUIRED_PROOF_BOOLEANS):
        raise ArtifactSetError("Metal N4 proof is incomplete")


def _verify_metal_n4(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    report_path = root / METAL_N4_REQUIRED_ARTIFACTS[0]
    executable_path = root / METAL_N4_REQUIRED_ARTIFACTS[1]
    report = _read_json_object(report_path, "Metal N4 report")
    status = report.get("status")
    if report.get("schema") != (
        "ror.ogre_next_metal_rt_n4_directional_shadow.v2"
    ) or status not in ("pass", "skip"):
        raise ArtifactSetError("Metal N4 report schema or status is invalid")
    _verify_metal_n4_provenance(report, executable_path, build_contract)

    payloads: dict[str, bytes] = {}
    for key, name, _, _ in METAL_N4_IMAGE_ARTIFACTS:
        path = root / name
        if status == "skip":
            if path.exists() or path.is_symlink():
                raise ArtifactSetError(
                    f"skipped Metal N4 evidence retained stale {key} data"
                )
            continue
        if path.is_symlink() or not path.is_file():
            raise ArtifactSetError(f"missing: {name}")
        if path.stat().st_size <= 0:
            raise ArtifactSetError(f"empty: {name}")
        try:
            payloads[key] = path.read_bytes()
        except OSError as error:
            raise ArtifactSetError(
                f"could not read Metal N4 {key}: {error}"
            ) from error
        manifest.append(
            {
                "path": name,
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    if status == "pass":
        _verify_metal_n4_pass_semantics(report, payloads)
        return

    _require_exact_keys(
        report,
        {
            "schema",
            "status",
            "scope",
            "reason",
            "provenance",
            "device_name",
            "required_apple_gpu_family",
            "required_metal_ray_tracing",
            "required_visibility_format",
        },
        "Metal N4 skip report",
    )
    if not (
        report.get("scope") == METAL_N4_SKIP_SCOPE
        and isinstance(report.get("reason"), str)
        and bool(report["reason"])
        and isinstance(report.get("device_name"), str)
        and type(report.get("required_apple_gpu_family")) is int
        and report.get("required_apple_gpu_family") == 9
        and report.get("required_metal_ray_tracing") is True
        and report.get("required_visibility_format") == "R16_FLOAT"
    ):
        raise ArtifactSetError("Metal N4 capability skip is not exact")


def _verify_child_runtime_receipt(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    try:
        validate_child_runtime_receipt(root, require_pass_or_skip=False)
    except ChildReceiptValidationError as error:
        raise ArtifactSetError(
            f"Ogre-Next child execution receipt is invalid: {error}"
        ) from error
    child_relative = expected_child_relative(build_contract)
    for relative in (
        CHILD_RUNTIME_RECEIPT_ARTIFACT,
        CHILD_RUNTIME_STDOUT_ARTIFACT,
        CHILD_RUNTIME_STDERR_ARTIFACT,
        child_relative,
    ):
        path = root.joinpath(*relative.split("/"))
        if path.is_symlink() or not path.is_file():
            raise ArtifactSetError(
                f"Ogre-Next child upload artifact is missing or indirect: {relative}"
            )
        manifest.append(
            {
                "path": relative,
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )


def verify_artifact_set(
    build_dir: Path,
    verify_metal_n2_evidence: bool = False,
    verify_metal_n3_evidence: bool = False,
    verify_metal_n4_evidence: bool = False,
    *,
    expected_ror_repository: str | None = None,
    expected_ror_ref: str | None = None,
    expected_ror_commit: str | None = None,
) -> list[dict[str, object]]:
    root = build_dir.expanduser().resolve()
    failures: list[str] = []
    manifest: list[dict[str, object]] = []
    required = REQUIRED_ARTIFACTS + (
        METAL_N2_REQUIRED_ARTIFACTS if verify_metal_n2_evidence else ()
    ) + (
        METAL_N3_REQUIRED_ARTIFACTS if verify_metal_n3_evidence else ()
    ) + (
        METAL_N4_REQUIRED_ARTIFACTS if verify_metal_n4_evidence else ()
    )
    for name in required:
        path = root / name
        if path.is_symlink():
            failures.append(f"symbolic link: {name}")
            continue
        if not path.is_file():
            failures.append(f"missing: {name}")
            continue
        size = path.stat().st_size
        if size == 0:
            failures.append(f"empty: {name}")
            continue
        manifest.append(
            {"path": name, "bytes": size, "sha256": sha256_file(path)}
        )
    if failures:
        raise ArtifactSetError(
            "OGRE-Next artifact set is incomplete: " + ", ".join(failures)
        )
    expected_source = _current_source_identity(
        expected_repository=expected_ror_repository,
        expected_ref=expected_ror_ref,
        expected_commit=expected_ror_commit,
    )
    build_contract = _read_build_contract(root, expected_source)
    if build_contract.get("schema_version") in (6, 7):
        _verify_child_runtime_receipt(root, manifest, build_contract)
    _verify_pssm(root, manifest, build_contract)
    _verify_rt4(root, manifest, build_contract)
    _verify_freetype_package_licenses(root, manifest)
    if verify_metal_n2_evidence:
        _verify_metal_n2(root, manifest, build_contract)
    if verify_metal_n3_evidence:
        _verify_metal_n3(root, manifest, build_contract)
    if verify_metal_n4_evidence:
        _verify_metal_n4(root, manifest, build_contract)
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--expected-ror-repository",
        help="trusted RoR repository identity (defaults to the canonical repo)",
    )
    parser.add_argument(
        "--expected-ror-ref",
        help="trusted RoR ref identity (defaults to CI environment or Git)",
    )
    parser.add_argument(
        "--expected-ror-commit",
        help="trusted RoR commit (defaults to GITHUB_SHA or checked-out Git)",
    )
    parser.add_argument(
        "--verify-metal-n2-evidence",
        action="store_true",
        help=(
            "cross-check attested Apple Metal N2 pass or capability-skip "
            "evidence"
        ),
    )
    parser.add_argument(
        "--verify-metal-n3-evidence",
        action="store_true",
        help=(
            "cross-check attested Apple Metal N3 pass or capability-skip "
            "evidence"
        ),
    )
    parser.add_argument(
        "--verify-metal-n4-evidence",
        action="store_true",
        help=(
            "cross-check Apple Metal N4 directional-shadow pass or "
            "capability-skip evidence"
        ),
    )
    args = parser.parse_args(argv)
    try:
        manifest = verify_artifact_set(
            args.build_dir,
            args.verify_metal_n2_evidence,
            args.verify_metal_n3_evidence,
            args.verify_metal_n4_evidence,
            expected_ror_repository=args.expected_ror_repository,
            expected_ror_ref=args.expected_ror_ref,
            expected_ror_commit=args.expected_ror_commit,
        )
    except (ArtifactSetError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(
        json.dumps(
            {"schema_version": 1, "status": "pass", "artifacts": manifest},
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
