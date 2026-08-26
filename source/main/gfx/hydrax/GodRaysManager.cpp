/*
--------------------------------------------------------------------------------
This source file is part of sssHydrax.
sssHydrax is a modified version of Hydrax (Copyright (C) 2008 Xavier Verguín González)
to adapt it to SonSilentSea.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place - Suite 330, Boston, MA 02111-1307, USA, or go to
http://www.gnu.org/copyleft/lesser.txt.

Author: Jose Luis Cercós Pita
--------------------------------------------------------------------------------
*/

#include <GodRaysManager.h>

#include "GodRaysModernGlsl.h"
#include "GodRaysModernHlsl.h"
#include <Hydrax.h>

#define _def_GodRays_Projector_Camera_Name "_Hydrax_GodRays_Projector_Camera"
#define _def_GodRays_ManualObject_Name     "_Hydrax_GodRays_ManualObject"
#define _def_GodRays_Depth_Map             "_Hydrax_GodRays_Depth_Map"

#define _def_GodRays_Material_Name  "_Hydrax_GodRays_Material"
#define _def_GodRays_Shader_VP_Name "_Hydrax_GodRays_VP"
#define _def_GodRays_Shader_FP_Name "_Hydrax_GodRays_FP"

#define _def_GodRaysDepth_Material_Name  "_Hydrax_GodRaysDepth_Material"
#define _def_GodRaysDepth_Shader_VP_Name "_Hydrax_GodRaysDepth_VP"
#define _def_GodRaysDepth_Shader_FP_Name "_Hydrax_GodRaysDepth_FP"

const Ogre::Matrix4
      PROJECTIONCLIPSPACE2DTOIMAGESPACE_PERSPECTIVE(
               0.5,    0,    0,  0.5,
               0,   -0.5,    0,  0.5,
               0,      0,    1,    0,
               0,      0,    0,    1);

namespace Hydrax
{
	GodRaysManager::GodRaysManager(Hydrax *h)
		: mCreated(false)
		, mManualGodRays(0)
		, mProjectorCamera(0)
		, mProjectorSN(0)
		, mPerlin(0)
		, mNoiseDerivation(3)
		, mNoisePositionMultiplier(50)
		, mNoiseYNormalMultiplier(10)
		, mNoiseNormalMultiplier(0.175)
		, mSimulationSpeed(5.0f)
		, mNumberOfRays(100)
		, mRaysSize(0.03f)
		, mObjectsIntersections(false)
		, mHydrax(h)
	{
		for (int k = 0; k < 2; k++)
		{
			mMaterials[k].reset();
		}
	}

	GodRaysManager::~GodRaysManager()
	{
		remove();
	}

	void GodRaysManager::create(const HydraxComponent& HC)
	{
		if (mCreated)
		{
			remove();
		}

		// Create our perlin noise module
		mPerlin = new Noise::Perlin(Noise::Perlin::Options(8, 0.085f, 0.49, 2, 0.672));
		mPerlin->create();

		// Initial values, some of them need to be updated each frame
		mProjectorCamera = mHydrax->getSceneManager()->createCamera(_def_GodRays_Projector_Camera_Name);
		mProjectorCamera->setProjectionType(Ogre::PT_PERSPECTIVE);
		// Not forget to set near+far distance in materials
		mProjectorCamera->setNearClipDistance(8);
		mProjectorCamera->setFarClipDistance(40);
		mProjectorCamera->setAspectRatio(1);
		mProjectorCamera->setFOVy(Ogre::Degree(45.0f));
		mProjectorCamera->setVisible(false);
		mProjectorSN = mHydrax->getSceneManager()->getRootSceneNode()->createChildSceneNode();
		mProjectorSN->setPosition(0,0,0);
		mProjectorSN->attachObject(mProjectorCamera);
		mProjectorSN->setDirection(0, -1, 0);

		if (mObjectsIntersections)
		{
		    _createDepthRTT();
		}

		if (!_createMaterials(HC))
		{
			HydraxLOG("GodRaysManager::create(): shader creation failed.");
			mCreated = true;
			remove();
			return;
		}

		std::vector<Ogre::Technique*>::iterator TechIt;

		for(TechIt = mDepthTechniques.begin(); TechIt != mDepthTechniques.end(); TechIt++)
		{
			if (!(*TechIt))
			{
				mDepthTechniques.erase(TechIt);
				// TechIt-- ?
				continue;
			}

			addDepthTechnique((*TechIt), false);
		}

		_createGodRays();

		mCreated = true;
	}

	void GodRaysManager::_createGodRays()
    {
		mManualGodRays = mHydrax->getSceneManager()->createManualObject(_def_GodRays_ManualObject_Name);
		mManualGodRays->setDynamic(true);
		mManualGodRays->setVisible(mHydrax->_isCurrentFrameUnderwater());

		mManualGodRays->begin(_def_GodRays_Material_Name, Ogre::RenderOperation::OT_TRIANGLE_LIST);
#if OGRE_VERSION_MAJOR >= 14
		mManualGodRays->setRenderQueueGroup(Ogre::RENDER_QUEUE_SKIES_LATE+1);
#else
		mManualGodRays->setRenderQueueGroup(Ogre::RENDER_QUEUE_9+1);
#endif

		for(int r = 0; r < mNumberOfRays; r++)
		{
			// Rays are modeled as pyramids, 12 vertex each ray
			for (int k = 0; k < 12; k++)
			{
				mManualGodRays->position(0, 0, 0);
			    mManualGodRays->index(k);
			}
		}

        mManualGodRays->end();
        mProjectorSN->attachObject(mManualGodRays);
    }

	void GodRaysManager::remove()
	{
		if (!mCreated)
		{
			return;
		}

		delete mPerlin;
		mPerlin = static_cast<Noise::Perlin*>(NULL);

		if (mProjectorSN)
		{
			mProjectorSN->detachAllObjects();
		}
		if (mManualGodRays)
		{
			mHydrax->getSceneManager()->destroyManualObject(mManualGodRays);
			mManualGodRays = static_cast<Ogre::ManualObject*>(NULL);
		}

		auto removeGpuProgram = [](const Ogre::String& name)
		{
			Ogre::HighLevelGpuProgramManager& manager =
				Ogre::HighLevelGpuProgramManager::getSingleton();
			if (manager.resourceExists(name))
			{
				manager.unload(name);
				manager.remove(name);
			}
		};

		if (Ogre::MaterialManager::getSingleton().resourceExists(_def_GodRays_Material_Name))
		{
			Ogre::MaterialManager::getSingleton().remove(_def_GodRays_Material_Name);

		}
		removeGpuProgram(_def_GodRays_Shader_VP_Name);
		removeGpuProgram(_def_GodRays_Shader_FP_Name);

		if (Ogre::MaterialManager::getSingleton().resourceExists(_def_GodRaysDepth_Material_Name))
		{
			Ogre::MaterialManager::getSingleton().remove(_def_GodRaysDepth_Material_Name);

		}
		removeGpuProgram(_def_GodRaysDepth_Shader_VP_Name);
		removeGpuProgram(_def_GodRaysDepth_Shader_FP_Name);

		for (int k = 0; k < 2; k++)
		{
			mMaterials[k].reset();
		}

		if (mProjectorRTT)
		{
			Ogre::RenderTarget* RT = mProjectorRTT->getBuffer()->getRenderTarget();
            RT->removeAllListeners();
            RT->removeAllViewports();

			Ogre::TextureManager::getSingleton().remove(mProjectorRTT->getName());
			mProjectorRTT.reset();
		}

		if (mProjectorCamera)
		{
			mHydrax->getSceneManager()->destroyCamera(mProjectorCamera);
			mProjectorCamera = static_cast<Ogre::Camera*>(NULL);
		}

		if (mProjectorSN)
		{
			mProjectorSN->getParentSceneNode()->removeAndDestroyChild(mProjectorSN);
			mProjectorSN = static_cast<Ogre::SceneNode*>(NULL);
		}

		mCreated = false;
	}

	void GodRaysManager::update(const Ogre::Real& timeSinceLastFrame)
	{
		if (!mCreated || !mHydrax->_isCurrentFrameUnderwater())
		{
			return;
		}

		mPerlin->update(timeSinceLastFrame);

		_updateRays();
		_updateProjector();

		if (mObjectsIntersections)
		{
			_updateMaterialsParameters();
			mProjectorRTT->getBuffer()->getRenderTarget()->update();
		}
	}

	void GodRaysManager::_updateRays()
	{
		// Get frustum corners to calculate far plane dimensions
		const Ogre::Vector3 *FrustumCorners = mProjectorCamera->getWorldSpaceCorners();
		// Calcule far plane dimensions
		float FarWidth   = (FrustumCorners[4] - FrustumCorners[5]).length();
		Ogre::Real RaysLength = mProjectorCamera->getFarClipDistance();

		mManualGodRays->beginUpdate(0);

		Ogre::Vector2 Pos;
		Ogre::Real Dis, RayLength;

		// Rays are modeled as piramids, 12 vertex each ray
		//
		//       // 0\\
		//      /|    | |
		//      ||    | |
		//      ||    | |     (0,0)   (1,0)
		//      ||    | |     A       B
		//      ||    | |
		//      |A----|-|B    (0,1)   (1,1)
		//      |/    |/      C       D
		//     C------D

		for(int k = 0; k < mNumberOfRays; k++)
		{
			Pos       = _calculateRayPosition(k);
			Dis       = mRaysSize*RaysLength;
            RayLength = RaysLength*(0.95+Pos.length());

            Pos *= FarWidth/2;

			// 4 Planes, 3 vertices each plane, 12 vertices per ray
			// ----> 1/4
		    // 0
            mManualGodRays->position(0, 0, 0);
		    // A
            mManualGodRays->position(Pos.x, Pos.y, -RayLength);
		    // B
            mManualGodRays->position(Pos.x+Dis, Pos.y, -RayLength);
			// ----> 2/4
		    // 0
            mManualGodRays->position(0, 0, 0);
		    // D
            mManualGodRays->position(Pos.x+Dis, Pos.y+Dis, -RayLength);
		    // B
            mManualGodRays->position(Pos.x+Dis, Pos.y, -RayLength);
			// ----> 3/4
		    // 0
            mManualGodRays->position(0, 0, 0);
		    // C
            mManualGodRays->position(Pos.x, Pos.y+Dis, -RayLength);
		    // D
            mManualGodRays->position(Pos.x+Dis, Pos.y+Dis, -RayLength);
			// ----> 4/4
		    // 0
            mManualGodRays->position(0, 0, 0);
		    // C
            mManualGodRays->position(Pos.x, Pos.y+Dis, -RayLength);
		    // A
            mManualGodRays->position(Pos.x, Pos.y, -RayLength);
		}

		mManualGodRays->end();
	}

	Ogre::Vector2 GodRaysManager::_calculateRayPosition(const int& RayNumber)
	{
		Ogre::Real sqrt_NumberOfRays = Ogre::Math::Sqrt(mNumberOfRays);
		Ogre::Real XCoord = RayNumber;

		while (XCoord >= sqrt_NumberOfRays)
		{
			XCoord -= sqrt_NumberOfRays;
		}

		Ogre::Vector2 RayPos =
			Ogre::Vector2( // X coord
			              static_cast<int>(XCoord),
						  // Y coord
						  static_cast<int>((RayNumber+sqrt_NumberOfRays)/sqrt_NumberOfRays)-1);

		RayPos /= sqrt_NumberOfRays;
		RayPos -= Ogre::Vector2(0.5, 0.5);
		RayPos *= 2;

		Ogre::Vector2 Position = RayPos*mNoisePositionMultiplier + Ogre::Vector2(mProjectorSN->getPosition().x, mProjectorSN->getPosition().z);

		Ogre::Vector3
			m_x = Ogre::Vector3(Position.x-mNoiseDerivation, mPerlin->getValue(Position.x-mNoiseDerivation,0), 0),
			p_x = Ogre::Vector3(Position.x+mNoiseDerivation, mPerlin->getValue(Position.x+mNoiseDerivation,0), 0),
			m_y = Ogre::Vector3(0, mPerlin->getValue(0,Position.y-mNoiseDerivation), Position.y-mNoiseDerivation),
			p_y = Ogre::Vector3(0, mPerlin->getValue(0,Position.y+mNoiseDerivation), Position.y+mNoiseDerivation);

        m_x.y *= mNoiseYNormalMultiplier; p_x.y *= mNoiseYNormalMultiplier;
		m_y.y *= mNoiseYNormalMultiplier; p_y.y *= mNoiseYNormalMultiplier;

		Ogre::Vector3 Normal = (p_x-m_x).crossProduct((p_y-m_y));

		Normal *= mNoiseNormalMultiplier;

		return RayPos + Ogre::Vector2(Normal.x, Normal.z);
	}

	void GodRaysManager::setNumberOfRays(const int& NumberOfRays)
	{
		mNumberOfRays = NumberOfRays;

		if (!mCreated)
		{
			return;
		}

		mProjectorSN->detachObject(mManualGodRays);

		mHydrax->getSceneManager()->destroyManualObject(mManualGodRays);
		mManualGodRays = static_cast<Ogre::ManualObject*>(NULL);

		_createGodRays();
	}

	void GodRaysManager::setObjectIntersectionsEnabled(const bool& Enable)
	{
		mObjectsIntersections = Enable;

		const HydraxComponent& Components = mHydrax->getComponents();

		create(Components);
	}

	void GodRaysManager::_updateMaterialsParameters()
	{
        if (!mObjectsIntersections)
		{
			return;
		}

		Ogre::GpuProgramParametersSharedPtr VP_Parameters, FP_Parameters;

		// God rays material
		VP_Parameters = mMaterials[0]->getTechnique(0)->getPass(0)->getVertexProgramParameters();
		FP_Parameters = mMaterials[0]->getTechnique(0)->getPass(0)->getFragmentProgramParameters();

		Ogre::Matrix4 TexViewProj =
			PROJECTIONCLIPSPACE2DTOIMAGESPACE_PERSPECTIVE *
			mProjectorCamera->getProjectionMatrixWithRSDepth() *
			mProjectorCamera->getViewMatrix();

		VP_Parameters->setNamedConstant("uTexViewProj", TexViewProj);

		FP_Parameters->setNamedConstant("uLightPosition", mProjectorSN->getPosition());
		FP_Parameters->setNamedConstant("uLightFarClipDistance", mProjectorCamera->getFarClipDistance());

		// Depth material
		FP_Parameters = mMaterials[1]->getTechnique(0)->getPass(0)->getFragmentProgramParameters();

		FP_Parameters->setNamedConstant("uLightPosition", mProjectorSN->getPosition());
		FP_Parameters->setNamedConstant("uLightFarClipDistance", mProjectorCamera->getFarClipDistance());

		std::vector<Ogre::Technique*>::iterator TechIt;

		for(TechIt = mDepthTechniques.begin(); TechIt != mDepthTechniques.end(); TechIt++)
		{
			if (!(*TechIt))
			{
				mDepthTechniques.erase(TechIt);
				// TechIt-- ?
				continue;
			}

			(*TechIt)->getPass(0)->getFragmentProgramParameters()->setNamedConstant("uLightPosition", mProjectorSN->getPosition());
			(*TechIt)->getPass(0)->getFragmentProgramParameters()->setNamedConstant("uLightFarClipDistance", mProjectorCamera->getFarClipDistance());
		}
	}

	void GodRaysManager::_updateProjector()
	{
		const Ogre::Vector3& SunPosition = mHydrax->getSunPosition();
		const Ogre::Vector3& CameraPosition = mHydrax->getCamera()->getDerivedPosition();

		Ogre::Plane WaterPlane = Ogre::Plane(Ogre::Vector3(0,1,0), mHydrax->getPosition());
		Ogre::Ray SunToCameraRay = Ogre::Ray(SunPosition, CameraPosition-SunPosition);

		Ogre::Vector3 WaterProjectionPoint = SunToCameraRay.getPoint(SunToCameraRay.intersects(WaterPlane).second);

		mProjectorSN->setPosition(WaterProjectionPoint);
		mProjectorCamera->setFarClipDistance((WaterProjectionPoint-CameraPosition).length());
		mProjectorSN->setDirection(-(WaterProjectionPoint-CameraPosition).normalisedCopy(), Ogre::Node::TS_WORLD);
	}

	bool GodRaysManager::_createMaterials(const HydraxComponent& HC)
	{
		Ogre::String VertexProgramData, FragmentProgramData;
		Ogre::GpuProgramParametersSharedPtr VP_Parameters, FP_Parameters;
		bool GpuProgramsCreated = false;
        Ogre::String EntryPoints[2];
        if(mHydrax->getShaderMode() == MaterialManager::SM_GLSL)
        {
            EntryPoints[0] = Ogre::String("main");
            EntryPoints[1] = Ogre::String("main");
        }
        else
        {
            EntryPoints[0] = Ogre::String("main_vp");
            EntryPoints[1] = Ogre::String("main_fp");
        }
		Ogre::String GpuProgramsData[2]; Ogre::String GpuProgramNames[2];
		MaterialManager *mMaterialManager = mHydrax->getMaterialManager();

		int NumberOfDepthChannels = 0;

		if (_isComponent(HC, HYDRAX_COMPONENT_CAUSTICS))
		{
		    NumberOfDepthChannels++;
		}

		// God Rays material

		VertexProgramData = "";
		FragmentProgramData = "";

		// Vertex program

		switch (mHydrax->getShaderMode())
		{
		    case MaterialManager::SM_HLSL:
			{
				VertexProgramData = ModernHlsl::godRaysVertexSource(
					mObjectsIntersections);
			}
			break;

			case MaterialManager::SM_GLSL:
			{
				VertexProgramData = ModernGlsl::godRaysVertexSource(
					mObjectsIntersections);
			}
			break;
		}

		// Fragment program

		switch (mHydrax->getShaderMode())
		{
		    case MaterialManager::SM_HLSL:
			{
				FragmentProgramData = ModernHlsl::godRaysFragmentSource(
					mObjectsIntersections,
					NumberOfDepthChannels != 0);
			}
			break;

			case MaterialManager::SM_GLSL:
			{
				FragmentProgramData = ModernGlsl::godRaysFragmentSource(
					mObjectsIntersections,
					NumberOfDepthChannels != 0);
			}
			break;
		}

		// Build our material
		mMaterials[0] = Ogre::MaterialManager::getSingleton().
			create(_def_GodRays_Material_Name,
			       Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

		Ogre::Pass *GR_Technique0_Pass0 = mMaterials[0]->getTechnique(0)->getPass(0);

		GR_Technique0_Pass0->setLightingEnabled(false);
		GR_Technique0_Pass0->setCullingMode(Ogre::CULL_NONE);
		GR_Technique0_Pass0->setDepthWriteEnabled(false);
		GR_Technique0_Pass0->setDepthCheckEnabled(mObjectsIntersections);
		GR_Technique0_Pass0->setSceneBlending(Ogre::SBT_ADD);

		GpuProgramsData[0] = VertexProgramData; GpuProgramsData[1] =  FragmentProgramData;
		GpuProgramNames[0] = _def_GodRays_Shader_VP_Name; GpuProgramNames[1] = _def_GodRays_Shader_FP_Name;

		if (mHydrax->getShaderMode() == MaterialManager::SM_HLSL)
		{
			const Ogre::String HlslTargets[2] = {"vs_4_0", "ps_4_0"};
			GpuProgramsCreated = mMaterialManager->fillGpuProgramsToPass(
				GR_Technique0_Pass0,
				GpuProgramNames,
				mHydrax->getShaderMode(),
				EntryPoints,
				GpuProgramsData,
				HlslTargets);
		}
		else
		{
			GpuProgramsCreated = mMaterialManager->fillGpuProgramsToPass(
				GR_Technique0_Pass0,
				GpuProgramNames,
				mHydrax->getShaderMode(),
				EntryPoints,
				GpuProgramsData);
		}
		if (!GpuProgramsCreated)
		{
			HydraxLOG("GodRaysManager::_createMaterials(): God Rays shader creation failed.");
			return false;
		}

		VP_Parameters = GR_Technique0_Pass0->getVertexProgramParameters();
		FP_Parameters = GR_Technique0_Pass0->getFragmentProgramParameters();

		VP_Parameters->setNamedAutoConstant("uWorldViewProj", Ogre::GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);

		if (!mObjectsIntersections)
		{
			return true;
		}

		Ogre::Matrix4 TexViewProj =
			PROJECTIONCLIPSPACE2DTOIMAGESPACE_PERSPECTIVE *
			mProjectorCamera->getProjectionMatrixWithRSDepth() *
			mProjectorCamera->getViewMatrix();

		VP_Parameters->setNamedAutoConstant("uWorld", Ogre::GpuProgramParameters::ACT_WORLD_MATRIX);
		VP_Parameters->setNamedConstant("uTexViewProj", TexViewProj);

		FP_Parameters->setNamedConstant("uLightPosition", mProjectorSN->getPosition());
		FP_Parameters->setNamedConstant("uLightFarClipDistance", mProjectorCamera->getFarClipDistance());

		int GLSLTextUnit = 0;
        if(mHydrax->getShaderMode() == MaterialManager::SM_GLSL)
        {
            FP_Parameters->setNamedConstant("uDepthMap", GLSLTextUnit);
            GLSLTextUnit++;
        }
		GR_Technique0_Pass0->createTextureUnitState(_def_GodRays_Depth_Map)->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);
		GR_Technique0_Pass0->getTextureUnitState(0)->setTextureName(_def_GodRays_Depth_Map);

		// Depth material

		VertexProgramData = "";
		FragmentProgramData = "";

		// Vertex program

		switch (mHydrax->getShaderMode())
		{
		    case MaterialManager::SM_HLSL:
			{
				VertexProgramData = ModernHlsl::godRaysDepthVertexSource();
			}
			break;

			case MaterialManager::SM_GLSL:
			{
				VertexProgramData = ModernGlsl::godRaysDepthVertexSource();
            }
			break;
		}

		// Fragment program

		switch (mHydrax->getShaderMode())
		{
		    case MaterialManager::SM_HLSL:
			{
				FragmentProgramData = ModernHlsl::godRaysDepthFragmentSource();
			}
			break;

			case MaterialManager::SM_GLSL:
			{
				FragmentProgramData = ModernGlsl::godRaysDepthFragmentSource();
            }
			break;
		}

		// Build our material
		mMaterials[1] = Ogre::MaterialManager::getSingleton().
			create(_def_GodRaysDepth_Material_Name,
			       Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

		Ogre::Pass *GRD_Technique0_Pass0 = mMaterials[1]->getTechnique(0)->getPass(0);

		mMaterials[1]->getTechnique(0)->setSchemeName("HydraxGodRaysDepth");

		GRD_Technique0_Pass0->setLightingEnabled(false);
		GRD_Technique0_Pass0->setCullingMode(Ogre::CULL_NONE);

		GpuProgramsData[0] = VertexProgramData; GpuProgramsData[1] =  FragmentProgramData;
		GpuProgramNames[0] = _def_GodRaysDepth_Shader_VP_Name; GpuProgramNames[1] = _def_GodRaysDepth_Shader_FP_Name;

		if (mHydrax->getShaderMode() == MaterialManager::SM_HLSL)
		{
			const Ogre::String HlslTargets[2] = {"vs_4_0", "ps_4_0"};
			GpuProgramsCreated = mMaterialManager->fillGpuProgramsToPass(
				GRD_Technique0_Pass0,
				GpuProgramNames,
				mHydrax->getShaderMode(),
				EntryPoints,
				GpuProgramsData,
				HlslTargets);
		}
		else
		{
			GpuProgramsCreated = mMaterialManager->fillGpuProgramsToPass(
				GRD_Technique0_Pass0,
				GpuProgramNames,
				mHydrax->getShaderMode(),
				EntryPoints,
				GpuProgramsData);
		}
		if (!GpuProgramsCreated)
		{
			HydraxLOG("GodRaysManager::_createMaterials(): depth shader creation failed.");
			return false;
		}

		VP_Parameters = GRD_Technique0_Pass0->getVertexProgramParameters();
		FP_Parameters = GRD_Technique0_Pass0->getFragmentProgramParameters();

		VP_Parameters->setNamedAutoConstant("uWorldViewProj", Ogre::GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);
		VP_Parameters->setNamedAutoConstant("uWorld", Ogre::GpuProgramParameters::ACT_WORLD_MATRIX);

		FP_Parameters->setNamedConstant("uLightPosition", mProjectorSN->getPosition());
		FP_Parameters->setNamedConstant("uLightFarClipDistance", mProjectorCamera->getFarClipDistance());

		return true;
	}

	void GodRaysManager::addDepthTechnique(Ogre::Technique *Technique, const bool& AutoUpdate)
	{
		if (!Ogre::MaterialManager::getSingleton().resourceExists(_def_GodRaysDepth_Material_Name))
		{
			HydraxLOG("GodRaysManager::addDepthTechnique(...) Objects intersection must be enabled and Hydrax::create() already called, skipping...");

			return;
		}

		Technique->removeAllPasses();
		Technique->createPass();
		Technique->setSchemeName("HydraxGodRaysDepth");

		Ogre::Pass *DM_Technique_Pass0 = Technique->getPass(0);

		DM_Technique_Pass0->setVertexProgram(_def_GodRaysDepth_Shader_VP_Name);
		DM_Technique_Pass0->setFragmentProgram(_def_GodRaysDepth_Shader_FP_Name);

		Ogre::GpuProgramParametersSharedPtr VP_Parameters = DM_Technique_Pass0->getVertexProgramParameters();
		Ogre::GpuProgramParametersSharedPtr FP_Parameters = DM_Technique_Pass0->getFragmentProgramParameters();

		VP_Parameters->setNamedAutoConstant("uWorldViewProj", Ogre::GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);
		VP_Parameters->setNamedAutoConstant("uWorld", Ogre::GpuProgramParameters::ACT_WORLD_MATRIX);

		FP_Parameters->setNamedConstant("uLightPosition", mProjectorSN->getPosition());
		FP_Parameters->setNamedConstant("uLightFarClipDistance", mProjectorCamera->getFarClipDistance());

		if (AutoUpdate)
		{
		    mDepthTechniques.push_back(Technique);
		}
	}

	bool GodRaysManager::_isComponent(const HydraxComponent &List, const HydraxComponent &ToCheck) const
	{
		if (List & ToCheck)
        {
            return true;
        }

        if (ToCheck == HYDRAX_COMPONENTS_NONE && List == HYDRAX_COMPONENTS_NONE)
        {
            return true;
        }

        if (ToCheck == HYDRAX_COMPONENTS_ALL && List == HYDRAX_COMPONENTS_ALL)
        {
            return true;
        }

        return false;
	}

	void GodRaysManager::_createDepthRTT()
	{
		mProjectorRTT = Ogre::TextureManager::getSingleton()
			.createManual(_def_GodRays_Depth_Map,
			              Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
                          Ogre::TEX_TYPE_2D,
						  // 256*256 must be sufficient
                          256,
						  256,
						  0,
						  // Only one channel
						  Ogre::PF_L8,
                          Ogre::TU_RENDERTARGET);

        Ogre::RenderTarget* RT_Texture = mProjectorRTT->getBuffer()->getRenderTarget();
		RT_Texture->setAutoUpdated(false);

        Ogre::Viewport *RT_Texture_Viewport = RT_Texture->addViewport(mProjectorCamera);
        RT_Texture_Viewport->setClearEveryFrame(true);
		RT_Texture_Viewport->setMaterialScheme("HydraxGodRaysDepth");
		RT_Texture_Viewport->setBackgroundColour(Ogre::ColourValue::White);
        RT_Texture_Viewport->setOverlaysEnabled(false);
        RT_Texture_Viewport->setSkiesEnabled(false);
		RT_Texture_Viewport->setShadowsEnabled(false);
		mDepthMapListener.mGodRaysManager = this;
		RT_Texture->addListener(&mDepthMapListener);
	}

	void GodRaysManager::DepthMapListener::preRenderTargetUpdate(const Ogre::RenderTargetEvent& evt)
	{
		Ogre::SceneManager::MovableObjectIterator EntityIterator = mGodRaysManager->mHydrax->getSceneManager()->getMovableObjectIterator("Entity");
		Ogre::Entity * CurrentEntity = NULL;
		unsigned int k = 0;

		mGodRaysManager->mHydrax->getMesh()->getEntity()->setVisible(false);

		while( EntityIterator.hasMoreElements() )
		{
			CurrentEntity = static_cast<Ogre::Entity *>(EntityIterator.peekNextValue());

			for( k = 0; k < CurrentEntity->getNumSubEntities(); k++ )
			{
				mMaterials.push(CurrentEntity->getSubEntity(k)->getMaterialName());
				CurrentEntity->getSubEntity(k)->setMaterialName(_def_GodRaysDepth_Material_Name);
			}

			EntityIterator.moveNext();
		}
	}

	void GodRaysManager::DepthMapListener::postRenderTargetUpdate(const Ogre::RenderTargetEvent& evt)
	{
		Ogre::SceneManager::MovableObjectIterator EntityIterator = mGodRaysManager->mHydrax->getSceneManager()->getMovableObjectIterator("Entity");
		Ogre::Entity * CurrentEntity = NULL;
		unsigned int k = 0;

		mGodRaysManager->mHydrax->getMesh()->getEntity()->setVisible(true);

		while( EntityIterator.hasMoreElements() )
		{
			CurrentEntity = static_cast<Ogre::Entity *>(EntityIterator.peekNextValue());

			for( k = 0; k < CurrentEntity->getNumSubEntities(); k++ )
			{
				CurrentEntity->getSubEntity(k)->setMaterialName(mMaterials.front());
				mMaterials.pop();
			}

			EntityIterator.moveNext();
		}
	}
}
