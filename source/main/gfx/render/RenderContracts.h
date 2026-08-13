/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Complete public include for the renderer-neutral boundary.

#pragma once

#include "HdrReference.h"
#include "GraphicsSceneSnapshotProducer.h"
#include "InputEventTransport.h"
#include "MaterialDescriptor.h"
#include "NativeRenderAssetPackage.h"
#include "Ogre14LegacyAssetTranslator.h"
#include "Ogre14LegacyMaterialClosure.h"
#include "Ogre14ParticleCaptureSource.h"
#include "ParallaxProbeReference.h"
#include "PbrReference.h"
#include "ReflectionProbeCaptureReceipt.h"
#include "ReflectionProbeRuntime.h"
#include "RenderAssetDeltaTransport.h"
#include "RenderAssetId.h"
#include "RenderAssetRegistry.h"
#include "RenderBridgeSessionIdentity.h"
#include "RenderFrame.h"
#include "RenderMath.h"
#include "RenderResourceDescriptors.h"
#include "RenderTransportEnvelope.h"
#include "RenderTransportStream.h"
#include "RenderValidation.h"
#include "RendererFrontend.h"
#include "RendererFrontendDirectDispatcher.h"
#include "RendererFrontendPresentationPolicy.h"
#include "RendererFrontendTransportDispatcher.h"
#include "ResourceHandle.h"
#include "SceneSnapshot.h"
#include "SceneSnapshotTransport.h"
#include "SceneGenerationBoundaryTransport.h"
