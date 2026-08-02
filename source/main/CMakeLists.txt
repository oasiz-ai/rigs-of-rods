####################################################################################################
#  SOURCE FILES
####################################################################################################

set(SOURCE_FILES
        main.cpp
        Application.{h,cpp}
        ForwardDeclarations.h
        AppContext.{h,cpp}
        GameContext.{h,cpp}
        audio/MumbleIntegration.{h,cpp}
        audio/Sound.{h,cpp}
        audio/SoundManager.{h,cpp}
        audio/SoundScriptManager.{h,cpp}
        gameplay/AutoPilot.{h,cpp}
        gameplay/Character.{h,cpp}
        gameplay/CharacterFactory.{h,cpp}
        gameplay/ChatSystem.{h,cpp}
        gameplay/CruiseControl.cpp
        gameplay/Engine.{h,cpp}
        gameplay/Landusemap.{h,cpp}
        gameplay/RaceSystem.{h,cpp}
        gameplay/RepairMode.{h,cpp}
        gameplay/Replay.{h,cpp}
        gameplay/SceneMouse.{h,cpp}
        gameplay/ScriptEvents.h
        gameplay/TorqueCurve.{h,cpp}
        gameplay/TyrePressure.{h,cpp}
        gameplay/VehicleAI.{h,cpp}
        gfx/AdvancedScreen.h
        gfx/ColoredTextAreaOverlayElement.{h,cpp}
        gfx/ColoredTextAreaOverlayElementFactory.h
        gfx/DustPool.{h,cpp}
        gfx/EnvironmentMap.{h,cpp}
        gfx/GfxActor.{h,cpp}
        gfx/GfxData.{h,cpp}
        gfx/GfxScene.{h,cpp}
        gfx/PostProcessPolicy.{h,cpp}
        gfx/PostProcessRuntime.{h,cpp}
        gfx/PostProcessRuntimeContract.{h,cpp}
        gfx/RendererBackendPolicy.{h,cpp}
        gfx/RendererStartupHandoff.{h,cpp}
        gfx/RendererStartupPlan.{h,cpp}
        gfx/RenderDisplayMetrics.h
        gfx/render/GraphicsSceneSnapshotProducer.{h,cpp}
        gfx/render/Ogre14GraphicsSceneSource.{h,cpp}
        gfx/render/Ogre14LegacyAssetTranslator.{h,cpp}
        gfx/render/Ogre14ProceduralRoadSource.{h,cpp}
        gfx/render/HdrReference.{h,cpp}
        gfx/render/InputEventTransport.{h,cpp}
        gfx/render/MaterialDescriptor.{h,cpp}
        gfx/render/ParallaxProbeReference.{h,cpp}
        gfx/render/PbrReference.{h,cpp}
        gfx/render/ReflectionProbeCaptureReceipt.{h,cpp}
        gfx/render/ReflectionProbeRuntime.{h,cpp}
        gfx/render/RenderAssetDeltaTransport.{h,cpp}
        gfx/render/RenderAssetId.{h,cpp}
        gfx/render/RenderAssetRegistry.{h,cpp}
        gfx/render/RenderBridgeControlTransport.{h,cpp}
        gfx/render/RenderBridgeSessionIdentity.{h,cpp}
        gfx/render/README.md
        gfx/render/RenderContracts.h
        gfx/render/RenderFrame.{h,cpp}
        gfx/render/RenderMath.h
        gfx/render/RenderResourceDescriptors.{h,cpp}
        gfx/render/RenderTransportDetail.h
        gfx/render/RenderTransportEnvelope.{h,cpp}
        gfx/render/RenderTransportStream.{h,cpp}
        gfx/render/RendererFrontend.{h,cpp}
        gfx/render/RendererFrontendTransportDispatcher.{h,cpp}
        gfx/render/RenderValidation.h
        gfx/render/ResourceHandle.{h,cpp}
        gfx/render/SceneSnapshot.{h,cpp}
        gfx/render/SceneSnapshotTransport.{h,cpp}
        gfx/HydraxWater.{h,cpp}
        gfx/IGfxWater.h
        gfx/MovableText.{h,cpp}
        gfx/ShadowManager.{h,cpp}
        gfx/SimBuffers.{h,cpp}
        gfx/Skidmark.{h,cpp}
        gfx/SkyManager.{h,cpp}
        gfx/SkyXManager.{h,cpp}
        gfx/SurveyMapTextureCreator.{h,cpp}
        gfx/GfxWater.{h,cpp}
        resources/LegacyMaterialCompatibilityPlan.{h,cpp}
        resources/LegacyMaterialScriptSanitizer.{h,cpp}
        gfx/camera/CameraManager.{h,cpp}
        gfx/camera/PerVehicleCameraContext.h
        gfx/hydrax/CfgFileManager.{h,cpp}
        gfx/hydrax/DecalsManager.{h,cpp}
        gfx/hydrax/Enums.{h,cpp}
        gfx/hydrax/FFT.{h,cpp}
        gfx/hydrax/GodRaysManager.{h,cpp}
        gfx/hydrax/GPUNormalMapManager.{h,cpp}
        gfx/hydrax/Help.{h,cpp}
        gfx/hydrax/Hydrax.{h,cpp}
        gfx/hydrax/Image.{h,cpp}
        gfx/hydrax/MaterialManager.{h,cpp}
        gfx/hydrax/Mesh.{h,cpp}
        gfx/hydrax/Module.{h,cpp}
        gfx/hydrax/Noise.{h,cpp}
        gfx/hydrax/Perlin.{h,cpp}
        gfx/hydrax/Prerequisites.{h,cpp}
        gfx/hydrax/PressurePoint.{h,cpp}
        gfx/hydrax/ProjectedGrid.{h,cpp}
        gfx/hydrax/RadialGrid.{h,cpp}
        gfx/hydrax/Real.{h,cpp}
        gfx/hydrax/RttManager.{h,cpp}
        gfx/hydrax/SimpleGrid.{h,cpp}
        gfx/hydrax/TextureManager.{h,cpp}
        gfx/hydrax/Wave.{h,cpp}
        gfx/particle/ExtinguishableFireAffector.{h,cpp}
        gfx/particle/ExtinguishableFireAffectorFactory.h
        gfx/particle/FireExtinguisherAffector.{h,cpp}
        gfx/particle/FireExtinguisherAffectorFactory.h
        gfx/particle/OgreParticleCustomParam.h
        gfx/particle/OgreShaderParticleRenderer.{h,cpp}
        gfx/skyx/AtmosphereManager.{h,cpp}
        gfx/skyx/BasicController.{h,cpp}
        gfx/skyx/CloudsManager.{h,cpp}
        gfx/skyx/ColorGradient.{h,cpp}
        gfx/skyx/Controller.h
        gfx/skyx/GPUManager.{h,cpp}
        gfx/skyx/MeshManager.{h,cpp}
        gfx/skyx/MoonManager.{h,cpp}
        gfx/skyx/Prerequisites.{h,cpp}
        gfx/skyx/SCfgFileManager.{h,cpp}
        gfx/skyx/SkyX.{h,cpp}
        gfx/skyx/VCloudsManager.{h,cpp}
        gfx/skyx/VClouds/DataManager.{h,cpp}
        gfx/skyx/VClouds/Ellipsoid.{h,cpp}
        gfx/skyx/VClouds/FastFakeRandom.{h,cpp}
        gfx/skyx/VClouds/GeometryBlock.{h,cpp}
        gfx/skyx/VClouds/GeometryManager.{h,cpp}
        gfx/skyx/VClouds/Lightning.{h,cpp}
        gfx/skyx/VClouds/LightningManager.{h,cpp}
        gfx/skyx/VClouds/VClouds.{h,cpp}
        gui/DashBoardManager.{h,cpp}
        gui/GUIManager.{h,cpp}
        gui/GUIUtils.{h,cpp}
        gui/OverlayWrapper.{h,cpp}
        gui/RTTLayer.{h,cpp}
        gui/imgui/imgui.{h,cpp}
        gui/imgui/imgui_demo.cpp
        gui/imgui/imgui_draw.cpp
        gui/imgui/imgui_widgets.cpp
        gui/imgui/OgreImGuiOverlay.{h,cpp}
        gui/imgui/OgreImGui.{h,cpp}
        gui/imgui/imconfig.h
        gui/imgui/imgui_internal.h
        gui/imgui/imstb_rectpack.h
        gui/imgui/imstb_textedit.h
        gui/imgui/imstb_truetype.h
        gui/panels/GUI_AngelScriptExamples.{h,cpp}
        gui/panels/GUI_CollisionsDebug.{h,cpp}
        gui/panels/GUI_ConsoleView.{h,cpp}
        gui/panels/GUI_ConsoleWindow.{h,cpp}
        gui/panels/GUI_DirectionArrow.{h,cpp}
        gui/panels/GUI_LoadingWindow.{h,cpp}
        gui/panels/GUI_FlexbodyDebug.{h,cpp}
        gui/panels/GUI_FrictionSettings.{h,cpp}
        gui/panels/GUI_TopMenubar.{h,cpp}
        gui/panels/GUI_TextureToolWindow.{h,cpp}
        gui/panels/GUI_RepositorySelector.{h,cpp}
        gui/panels/GUI_GameControls.{h,cpp}
        gui/panels/GUI_GameAbout.{h,cpp}
        gui/panels/GUI_GameChatBox.{h,cpp}
        gui/panels/GUI_GameMainMenu.{h,cpp}
        gui/panels/GUI_GameSettings.{h,cpp}
        gui/panels/GUI_MainSelector.{h,cpp}
        gui/panels/GUI_MessageBox.{h,cpp}
        gui/panels/GUI_MultiplayerSelector.{h,cpp}
        gui/panels/GUI_MultiplayerClientList.{h,cpp}
        gui/panels/GUI_NodeBeamUtils.{h,cpp}
        gui/panels/GUI_VehicleInfoTPanel.{h,cpp}
        gui/panels/GUI_ScriptMonitor.{h,cpp}
        gui/panels/GUI_SimPerfStats.{h,cpp}
        gui/panels/GUI_SurveyMap.{h,cpp}
        network/CurlHelpers.{h,cpp}
        network/DiscordRpc.{h,cpp}
        network/Network.{h,cpp}
        network/OutGauge.{h,cpp}
        network/RoRnet.h
        physics/Actor.{h,cpp}
        physics/ActorStateDigestAdapter.{h,cpp}
        physics/ApproxMath.h
        physics/ActorExport.cpp
        physics/ActorForcesEuler.cpp
        physics/BeamAxialResponse.h
        physics/BeamRestLengthScale.h
        physics/CalibratedBeamFractureCalibration.h
        physics/CalibratedBeamMaterial.h
        physics/CalibratedBeamMaterialAdapter.h
        physics/CalibratedBeamSavegame.h
        physics/CalibratedBeamSavegameJson.h
        physics/DeterministicContactOrder.h
        physics/DeterministicCounterNoise.h
        physics/DeterministicFixedStepCadence.h
        physics/DeterministicScenarioSchedule.h
        physics/DeterministicInputTrace.{h,cpp}
        physics/DeterministicInputTraceRuntime.{h,cpp}
        physics/DeterministicVehicleInput.{h,cpp}
        physics/DeterministicStateDigest.{h,cpp}
        physics/DeterministicStateTrace.{h,cpp}
        physics/DeterministicStateTraceCli.{h,cpp}
        physics/FixedStepCaptureBridge.h
        physics/HydroActuatorResponse.h
        physics/ActorManager.{h,cpp}
        physics/ActorSlideNode.cpp
        physics/ActorSpawner.{h,cpp}
        physics/ActorSpawnerFlow.cpp
        physics/CmdKeyInertia.{h,cpp}
        physics/Differentials.{h,cpp}
        physics/Savegame.cpp
        physics/SimConstants.h
        physics/SimData.{h,cpp}
        physics/SlideNode.{h,cpp}
        physics/air/AeroEngine.h
        physics/air/AirBrake.{h,cpp}
        physics/air/Airfoil.{h,cpp}
        physics/air/TurboJet.{h,cpp}
        physics/air/TurboProp.{h,cpp}
        physics/collision/CartesianToTriangleTransform.h
        physics/collision/Collisions.{h,cpp}
        physics/collision/DynamicCollisions.{h,cpp}
        physics/collision/PointColDetector.{h,cpp}
        physics/collision/Triangle.h
        physics/flex/Flexable.h
        physics/flex/FlexAirfoil.{h,cpp}
        physics/flex/FlexBody.{h,cpp}
        physics/flex/FlexFactory.{h,cpp}
        physics/flex/FlexMesh.{h,cpp}
        physics/flex/FlexMeshWheel.{h,cpp}
        physics/flex/FlexObj.{h,cpp}
        physics/flex/Locator_t.h
        physics/water/Buoyance.{h,cpp}
        physics/water/ScrewProp.{h,cpp}
        physics/water/Wavefield.{h,cpp}
        resources/CacheSystem.{h,cpp}
        resources/ContentManager.{h,cpp}
        resources/beamng/BeamNGMaterialInventory.{h,cpp}
        resources/beamng/BeamNGPackageManifest.{h,cpp}
        resources/beamng/BeamNGZipArchiveIndex.{h,cpp}
        resources/beamng/JBeamAdvancedStructureIR.{h,cpp}
        resources/beamng/JBeamCoordinateTransform.h
        resources/beamng/JBeamExpressionEvaluator.{h,cpp}
        resources/beamng/JBeamPartResolver.{h,cpp}
        resources/beamng/JBeamPressureWheelIR.{h,cpp}
        resources/beamng/JBeamStructuralIR.{h,cpp}
        resources/beamng/JBeamSyntax.{h,cpp}
        resources/beamng/JBeamToRigDef.{h,cpp}
        resources/addonpart_fileformat/AddonPartFileFormat.{h,cpp}
        resources/otc_fileformat/OTCFileFormat.{h,cpp}
        resources/odef_fileformat/ODefFileFormat.{h,cpp}
        resources/rig_def_fileformat/RigDef_File.{h,cpp}
        resources/rig_def_fileformat/RigDef_Node.{h,cpp}
        resources/rig_def_fileformat/RigDef_Parser.{h,cpp}
        resources/rig_def_fileformat/RigDef_Prerequisites.h
        resources/rig_def_fileformat/RigDef_Regexes.h
        resources/rig_def_fileformat/RigDef_SequentialImporter.{h,cpp}
        resources/rig_def_fileformat/RigDef_Serializer.{h,cpp}
        resources/rig_def_fileformat/RigDef_Validator.{h,cpp}
        resources/skin_fileformat/SkinFileFormat.{h,cpp}
        resources/terrn2_fileformat/TerrainBundleArchiveVerifier.{h,cpp}
        resources/terrn2_fileformat/TerrainBundleDependency.{h,cpp}
        resources/terrn2_fileformat/Terrn2FileFormat.{h,cpp}
        resources/tobj_fileformat/CityWorldNeoQ20Compatibility.{h,cpp}
        resources/tobj_fileformat/CityWorldPenguinRoadCompatibility.{h,cpp}
        resources/tobj_fileformat/CityWorldNeoQTreeCompatibility.{h,cpp}
        resources/tobj_fileformat/CityWorldNeoQTreePlan.inc
        resources/tobj_fileformat/TObjFileFormat.{h,cpp}
        resources/tuneup_fileformat/TuneupFileFormat.{h,cpp}
        system/AppCommandLine.cpp
        system/CommandLineInfo.{h,cpp}
        system/AppConfig.cpp
        system/Console.{h,cpp}
        system/ConsoleCmd.{h,cpp}
        system/CVar.{h,cpp}
        system/RendererBridgeChannel.{h,cpp}
        system/RendererBridgeEndpoint.{h,cpp}
        system/RendererBridgeLaunchPlan.{h,cpp}
        system/RendererBridgeProcessSupervisor.{h,cpp}
        system/RendererChildIntent.{h,cpp}
        system/RendererChildLauncher.{h,cpp}
        system/RendererOgre14GameBridge.{h,cpp}
        system/RendererOgre14GameHostSession.{h,cpp}
        system/RendererOgre14InputAdapter.{h,cpp}
        system/RendererOgre14InputEngineTarget.{h,cpp}
        system/RendererOgre14ProductSession.{h,cpp}
        system/RendererOgre14RuntimeOwnership.{h,cpp}
        system/RendererPackageRuntimeProbe.{h,cpp}
        system/RendererOgreNextLiveSession.{h,cpp}
        system/RendererSiblingPath.{h,cpp}
        system/RendererPackagedMediaPath.{h,cpp}
        terrain/OgreTerrainPSSMMaterialGenerator.{h,cpp}
        terrain/LocalLightBudget.h
        terrain/ProceduralManager.{h,cpp}
        terrain/ProceduralRoad.{h,cpp}
        terrain/SurveyMapEntity.h
        terrain/TerrainEditor.{h,cpp}
        terrain/TerrainGeometryManager.{h,cpp}
        terrain/Terrain.{h,cpp}
        terrain/TerrainObjectManager.{h,cpp}
        threadpool/ThreadPool.h
        utils/ConfigFile.{h,cpp}
        utils/ErrorUtils.{h,cpp}
        utils/ForceFeedback.{h,cpp}
        utils/GenericFileFormat.{h,cpp}
        utils/ImprovedConfigFile.h
        utils/InputEngine.{h,cpp}
        utils/InterThreadStoreVector.h
        utils/Language.{h,cpp}
        utils/MacOSControllerContract.h
        utils/MacOSInputBridge.h
        utils/MeshObject.{h,cpp}
        utils/PlatformUtils.{h,cpp}
        utils/SHA1.{h,cpp}
        utils/Utils.{h,cpp}
        utils/Vec3.h
        utils/WindowsRuntimePath.h
        utils/WriteTextToTexture.{h,cpp}
        utils/memory/RefCountingObject.h
        utils/memory/RefCountingObjectPtr.h
        utils/bbcode/BBDocument.{h,cpp}
        worldmodel/EpisodeArtifacts.{h,cpp}
        worldmodel/EpisodeCaptureSink.{h,cpp}
        worldmodel/EpisodeFormat.{h,cpp}
        worldmodel/EpisodeProvenance.{h,cpp}
        worldmodel/EpisodeValidator.{h,cpp}
        worldmodel/EpisodeWriter.{h,cpp}
        worldmodel/ObservationScheduler.{h,cpp}
        worldmodel/RoRRuntimeCaptureProvider.{h,cpp}
        worldmodel/RoRWorldModelRuntimeAdapter.{h,cpp}
        worldmodel/WorldModelCaptureContract.{h,cpp}
        worldmodel/WorldModelCaptureEncoding.{h,cpp}
        worldmodel/WorldModelLiveCaptureConfig.{h,cpp}
        worldmodel/WorldModelLiveCaptureController.{h,cpp}
        worldmodel/WorldModelPlatformIdentity.{h,cpp}
        worldmodel/WorldModelCaptureSession.{h,cpp}
        worldmodel/WorldModelRuntimeBackend.{h,cpp}
        worldmodel/WorldModelTelemetry.{h,cpp}
        )

if (APPLE AND ROR_OGRE14)
    list(APPEND SOURCE_FILES
            utils/MacOSControllerBackend.{h,cpp}
            )
endif ()

if (ROR_BUILD_TESTS)
    list(APPEND SOURCE_FILES
            resources/rig_def_fileformat/RigDef_TestHooks.{h,cpp}
            )
endif ()

if (ROR_USE_ANGELSCRIPT)
    list(APPEND SOURCE_FILES
            scripting/GameScript.{h,cpp}
            scripting/LocalStorage.{h,cpp}
            scripting/OgreScriptBuilder.{h,cpp}
            scripting/ScriptEngine.{h,cpp}
            scripting/ScriptUtils.h
            scripting/bindings/AngelScriptBindings.h
            scripting/bindings/ActorAngelscript.cpp
            scripting/bindings/AircraftEngineAngelscript.cpp
            scripting/bindings/AutopilotAngelscript.cpp
            scripting/bindings/CacheSystemAngelscript.cpp
            scripting/bindings/ConsoleAngelscript.cpp
            scripting/bindings/DashBoardManagerAngelscript.cpp
            scripting/bindings/EngineAngelscript.cpp
            scripting/bindings/GameScriptAngelscript.cpp
            scripting/bindings/GenericFileFormatAngelscript.cpp
            scripting/bindings/ImGuiAngelscript.cpp
            scripting/bindings/InputEngineAngelscript.cpp
            scripting/bindings/LocalStorageAngelscript.cpp
            scripting/bindings/MsgQueueAngelscript.cpp
            scripting/bindings/OgreAngelscript.cpp
            scripting/bindings/ProceduralRoadAngelscript.cpp
            scripting/bindings/ScrewpropAngelscript.cpp
            scripting/bindings/ScriptEventsAngelscript.cpp
            scripting/bindings/SoundScriptAngelscript.cpp
            scripting/bindings/TerrainAngelscript.cpp
            scripting/bindings/TurbojetAngelscript.cpp
            scripting/bindings/TurbopropAngelscript.cpp
            scripting/bindings/VehicleAiAngelscript.cpp
            )
endif ()

if (ROR_OGRE14)
    # Native OGRE types remain confined to the OGRE 14 integration directory.
    # The renderer-neutral boundary above continues to build in standalone
    # tests and with either renderer dependency graph.
    list(APPEND SOURCE_FILES
        gfx/ogre14/Ogre14LegacyNativeAssetExtractor.{h,cpp})
endif ()

include(SourceFileUtils)

# Expand file extensions (i.e. path/to/file.{h,cpp} becomes path/to/file.h;path/to/file.cpp)
expand_file_extensions(SOURCE_FILES ${SOURCE_FILES})

# Renderer-neutral contracts must remain independent of the legacy renderer
# PCH. Validation also relies on IEEE NaN/Inf classification in release builds,
# where the game otherwise enables fast math globally.
set(ROR_RENDER_CONTRACT_SOURCES
        gfx/render/GraphicsSceneSnapshotProducer.cpp
        gfx/render/Ogre14GraphicsSceneSource.cpp
        gfx/render/Ogre14LegacyAssetTranslator.cpp
        gfx/render/Ogre14ProceduralRoadSource.cpp
        gfx/render/HdrReference.cpp
        gfx/render/InputEventTransport.cpp
        gfx/render/MaterialDescriptor.cpp
        gfx/render/ParallaxProbeReference.cpp
        gfx/render/PbrReference.cpp
        gfx/render/ReflectionProbeCaptureReceipt.cpp
        gfx/render/ReflectionProbeRuntime.cpp
        gfx/render/RenderAssetDeltaTransport.cpp
        gfx/render/RenderAssetId.cpp
        gfx/render/RenderAssetRegistry.cpp
        gfx/render/RenderBridgeControlTransport.cpp
        gfx/render/RenderBridgeSessionIdentity.cpp
        gfx/render/RenderFrame.cpp
        gfx/render/RenderResourceDescriptors.cpp
        gfx/render/RenderTransportEnvelope.cpp
        gfx/render/RenderTransportStream.cpp
        gfx/render/RendererFrontend.cpp
        gfx/render/RendererFrontendTransportDispatcher.cpp
        gfx/render/ResourceHandle.cpp
        gfx/render/SceneSnapshot.cpp
        gfx/render/SceneSnapshotTransport.cpp
        )
set_source_files_properties(
        ${ROR_RENDER_CONTRACT_SOURCES}
        PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        )
set(ROR_RENDER_CONTRACT_STRICT_FP_SOURCES
        gfx/render/GraphicsSceneSnapshotProducer.cpp
        gfx/render/Ogre14GraphicsSceneSource.cpp
        gfx/render/Ogre14LegacyAssetTranslator.cpp
        gfx/render/Ogre14ProceduralRoadSource.cpp
        gfx/render/HdrReference.cpp
        gfx/render/InputEventTransport.cpp
        gfx/render/MaterialDescriptor.cpp
        gfx/render/ParallaxProbeReference.cpp
        gfx/render/PbrReference.cpp
        gfx/render/ReflectionProbeCaptureReceipt.cpp
        gfx/render/ReflectionProbeRuntime.cpp
        gfx/render/RenderAssetDeltaTransport.cpp
        gfx/render/RenderAssetId.cpp
        gfx/render/RenderAssetRegistry.cpp
        gfx/render/RenderBridgeControlTransport.cpp
        gfx/render/RenderBridgeSessionIdentity.cpp
        gfx/render/RenderFrame.cpp
        gfx/render/RenderResourceDescriptors.cpp
        gfx/render/RenderTransportEnvelope.cpp
        gfx/render/RenderTransportStream.cpp
        gfx/render/RendererFrontend.cpp
        gfx/render/RendererFrontendTransportDispatcher.cpp
        gfx/render/SceneSnapshot.cpp
        gfx/render/SceneSnapshotTransport.cpp
        )
if (MSVC)
    set_source_files_properties(
            ${ROR_RENDER_CONTRACT_STRICT_FP_SOURCES}
            PROPERTIES
            COMPILE_OPTIONS "/fp:strict"
            )
else ()
    set_source_files_properties(
            ${ROR_RENDER_CONTRACT_STRICT_FP_SOURCES}
            PROPERTIES
            COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off"
            )
endif ()

# Authenticated input traces inspect and preserve exact IEEE-754 object
# representations. Keep this small, non-solver boundary precise even when the
# release game enables fast math globally; otherwise MSVC may assume that
# hostile serialized values are finite and optimize away required checks.
set(ROR_DETERMINISTIC_INPUT_SOURCES
        physics/DeterministicInputTrace.cpp
        physics/DeterministicInputTraceRuntime.cpp
        physics/DeterministicVehicleInput.cpp
        )
if (MSVC)
    set_source_files_properties(
            ${ROR_DETERMINISTIC_INPUT_SOURCES}
            PROPERTIES
            COMPILE_OPTIONS "/fp:precise"
            SKIP_PRECOMPILE_HEADERS ON
            )
else ()
    set_source_files_properties(
            ${ROR_DETERMINISTIC_INPUT_SOURCES}
            PROPERTIES
            COMPILE_OPTIONS "-fno-fast-math"
            SKIP_PRECOMPILE_HEADERS ON
            )
endif ()

# The game uses fast-math in optimized builds, but recorder admission depends
# on IEEE NaN/Inf and signed-zero checks. Keep the validation boundary strict
# in every configuration so a release build cannot serialize hostile values.
set(WORLDMODEL_STRICT_FP_SOURCES
        worldmodel/RoRRuntimeCaptureProvider.cpp
        worldmodel/WorldModelCaptureSession.cpp
        worldmodel/WorldModelCaptureEncoding.cpp
        worldmodel/WorldModelTelemetry.cpp
        )
if (MSVC)
    set_source_files_properties(
        ${WORLDMODEL_STRICT_FP_SOURCES}
        PROPERTIES
            COMPILE_OPTIONS "/fp:strict"
            SKIP_PRECOMPILE_HEADERS ON
    )
else ()
    set_source_files_properties(
        ${WORLDMODEL_STRICT_FP_SOURCES}
        PROPERTIES
            COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off"
            SKIP_PRECOMPILE_HEADERS ON
    )
endif ()

# Generate source groups for use in IDEs
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} FILES ${SOURCE_FILES})

if (WIN32)
    # clang-cl doesn't support resource files
    if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        list(APPEND SOURCE_FILES "icon.rc")
    endif ()
endif ()


####################################################################################################
#  EXECUTABLE TARGET
####################################################################################################
set(BINNAME "RoR")
add_executable(${BINNAME} ${SOURCE_FILES})

if (ROR_RENDERER_PUBLIC_LAUNCHER)
    # Preserve the logical game target so its dependency/resource/test wiring
    # remains intact, while giving the two process roles exact, non-colliding
    # sibling names. The public executable carries Ogre-Next-first intent; the
    # immutable package facts continue to admit only this OGRE 14 child until a
    # production RoR-OgreNext child clears its independent renderer gates.
    set_target_properties(
        ${BINNAME}
        PROPERTIES
            OUTPUT_NAME "RoR-Ogre14"
            PDB_NAME "RoR-Ogre14"
            DEBUG_POSTFIX "")
    if (WIN32 AND NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        target_compile_definitions(
            ${BINNAME}
            PRIVATE
                ROR_RESOURCE_INTERNAL_NAME_STRING=\"RoR-Ogre14\"
                ROR_RESOURCE_ORIGINAL_FILENAME_STRING=\"RoR-Ogre14.exe\")
    endif ()

    include(RendererLauncherPackageConfig)
    set(_ror_renderer_launcher_generated_include_dir
        "${CMAKE_CURRENT_BINARY_DIR}/renderer-launcher-generated")
    ror_configure_renderer_launcher_package_facts(
        "${CMAKE_CURRENT_LIST_DIR}/system/RendererLauncherPackageConfig.h.in"
        "${_ror_renderer_launcher_generated_include_dir}"
        _ror_renderer_launcher_generated_header)

    set(_ror_renderer_launcher_source_tree_sources
        system/RendererLauncherMain.cpp
        system/RendererPublicLauncher.cpp
        system/RendererPublicLauncher.h
        system/RendererBridgeEndpoint.cpp
        system/RendererBridgeEndpoint.h
        system/RendererBridgeLaunchPlan.cpp
        system/RendererBridgeLaunchPlan.h
        system/RendererBridgeProcessSupervisor.cpp
        system/RendererBridgeProcessSupervisor.h
        system/RendererChildIntent.cpp
        system/RendererChildIntent.h
        system/RendererChildLauncher.cpp
        system/RendererChildLauncher.h
        system/RendererPackageRuntimeProbe.cpp
        system/RendererPackageRuntimeProbe.h
        system/RendererSiblingPath.cpp
        system/RendererSiblingPath.h
        system/RendererPackagedMediaPath.cpp
        system/RendererPackagedMediaPath.h
        gfx/RendererBackendPolicy.cpp
        gfx/RendererBackendPolicy.h
        gfx/RendererStartupPlan.cpp
        gfx/RendererStartupPlan.h
        gfx/RendererStartupHandoff.cpp
        gfx/RendererStartupHandoff.h)
    set(_ror_renderer_launcher_sources
        ${_ror_renderer_launcher_source_tree_sources}
        "${_ror_renderer_launcher_generated_header}")

    add_executable(
        ror_renderer_launcher
        ${_ror_renderer_launcher_sources})
    add_dependencies(ror_renderer_launcher ${BINNAME})
    if (ROR_OGRE_NEXT_PRODUCTION_PACKAGE)
        # The launcher is never emitted ahead of the authenticated real-child
        # stage. Immutable availability/readiness facts remain fail-closed
        # until the separate native admission pass promotes them.
        add_dependencies(
            ror_renderer_launcher
            ${ROR_OGRE_NEXT_PRODUCT_TARGET})
    endif ()
    target_include_directories(
        ror_renderer_launcher
        PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/gfx"
            "${CMAKE_CURRENT_LIST_DIR}/system"
            "${_ror_renderer_launcher_generated_include_dir}")
    set_target_properties(
        ror_renderer_launcher
        PROPERTIES
            OUTPUT_NAME "RoR"
            DEBUG_POSTFIX ""
            CXX_STANDARD 11
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO)
    if (MSVC)
        target_compile_options(
            ror_renderer_launcher
            PRIVATE /W4 /WX /fp:strict /permissive-)
    else ()
        target_compile_options(
            ror_renderer_launcher
            PRIVATE
                -Wall
                -Wextra
                -Werror
                -pedantic
                -fno-fast-math
                -ffp-contract=off)
    endif ()
    if (WIN32)
        set_target_properties(
            ror_renderer_launcher
            PROPERTIES WIN32_EXECUTABLE YES)
        if (NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
            target_sources(ror_renderer_launcher PRIVATE "icon.rc")
            target_include_directories(
                ror_renderer_launcher
                PRIVATE "${CMAKE_BINARY_DIR}/source/version_info")
            add_dependencies(ror_renderer_launcher generate_version)
        endif ()
        target_compile_definitions(
            ror_renderer_launcher
            PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
        target_link_libraries(ror_renderer_launcher PRIVATE Shell32)
        if (MINGW)
            target_link_options(ror_renderer_launcher PRIVATE -municode)
        endif ()
    endif ()
    set_property(TARGET ror_renderer_launcher PROPERTY FOLDER "Application")
    source_group(
        TREE "${CMAKE_CURRENT_LIST_DIR}"
        FILES ${_ror_renderer_launcher_source_tree_sources})
    source_group(
        "Generated"
        FILES "${_ror_renderer_launcher_generated_header}")

    message(STATUS
        "Renderer suite packages Ogre-Next-first RoR with admitted "
        "RoR-Ogre14 fallback")
endif ()

if (ROR_OGRE14 AND UNIX AND NOT APPLE)
    # Keep both direct dependencies and explicitly loaded OGRE plugins
    # discoverable when the installed tree moves. Dependency libraries and
    # plugins carry their own package-relative $ORIGIN paths.
    set_target_properties(
        ${BINNAME}
        PROPERTIES
            INSTALL_RPATH "$ORIGIN/lib;$ORIGIN/lib/OGRE"
            INSTALL_RPATH_USE_LINK_PATH FALSE)
endif ()

if (ROR_BUILD_TESTS)
    target_compile_definitions(
        ${BINNAME}
        PRIVATE ROR_ENABLE_INTERNAL_TEST_HOOKS
    )
endif ()

if (WIN32)
    set_target_properties(${BINNAME} PROPERTIES WIN32_EXECUTABLE YES)
    # disable some annoying VS warnings:
    # warning C4244: 'initializing' : conversion from 'const float' to 'int', possible loss of data
    # warning C4305: 'initializing' : truncation from 'double' to 'const float'
    target_compile_options(${BINNAME} PRIVATE /wd4305 /wd4244)
    # Disable non-standard behavior
    target_compile_options(${BINNAME} PRIVATE /permissive-)
endif ()

if (MSVC)
    # set startup project for Visual Studio Builds
    set(_ror_visual_studio_startup_target ${BINNAME})
    if (ROR_RENDERER_PUBLIC_LAUNCHER)
        set(_ror_visual_studio_startup_target ror_renderer_launcher)
    endif ()
    set_property(
            DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            PROPERTY
            VS_STARTUP_PROJECT ${_ror_visual_studio_startup_target}
    )
    # set Visual Studio working directory to the Configuration directory
    set_target_properties(
            ${BINNAME}
            PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${RUNTIME_OUTPUT_DIRECTORY}"
    )
    if (ROR_RENDERER_PUBLIC_LAUNCHER)
        set_target_properties(
                ror_renderer_launcher
                PROPERTIES
                VS_DEBUGGER_WORKING_DIRECTORY "${RUNTIME_OUTPUT_DIRECTORY}"
        )
    endif ()
endif ()

####################################################################################################
#  PREPROCESSOR DEFINITIONS
####################################################################################################

target_compile_definitions(${BINNAME} PRIVATE
        USE_MUMBLE  # build with support for Mumble positional audio, has no dependencies but requires linking against librt on UNIX
        #FEAT_DEBUG_MUTEX
        )

if (ROR_FEAT_TIMING)
    target_compile_definitions(${BINNAME} PRIVATE FEAT_TIMING)
endif ()

if (ROR_USE_OIS_G27)
    target_compile_definitions(${BINNAME} PRIVATE USE_OIS_G27)
endif ()

if (WIN32)
    target_compile_definitions(${BINNAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif ()

####################################################################################################
#  INCLUDE DIRECTORIES
####################################################################################################

target_include_directories(${BINNAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/external/header_only
        .
        audio
        datatypes
        gameplay
        gfx
        gfx/camera
        gfx/hydrax
        gfx/particle
        gfx/skyx
        gui
        gui/imgui
        gui/mygui
        gui/panels
        network
        physics
        physics/air
        physics/collision
        physics/flex
        physics/utils
        physics/water
        resources
        resources/addonpart_fileformat
        resources/odef_fileformat/
        resources/otc_fileformat/
        resources/rig_def_fileformat
        resources/skin_fileformat
        resources/terrn2_fileformat
        resources/tobj_fileformat
        resources/tuneup_fileformat
        system
        scripting
        scripting/bindings
        terrain
        terrain/map
        threadpool
        utils
        utils/memory
        utils/bbcode
        worldmodel
        )


####################################################################################################
# EXTERNAL DEPENDENCIES
#
# In CMake searching for external dependencies is done with the help of FindModule scripts. These
# are run by calling the find_package() command.
# CMake already ships with many ready-made FindModules (e.g. for OpenAL). Additional FindModule
# scripts that can be used are placed in the CMakeModule directory `<ror_root>/cmake`. These
# scripts typically define a set of variables, such as
#
#   <dependency_name>_FOUND           # indicates if the dependecy was found
#   <dependency_name>_INCLUDE_DIRS    # directories with headers to include
#   <dependency_name>_LIBRARIES       # libraries to link against
#
# Instead of directly using these variables, in modern CMake it is common to use additionally
# provided 'imported targets' for the sought dependency. This greatly facilitates dealing with
# dependencies. Adding a dependency to  an executable target is now simply a matter of specifying
# the imported target of the dependency in the target_link_libraries() command.
#
####################################################################################################


#  REQUIRED DEPENDENCIES
# -----------------------

if (WIN32)
    set(OS_LIBS Ws2_32)
elseif (APPLE)
    # Darwin provides POSIX shared-memory APIs in libSystem and has no librt.
    # It also must not acquire the Linux/X11 desktop dependency implicitly.
    set(OS_LIBS ${CMAKE_DL_LIBS})
else ()
    find_package(X11 REQUIRED)
    set(OS_LIBS X11::X11 ${CMAKE_DL_LIBS} rt)
endif ()
target_link_libraries(${BINNAME} PRIVATE ${OS_LIBS} version_info)


target_link_libraries(${BINNAME} PRIVATE
        Threads::Threads
        ${OGRE_LIBRARIES}
        ois::ois
        fmt::fmt
        MyGUI::MyGUI
        OpenSSL::Crypto
        )
if (ROR_OGRE14)
    # TerrainMaterialGeneratorA consumes RTShaderSystem directly.
    target_link_libraries(${BINNAME} PRIVATE
        OGRE::RTShaderSystem
        SDL2::SDL2)
endif ()
target_include_directories(${BINNAME} PRIVATE ${OGRE_INCLUDE_DIRS} ${RapidJSON_INCLUDE_DIRS})
if (TARGET MyGUI::OgrePlatform)
    target_link_libraries(${BINNAME} PRIVATE MyGUI::OgrePlatform)
endif ()
target_compile_definitions(${BINNAME} PRIVATE $<$<PLATFORM_ID:WINDOWS>:MYGUI_STATIC>)

#  OPTIONAL DEPENDENCIES
# -----------------------

if (ROR_USE_OPENAL)
    target_link_libraries(${BINNAME} PRIVATE ${OPENAL_LIBRARY})
    target_include_directories(${BINNAME} PRIVATE ${OPENAL_INCLUDE_DIR})
    target_compile_definitions(${BINNAME} PRIVATE USE_OPENAL)
endif ()

if (ROR_USE_DISCORD_RPC)
    target_link_libraries(${BINNAME} PRIVATE discord-rpc::discord-rpc)
    target_compile_definitions(${BINNAME} PRIVATE USE_DISCORD_RPC)
endif ()

if (ROR_USE_SOCKETW)
    target_link_libraries(${BINNAME} PRIVATE SocketW::SocketW)
    target_compile_definitions(${BINNAME} PRIVATE USE_SOCKETW)
endif ()

if (ROR_USE_ANGELSCRIPT)
    target_link_libraries(${BINNAME} PRIVATE Angelscript::angelscript angelscript_addons)
    target_include_directories(${BINNAME} PRIVATE ${CMAKE_SOURCE_DIR}/external/angelscript_addons)
    target_compile_definitions(${BINNAME} PRIVATE USE_ANGELSCRIPT AS_USE_NAMESPACE)
endif ()

if (ROR_USE_CURL)
    target_link_libraries(${BINNAME} PRIVATE CURL::libcurl)
    target_compile_definitions(${BINNAME} PRIVATE USE_CURL)

    if (WIN32)
        target_compile_definitions(${BINNAME} PRIVATE WIN32_LEAN_AND_MEAN)
    endif ()
endif ()

if (ROR_USE_CAELUM)
    target_link_libraries(${BINNAME} PRIVATE Caelum::Caelum)
    target_compile_definitions(${BINNAME} PRIVATE USE_CAELUM)
endif ()

if (ROR_USE_PAGED)
    target_link_libraries(${BINNAME} PRIVATE PagedGeometry::PagedGeometry)
    target_compile_definitions(${BINNAME} PRIVATE USE_PAGED)
endif ()

# Caelum and PagedGeometry still expose std::auto_ptr in their public headers.
# MSVC removes that compatibility type in C++17 unless its documented
# transition switch is enabled. Scope it to RoR's legacy consumer target and
# only when at least one affected header surface is active.
if (MSVC AND (ROR_USE_CAELUM OR ROR_USE_PAGED))
    target_compile_definitions(${BINNAME} PRIVATE _HAS_AUTO_PTR_ETC=1)
endif ()

####################################################################################################
#  POST-BUILD STEPS
####################################################################################################

if (ROR_USE_PCH)
    target_precompile_headers(${BINNAME} PRIVATE pch.h)
endif ()

extract_pot("${SOURCE_FILES}")

# Configure plugins.cfg
# -----------------------
if (NOT WIN32)
    set(CFG_COMMENT_RENDERSYSTEM_D3D9 "# ")
endif ()

if (WIN32)
    set(CFG_COMMENT_RENDERSYSTEM_GL "# ")
endif ()

set(CFG_COMMENT_RENDERSYSTEM_D3D11 "# ")
set(CFG_COMMENT_RENDERSYSTEM_GL3PLUS "# ")
set(CFG_COMMENT_RENDERSYSTEM_METAL "# ")
set(CFG_COMMENT_PLUGIN_CG "")

if (ROR_OGRE14)
    ror_ogre14_runtime_contract(
        _ror_ogre14_runtime
        "${CMAKE_SYSTEM_NAME}"
        "${CMAKE_SYSTEM_PROCESSOR}")
    get_property(
        _ror_ogre14_multi_config
        GLOBAL
        PROPERTY GENERATOR_IS_MULTI_CONFIG)
    ror_ogre14_package_roots(
        _ror_ogre14_package
        "${_ror_ogre14_multi_config}"
        "${CMAKE_BUILD_TYPE}"
        "${CMAKE_CONFIGURATION_TYPES}")

    # Conan CMakeDeps exposes one immutable package root per build
    # configuration. Never derive an aggregate root from OGRE_INCLUDE_DIRS:
    # multi-config generation intentionally carries both package variants.
    ror_ogre14_media_root(
        _ror_ogre14_media_root
        "${_ror_ogre14_package_MEDIA}"
        "${CMAKE_SYSTEM_NAME}")
    set(_ror_ogre14_plugin_dir_RELEASE
        "${_ror_ogre14_package_RELEASE}/${_ror_ogre14_runtime_PACKAGE_PLUGIN_SUBDIR}")
    set(_ror_ogre14_plugin_dir_DEBUG
        "${_ror_ogre14_package_DEBUG}/${_ror_ogre14_runtime_PACKAGE_PLUGIN_SUBDIR}")
    foreach (_ror_ogre14_plugin_dir IN ITEMS
            "${_ror_ogre14_plugin_dir_RELEASE}"
            "${_ror_ogre14_plugin_dir_DEBUG}")
        if (NOT IS_DIRECTORY "${_ror_ogre14_plugin_dir}")
            message(FATAL_ERROR
                "The pinned OGRE 14 package is missing its platform plugin "
                "directory: ${_ror_ogre14_plugin_dir}")
        endif ()
    endforeach ()

    # Select exactly one supported renderer for each R0 target. The macOS
    # package also carries Metal for the later migration slice, but RoR keeps
    # it inactive until Cocoa hosting and authored MSL materials are ready.
    ror_ogre14_plugin_template_contract(
        _ror_ogre14_plugin_comments
        "${_ror_ogre14_runtime_RENDERER_PLUGIN}")
    set(CFG_COMMENT_RENDERSYSTEM_D3D9
        "${_ror_ogre14_plugin_comments_D3D9}")
    set(CFG_COMMENT_RENDERSYSTEM_D3D11
        "${_ror_ogre14_plugin_comments_D3D11}")
    set(CFG_COMMENT_RENDERSYSTEM_GL
        "${_ror_ogre14_plugin_comments_GL}")
    set(CFG_COMMENT_RENDERSYSTEM_GL3PLUS
        "${_ror_ogre14_plugin_comments_GL3PLUS}")
    set(CFG_COMMENT_RENDERSYSTEM_METAL
        "${_ror_ogre14_plugin_comments_METAL}")
    set(CFG_COMMENT_PLUGIN_CG
        "${_ror_ogre14_plugin_comments_CG}")
endif ()

if (ROR_OGRE14)
    set(PLUGINS_FOLDER "${_ror_ogre14_plugin_dir_RELEASE}")
elseif (DEFINED OGRE_PLUGIN_DIR AND NOT WIN32)
    set(PLUGINS_FOLDER "${OGRE_PLUGIN_DIR}")
else ()
    set(PLUGINS_FOLDER ".")
endif ()

if (ROR_OGRE14)
    set(CFG_OGRE_PLUGIN_CAELUM "# Plugin=Caelum disabled for OGRE 14 bring-up")
    set(CFG_OGRE_PLUGIN_CAELUM_D "# Plugin=Caelum disabled for OGRE 14 bring-up")
elseif (WIN32)
    set(CFG_OGRE_PLUGIN_CAELUM "Plugin=Caelum")
    set(CFG_OGRE_PLUGIN_CAELUM_D "Plugin=Caelum_d")
elseif (APPLE)
    if (ROR_USE_CAELUM STREQUAL "SYSTEM")
        set(CFG_OGRE_PLUGIN_CAELUM "Plugin=../libCaelum.dylib")
        set(CFG_OGRE_PLUGIN_CAELUM_D "Plugin=../libCaelum_d.dylib")
    else ()
        set(CFG_OGRE_PLUGIN_CAELUM "Plugin=libCaelum.dylib")
        set(CFG_OGRE_PLUGIN_CAELUM_D "Plugin=libCaelum_d.dylib")
    endif ()
else ()
    if (ROR_USE_CAELUM STREQUAL "SYSTEM")
        set(CFG_OGRE_PLUGIN_CAELUM "Plugin=../libCaelum.so")
        set(CFG_OGRE_PLUGIN_CAELUM_D "Plugin=../libCaelum_d.so")
    else ()
        set(CFG_OGRE_PLUGIN_CAELUM "Plugin=libCaelum.so")
        set(CFG_OGRE_PLUGIN_CAELUM_D "Plugin=libCaelum_d.so")
    endif ()
endif ()

configure_file(plugins.cfg.in ${RUNTIME_OUTPUT_DIRECTORY}/plugins.cfg)
if (ROR_OGRE14)
    # OGRE 14 appends its own Windows Debug binary suffix while loading an
    # unsuffixed token. Reusing the release template avoids `_d_d.dll`.
    set(PLUGINS_FOLDER "${_ror_ogre14_plugin_dir_DEBUG}")
    configure_file(plugins.cfg.in ${RUNTIME_OUTPUT_DIRECTORY}/plugins_d.cfg)
    set(PLUGINS_FOLDER "${_ror_ogre14_plugin_dir_RELEASE}")
else ()
    configure_file(plugins_d.cfg.in ${RUNTIME_OUTPUT_DIRECTORY}/plugins_d.cfg)
endif ()

# Copy resources to bin directory
# -----------------------
recursive_zip_folder("${CMAKE_SOURCE_DIR}/resources" "${RUNTIME_OUTPUT_DIRECTORY}/resources")
if (ROR_CREATE_CONTENT_FOLDER)
    recursive_zip_folder("${CMAKE_SOURCE_DIR}/content" "${RUNTIME_OUTPUT_DIRECTORY}/content")
endif ()

if (ROR_OGRE14)
    # OGRE 14's renderer, RTShaderSystem, and Terrain component load these
    # shader libraries by resource name at runtime. Stage the exact media
    # shipped by the pinned OGRE package instead of carrying stale third-party
    # copies in the source tree.
    set(_ror_ogre14_main_media
        "${_ror_ogre14_media_root}/Main")
    set(_ror_ogre14_rtshader_media
        "${_ror_ogre14_media_root}/RTShaderLib")
    set(_ror_ogre14_terrain_media
        "${_ror_ogre14_media_root}/Terrain")

    file(MAKE_DIRECTORY
        "${RUNTIME_OUTPUT_DIRECTORY}/resources/ogre14/Main"
        "${RUNTIME_OUTPUT_DIRECTORY}/resources/ogre14/RTShaderLib"
        "${RUNTIME_OUTPUT_DIRECTORY}/resources/ogre14/Terrain")
    fast_copy(
        "${_ror_ogre14_main_media}"
        "${RUNTIME_OUTPUT_DIRECTORY}/resources/ogre14/Main")
    fast_copy(
        "${_ror_ogre14_rtshader_media}"
        "${RUNTIME_OUTPUT_DIRECTORY}/resources/ogre14/RTShaderLib")
    fast_copy(
        "${_ror_ogre14_terrain_media}"
        "${RUNTIME_OUTPUT_DIRECTORY}/resources/ogre14/Terrain")
    add_dependencies(
        ${BINNAME}
        fast_copy_Main
        fast_copy_RTShaderLib
        fast_copy_Terrain)
endif ()

fast_copy("${CMAKE_SOURCE_DIR}/resources/managed_materials" "${RUNTIME_OUTPUT_DIRECTORY}/resources/managed_materials")
fast_copy("${CMAKE_SOURCE_DIR}/resources/fonts" "${RUNTIME_OUTPUT_DIRECTORY}/languages")
fast_copy("${CMAKE_SOURCE_DIR}/languages" "${RUNTIME_OUTPUT_DIRECTORY}/languages")

if (APPLE AND ROR_OGRE14)
    # Build a signed, relocatable application only when explicitly requested.
    # StageMacOSBundle derives the complete dylib closure from the executable
    # and active OGRE plugins; ROR_RUNTIME_DYLIBS remains available for future
    # non-linked runtime libraries.
    set(_ror_macos_bundle_path
        "${RUNTIME_OUTPUT_DIRECTORY}/RoR.app")
    set(_ror_macos_primary_target ${BINNAME})
    set(_ror_macos_sibling_executables "")
    if (ROR_RENDERER_PUBLIC_LAUNCHER)
        set(_ror_macos_primary_target ror_renderer_launcher)
        set(_ror_macos_sibling_executables "$<TARGET_FILE:${BINNAME}>")
    endif ()
    set(_ror_macos_bundle_dependencies
        ${BINNAME}
        ${_ror_macos_primary_target}
        zip_folder_resources
        fast_copy_managed_materials
        fast_copy_fonts
        fast_copy_languages
        fast_copy_Main
        fast_copy_RTShaderLib
        fast_copy_Terrain)
    if (ROR_OGRE_NEXT_PRODUCTION_PACKAGE)
        list(APPEND _ror_macos_sibling_executables
            "${ROR_OGRE_NEXT_PRODUCT_CHILD}")
        list(APPEND _ror_macos_bundle_dependencies
            ${ROR_OGRE_NEXT_PRODUCT_TARGET})
    endif ()
    # A CMake list interpolated directly into COMMAND expands into multiple
    # argv records even when the surrounding -D argument is quoted. Preserve
    # the semicolon as data so StageMacOSBundle receives the complete sibling
    # inventory in one cache-definition argument.
    string(REPLACE ";" "\\;"
        _ror_macos_sibling_executables_argument
        "${_ror_macos_sibling_executables}")
    set(_ror_macos_bundle_arguments
        "-DROR_BUNDLE=${_ror_macos_bundle_path}"
        "-DROR_EXECUTABLE=$<TARGET_FILE:${_ror_macos_primary_target}>"
        "-DROR_BUNDLE_EXECUTABLE_NAME=RoR"
        "-DROR_SIBLING_EXECUTABLES=${_ror_macos_sibling_executables_argument}"
        "-DROR_RESOURCES=${RUNTIME_OUTPUT_DIRECTORY}/resources"
        "-DROR_LANGUAGES=${RUNTIME_OUTPUT_DIRECTORY}/languages"
        "-DROR_OGRE_PACKAGE_DIR=$<IF:$<CONFIG:Debug>,${_ror_ogre14_package_DEBUG},${_ror_ogre14_package_RELEASE}>"
        "-DROR_OGRE_PLUGIN_DIR=$<IF:$<CONFIG:Debug>,${_ror_ogre14_plugin_dir_DEBUG},${_ror_ogre14_plugin_dir_RELEASE}>"
        "-DROR_OGRE_PLUGINS_CFG=$<IF:$<CONFIG:Debug>,${RUNTIME_OUTPUT_DIRECTORY}/plugins_d.cfg,${RUNTIME_OUTPUT_DIRECTORY}/plugins.cfg>"
        "-DROR_OGRE_INSTALL_PLUGIN_FOLDER=${_ror_ogre14_runtime_INSTALL_PLUGIN_FOLDER}"
        "-DROR_RUNTIME_DYLIBS="
        "-DROR_INFO_PLIST_TEMPLATE=${CMAKE_SOURCE_DIR}/cmake/macos/RoRInfo.plist.in"
        "-DROR_BUNDLE_SHORT_VERSION=${CMAKE_PROJECT_VERSION}"
        "-DROR_BUNDLE_BUILD_VERSION=${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.0"
        "-DROR_MACOS_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")

    if (ROR_OGRE_NEXT_PRODUCTION_PACKAGE)
        list(APPEND _ror_macos_bundle_arguments
            "-DROR_OGRE_NEXT_PRODUCT_ROOT=${ROR_OGRE_NEXT_PRODUCT_STAGE_ROOT}")
    endif ()

    if (ROR_CREATE_CONTENT_FOLDER)
        list(APPEND _ror_macos_bundle_arguments
            "-DROR_CONTENT=${RUNTIME_OUTPUT_DIRECTORY}/content")
        list(APPEND _ror_macos_bundle_dependencies zip_folder_content)
    endif ()

    add_custom_target(ror_macos_bundle
        COMMAND
            "${CMAKE_COMMAND}"
            ${_ror_macos_bundle_arguments}
            -P "${CMAKE_SOURCE_DIR}/cmake/macos/StageMacOSBundle.cmake"
        COMMENT "Staging and ad-hoc signing ${_ror_macos_bundle_path}"
        USES_TERMINAL
        VERBATIM)
    add_dependencies(
        ror_macos_bundle
        ${_ror_macos_bundle_dependencies})
    set_property(TARGET ror_macos_bundle PROPERTY FOLDER "Packaging")

    unset(_ror_macos_bundle_path)
    unset(_ror_macos_primary_target)
    unset(_ror_macos_sibling_executables)
    unset(_ror_macos_sibling_executables_argument)
    unset(_ror_macos_bundle_dependencies)
    unset(_ror_macos_bundle_arguments)
endif ()

# TODO: Run fixup_bundle instead
if (IS_DIRECTORY "${CMAKE_BINARY_DIR}/conan/bin")
    file(COPY "${CMAKE_BINARY_DIR}/conan/bin/" DESTINATION "${CMAKE_BINARY_DIR}/bin/")
endif ()


#  Install targets
# -----------------------
set(_ror_application_install_targets ${BINNAME})
if (ROR_RENDERER_PUBLIC_LAUNCHER)
    list(PREPEND _ror_application_install_targets ror_renderer_launcher)
endif ()
install(
    TARGETS ${_ror_application_install_targets}
    DESTINATION .
    COMPONENT "Base_Game")
unset(_ror_application_install_targets)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/fonts/ DESTINATION languages COMPONENT "Base_Game")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/languages/ DESTINATION languages COMPONENT "Base_Game" FILES_MATCHING PATTERN "*.mo" )
install(DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}/resources/ DESTINATION resources COMPONENT "Base_Game")
if (ROR_CREATE_CONTENT_FOLDER)
    install(DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}/content/ DESTINATION content COMPONENT "Base_Content")
endif ()

if (ROR_OGRE_NEXT_PRODUCTION_PACKAGE AND NOT APPLE)
    # Preserve the verified product-stage layout in Linux/Windows installs and
    # CPack archives: RoR-OgreNext sits beside RoR and resolves its immutable
    # media under resources/ogrenext. USE_SOURCE_PERMISSIONS retains the native
    # execute bit on Unix. A missing stage is a hard install failure.
    install(
        DIRECTORY "${ROR_OGRE_NEXT_PRODUCT_STAGE_ROOT}/"
        DESTINATION .
        COMPONENT "Base_Game"
        USE_SOURCE_PERMISSIONS)
endif ()

if (ROR_OGRE14 AND NOT APPLE)
    # Never install the build-tree config: its PluginFolder deliberately names
    # the absolute Conan package used for local execution.
    set(_ror_ogre14_build_plugin_folder "${PLUGINS_FOLDER}")
    set(PLUGINS_FOLDER
        "${_ror_ogre14_runtime_INSTALL_PLUGIN_FOLDER}")
    configure_file(
        plugins.cfg.in
        ${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg)
    configure_file(
        plugins.cfg.in
        ${CMAKE_CURRENT_BINARY_DIR}/plugins-install-d.cfg)
    set(PLUGINS_FOLDER "${_ror_ogre14_build_plugin_folder}")
endif ()

if (WIN32)
    if (ROR_OGRE14)
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg
            DESTINATION .
            RENAME plugins.cfg
            COMPONENT "Base_Game")
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/plugins-install-d.cfg
            DESTINATION .
            RENAME plugins_d.cfg
            COMPONENT "Base_Game")
    else ()
        install(FILES ${RUNTIME_OUTPUT_DIRECTORY}/plugins.cfg DESTINATION . COMPONENT "Base_Game")
    endif ()
    install(FILES ${CMAKE_SOURCE_DIR}/tools/windows/.itch.toml DESTINATION . COMPONENT "Base_Game")
    if (ROR_OGRE14)
        string(CONCAT
            _ror_windows_runtime_install_code
            "set(ROR_WINDOWS_RUNTIME_OUTPUT_DIRECTORY "
                "\"${RUNTIME_OUTPUT_DIRECTORY}\")\n"
            "set(ROR_WINDOWS_EXPECTED_PLUGINS "
                "\"${_ror_ogre14_runtime_ACTIVE_PLUGINS}\")\n"
            "set(ROR_WINDOWS_PLUGIN_BINARIES_USE_DEBUG_SUFFIX "
                "\"${_ror_ogre14_runtime_PLUGIN_BINARIES_USE_DEBUG_SUFFIX}\")\n"
            "include(\"${CMAKE_SOURCE_DIR}/cmake/windows/"
                "StageWindowsRuntime.cmake\")\n")
        install(
            CODE "${_ror_windows_runtime_install_code}"
            COMPONENT "Base_Game")
    else ()
        install(CODE "file(GLOB files ${RUNTIME_OUTPUT_DIRECTORY}/*.dll)
                file(INSTALL \${files} DESTINATION \${CMAKE_INSTALL_PREFIX})" COMPONENT "Base_Game")
    endif ()
    install(CODE "file(GLOB files ${RUNTIME_OUTPUT_DIRECTORY}/*.exe)
            file(INSTALL \${files} DESTINATION \${CMAKE_INSTALL_PREFIX})" COMPONENT "Base_Game")
elseif (APPLE)
    set(PLUGINS_FOLDER "lib")
    configure_file(plugins.cfg.in ${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg)
    if (ROR_OGRE14)
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg
            DESTINATION .
            RENAME plugins.cfg
            COMPONENT "Base_Game")
        configure_file(
            plugins.cfg.in
            ${CMAKE_CURRENT_BINARY_DIR}/plugins-install-d.cfg)
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/plugins-install-d.cfg
            DESTINATION .
            RENAME plugins_d.cfg
            COMPONENT "Base_Game")
    else ()
        # Preserve the legacy component ownership exactly; downstream
        # component-less installs historically include this file.
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg
            DESTINATION .
            RENAME plugins.cfg)
    endif ()
    install(CODE "file(GLOB_RECURSE files ${RUNTIME_OUTPUT_DIRECTORY}/*.dylib)
            file(INSTALL \${files} DESTINATION \${CMAKE_INSTALL_PREFIX}/lib FOLLOW_SYMLINK_CHAIN)"
            COMPONENT "Base_Game")
else ()
    if (ROR_OGRE14)
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg
            DESTINATION .
            RENAME plugins.cfg
            COMPONENT "Base_Game")
        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/plugins-install-d.cfg
            DESTINATION .
            RENAME plugins_d.cfg
            COMPONENT "Base_Game")
        install(
            PROGRAMS ${CMAKE_SOURCE_DIR}/tools/linux/RunRoR-ogre14
            DESTINATION .
            RENAME RunRoR
            COMPONENT "Base_Game")
        install(
            FILES ${CMAKE_SOURCE_DIR}/tools/linux/.itch.toml
            DESTINATION .
            COMPONENT "Base_Game")

        set(_ror_linux_install_root_code
[=[set(ROR_LINUX_INSTALL_ROOT "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
]=])
        set(_ror_linux_installed_game_executable "RoR")
        if (ROR_RENDERER_PUBLIC_LAUNCHER)
            set(_ror_linux_installed_game_executable "RoR-Ogre14")
        endif ()
        ror_ogre14_cmakedeps_runtime_search_dirs(
            _ror_linux_runtime_search_dirs
            "${CMAKE_BUILD_TYPE}")
        ror_ogre14_install_set_list_code(
            _ror_linux_runtime_search_dirs_install_code
            ROR_LINUX_RUNTIME_SEARCH_DIRS
            _ror_linux_runtime_search_dirs)
        string(CONCAT
            _ror_linux_runtime_install_code
            "set(ROR_LINUX_BUILD_EXECUTABLE "
                "\"$<TARGET_FILE:${BINNAME}>\")\n"
            "${_ror_linux_install_root_code}"
            "set(ROR_LINUX_INSTALLED_EXECUTABLE_NAME "
                "\"${_ror_linux_installed_game_executable}\")\n"
            "set(ROR_LINUX_PLUGIN_DIR "
                "\"$<IF:$<CONFIG:Debug>,"
                "${_ror_ogre14_plugin_dir_DEBUG},"
                "${_ror_ogre14_plugin_dir_RELEASE}>\")\n"
            "set(ROR_LINUX_PLUGINS_CFG "
                "\"${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg\")\n"
            "${_ror_linux_runtime_search_dirs_install_code}"
            "set(ROR_LINUX_INSTALL_PLUGIN_FOLDER "
                "\"${_ror_ogre14_runtime_INSTALL_PLUGIN_FOLDER}\")\n"
            "set(ROR_LINUX_CONTRACT_MODULE "
                "\"${CMAKE_SOURCE_DIR}/cmake/linux/"
                "LinuxRuntimeContract.cmake\")\n"
            "include(\"${CMAKE_SOURCE_DIR}/cmake/linux/"
                "StageLinuxRuntime.cmake\")\n")
        install(
            CODE "${_ror_linux_runtime_install_code}"
            COMPONENT "Base_Game")
    else ()
        set(PLUGINS_FOLDER "lib")
        configure_file(plugins.cfg.in ${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg DESTINATION . RENAME plugins.cfg)
        install(PROGRAMS ${CMAKE_SOURCE_DIR}/tools/linux/RunRoR DESTINATION .)
        install(FILES ${CMAKE_SOURCE_DIR}/tools/linux/.itch.toml DESTINATION .)
        install(CODE "file(GLOB_RECURSE files ${RUNTIME_OUTPUT_DIRECTORY}/*.so)
                file(INSTALL \${files} DESTINATION \${CMAKE_INSTALL_PREFIX}/lib FOLLOW_SYMLINK_CHAIN)")
    endif ()
endif ()

if (ROR_OGRE14)
    unset(_ror_ogre14_media_root)
    unset(_ror_ogre14_package_RELEASE)
    unset(_ror_ogre14_package_DEBUG)
    unset(_ror_ogre14_package_MEDIA)
    unset(_ror_ogre14_plugin_dir)
    unset(_ror_ogre14_plugin_dir_RELEASE)
    unset(_ror_ogre14_plugin_dir_DEBUG)
    unset(_ror_ogre14_main_media)
    unset(_ror_ogre14_rtshader_media)
    unset(_ror_ogre14_terrain_media)
    unset(_ror_ogre14_multi_config)
    unset(_ror_ogre14_build_plugin_folder)
    unset(_ror_linux_install_root_code)
    unset(_ror_linux_installed_game_executable)
    unset(_ror_linux_runtime_search_dirs)
    unset(_ror_linux_runtime_search_dirs_install_code)
    unset(_ror_linux_runtime_install_code)
    unset(_ror_ogre14_runtime_PACKAGE_PLUGIN_SUBDIR)
    unset(_ror_ogre14_runtime_RENDERER_PLUGIN)
    unset(_ror_ogre14_runtime_INSTALL_PLUGIN_FOLDER)
    unset(_ror_ogre14_runtime_ACTIVE_PLUGINS)
    unset(_ror_ogre14_runtime_PLUGIN_BINARIES_USE_DEBUG_SUFFIX)
    unset(_ror_windows_runtime_install_code)
    unset(_ror_ogre14_plugin_comments_D3D9)
    unset(_ror_ogre14_plugin_comments_D3D11)
    unset(_ror_ogre14_plugin_comments_GL)
    unset(_ror_ogre14_plugin_comments_GL3PLUS)
    unset(_ror_ogre14_plugin_comments_METAL)
    unset(_ror_ogre14_plugin_comments_CG)
endif ()


#  CPack
# -----------------------
set(CPACK_PACKAGE_NAME "Rigs of Rods")
set(CPACK_PACKAGE_FILE_NAME "rigs-of-rods-${CMAKE_PROJECT_VERSION}-${ROR_BUILD_INSTALLER}")
set(CPACK_PACKAGE_DESCRIPTION "Rigs of Rods soft-body physics simulator")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Rigs of Rods is an open source vehicle simulator based on soft-body physics.")
set(CPACK_PACKAGE_CONTACT "Edgar@AnotherFoxGuy.com")
set(CPACK_PACKAGE_VENDOR "AnotherFoxGuy")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/tools/windows/license.rtf")
set(CPACK_PACKAGE_EXECUTABLES "RoR" "Rigs of Rods")


if (WIN32 AND NOT ROR_BUILD_INSTALLER STREQUAL "Off")
    set(CPACK_GENERATOR INNOSETUP)
    set(CPACK_ADD_REMOVE TRUE)
    set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/tools/windows/AfterInstall.txt")
    set(CPACK_CREATE_DESKTOP_LINKS "RoR" "Rigs of Rods")

    set(CPACK_INNOSETUP_ALLOW_CUSTOM_DIRECTORY ON)
    set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
    set(CPACK_INNOSETUP_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ror.ico")
    set(CPACK_INNOSETUP_SETUP_WizardImageFile "${CMAKE_SOURCE_DIR}/tools/windows/RoRSetupLarge.bmp")
    set(CPACK_INNOSETUP_SETUP_WizardSmallImageFile "${CMAKE_SOURCE_DIR}/tools/windows/ror-64.bmp")
    set(CPACK_INNOSETUP_SETUP_AppId "{{EF536888-919D-47C0-8675-2F7AFA52EE27}")

    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")

    cpack_configure_downloads("http://prdownloads.sourceforge.net/rigs-of-rods/")
    
    if(ROR_BUILD_INSTALLER STREQUAL "Bundled")
        set(CPACK_INNOSETUP_SETUP_DiskSpanning True)
    endif()

    cpack_add_component("Base_Game"     DISPLAY_NAME "Game" GROUP "Main" REQUIRED TRUE)
    cpack_add_component("Base_Content"  DISPLAY_NAME "Starter content" GROUP "Main")

    if(NOT ROR_BUILD_INSTALLER STREQUAL "Offline")
        dl_contentpack("https://github.com/CuriousMike56/ContentPacks/releases/download/2022.12/ContentPack_HeavyEquipment.zip" "HeavyEquipment")
        dl_contentpack("https://github.com/CuriousMike56/ContentPacks/releases/download/2022.12/ContentPack_LightVehicles.zip" "LightVehicles")
        dl_contentpack("https://github.com/CuriousMike56/ContentPacks/releases/download/2022.12/ContentPack_Loads.zip" "Loads")
        dl_contentpack("https://github.com/CuriousMike56/ContentPacks/releases/download/2022.12/ContentPack_AirSea.zip" "AirSea")
        dl_contentpack("https://github.com/CuriousMike56/ContentPacks/releases/download/2022.12/ContentPack_Trains.zip" "Trains")
        dl_contentpack("https://github.com/CuriousMike56/ContentPacks/releases/download/2022.12/ContentPack_Terrains.zip" "Terrains")
    endif ()
    
    file(DOWNLOAD "https://download.microsoft.com/download/1/7/1/1718CCC4-6315-4D8E-9543-8E28A4E18C4C/dxwebsetup.exe" "${CMAKE_BINARY_DIR}/dxwebsetup.exe")
    file(DOWNLOAD "https://aka.ms/vs/17/release/vc_redist.x86.exe" "${CMAKE_BINARY_DIR}/vc_redist.x86.exe")
    file(DOWNLOAD "https://aka.ms/vs/17/release/vc_redist.x64.exe" "${CMAKE_BINARY_DIR}/vc_redist.x64.exe")
    configure_file("${CMAKE_SOURCE_DIR}/tools/windows/dependencies.iss" "${CMAKE_BINARY_DIR}/dependencies.iss" @ONLY)
    set(CPACK_INNOSETUP_EXTRA_SCRIPTS "${CMAKE_BINARY_DIR}/dependencies.iss")

else ()
    set(CPACK_GENERATOR ZIP)
endif ()

if (NOT (APPLE AND ROR_RENDERER_PUBLIC_LAUNCHER))
    include(CPack)
endif ()
