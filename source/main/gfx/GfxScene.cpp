/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2020 Petr Ohlidal

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

#include "GfxScene.h"
#include "gfx/ogre14/detail/OgreNextDemoPrivatePolicy.h"
#include "gfx/render/ogrenext/OgreNextPssmShadowPolicy.h"
#include "system/detail/OgreNextDemoFrameNormalization.h"

#include "AppContext.h"
#include "Actor.h"
#include "ActorManager.h"
#include "ApproxMath.h"
#include "Console.h"
#include "DustPool.h"
#include "FlexBody.h"
#include "FlexMesh.h"
#include "FlexMeshWheel.h"
#include "FlexObj.h"
#include "HydraxWater.h"
#include "GameContext.h"
#include "GfxActor.h"
#include "GUIManager.h"
#include "GUIUtils.h"
#include "GUI_DirectionArrow.h"
#include "OverlayWrapper.h"
#include "SkyManager.h"
#include "SkyXManager.h"
#include "TerrainGeometryManager.h"
#include "Terrain.h"
#include "TerrainObjectManager.h"
#include "Utils.h"

#include "imgui_internal.h"

#include <Ogre.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>

using namespace Ogre;
using namespace RoR;

namespace
{

class OgreNextDemoTerrainPendingGuard final
{
public:
    explicit OgreNextDemoTerrainPendingGuard(
        RoR::Gfx::Detail::Ogre14ToOgreNextTerrainSource& source) noexcept:
        m_source(source)
    {
    }

    ~OgreNextDemoTerrainPendingGuard()
    {
        if (m_armed)
            m_source.Discard();
    }

    void Arm() noexcept { m_armed = true; }
    void Release() noexcept { m_armed = false; }

private:
    RoR::Gfx::Detail::Ogre14ToOgreNextTerrainSource& m_source;
    bool m_armed = false;
};

RoR::Render::Matrix4x4 ToRendererBoundaryMatrix(
    const Ogre::Matrix4& matrix)
{
    RoR::Render::Matrix4x4 converted;
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        for (std::size_t column = 0U; column < 4U; ++column)
        {
            converted.elements[column * 4U + row] =
                static_cast<float>(matrix[row][column]);
        }
    }
    return converted;
}

bool CaptureOgre14MainCamera(
    RoR::Render::GraphicsSceneCameraInput& output)
{
    if (RoR::App::GetCameraManager() == nullptr ||
        RoR::App::GetAppContext() == nullptr)
    {
        return false;
    }
    Ogre::Camera* const camera =
        RoR::App::GetCameraManager()->GetCamera();
    Ogre::Viewport* const viewport =
        RoR::App::GetAppContext()->GetViewport();
    if (camera == nullptr || viewport == nullptr ||
        camera->getViewport() != viewport ||
        camera->isCustomProjectionMatrixEnabled())
    {
        return false;
    }

    const Ogre::RealRect extents = camera->getFrustumExtents();
    RoR::Render::Ogre14CameraCaptureInput input;
    input.view_id = 1U;
    input.width = static_cast<std::uint32_t>(viewport->getActualWidth());
    input.height = static_cast<std::uint32_t>(viewport->getActualHeight());
    input.view_from_render =
        ToRendererBoundaryMatrix(camera->getViewMatrix(true));
    if (camera->getProjectionType() == Ogre::PT_PERSPECTIVE)
    {
        input.projection =
            RoR::Render::Ogre14CameraProjectionKind::PERSPECTIVE;
    }
    else if (camera->getProjectionType() == Ogre::PT_ORTHOGRAPHIC)
    {
        input.projection =
            RoR::Render::Ogre14CameraProjectionKind::ORTHOGRAPHIC;
    }
    else
    {
        return false;
    }
    input.left = static_cast<float>(extents.left);
    input.right = static_cast<float>(extents.right);
    input.top = static_cast<float>(extents.top);
    input.bottom = static_cast<float>(extents.bottom);
    input.near_plane =
        static_cast<float>(camera->getNearClipDistance());
    input.far_plane =
        static_cast<float>(camera->getFarClipDistance());
    // OGRE 14 has no scene-linear view-exposure state. Identity is therefore
    // exact here; optional display postprocessing remains outside the scene.
    input.exposure = 1.0F;
    input.visibility_mask = viewport->getVisibilityMask();
    return RoR::Render::BuildOgre14GraphicsSceneCamera(input, output).ok();
}

RoR::Render::ValidationResult CaptureOgreNextDemoMainShadowLight(
    Ogre::SceneManager& scene_manager,
    Ogre::Light* terrain_main_light,
    RoR::Render::Ogre14GraphicsSceneLightIdentityRegistry& identity_registry,
    std::vector<RoR::Render::GraphicsSceneLightInput>& output)
{
    if (terrain_main_light == nullptr)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "ogre_next_demo.lights.terrain_main",
            "loaded demo terrain has no authoritative main light");
    }

    // SceneManager documents this registry view as unsafe during concurrent
    // creation/destruction. Capture runs only on the joined main-thread
    // boundary after BufferSimulationData(), where scene mutation is quiescent.
    const Ogre::SceneManager::MovableObjectMap& managed_lights =
        scene_manager.getMovableObjects(Ogre::MOT_LIGHT);
    Ogre::Light* candidate = nullptr;
    std::size_t candidate_count = 0U;
    for (const auto& managed_entry : managed_lights)
    {
        Ogre::MovableObject* const object = managed_entry.second;
        Ogre::Light* const light = dynamic_cast<Ogre::Light*>(object);
        if (light == nullptr)
        {
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::WRONG_RESOURCE_KIND,
                "lights.native_object",
                "OGRE MOT_LIGHT inventory contains a non-Light object");
        }
        if (managed_entry.first != light->getName())
        {
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "lights.exact_name",
                "OGRE managed-light key does not equal the exact Light name");
        }
        if (light->getType() == Ogre::Light::LT_DIRECTIONAL &&
            light->getVisible() && light->getCastShadows() &&
            light->getVisibilityFlags() != 0U && light->getLightMask() != 0U)
        {
            candidate = light;
            ++candidate_count;
        }
    }
    if (candidate_count != 1U || candidate != terrain_main_light)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "ogre_next_demo.lights.main_shadow",
            "demo capture requires exactly the terrain main light as its one visible directional shadow caster");
    }

#ifndef OGRE_NODELESS_POSITIONING
    if (!candidate->isAttached())
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "ogre_next_demo.lights.parent_scene_node",
            "this OGRE build requires the terrain main light to be attached");
    }
#endif

    RoR::Render::Ogre14GraphicsSceneLightCaptureInput input;
    input.exact_name = candidate->getName();
    input.kind = RoR::Render::Ogre14GraphicsSceneLightKind::DIRECTIONAL;
    const Ogre::ColourValue diffuse = candidate->getDiffuseColour();
    const Ogre::ColourValue specular = candidate->getSpecularColour();
    input.diffuse_linear = {
        static_cast<float>(diffuse.r),
        static_cast<float>(diffuse.g),
        static_cast<float>(diffuse.b)};
    input.specular_linear = {
        static_cast<float>(specular.r),
        static_cast<float>(specular.g),
        static_cast<float>(specular.b)};
    input.power_scale = static_cast<float>(candidate->getPowerScale());
    input.visible = true;
    input.visibility_flags = candidate->getVisibilityFlags();
    input.light_mask = candidate->getLightMask();
    input.attenuation_range = candidate->getAttenuationRange();
    input.attenuation_constant = candidate->getAttenuationConstant();
    input.attenuation_linear = candidate->getAttenuationLinear();
    input.attenuation_quadratic = candidate->getAttenuationQuadric();
    input.inner_cone_radians = static_cast<float>(
        candidate->getSpotlightInnerAngle().valueRadians());
    input.outer_cone_radians = static_cast<float>(
        candidate->getSpotlightOuterAngle().valueRadians());
    input.spot_falloff = static_cast<float>(candidate->getSpotlightFalloff());
    input.casts_shadows = true;
    const Ogre::Vector3 direction = candidate->getDerivedDirection();
    input.derived_direction = {
        static_cast<float>(direction.x),
        static_cast<float>(direction.y),
        static_cast<float>(direction.z)};

    std::vector<RoR::Render::Ogre14GraphicsSceneLightCaptureInput> inputs;
    inputs.push_back(std::move(input));
    return RoR::Render::BuildOgre14GraphicsSceneLights(
        inputs, identity_registry, output);
}

RoR::Render::ValidationResult NativeStaticFailure(
    RoR::Render::ValidationCode code,
    const char* field,
    const char* detail)
{
    return RoR::Render::ValidationResult::Failure(code, field, detail);
}

RoR::Render::ValidationResult CaptureOgreNextDemoStaticAdmissionView(
    RoR::Render::Float3& camera_position, float& radius_meters)
{
    if (RoR::App::GetCameraManager() == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "ogre_next_demo.static_capture.camera",
            "demo static admission requires the live main camera");
    }
    Ogre::Camera* const camera =
        RoR::App::GetCameraManager()->GetCamera();
    if (camera == nullptr ||
        camera->getProjectionType() != Ogre::PT_PERSPECTIVE)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "ogre_next_demo.static_capture.camera",
            "demo static admission requires the normalized perspective camera");
    }

    const Ogre::RealRect extents = camera->getFrustumExtents();
    float target_aspect = 0.0F;
    RoR::Render::ValidationResult validation =
        RoR::Detail::CaptureOgreNextDemoDrawableAspect(target_aspect);
    if (!validation)
        return validation;
    float candidate_radius = 0.0F;
    validation = RoR::Gfx::Detail::BuildOgreNextDemoStaticCaptureRadius(
            static_cast<float>(extents.left),
            static_cast<float>(extents.right),
            static_cast<float>(extents.top),
            static_cast<float>(extents.bottom),
            static_cast<float>(camera->getNearClipDistance()),
            RoR::Render::kOgreNextPssmFarMeters,
            target_aspect, candidate_radius);
    if (!validation)
        return validation;

    const Ogre::Vector3 native_position = camera->getDerivedPosition();
    const RoR::Render::Float3 candidate_position{
        static_cast<float>(native_position.x),
        static_cast<float>(native_position.y),
        static_cast<float>(native_position.z)};
    if (!std::isfinite(candidate_position.x) ||
        !std::isfinite(candidate_position.y) ||
        !std::isfinite(candidate_position.z))
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::VALUE_OUT_OF_RANGE,
            "ogre_next_demo.static_capture.camera",
            "demo static admission camera position is non-finite");
    }
    camera_position = candidate_position;
    radius_meters = candidate_radius;
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::Float3 ToFloat3(const Ogre::ColourValue& color)
{
    return {
        static_cast<float>(color.r),
        static_cast<float>(color.g),
        static_cast<float>(color.b)};
}

RoR::Render::Float4 ToFloat4(const Ogre::ColourValue& color)
{
    return {
        static_cast<float>(color.r),
        static_cast<float>(color.g),
        static_cast<float>(color.b),
        static_cast<float>(color.a)};
}

std::string BuildNativeStaticMeshCacheKey(
    const RoR::Render::Ogre14GraphicsSceneMeshAssetIdentity& identity)
{
    std::string key;
    const auto append_u32 = [&key](std::uint32_t value)
    {
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
        {
            key.push_back(static_cast<char>((value >> shift) & 0xFFU));
        }
    };
    key.append(identity.exact_resource_group);
    key.push_back('\0');
    key.append(identity.exact_mesh_name);
    key.push_back('\0');
    append_u32(identity.submesh_index);
    append_u32(identity.vertex_start);
    append_u32(identity.vertex_count);
    append_u32(identity.index_start);
    append_u32(identity.index_count);
    key.push_back(identity.reverse_winding ? '\1' : '\0');
    return key;
}

struct Ogre14ResolvedMaterialFirstPass
{
    Ogre::MaterialPtr material;
    Ogre::Technique* technique = nullptr;
    Ogre::Pass* pass = nullptr;
};

struct Ogre14MaterialSectionReference
{
    Ogre::MaterialPtr material;
    std::string exact_resource_group;
    std::string exact_name;
    RoR::Render::Ogre14GraphicsSceneMaterialCull cull =
        RoR::Render::Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
    bool reverse_winding = false;
};

RoR::Render::ValidationResult ResolveOgre14MaterialFirstPass(
    const Ogre::MaterialPtr& material,
    Ogre14ResolvedMaterialFirstPass& output)
{
    if (!material || material->getName().empty() ||
        material->getNumTechniques() == 0U)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "assets.material.native_resource",
            "OGRE drawable has no authored material technique");
    }
    Ogre::Technique* const technique = material->getTechnique(0U);
    if (technique == nullptr || technique->getNumPasses() == 0U)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "assets.material.native_pass",
            "OGRE drawable material has no authored first pass");
    }
    Ogre::Pass* const pass = technique->getPass(0U);
    if (pass == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "assets.material.native_pass",
            "OGRE drawable material first pass is null");
    }

    Ogre14ResolvedMaterialFirstPass candidate;
    candidate.material = material;
    candidate.technique = technique;
    candidate.pass = pass;
    output = std::move(candidate);
    return RoR::Render::ValidationResult::Success();
}

// This is deliberately narrower than the factor fallback below. Exact
// material admission needs the authored key, owning native MaterialPtr and
// first-pass cull before any fallback-only blend/alpha/write policy is
// applied. It must not make an unsupported exact material look like an
// eligible fallback; the fallback wrapper still performs every legacy gate.
RoR::Render::ValidationResult CaptureOgre14MaterialSectionReference(
    const Ogre14ResolvedMaterialFirstPass& resolved,
    Ogre14MaterialSectionReference& output)
{
    RoR::Render::Ogre14GraphicsSceneMaterialCull cull;
    bool reverse_winding = false;
    switch (resolved.pass->getCullingMode())
    {
    case Ogre::CULL_NONE:
        cull = RoR::Render::Ogre14GraphicsSceneMaterialCull::NONE;
        break;
    case Ogre::CULL_CLOCKWISE:
        cull = RoR::Render::Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
        break;
    case Ogre::CULL_ANTICLOCKWISE:
        cull =
            RoR::Render::Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
        reverse_winding = true;
        break;
    default:
        return NativeStaticFailure(
            RoR::Render::ValidationCode::INVALID_ENUM,
            "assets.material.cull",
            "OGRE material has an unknown culling mode");
    }

    Ogre14MaterialSectionReference candidate;
    candidate.material = resolved.material;
    candidate.exact_resource_group = resolved.material->getGroup();
    candidate.exact_name = resolved.material->getName();
    candidate.cull = cull;
    candidate.reverse_winding = reverse_winding;
    output = std::move(candidate);
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::ValidationResult CaptureOgre14MaterialFallbackInput(
    const Ogre::MaterialPtr& material,
    RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput& output,
    bool& reverse_winding)
{
    Ogre14ResolvedMaterialFirstPass resolved;
    RoR::Render::ValidationResult validation =
        ResolveOgre14MaterialFirstPass(material, resolved);
    if (!validation)
        return validation;
    Ogre::Technique* const technique = resolved.technique;
    Ogre::Pass* const pass = resolved.pass;

    bool write_red = false;
    bool write_green = false;
    bool write_blue = false;
    bool write_alpha = false;
    pass->getColourWriteEnabled(
        write_red, write_green, write_blue, write_alpha);
    if (!write_red || !write_green || !write_blue || !write_alpha ||
        pass->getSceneBlendingOperation() != Ogre::SBO_ADD ||
        pass->getSceneBlendingOperationAlpha() != Ogre::SBO_ADD)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "assets.material.color_write",
            "portable fallback requires additive RGBA color writes");
    }

    const Ogre::SceneBlendFactor source = pass->getSourceBlendFactor();
    const Ogre::SceneBlendFactor destination = pass->getDestBlendFactor();
    const Ogre::SceneBlendFactor source_alpha =
        pass->getSourceBlendFactorAlpha();
    const Ogre::SceneBlendFactor destination_alpha =
        pass->getDestBlendFactorAlpha();
    const bool replace = source == Ogre::SBF_ONE &&
        destination == Ogre::SBF_ZERO &&
        source_alpha == Ogre::SBF_ONE &&
        destination_alpha == Ogre::SBF_ZERO;
    const bool straight_alpha = source == Ogre::SBF_SOURCE_ALPHA &&
        destination == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
        ((source_alpha == Ogre::SBF_SOURCE_ALPHA &&
          destination_alpha == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA) ||
         (source_alpha == Ogre::SBF_ONE &&
          destination_alpha == Ogre::SBF_ZERO));
    if (!replace && !straight_alpha)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "assets.material.blend",
            "portable fallback supports replace or straight-alpha blending");
    }

    Ogre14MaterialSectionReference reference;
    validation = CaptureOgre14MaterialSectionReference(resolved, reference);
    if (!validation)
        return validation;

    RoR::Render::Ogre14GraphicsSceneMaterialAlphaReject alpha_reject;
    switch (pass->getAlphaRejectFunction())
    {
    case Ogre::CMPF_ALWAYS_PASS:
        alpha_reject = RoR::Render::
            Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
        break;
    case Ogre::CMPF_GREATER_EQUAL:
        alpha_reject = RoR::Render::
            Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL;
        break;
    default:
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "assets.material.alpha_reject",
            "portable fallback supports always-pass or greater-equal alpha "
            "rejection");
    }

    RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput candidate;
    candidate.exact_resource_group = reference.exact_resource_group;
    candidate.exact_name = reference.exact_name;
    candidate.pass_count =
        static_cast<std::uint32_t>(technique->getNumPasses());
    candidate.texture_unit_count =
        static_cast<std::uint32_t>(pass->getNumTextureUnitStates());
    candidate.has_vertex_program = pass->hasVertexProgram();
    candidate.has_fragment_program = pass->hasFragmentProgram();
    candidate.lighting_enabled = pass->getLightingEnabled();
    candidate.diffuse_linear = ToFloat4(pass->getDiffuse());
    candidate.ambient_linear = ToFloat3(pass->getAmbient());
    candidate.specular_linear = ToFloat3(pass->getSpecular());
    candidate.emissive_linear = ToFloat3(pass->getSelfIllumination());
    candidate.shininess = static_cast<float>(pass->getShininess());
    candidate.blend = replace
        ? RoR::Render::Ogre14GraphicsSceneMaterialBlend::REPLACE
        : RoR::Render::Ogre14GraphicsSceneMaterialBlend::STRAIGHT_ALPHA;
    candidate.cull = reference.cull;
    candidate.alpha_reject = alpha_reject;
    candidate.alpha_reject_value = pass->getAlphaRejectValue();
    output = std::move(candidate);
    reverse_winding = reference.reverse_winding;
    return RoR::Render::ValidationResult::Success();
}

// Disposable demo-only policy. Authored textures/programs are not translated
// by the first playable build, but they must not erase real mesh geometry or
// live simulation deformation. Such sections receive one canonical neutral
// opaque matte while retaining the exact native cull/winding convention.
RoR::Render::ValidationResult CaptureOgreNextDemoMaterialInput(
    const Ogre::MaterialPtr& material,
    RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput& output,
    bool& reverse_winding,
    bool& used_matte)
{
    Ogre14ResolvedMaterialFirstPass resolved;
    RoR::Render::ValidationResult validation =
        ResolveOgre14MaterialFirstPass(material, resolved);
    if (!validation)
        return validation;

    const bool has_authored_program =
        resolved.pass->hasVertexProgram() ||
        resolved.pass->hasFragmentProgram() ||
        resolved.pass->hasGeometryProgram() ||
        resolved.pass->hasTessellationHullProgram() ||
        resolved.pass->hasTessellationDomainProgram() ||
        resolved.pass->hasComputeProgram();
    const bool needs_demo_matte =
        RoR::Gfx::Detail::OgreNextDemoRequiresMatte(
            resolved.pass->getNumTextureUnitStates(), has_authored_program);
    if (!needs_demo_matte)
    {
        used_matte = false;
        return CaptureOgre14MaterialFallbackInput(
            material, output, reverse_winding);
    }

    Ogre14MaterialSectionReference reference;
    validation = CaptureOgre14MaterialSectionReference(resolved, reference);
    if (!validation)
        return validation;

    RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput candidate;
    candidate.exact_resource_group = "RoR/OgreNextDemo";
    switch (reference.cull)
    {
    case RoR::Render::Ogre14GraphicsSceneMaterialCull::NONE:
        candidate.exact_name = "MatteNeutral/CullNone/v1";
        break;
    case RoR::Render::Ogre14GraphicsSceneMaterialCull::CLOCKWISE:
        candidate.exact_name = "MatteNeutral/CullClockwise/v1";
        break;
    case RoR::Render::Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE:
        candidate.exact_name = "MatteNeutral/CullAnticlockwise/v1";
        break;
    }
    candidate.pass_count = 1U;
    candidate.texture_unit_count = 0U;
    candidate.has_vertex_program = false;
    candidate.has_fragment_program = false;
    candidate.lighting_enabled = true;
    candidate.diffuse_linear = {0.64F, 0.67F, 0.70F, 1.0F};
    candidate.ambient_linear = {1.0F, 1.0F, 1.0F};
    candidate.specular_linear = {};
    candidate.emissive_linear = {};
    candidate.shininess = 0.0F;
    candidate.blend =
        RoR::Render::Ogre14GraphicsSceneMaterialBlend::REPLACE;
    candidate.cull = reference.cull;
    candidate.alpha_reject = RoR::Render::
        Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
    candidate.alpha_reject_value = 0U;

    RoR::Render::MaterialDescriptor portable_matte;
    validation = RoR::Render::BuildOgre14GraphicsSceneMaterialFallback(
        candidate, portable_matte);
    if (!validation)
        return validation;
    output = std::move(candidate);
    reverse_winding = reference.reverse_winding;
    used_matte = true;
    return RoR::Render::ValidationResult::Success();
}

bool IsOgreNextDemoInvisibleCabMaterial(const Ogre::MaterialPtr& material)
{
    Ogre14ResolvedMaterialFirstPass resolved;
    if (!ResolveOgre14MaterialFirstPass(material, resolved))
        return false;
    const Ogre::ColourValue& diffuse = resolved.pass->getDiffuse();
    return RoR::Gfx::Detail::OgreNextDemoOmitsInvisibleCab(
        material->getName(), static_cast<float>(diffuse.a),
        resolved.pass->getDepthWriteEnabled());
}

RoR::Render::ValidationResult ValidateOgre14StaticVertexDeclaration(
    const Ogre::VertexData& vertex_data)
{
    std::uint32_t positions = 0U;
    std::uint32_t normals = 0U;
    std::uint32_t tangents = 0U;
    std::uint32_t colors = 0U;
    bool uv0 = false;
    bool uv1 = false;
    for (const Ogre::VertexElement& element :
         vertex_data.vertexDeclaration->getElements())
    {
        switch (element.getSemantic())
        {
        case Ogre::VES_POSITION:
            ++positions;
            if (element.getType() != Ogre::VET_FLOAT3)
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "assets.mesh.vertex.position_format",
                    "portable static capture requires FLOAT3 positions");
            break;
        case Ogre::VES_NORMAL:
            ++normals;
            if (element.getType() != Ogre::VET_FLOAT3)
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "assets.mesh.vertex.normal_format",
                    "portable static capture requires FLOAT3 normals");
            break;
        case Ogre::VES_TANGENT:
            ++tangents;
            if (element.getType() != Ogre::VET_FLOAT4)
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "assets.mesh.vertex.tangent_format",
                    "portable static capture requires FLOAT4 tangents with "
                    "authored handedness");
            break;
        case Ogre::VES_COLOUR:
            ++colors;
            if (element.getType() != Ogre::VET_UBYTE4_NORM)
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "assets.mesh.vertex.color_format",
                    "portable static capture requires normalized UBYTE4 "
                    "vertex color");
            break;
        case Ogre::VES_TEXTURE_COORDINATES:
            if (element.getIndex() == 0U)
            {
                if (uv0)
                    return NativeStaticFailure(
                        RoR::Render::ValidationCode::SIZE_MISMATCH,
                        "assets.mesh.vertex.texture_coordinates",
                        "OGRE vertex declaration duplicates UV set zero");
                uv0 = true;
            }
            else if (element.getIndex() == 1U)
            {
                if (uv1)
                    return NativeStaticFailure(
                        RoR::Render::ValidationCode::SIZE_MISMATCH,
                        "assets.mesh.vertex.texture_coordinates",
                        "OGRE vertex declaration duplicates UV set one");
                uv1 = true;
            }
            else
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "assets.mesh.vertex.texture_coordinates",
                    "portable static capture supports exactly UV sets zero "
                    "and one");
            if (element.getType() != Ogre::VET_FLOAT2)
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "assets.mesh.vertex.uv_format",
                    "portable static capture requires FLOAT2 UVs");
            break;
        case Ogre::VES_BLEND_WEIGHTS:
        case Ogre::VES_BLEND_INDICES:
            return NativeStaticFailure(
                RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                "static_meshes.unsupported.deformable",
                "blend streams belong to the deformable geometry adapter");
        case Ogre::VES_BINORMAL:
        case Ogre::VES_COLOUR2:
            return NativeStaticFailure(
                RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                "assets.mesh.vertex.semantic",
                "portable static mesh schema cannot preserve this authored "
                "vertex semantic");
        default:
            return NativeStaticFailure(
                RoR::Render::ValidationCode::INVALID_ENUM,
                "assets.mesh.vertex.semantic",
                "OGRE vertex declaration contains an unknown semantic");
        }
    }
    if (positions != 1U || normals > 1U || tangents > 1U || colors > 1U)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "assets.mesh.vertex.declaration",
            "portable static capture requires one position and at most one "
            "normal, tangent, color, UV0, and UV1 element");
    }
    (void)uv0;
    (void)uv1;
    return RoR::Render::ValidationResult::Success();
}

template <typename Portable, std::size_t ComponentCount>
RoR::Render::ValidationResult ExtractOgre14FloatVertexStream(
    const Ogre::VertexData& vertex_data,
    Ogre::VertexElementSemantic semantic,
    unsigned short semantic_index,
    Ogre::VertexElementType required_type,
    std::vector<Portable>& output)
{
    const Ogre::VertexElement* const element =
        vertex_data.vertexDeclaration->findElementBySemantic(
            semantic, semantic_index);
    if (element == nullptr)
    {
        output.clear();
        return RoR::Render::ValidationResult::Success();
    }
    if (element->getType() != required_type)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "assets.mesh.vertex.format",
            "OGRE vertex element type changed after declaration validation");
    }
    const Ogre::HardwareVertexBufferSharedPtr buffer =
        vertex_data.vertexBufferBinding->getBuffer(element->getSource());
    if (!buffer || vertex_data.vertexStart > buffer->getNumVertices() ||
        vertex_data.vertexCount >
            buffer->getNumVertices() - vertex_data.vertexStart ||
        element->getOffset() + element->getSize() > buffer->getVertexSize())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "assets.mesh.vertex.buffer_range",
            "OGRE vertex draw range exceeds its bound hardware buffer");
    }

    Ogre::HardwareBufferLockGuard lock(
        buffer, Ogre::HardwareBuffer::HBL_READ_ONLY);
    if (lock.pData == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "assets.mesh.vertex.cpu_data",
            "OGRE vertex buffer did not expose read-only CPU data");
    }
    std::vector<Portable> candidate(vertex_data.vertexCount);
    const auto* const bytes = static_cast<const unsigned char*>(lock.pData);
    const std::size_t stride = buffer->getVertexSize();
    for (std::size_t index = 0U; index < candidate.size(); ++index)
    {
        float components[ComponentCount] = {};
        const std::size_t native_index =
            static_cast<std::size_t>(vertex_data.vertexStart) + index;
        std::memcpy(
            components,
            bytes + native_index * stride + element->getOffset(),
            sizeof(components));
        if constexpr (ComponentCount == 2U)
            candidate[index] = {components[0U], components[1U]};
        else if constexpr (ComponentCount == 3U)
            candidate[index] = {
                components[0U], components[1U], components[2U]};
        else
            candidate[index] = {
                components[0U], components[1U], components[2U],
                components[3U]};
    }
    output = std::move(candidate);
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::ValidationResult ExtractOgre14ColorVertexStream(
    const Ogre::VertexData& vertex_data,
    std::vector<RoR::Render::Float4>& output)
{
    const Ogre::VertexElement* const element =
        vertex_data.vertexDeclaration->findElementBySemantic(
            Ogre::VES_COLOUR);
    if (element == nullptr)
    {
        output.clear();
        return RoR::Render::ValidationResult::Success();
    }
    const Ogre::HardwareVertexBufferSharedPtr buffer =
        vertex_data.vertexBufferBinding->getBuffer(element->getSource());
    if (!buffer || vertex_data.vertexStart > buffer->getNumVertices() ||
        vertex_data.vertexCount >
            buffer->getNumVertices() - vertex_data.vertexStart ||
        element->getOffset() + 4U > buffer->getVertexSize())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "assets.mesh.vertex.color_range",
            "OGRE color draw range exceeds its bound hardware buffer");
    }
    Ogre::HardwareBufferLockGuard lock(
        buffer, Ogre::HardwareBuffer::HBL_READ_ONLY);
    if (lock.pData == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "assets.mesh.vertex.cpu_data",
            "OGRE color buffer did not expose read-only CPU data");
    }
    std::vector<RoR::Render::Float4> candidate(vertex_data.vertexCount);
    const auto* const bytes = static_cast<const unsigned char*>(lock.pData);
    const std::size_t stride = buffer->getVertexSize();
    constexpr float kByteToUnit = 1.0F / 255.0F;
    for (std::size_t index = 0U; index < candidate.size(); ++index)
    {
        const std::size_t native_index =
            static_cast<std::size_t>(vertex_data.vertexStart) + index;
        const unsigned char* const color =
            bytes + native_index * stride + element->getOffset();
        // VET_UBYTE4_NORM is OGRE's PF_BYTE_RGBA memory order on every
        // endian; no render-system-specific ARGB conversion is required.
        candidate[index] = {
            color[0U] * kByteToUnit,
            color[1U] * kByteToUnit,
            color[2U] * kByteToUnit,
            color[3U] * kByteToUnit};
    }
    output = std::move(candidate);
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::ValidationResult ExtractOgre14CpuMeshSection(
    const Ogre::RenderOperation& operation,
    const std::string& debug_name,
    bool reverse_winding,
    std::uint64_t topology_revision,
    std::shared_ptr<const RoR::Render::RenderAssetPayload>& payload)
{
    if (operation.operationType != Ogre::RenderOperation::OT_TRIANGLE_LIST ||
        !operation.useIndexes || operation.vertexData == nullptr ||
        operation.indexData == nullptr ||
        operation.indexData->indexBuffer == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "assets.mesh.native_topology",
            "portable static capture requires indexed OGRE triangle lists");
    }
    if (operation.vertexData->vertexDeclaration == nullptr ||
        operation.vertexData->vertexBufferBinding == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "assets.mesh.vertex.declaration",
            "OGRE static draw has no vertex declaration or buffer binding");
    }
    RoR::Render::ValidationResult validation =
        ValidateOgre14StaticVertexDeclaration(*operation.vertexData);
    if (!validation)
        return validation;

    RoR::Render::Ogre14GraphicsSceneCpuMeshSectionInput input;
    input.debug_name = debug_name;
    input.topology_revision = topology_revision;
    input.reverse_winding = reverse_winding;
    validation = ExtractOgre14FloatVertexStream<RoR::Render::Float3, 3U>(
        *operation.vertexData, Ogre::VES_POSITION, 0U, Ogre::VET_FLOAT3,
        input.positions);
    if (!validation)
        return validation;
    validation = ExtractOgre14FloatVertexStream<RoR::Render::Float3, 3U>(
        *operation.vertexData, Ogre::VES_NORMAL, 0U, Ogre::VET_FLOAT3,
        input.normals);
    if (!validation)
        return validation;
    validation = ExtractOgre14FloatVertexStream<RoR::Render::Float4, 4U>(
        *operation.vertexData, Ogre::VES_TANGENT, 0U, Ogre::VET_FLOAT4,
        input.tangents);
    if (!validation)
        return validation;
    validation = ExtractOgre14FloatVertexStream<RoR::Render::Float2, 2U>(
        *operation.vertexData, Ogre::VES_TEXTURE_COORDINATES, 0U,
        Ogre::VET_FLOAT2, input.texture_coordinates_0);
    if (!validation)
        return validation;
    validation = ExtractOgre14FloatVertexStream<RoR::Render::Float2, 2U>(
        *operation.vertexData, Ogre::VES_TEXTURE_COORDINATES, 1U,
        Ogre::VET_FLOAT2, input.texture_coordinates_1);
    if (!validation)
        return validation;
    validation = ExtractOgre14ColorVertexStream(
        *operation.vertexData, input.colors);
    if (!validation)
        return validation;

    const Ogre::HardwareIndexBufferSharedPtr index_buffer =
        operation.indexData->indexBuffer;
    const std::size_t index_start = operation.indexData->indexStart;
    const std::size_t index_count = operation.indexData->indexCount;
    if (index_start > index_buffer->getNumIndexes() ||
        index_count > index_buffer->getNumIndexes() - index_start)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "assets.mesh.index.buffer_range",
            "OGRE index draw range exceeds its bound hardware buffer");
    }
    Ogre::HardwareBufferLockGuard index_lock(
        index_buffer, Ogre::HardwareBuffer::HBL_READ_ONLY);
    if (index_lock.pData == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "assets.mesh.index.cpu_data",
            "OGRE index buffer did not expose read-only CPU data");
    }
    input.indices.resize(index_count);
    if (index_buffer->getType() == Ogre::HardwareIndexBuffer::IT_16BIT)
    {
        input.index_format = RoR::Render::MeshIndexFormat::UINT16;
        const auto* const indices =
            static_cast<const std::uint16_t*>(index_lock.pData);
        for (std::size_t index = 0U; index < index_count; ++index)
            input.indices[index] = indices[index_start + index];
    }
    else if (index_buffer->getType() ==
             Ogre::HardwareIndexBuffer::IT_32BIT)
    {
        input.index_format = RoR::Render::MeshIndexFormat::UINT32;
        const auto* const indices =
            static_cast<const std::uint32_t*>(index_lock.pData);
        for (std::size_t index = 0U; index < index_count; ++index)
            input.indices[index] = indices[index_start + index];
    }
    else
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::INVALID_ENUM,
            "assets.mesh.index.format",
            "OGRE index buffer has an unknown format");
    }
    // Sanitize the local extraction before the ordinary builder performs its
    // canonical descriptor validation. The private input and candidate
    // payload are not published until every post-normalization check passes.
    validation = RoR::Gfx::Detail::BuildOgreNextDemoMatteTangents(
        input.positions.size(), input.normals, input.tangents);
    if (!validation)
        return validation;
    if (input.texture_coordinates_0.empty())
    {
        input.texture_coordinates_0.assign(input.positions.size(), {});
    }
    input.texture_coordinates_1.clear();
    input.colors.clear();
    std::shared_ptr<const RoR::Render::RenderAssetPayload> candidate_payload;
    validation = RoR::Render::BuildOgre14GraphicsSceneStaticMeshPayload(
        input, candidate_payload);
    if (!validation)
        return validation;
    RoR::Render::MeshResourceDescriptor candidate_mesh =
        std::get<RoR::Render::MeshResourceDescriptor>(*candidate_payload);
    validation = RoR::Gfx::Detail::NormalizeOgreNextDemoMatteMesh(
        candidate_mesh);
    if (!validation)
        return validation;
    candidate_payload =
        std::make_shared<const RoR::Render::RenderAssetPayload>(
            std::move(candidate_mesh));
    payload = std::move(candidate_payload);
    return RoR::Render::ValidationResult::Success();
}

std::string BuildNativeTerrainPageCacheKey(
    const RoR::Render::Ogre14GraphicsSceneTerrainPageIdentity& identity)
{
    std::string key("ror.ogre14.native.terrain.page.cache.v1");
    const auto append_u32 = [&key](std::uint32_t value)
    {
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
            key.push_back(static_cast<char>((value >> shift) & 0xFFU));
    };
    const auto append_u64 = [&key](std::uint64_t value)
    {
        for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
            key.push_back(static_cast<char>((value >> shift) & 0xFFU));
    };
    const auto append_string = [&key, &append_u64](const std::string& value)
    {
        append_u64(static_cast<std::uint64_t>(value.size()));
        key.append(value);
    };
    key.push_back('\0');
    append_string(identity.exact_resource_group);
    append_string(identity.exact_filename_prefix);
    append_string(identity.exact_filename_extension);
    append_string(identity.exact_slot_filename);
    append_u32(static_cast<std::uint32_t>(identity.slot_x));
    append_u32(static_cast<std::uint32_t>(identity.slot_y));
    return key;
}

RoR::Render::ValidationResult ConvertOgre14TerrainAlignment(
    Ogre::Terrain::Alignment alignment,
    RoR::Render::Ogre14GraphicsSceneTerrainAlignment& output)
{
    switch (alignment)
    {
    case Ogre::Terrain::ALIGN_X_Z:
        output = RoR::Render::Ogre14GraphicsSceneTerrainAlignment::X_Z;
        return RoR::Render::ValidationResult::Success();
    case Ogre::Terrain::ALIGN_X_Y:
        output = RoR::Render::Ogre14GraphicsSceneTerrainAlignment::X_Y;
        return RoR::Render::ValidationResult::Success();
    case Ogre::Terrain::ALIGN_Y_Z:
        output = RoR::Render::Ogre14GraphicsSceneTerrainAlignment::Y_Z;
        return RoR::Render::ValidationResult::Success();
    }
    return NativeStaticFailure(
        RoR::Render::ValidationCode::INVALID_ENUM,
        "terrain.pages.alignment",
        "OGRE TerrainGroup has an unknown alignment");
}

RoR::Render::ValidationResult CaptureOgre14TerrainPages(
    RoR::TerrainGeometryManager* geometry_manager,
    const std::map<
        std::string,
        RoR::Render::Ogre14GraphicsSceneTerrainPageCacheEntry,
        std::less<>>& terrain_cache,
    std::map<std::string,
             RoR::Render::Ogre14GraphicsSceneTerrainPageCacheEntry,
             std::less<>>& captured_cache,
    std::vector<RoR::Gfx::Detail::OgreNextDemoTerrainPageMesh>&
        captured_pages)
{
    std::map<std::string,
             RoR::Render::Ogre14GraphicsSceneTerrainPageCacheEntry,
             std::less<>> candidate_cache;
    std::vector<RoR::Gfx::Detail::OgreNextDemoTerrainPageMesh>
        candidate_pages;
    if (geometry_manager == nullptr ||
        geometry_manager->getTerrainGroup() == nullptr)
    {
        captured_cache = std::move(candidate_cache);
        captured_pages = std::move(candidate_pages);
        return RoR::Render::ValidationResult::Success();
    }

    Ogre::TerrainGroup* const group = geometry_manager->getTerrainGroup();
    if (group->getNumTerrainPrepareRequests() != 0U)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::REVISION_MISMATCH,
            "terrain.pages.native_update",
            "TerrainGroup has background preparation work");
    }

    // The product bridge deliberately skips the legacy render traversal, so
    // OGRE's WorkQueue main-thread completion pump is not otherwise reached.
    // Join every resident page here before copying CPU geometry; this is the
    // private demo boundary that prevents SkyX hourly light-map updates from
    // starving the first (and later) captures forever.
    try
    {
        for (const auto& slot_entry : group->getTerrainSlots())
        {
            Ogre::TerrainGroup::TerrainSlot* const slot = slot_entry.second;
            if (slot != nullptr && slot->instance != nullptr)
                slot->instance->waitForDerivedProcesses();
        }
    }
    catch (...)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::REVISION_MISMATCH,
            "terrain.pages.derived_join",
            "OGRE failed while joining private demo terrain work");
    }
    if (group->getNumTerrainPrepareRequests() != 0U ||
        group->isDerivedDataUpdateInProgress())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::REVISION_MISMATCH,
            "terrain.pages.native_update",
            "TerrainGroup still has work after the private demo join");
    }

    RoR::Render::Ogre14GraphicsSceneTerrainAlignment group_alignment;
    RoR::Render::ValidationResult validation = ConvertOgre14TerrainAlignment(
        group->getAlignment(), group_alignment);
    if (!validation)
        return validation;

    const Ogre::TerrainGroup::TerrainSlotMap& slots =
        group->getTerrainSlots();
    std::vector<RoR::Render::Ogre14GraphicsSceneTerrainPageCaptureInput>
        page_inputs;
    page_inputs.reserve(slots.size());
    std::vector<std::pair<std::uint32_t, const Ogre::Terrain*>> native_pages;
    native_pages.reserve(slots.size());
    for (const auto& slot_entry : slots)
    {
        const Ogre::TerrainGroup::TerrainSlot* const slot =
            slot_entry.second;
        if (slot == nullptr)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::MISSING_REFERENCE,
                "terrain.pages.native_slot",
                "TerrainGroup slot inventory contains a null slot");
        }
        if (slot->x < -32768L || slot->x > 32767L ||
            slot->y < -32768L || slot->y > 32767L ||
            group->packIndex(slot->x, slot->y) != slot_entry.first)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "terrain.pages.native_slot_key",
                "TerrainGroup packed key and signed slot coordinates disagree");
        }
        Ogre::Terrain* const terrain = slot->instance;
        if (terrain == nullptr || !terrain->isLoaded() ||
            terrain->getHeightData() == nullptr)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::MISSING_REFERENCE,
                "terrain.pages.native_page",
                "every defined TerrainGroup slot must be fully loaded with CPU heights");
        }
        if (terrain->isDerivedDataUpdateInProgress())
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "terrain.pages.derived_data",
                "terrain derived data is changing during capture");
        }

        const std::uint32_t size = terrain->getSize();
        constexpr std::uint32_t kMaximumPortableTerrainPageSize = 2049U;
        if (size < 3U || size > kMaximumPortableTerrainPageSize)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::INVALID_DIMENSIONS,
                "terrain.pages.size",
                "native terrain page is outside the bounded portable size");
        }
        if (terrain->getAlignment() != group->getAlignment() ||
            size != group->getTerrainSize() ||
            terrain->getWorldSize() != group->getTerrainWorldSize())
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "terrain.pages.group_layout",
                "loaded terrain page disagrees with its TerrainGroup layout");
        }

        const Ogre::Vector3& page_position = terrain->getPosition();
        Ogre::Vector3 expected_page_position;
        group->convertTerrainSlotToWorldPosition(
            slot->x, slot->y, &expected_page_position);
        const float transform_tolerance = (std::max)(
            1.0e-5F,
            static_cast<float>(terrain->getWorldSize()) * 2.0e-6F);
        const auto vectors_match = [transform_tolerance](
            const Ogre::Vector3& first, const Ogre::Vector3& second)
        {
            return std::fabs(static_cast<float>(first.x - second.x)) <=
                       transform_tolerance &&
                std::fabs(static_cast<float>(first.y - second.y)) <=
                       transform_tolerance &&
                std::fabs(static_cast<float>(first.z - second.z)) <=
                       transform_tolerance;
        };
        Ogre::SceneNode* const terrain_node = terrain->_getRootSceneNode();
        if (!vectors_match(page_position, expected_page_position) ||
            terrain_node == nullptr ||
            !vectors_match(terrain_node->_getDerivedPosition(),
                           page_position))
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "terrain.pages.render_transform",
                "terrain page and TerrainGroup render transforms disagree");
        }
        const Ogre::Vector3& derived_scale =
            terrain_node->_getDerivedScale();
        const Ogre::Quaternion& derived_orientation =
            terrain_node->_getDerivedOrientation();
        constexpr float kLinearTransformTolerance = 1.0e-5F;
        if (std::fabs(static_cast<float>(derived_scale.x) - 1.0F) >
                kLinearTransformTolerance ||
            std::fabs(static_cast<float>(derived_scale.y) - 1.0F) >
                kLinearTransformTolerance ||
            std::fabs(static_cast<float>(derived_scale.z) - 1.0F) >
                kLinearTransformTolerance ||
            std::fabs(static_cast<float>(derived_orientation.x)) >
                kLinearTransformTolerance ||
            std::fabs(static_cast<float>(derived_orientation.y)) >
                kLinearTransformTolerance ||
            std::fabs(static_cast<float>(derived_orientation.z)) >
                kLinearTransformTolerance ||
            std::fabs(std::fabs(static_cast<float>(derived_orientation.w)) -
                      1.0F) > kLinearTransformTolerance)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                "terrain.pages.render_transform",
                "terrain page node has unsupported rotation or scale");
        }

        RoR::Render::Ogre14GraphicsSceneTerrainPageCaptureInput input;
        input.identity.exact_resource_group = group->getResourceGroup();
        input.identity.exact_filename_prefix = group->getFilenamePrefix();
        input.identity.exact_filename_extension =
            group->getFilenameExtension();
        input.identity.exact_slot_filename = slot->def.filename.empty()
            ? group->generateFilename(slot->x, slot->y)
            : slot->def.filename;
        input.identity.slot_x = static_cast<std::int32_t>(slot->x);
        input.identity.slot_y = static_cast<std::int32_t>(slot->y);
        input.alignment = group_alignment;
        input.size = size;
        input.minimum_batch_size = terrain->getMinBatchSize();
        input.maximum_batch_size = terrain->getMaxBatchSize();
        input.lod_level_count = terrain->getNumLodLevels();
        input.lod_levels_per_leaf = terrain->getNumLodLevelsPerLeaf();
        input.highest_lod_prepared = terrain->getHighestLodPrepared();
        input.highest_lod_loaded = terrain->getHighestLodLoaded();
        input.target_lod_level = terrain->getTargetLodLevel();
        input.world_size = static_cast<float>(terrain->getWorldSize());
        input.skirt_size = static_cast<float>(terrain->getSkirtSize());
        input.page_world_position = {
            static_cast<float>(page_position.x),
            static_cast<float>(page_position.y),
            static_cast<float>(page_position.z)};
        input.derived_data_update_in_progress =
            terrain->isDerivedDataUpdateInProgress();
        // OGRE 14 Terrain exposes no hole/cut topology in this component.
        input.has_holes = false;

        // Material authority is captured independently by the private
        // OgreNextDemo composite source. The CPU geometry path retains only
        // its canonical clockwise-front topology convention.
        input.material.cull =
            RoR::Render::Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
        input.visibility_mask = terrain->getVisibilityFlags();
        input.visible = true;
        input.casts_shadows = false;
        input.receives_shadows = false;
        input.visible_in_reflections = true;

        const std::size_t sample_count =
            static_cast<std::size_t>(size) * size;
        const float* const heights = terrain->getHeightData();
        input.height_samples.assign(heights, heights + sample_count);
        const std::size_t halo_side = static_cast<std::size_t>(size) + 2U;
        input.normal_neighbourhood_positions.reserve(halo_side * halo_side);
        for (std::int32_t y = -1; y <= static_cast<std::int32_t>(size); ++y)
        {
            for (std::int32_t x = -1;
                 x <= static_cast<std::int32_t>(size); ++x)
            {
                Ogre::Vector3 point;
                terrain->getPointFromSelfOrNeighbour(x, y, &point);
                input.normal_neighbourhood_positions.push_back({
                    static_cast<float>(point.x),
                    static_cast<float>(point.y),
                    static_cast<float>(point.z)});
            }
        }
        page_inputs.push_back(std::move(input));
        native_pages.emplace_back(slot_entry.first, terrain);
    }

    std::sort(page_inputs.begin(), page_inputs.end(),
        [](const auto& first, const auto& second)
        {
            if (first.identity.slot_y != second.identity.slot_y)
                return first.identity.slot_y < second.identity.slot_y;
            return first.identity.slot_x < second.identity.slot_x;
        });
    candidate_pages.reserve(page_inputs.size());
    for (const auto& input : page_inputs)
    {
        const std::string cache_key =
            BuildNativeTerrainPageCacheKey(input.identity);
        const auto cached = terrain_cache.find(cache_key);
        RoR::Render::Ogre14GraphicsSceneTerrainPageCacheEntry cache_entry;
        validation = RoR::Render::
            ResolveOgre14GraphicsSceneTerrainPageCacheEntry(
                input,
                cached != terrain_cache.end() ? &cached->second : nullptr,
                cache_entry);
        if (!validation)
            return validation;

        RoR::Gfx::Detail::OgreNextDemoTerrainPageMesh page;
        page.slot_x = input.identity.slot_x;
        page.slot_y = input.identity.slot_y;
        page.exact_page_key = cache_key;
        page.mesh_payload = cache_entry.mesh_payload;
        page.page_world_position = input.page_world_position;
        page.visibility_mask = input.visibility_mask;
        page.visible = input.visible;
        const auto inserted = candidate_cache.emplace(
            cache_key, std::move(cache_entry));
        if (!inserted.second)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::DUPLICATE_IDENTIFIER,
                "terrain.pages.cache_key",
                "distinct terrain pages produced one exact native cache key");
        }
        candidate_pages.push_back(std::move(page));
    }

    if (group->getNumTerrainPrepareRequests() != 0U ||
        group->isDerivedDataUpdateInProgress() ||
        group->getTerrainSlots().size() != native_pages.size())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::REVISION_MISMATCH,
            "terrain.pages.native_update",
            "TerrainGroup changed while its CPU snapshot was copied");
    }
    for (const auto& native_page : native_pages)
    {
        const auto current = group->getTerrainSlots().find(native_page.first);
        if (current == group->getTerrainSlots().end() ||
            current->second == nullptr ||
            current->second->instance != native_page.second ||
            !native_page.second->isLoaded() ||
            native_page.second->isDerivedDataUpdateInProgress())
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "terrain.pages.native_update",
                "terrain page changed while its CPU snapshot was copied");
        }
    }

    captured_cache = std::move(candidate_cache);
    captured_pages = std::move(candidate_pages);
    return RoR::Render::ValidationResult::Success();
}


std::string BuildNativeDynamicMeshCacheKey(
    const RoR::Render::Ogre14GraphicsSceneDynamicSectionIdentity& identity)
{
    return std::to_string(identity.actor_instance_id) + "/" +
        std::to_string(static_cast<unsigned int>(identity.component_kind)) +
        "/" + std::to_string(identity.component_id) + "/" +
        std::to_string(identity.section_index);
}

RoR::Render::ValidationResult ValidateOgre14DynamicVertexDeclaration(
    const Ogre::VertexData& vertex_data,
    bool allow_ogre_next_demo_color_drop)
{
    if (vertex_data.vertexDeclaration == nullptr ||
        vertex_data.vertexBufferBinding == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "dynamic_meshes.native_vertex_declaration",
            "deformable draw has no vertex declaration or binding");
    }
    std::uint32_t positions = 0U;
    std::uint32_t normals = 0U;
    bool uv0 = false;
    bool ignored_demo_color = false;
    for (const Ogre::VertexElement& element :
         vertex_data.vertexDeclaration->getElements())
    {
        switch (element.getSemantic())
        {
        case Ogre::VES_POSITION:
            ++positions;
            if (element.getType() != Ogre::VET_FLOAT3)
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "dynamic_meshes.native_position_format",
                    "joined deformable staging requires FLOAT3 positions");
            break;
        case Ogre::VES_NORMAL:
            ++normals;
            if (element.getType() != Ogre::VET_FLOAT3)
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "dynamic_meshes.native_normal_format",
                    "joined deformable staging requires FLOAT3 normals");
            break;
        case Ogre::VES_TEXTURE_COORDINATES:
            if (element.getIndex() != 0U || uv0 ||
                element.getType() != Ogre::VET_FLOAT2)
            {
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "dynamic_meshes.native_texture_coordinates",
                    "deformable capture supports one FLOAT2 UV0 stream");
            }
            uv0 = true;
            break;
        case Ogre::VES_COLOUR:
            if (allow_ogre_next_demo_color_drop && !ignored_demo_color)
            {
                // Alexis FlexBody blend colors exist only to modulate its
                // authored textured material. The private demo substitutes an
                // untextured matte, so carrying this frame-varying stream
                // would have no visual consumer.
                ignored_demo_color = true;
                break;
            }
            return NativeStaticFailure(
                RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                "dynamic_meshes.dynamic_vertex_colors",
                "frame-varying deformable colors require an explicit update "
                "stream");
        case Ogre::VES_TANGENT:
            return NativeStaticFailure(
                RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                "dynamic_meshes.dynamic_tangents",
                "deformable tangents require joined CPU tangent staging");
        case Ogre::VES_BLEND_WEIGHTS:
        case Ogre::VES_BLEND_INDICES:
        case Ogre::VES_BINORMAL:
        case Ogre::VES_COLOUR2:
            return NativeStaticFailure(
                RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                "dynamic_meshes.native_vertex_semantic",
                "deformable capture cannot preserve this vertex semantic");
        default:
            return NativeStaticFailure(
                RoR::Render::ValidationCode::INVALID_ENUM,
                "dynamic_meshes.native_vertex_semantic",
                "deformable draw contains an unknown vertex semantic");
        }
    }
    if (positions != 1U || normals != 1U)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "dynamic_meshes.native_vertex_declaration",
            "deformable capture requires exactly one position and normal "
            "stream");
    }
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::ValidationResult CopyOgre14DynamicIndices(
    const Ogre::RenderOperation& operation,
    const RoR::FlexMeshTopologySection& topology,
    std::vector<std::uint32_t>& output,
    RoR::Render::MeshIndexFormat& format)
{
    if (operation.operationType != Ogre::RenderOperation::OT_TRIANGLE_LIST ||
        !operation.useIndexes || operation.indexData == nullptr ||
        operation.indexData->indexBuffer.isNull() ||
        operation.indexData->indexCount == 0U ||
        operation.indexData->indexCount % 3U != 0U)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "dynamic_meshes.native_topology",
            "deformable capture requires a nonempty indexed triangle list");
    }
    const Ogre::HardwareIndexBufferSharedPtr buffer =
        operation.indexData->indexBuffer;
    const std::size_t start = operation.indexData->indexStart;
    const std::size_t count = operation.indexData->indexCount;
    if (start > buffer->getNumIndexes() ||
        count > buffer->getNumIndexes() - start)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "dynamic_meshes.native_index_range",
            "deformable index range exceeds its immutable buffer");
    }
    if (topology.revision == 0U || topology.indices.size() != count)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "dynamic_meshes.cpu_topology",
            "CPU-owned topology does not match the native draw range");
    }
    if (topology.index_format ==
            RoR::FlexMeshTopologySection::IndexFormat::UINT16 &&
        buffer->getType() == Ogre::HardwareIndexBuffer::IT_16BIT)
    {
        format = RoR::Render::MeshIndexFormat::UINT16;
    }
    else if (topology.index_format ==
                 RoR::FlexMeshTopologySection::IndexFormat::UINT32 &&
             buffer->getType() == Ogre::HardwareIndexBuffer::IT_32BIT)
    {
        format = RoR::Render::MeshIndexFormat::UINT32;
    }
    else
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::REVISION_MISMATCH,
            "dynamic_meshes.cpu_index_format",
            "CPU-owned and native deformable index formats differ");
    }
    output = topology.indices;
    return RoR::Render::ValidationResult::Success();
}

using JoinedVertexRange = std::pair<std::size_t, std::size_t>;

RoR::Render::ValidationResult BuildOgre14JoinedVertexRanges(
    const Ogre::Mesh& mesh,
    std::size_t staging_vertex_count,
    std::map<const Ogre::VertexData*, JoinedVertexRange>& ranges)
{
    std::map<const Ogre::VertexData*, JoinedVertexRange> candidate;
    std::size_t offset = 0U;
    const auto append = [&candidate, &offset](
        const Ogre::VertexData* data) -> bool
    {
        if (data == nullptr || data->vertexStart != 0U ||
            data->vertexCount == 0U ||
            data->vertexCount >
                (std::numeric_limits<std::size_t>::max)() - offset)
        {
            return false;
        }
        if (!candidate.emplace(
                data, JoinedVertexRange{offset, data->vertexCount}).second)
        {
            return false;
        }
        offset += data->vertexCount;
        return true;
    };

    if (mesh.sharedVertexData != nullptr && !append(mesh.sharedVertexData))
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "dynamic_meshes.joined_vertex_ranges",
            "shared deformable vertex range is invalid");
    }
    for (std::size_t index = 0U; index < mesh.getNumSubMeshes(); ++index)
    {
        const Ogre::SubMesh* const submesh = mesh.getSubMesh(index);
        if (submesh == nullptr)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::MISSING_REFERENCE,
                "dynamic_meshes.native_submesh",
                "deformable Mesh contains a null SubMesh");
        }
        if (!submesh->useSharedVertices && !append(submesh->vertexData))
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::SIZE_MISMATCH,
                "dynamic_meshes.joined_vertex_ranges",
                "private deformable vertex range is invalid or aliased");
        }
    }
    if (offset != staging_vertex_count)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "dynamic_meshes.joined_vertex_count",
            "copied CPU staging count does not match OGRE vertex ownership");
    }
    ranges = std::move(candidate);
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::ValidationResult CaptureOgre14DynamicEntitySections(
    Ogre::Entity* entity,
    RoR::Render::Ogre14GraphicsSceneDynamicSectionIdentity identity,
    RoR::GfxActorCaptureLifecycle actor_lifecycle,
    const std::vector<Ogre::Vector3>& joined_positions,
    const std::vector<Ogre::Vector3>& joined_normals,
    const std::vector<Ogre::Vector2>& joined_texcoords0,
    const std::vector<RoR::FlexMeshTopologySection>& topology_sections,
    bool has_dynamic_vertex_colors,
    std::map<std::string,
             RoR::Render::Ogre14GraphicsSceneDynamicMeshCacheEntry,
             std::less<>>& mesh_cache,
    std::vector<RoR::Render::Ogre14GraphicsSceneDynamicSectionCaptureInput>&
        sections)
{
    if (entity == nullptr || !entity->isAttached() ||
        entity->getName().empty())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "dynamic_meshes.native_entity",
            "deformable component has no attached named Entity");
    }
    const Ogre::MeshPtr mesh = entity->getMesh();
    if (mesh.isNull() ||
        entity->getNumSubEntities() != mesh->getNumSubMeshes() ||
        entity->getNumSubEntities() != topology_sections.size() ||
        entity->getNumSubEntities() >
            (std::numeric_limits<std::uint32_t>::max)())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "dynamic_meshes.native_submeshes",
            "deformable Entity and Mesh section inventories differ");
    }
    if (entity->hasSkeleton() || entity->hasVertexAnimation() ||
        mesh->hasSkeleton() || mesh->hasVertexAnimation())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "dynamic_meshes.native_animation",
            "soft-body capture cannot also preserve skeletal/vertex animation");
    }
    if (entity->getRenderingDistance() != 0.0F ||
        joined_positions.empty() ||
        joined_positions.size() != joined_normals.size() ||
        (!joined_texcoords0.empty() &&
         joined_texcoords0.size() != joined_positions.size()))
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::SIZE_MISMATCH,
            "dynamic_meshes.joined_staging",
            "deformable staging or rendering-distance state is incomplete");
    }
    std::map<const Ogre::VertexData*, JoinedVertexRange> vertex_ranges;
    RoR::Render::ValidationResult validation =
        BuildOgre14JoinedVertexRanges(
            *mesh, joined_positions.size(), vertex_ranges);
    if (!validation)
        return validation;

    const RoR::Render::Matrix4x4 render_from_object =
        ToRendererBoundaryMatrix(static_cast<const Ogre::Matrix4&>(
            entity->_getParentNodeFullTransform()));
    Ogre::SceneNode* const parent_scene_node =
        entity->getParentSceneNode();
    const bool parent_in_scene_graph =
        parent_scene_node != nullptr &&
        parent_scene_node->isInSceneGraph();
    for (std::size_t section_index = 0U;
         section_index < entity->getNumSubEntities(); ++section_index)
    {
        Ogre::SubEntity* const sub_entity =
            entity->getSubEntity(section_index);
        if (sub_entity == nullptr ||
            sub_entity->getSubMesh() != mesh->getSubMesh(section_index))
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "dynamic_meshes.native_submesh",
                "deformable SubEntity no longer maps to its Mesh section");
        }
        if (identity.component_kind == RoR::Render::
                Ogre14GraphicsSceneDynamicComponentKind::CAB &&
            IsOgreNextDemoInvisibleCabMaterial(sub_entity->getMaterial()))
        {
            // Alexis's globals cab section is intentionally invisible (zero
            // alpha and no depth write). Matting it would create geometry the
            // authored game never displays, so the demo omits it exactly.
            continue;
        }
        Ogre::RenderOperation operation;
        sub_entity->getSubMesh()->_getRenderOperation(operation, 0U);
        if (operation.vertexData == nullptr ||
            operation.indexData == nullptr ||
            operation.vertexData->vertexStart >
                (std::numeric_limits<std::uint32_t>::max)() ||
            operation.vertexData->vertexCount >
                (std::numeric_limits<std::uint32_t>::max)() ||
            operation.indexData->indexStart >
                (std::numeric_limits<std::uint32_t>::max)() ||
            operation.indexData->indexCount >
                (std::numeric_limits<std::uint32_t>::max)())
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::SIZE_MISMATCH,
                "dynamic_meshes.native_draw_range",
                "deformable draw range is absent or exceeds uint32");
        }
        validation = ValidateOgre14DynamicVertexDeclaration(
            *operation.vertexData,
            RoR::Gfx::Detail::OgreNextDemoDropsDynamicBlendColors(
                has_dynamic_vertex_colors));
        if (!validation)
            return validation;
        const auto range = vertex_ranges.find(operation.vertexData);
        if (range == vertex_ranges.end() ||
            range->second.second != operation.vertexData->vertexCount ||
            topology_sections[section_index].vertex_count !=
                operation.vertexData->vertexCount)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "dynamic_meshes.joined_vertex_range",
                "deformable draw is not backed by its copied staging range");
        }

        RoR::Render::Ogre14GraphicsSceneDynamicSectionCaptureInput section;
        identity.section_index = static_cast<std::uint32_t>(section_index);
        section.identity = identity;
        section.exact_entity_name = entity->getName();
        section.render_from_object = render_from_object;
        section.visibility_mask = entity->getVisibilityFlags();
        section.visible = RoR::IsGfxActorCaptureEffectivelyVisible(
            actor_lifecycle, parent_in_scene_graph,
            entity->getVisible(), sub_entity->isVisible());
        section.casts_shadows = entity->getCastShadows();
        section.visible_in_reflections = true;
        section.has_dynamic_vertex_colors = false;

        bool reverse_winding = false;
        bool used_demo_matte = false;
        validation = CaptureOgreNextDemoMaterialInput(
            sub_entity->getMaterial(), section.material, reverse_winding,
            used_demo_matte);
        if (!validation)
            return validation;
        (void)used_demo_matte;
        section.mesh_reverse_winding = reverse_winding;
        section.receives_shadows =
            sub_entity->getMaterial()->getReceiveShadows();

        const std::size_t offset = range->second.first;
        const std::size_t count = range->second.second;
        RoR::Render::Ogre14GraphicsSceneCpuMeshSectionInput base;
        base.debug_name = mesh->getGroup() + "/" + mesh->getName() + "#" +
            std::to_string(section_index);
        base.reverse_winding = reverse_winding;
        base.positions.reserve(count);
        base.normals.reserve(count);
        if (!joined_texcoords0.empty())
            base.texture_coordinates_0.reserve(count);
        for (std::size_t index = 0U; index < count; ++index)
        {
            const Ogre::Vector3& position = joined_positions[offset + index];
            const Ogre::Vector3& normal = joined_normals[offset + index];
            base.positions.push_back({
                static_cast<float>(position.x),
                static_cast<float>(position.y),
                static_cast<float>(position.z)});
            base.normals.push_back({
                static_cast<float>(normal.x),
                static_cast<float>(normal.y),
                static_cast<float>(normal.z)});
            if (!joined_texcoords0.empty())
            {
                const Ogre::Vector2& uv = joined_texcoords0[offset + index];
                base.texture_coordinates_0.push_back({
                    static_cast<float>(uv.x), static_cast<float>(uv.y)});
            }
        }
        std::string cache_key = BuildNativeDynamicMeshCacheKey(identity);
        cache_key.append("/OgreNextDemoRT4/v1");
        auto cached = mesh_cache.find(cache_key);
        const std::size_t native_state_count = mesh->getStateCount();
        const std::uint64_t native_mesh_handle =
            static_cast<std::uint64_t>(mesh->getHandle());
        const bool cache_matches =
            cached != mesh_cache.end() &&
            cached->second.native_mesh_handle == native_mesh_handle &&
            cached->second.native_state_count == native_state_count &&
            cached->second.cpu_topology_revision ==
                topology_sections[section_index].revision &&
            cached->second.vertex_start == operation.vertexData->vertexStart &&
            cached->second.vertex_count == operation.vertexData->vertexCount &&
            cached->second.index_start == operation.indexData->indexStart &&
            cached->second.index_count == operation.indexData->indexCount &&
            cached->second.reverse_winding == reverse_winding &&
            cached->second.payload != nullptr;
        if (cache_matches)
        {
            section.mesh_payload = cached->second.payload;
        }
        else
        {
            // Immutable topology is copied only for a new/reloaded native
            // allocation. Stable frames never read back a dynamic buffer.
            validation = CopyOgre14DynamicIndices(
                operation, topology_sections[section_index],
                base.indices, base.index_format);
            if (!validation)
                return validation;
            base.topology_revision = 1U;
            if (cached != mesh_cache.end() && cached->second.payload != nullptr &&
                std::holds_alternative<RoR::Render::MeshResourceDescriptor>(
                    *cached->second.payload))
            {
                const std::uint64_t prior =
                    std::get<RoR::Render::MeshResourceDescriptor>(
                        *cached->second.payload).topology_revision;
                if (prior == (std::numeric_limits<std::uint64_t>::max)())
                {
                    return NativeStaticFailure(
                        RoR::Render::ValidationCode::REVISION_MISMATCH,
                        "dynamic_meshes.topology_revision",
                        "deformable topology revision would overflow");
                }
                base.topology_revision = prior + 1U;
            }
            // RT4 uses one cross-renderer vertex layout for every object,
            // including the small factor-only subset. Normalize all private
            // non-terrain meshes, not only sections whose material was matted.
            // Sanitize the local joined state before the ordinary dynamic
            // builder validates it, then retain the full private post-check.
            validation = RoR::Gfx::Detail::BuildOgreNextDemoMatteTangents(
                base.positions.size(), base.normals, base.tangents);
            if (!validation)
                return validation;
            if (base.texture_coordinates_0.empty())
            {
                base.texture_coordinates_0.assign(base.positions.size(), {});
            }
            base.texture_coordinates_1.clear();
            base.colors.clear();
            std::shared_ptr<const RoR::Render::RenderAssetPayload>
                candidate_payload;
            validation =
                RoR::Render::BuildOgre14GraphicsSceneDynamicMeshPayload(
                    base, candidate_payload);
            if (!validation)
                return validation;
            RoR::Render::MeshResourceDescriptor candidate_mesh =
                std::get<RoR::Render::MeshResourceDescriptor>(
                    *candidate_payload);
            validation = RoR::Gfx::Detail::
                NormalizeOgreNextDemoMatteMesh(candidate_mesh);
            if (!validation)
                return validation;
            candidate_payload = std::make_shared<const
                RoR::Render::RenderAssetPayload>(std::move(candidate_mesh));
            section.mesh_payload = std::move(candidate_payload);
            RoR::Render::Ogre14GraphicsSceneDynamicMeshCacheEntry entry;
            entry.native_mesh_handle = native_mesh_handle;
            entry.native_state_count = native_state_count;
            entry.cpu_topology_revision =
                topology_sections[section_index].revision;
            entry.vertex_start = static_cast<std::uint32_t>(
                operation.vertexData->vertexStart);
            entry.vertex_count = static_cast<std::uint32_t>(
                operation.vertexData->vertexCount);
            entry.index_start = static_cast<std::uint32_t>(
                operation.indexData->indexStart);
            entry.index_count = static_cast<std::uint32_t>(
                operation.indexData->indexCount);
            entry.reverse_winding = reverse_winding;
            entry.payload = section.mesh_payload;
            mesh_cache[cache_key] = std::move(entry);
        }

        auto state = std::make_shared<
            RoR::Render::Ogre14GraphicsSceneJoinedDynamicState>();
        state->topology_revision =
            std::get<RoR::Render::MeshResourceDescriptor>(
                *section.mesh_payload).topology_revision;
        state->positions = std::move(base.positions);
        state->normals = std::move(base.normals);
        validation = RoR::Gfx::Detail::BuildOgreNextDemoMatteTangents(
            state->positions.size(), state->normals, state->tangents);
        if (!validation)
            return validation;
        state->updated_local_bounds.minimum = state->positions.front();
        state->updated_local_bounds.maximum = state->positions.front();
        for (const RoR::Render::Float3& position : state->positions)
        {
            state->updated_local_bounds.minimum.x = (std::min)(
                state->updated_local_bounds.minimum.x, position.x);
            state->updated_local_bounds.minimum.y = (std::min)(
                state->updated_local_bounds.minimum.y, position.y);
            state->updated_local_bounds.minimum.z = (std::min)(
                state->updated_local_bounds.minimum.z, position.z);
            state->updated_local_bounds.maximum.x = (std::max)(
                state->updated_local_bounds.maximum.x, position.x);
            state->updated_local_bounds.maximum.y = (std::max)(
                state->updated_local_bounds.maximum.y, position.y);
            state->updated_local_bounds.maximum.z = (std::max)(
                state->updated_local_bounds.maximum.z, position.z);
        }
        section.state = std::move(state);
        sections.push_back(std::move(section));
    }
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::ValidationResult CaptureOgre14StaticMeshObjects(
    RoR::TerrainObjectManager* object_manager,
    const RoR::Render::Float3& camera_position,
    float capture_radius_meters,
    std::set<std::uint64_t>& admitted_static_objects,
    RoR::Render::Ogre14GraphicsSceneStaticIdentityRegistry& identity_registry,
    std::map<std::string,
             RoR::Render::Ogre14GraphicsSceneStaticMeshCacheEntry,
             std::less<>>& mesh_cache,
    std::vector<RoR::Render::GraphicsSceneAssetInput>& assets,
    std::vector<RoR::Render::GraphicsSceneStaticMeshInput>& static_meshes)
{
    RoR::Render::Ogre14GraphicsSceneUnsupportedGeometry unsupported;
    if (object_manager != nullptr)
    {
        unsupported.procedural = object_manager->HasProceduralGeometry();
        unsupported.paged = object_manager->HasPagedStaticGeometry();
        unsupported.animated = object_manager->HasAnimatedStaticGeometry();
    }
    RoR::Render::ValidationResult validation =
        RoR::Render::ValidateOgre14GraphicsSceneStaticCoverage(unsupported);
    if (!validation)
        return validation;

    std::map<std::string,
             RoR::Render::Ogre14GraphicsSceneStaticMeshCacheEntry,
             std::less<>> candidate_cache = mesh_cache;
    std::vector<RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput>
        sections;
    std::size_t omitted_demo_speed_bumps = 0U;
    if (object_manager != nullptr)
    {
        for (const RoR::TerrainObjectManager::StaticGraphicsObject& record :
             object_manager->GetStaticGraphicsObjects())
        {
            MeshObject* const mesh_object = record.mesh_object;
            Ogre::Entity* const entity =
                mesh_object != nullptr ? mesh_object->getEntity() : nullptr;
            const Ogre::MeshPtr mesh =
                mesh_object != nullptr ? mesh_object->getLoadedMesh()
                                       : Ogre::MeshPtr{};
            if (record.stable_id == 0U || mesh_object == nullptr ||
                entity == nullptr || !mesh || !entity->isAttached() ||
                entity->getMesh().get() != mesh.get())
            {
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::MISSING_REFERENCE,
                    "static_meshes.native_object",
                    "terrain static-object inventory contains an incomplete "
                    "MeshObject");
            }
            if (entity->hasSkeleton() || entity->hasVertexAnimation() ||
                mesh->hasSkeleton() || mesh->hasVertexAnimation())
            {
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "static_meshes.unsupported.deformable",
                    "terrain MeshObject contains skeletal or vertex animation");
            }
            if (entity->getRenderingDistance() != 0.0F)
            {
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "static_meshes.native_rendering_distance",
                    "portable static instances do not yet carry OGRE "
                    "rendering-distance culling");
            }
            if (entity->getNumSubEntities() != mesh->getNumSubMeshes() ||
                entity->getNumSubEntities() >
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)()))
            {
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::SIZE_MISMATCH,
                    "static_meshes.native_submeshes",
                    "Entity and Mesh submesh inventories do not match");
            }

            if (admitted_static_objects.find(record.stable_id) ==
                admitted_static_objects.end())
            {
                const Ogre::AxisAlignedBox& native_bounds =
                    entity->getWorldBoundingBox(true);
                if (native_bounds.isNull() || native_bounds.isInfinite())
                {
                    return NativeStaticFailure(
                        RoR::Render::ValidationCode::VALUE_OUT_OF_RANGE,
                        "ogre_next_demo.static_capture.bounds",
                        "static Entity has no finite world AABB");
                }
                const Ogre::Vector3 native_minimum =
                    native_bounds.getMinimum();
                const Ogre::Vector3 native_maximum =
                    native_bounds.getMaximum();
                RoR::Render::Bounds3 world_bounds;
                world_bounds.minimum = {
                    static_cast<float>(native_minimum.x),
                    static_cast<float>(native_minimum.y),
                    static_cast<float>(native_minimum.z)};
                world_bounds.maximum = {
                    static_cast<float>(native_maximum.x),
                    static_cast<float>(native_maximum.y),
                    static_cast<float>(native_maximum.z)};
                bool within_capture_radius = false;
                validation =
                    RoR::Gfx::Detail::ClassifyOgreNextDemoStaticBounds(
                        world_bounds, camera_position,
                        capture_radius_meters, within_capture_radius);
                if (!validation)
                    return validation;
                if (!within_capture_radius)
                    continue;
                admitted_static_objects.insert(record.stable_id);
            }

            Ogre::SceneNode* const parent_scene_node =
                entity->getParentSceneNode();
            if (parent_scene_node == nullptr)
            {
                return NativeStaticFailure(
                    RoR::Render::ValidationCode::MISSING_REFERENCE,
                    "static_meshes.native_parent",
                    "attached terrain Entity has no parent SceneNode");
            }
            const Ogre::Vector3& derived_scale =
                parent_scene_node->_getDerivedScale();
            if (RoR::Gfx::Detail::OgreNextDemoOmitsNonUniformSpeedBump(
                    mesh->getName(),
                    {static_cast<float>(derived_scale.x),
                     static_cast<float>(derived_scale.y),
                     static_cast<float>(derived_scale.z)}))
            {
                // CityWorld contains six minor speed-bump instances with an
                // authored (1,.5,.5) transform. RT4 deliberately admits only
                // uniform instance scale; the disposable demo omits exactly
                // these objects instead of broadening the renderer contract.
                ++omitted_demo_speed_bumps;
                continue;
            }

            const RoR::Render::Matrix4x4 render_from_object =
                ToRendererBoundaryMatrix(
                    static_cast<const Ogre::Matrix4&>(
                        entity->_getParentNodeFullTransform()));
            for (std::size_t section_index = 0U;
                 section_index < entity->getNumSubEntities();
                 ++section_index)
            {
                Ogre::SubEntity* const sub_entity =
                    entity->getSubEntity(section_index);
                if (sub_entity == nullptr || sub_entity->getSubMesh() !=
                                                 mesh->getSubMesh(section_index))
                {
                    return NativeStaticFailure(
                        RoR::Render::ValidationCode::REVISION_MISMATCH,
                        "static_meshes.native_submeshes",
                        "Entity SubEntity no longer maps to its Mesh SubMesh");
                }
                RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput
                    section;
                section.stable_object_id = record.stable_id;
                section.section_index =
                    static_cast<std::uint32_t>(section_index);
                section.exact_entity_name = entity->getName();
                section.render_from_object = render_from_object;
                section.visibility_mask = entity->getVisibilityFlags();
                section.visible = entity->getVisible() &&
                    sub_entity->isVisible();
                section.casts_shadows = entity->getCastShadows();
                section.visible_in_reflections = true;

                bool reverse_winding = false;
                bool used_demo_matte = false;
                validation = CaptureOgreNextDemoMaterialInput(
                    sub_entity->getMaterial(), section.material,
                    reverse_winding, used_demo_matte);
                if (!validation)
                    return validation;
                (void)used_demo_matte;
                section.receives_shadows =
                    sub_entity->getMaterial()->getReceiveShadows();

                Ogre::RenderOperation operation;
                // Capture authored LOD zero, not the camera-selected Entity
                // LOD left behind by the preceding OGRE render traversal.
                sub_entity->getSubMesh()->_getRenderOperation(operation, 0U);
                if (operation.vertexData == nullptr ||
                    operation.indexData == nullptr ||
                    operation.vertexData->vertexCount >
                        (std::numeric_limits<std::uint32_t>::max)() ||
                    operation.indexData->indexCount >
                        (std::numeric_limits<std::uint32_t>::max)())
                {
                    return NativeStaticFailure(
                        RoR::Render::ValidationCode::SIZE_MISMATCH,
                        "assets.mesh.native_draw_range",
                        "OGRE static draw range is absent or exceeds uint32");
                }
                section.mesh_identity.exact_resource_group = mesh->getGroup();
                section.mesh_identity.exact_mesh_name = mesh->getName();
                section.mesh_identity.submesh_index = section.section_index;
                section.mesh_identity.vertex_start =
                    operation.vertexData->vertexStart;
                section.mesh_identity.vertex_count =
                    operation.vertexData->vertexCount;
                section.mesh_identity.index_start =
                    operation.indexData->indexStart;
                section.mesh_identity.index_count =
                    operation.indexData->indexCount;
                section.mesh_identity.reverse_winding = reverse_winding;

                std::string cache_key =
                    BuildNativeStaticMeshCacheKey(section.mesh_identity);
                cache_key.append("/OgreNextDemoRT4/v1");
                auto cached = candidate_cache.find(cache_key);
                const std::size_t native_state_count = mesh->getStateCount();
                if (cached != candidate_cache.end() &&
                    cached->second.native_mesh == mesh.get() &&
                    cached->second.native_state_count == native_state_count &&
                    cached->second.payload != nullptr)
                {
                    section.mesh_payload = cached->second.payload;
                }
                else
                {
                    std::uint64_t topology_revision = 1U;
                    if (cached != candidate_cache.end() &&
                        cached->second.payload != nullptr &&
                        std::holds_alternative<
                            RoR::Render::MeshResourceDescriptor>(
                                *cached->second.payload))
                    {
                        const std::uint64_t previous_revision =
                            std::get<RoR::Render::MeshResourceDescriptor>(
                                *cached->second.payload).topology_revision;
                        if (previous_revision ==
                            (std::numeric_limits<std::uint64_t>::max)())
                        {
                            return NativeStaticFailure(
                                RoR::Render::ValidationCode::REVISION_MISMATCH,
                                "assets.mesh.topology_revision",
                                "OGRE mesh topology revision would overflow");
                        }
                        topology_revision = previous_revision + 1U;
                    }
                    validation = ExtractOgre14CpuMeshSection(
                        operation,
                        mesh->getGroup() + "/" + mesh->getName() + "#" +
                            std::to_string(section_index),
                        reverse_winding, topology_revision,
                        section.mesh_payload);
                    if (!validation)
                        return validation;
                    RoR::Render::Ogre14GraphicsSceneStaticMeshCacheEntry entry;
                    entry.native_mesh = mesh.get();
                    entry.native_state_count = native_state_count;
                    entry.payload = section.mesh_payload;
                    candidate_cache[cache_key] = std::move(entry);
                }
                sections.push_back(std::move(section));
            }
        }
    }

    if (omitted_demo_speed_bumps != 0U)
    {
        static bool reported_speed_bump_omission = false;
        if (!reported_speed_bump_omission)
        {
            LOG(fmt::format(
                "OgreNextDemo: omitted {} non-uniform topeQr.mesh instances",
                omitted_demo_speed_bumps));
            reported_speed_bump_omission = true;
        }
    }

    validation = RoR::Render::BuildOgre14GraphicsSceneStaticInventory(
        sections, identity_registry, assets, static_meshes);
    if (!validation)
        return validation;
    mesh_cache = std::move(candidate_cache);
    return RoR::Render::ValidationResult::Success();
}

} // namespace

void GfxScene::CreateDustPools()
{
    ROR_ASSERT(m_dustpools.size() == 0);
    m_dustpools["dust"]   = new DustPool(m_scene_manager, "tracks/Dust",   20);
    m_dustpools["clump"]  = new DustPool(m_scene_manager, "tracks/Clump",  20);
    m_dustpools["sparks"] = new DustPool(m_scene_manager, "tracks/Sparks", 10);
    m_dustpools["drip"]   = new DustPool(m_scene_manager, "tracks/Drip",   50);
    m_dustpools["splash"] = new DustPool(m_scene_manager, "tracks/Splash", 20);
    m_dustpools["ripple"] = new DustPool(m_scene_manager, "tracks/Ripple", 20);
}

void GfxScene::ClearScene()
{
    ResetOgre14GraphicsSceneGeneration();

    // Delete dustpools
    for (auto itor : m_dustpools)
    {
        itor.second->Discard(m_scene_manager);
        delete itor.second;
    }
    m_dustpools.clear();

    // Wipe scene manager
    m_scene_manager->clearScene();
    m_gfx_freebeams_grouping_node = nullptr;

    // Recover from the wipe
    App::GetCameraManager()->ReCreateCameraNode();
    App::GetGuiManager()->DirectionArrow.CreateArrow();
    m_gfx_freebeams_grouping_node = m_scene_manager->getRootSceneNode()->createChildSceneNode("FreeBeam Visuals");
}

void GfxScene::ResetOgre14GraphicsSceneGeneration() noexcept
{
    DiscardOgre14GraphicsSceneCapture();
    m_ogre_next_demo_terrain_source.Reset();
    m_ogre14_joined_buffer_epoch = 0U;
    m_ogre14_joined_buffer_ready = false;
    m_ogre14_joined_buffer_atomic = false;
    m_ogre14_post_update_scene_epoch = 0U;
    m_ogre14_simulation_tick = 0U;
    m_ogre14_simulation_time_seconds = 0.0;
    m_ogre14_light_identity_registry.Reset();
    m_ogre14_static_identity_registry.Reset();
    m_ogre14_dynamic_identity_registry.Reset();
    m_live_gfx_actors.clear();
    m_gfx_actor_inventory.Clear();
    m_all_gfx_characters.clear();
    // Native mesh pointers and map-scoped source identities cannot cross the
    // SceneManager teardown/reload boundary.
    m_ogre14_static_mesh_cache.clear();
    m_ogre_next_demo_admitted_static_objects.clear();
    m_ogre14_terrain_page_cache.clear();
    m_ogre14_dynamic_mesh_cache.clear();
}

void GfxScene::Init()
{
    ROR_ASSERT(!m_scene_manager);
    m_scene_manager = App::GetAppContext()->GetOgreRoot()->createSceneManager();
    App::GetAppContext()->RegisterRTShaderSceneManager(m_scene_manager);
    m_gfx_freebeams_grouping_node = m_scene_manager->getRootSceneNode()->createChildSceneNode("FreeBeam Visuals");

    m_skidmark_conf.LoadDefaultSkidmarkDefs();
}

void GfxScene::UpdateScene(float dt)
{
    // NOTE: The `dt` parameter here is simulation time (0 when paused), not real time!
    // ================================================================================

    // Actors - start threaded tasks
    for (GfxActor* gfx_actor: m_live_gfx_actors)
    {
        gfx_actor->UpdateFlexbodies(); // Push flexbody tasks to threadpool
        gfx_actor->UpdateWheelVisuals(); // Push flexwheel tasks to threadpool
    }

    // Var
    GfxActor* player_gfx_actor = nullptr;
    if (m_simbuf.simbuf_player_actor != nullptr)
    {
        player_gfx_actor = m_simbuf.simbuf_player_actor->GetGfxActor();
    }

    // FOV
    if (m_simbuf.simbuf_camera_behavior != CameraManager::CAMERA_BEHAVIOR_STATIC)
    {
        float fov = (m_simbuf.simbuf_camera_behavior == CameraManager::CAMERA_BEHAVIOR_VEHICLE_CINECAM)
            ? App::gfx_fov_internal->getFloat() : App::gfx_fov_external->getFloat();
        RoR::App::GetCameraManager()->GetCamera()->setFOVy(Ogre::Degree(fov));
    }

    // Particles
    if (App::gfx_particles_mode->getInt() == 1)
    {
        // Generate particles as needed
        for (GfxActor* gfx_actor: m_gfx_actor_inventory.Active())
        {
            float dt_actor = (!gfx_actor->GetSimDataBuffer().simbuf_physics_paused) ? dt : 0.f;
            gfx_actor->UpdateParticles(dt_actor);
        }

        // Update particle movement
        for (auto itor : m_dustpools)
        {
            itor.second->update();
        }
    }

    // Realtime reflections on player vehicle
    // IMPORTANT: Toggles visibility of all meshes -> must be done before any other visibility control is evaluated (i.e. aero propellers)
    if (player_gfx_actor != nullptr)
    {
        // Safe to be called here, only modifies OGRE objects, doesn't read any physics state.
        m_envmap.UpdateEnvMap(player_gfx_actor->GetSimDataBuffer().simbuf_pos, player_gfx_actor);
    }

    // Terrain - animated meshes and paged geometry
    App::GetGameContext()->GetTerrain()->getObjectManager()->UpdateTerrainObjects(dt);

    // Terrain - lightmap; TODO: ported as-is from Terrain::update(), is it needed? ~ only_a_ptr, 05/2018
    App::GetGameContext()->GetTerrain()->getGeometryManager()->UpdateMainLightPosition(); // TODO: Is this necessary? I'm leaving it here just in case ~ only_a_ptr, 04/2017

    // Terrain - water
    auto water = App::GetGameContext()->GetTerrain()->getWater();
    auto gfx_water = App::GetGameContext()->GetTerrain()->getGfxWater();
    if (water)
    {
        if (player_gfx_actor != nullptr)
        {
            gfx_water->SetReflectionPlaneHeight(water->CalcWavesHeight(player_gfx_actor->GetSimDataBuffer().simbuf_pos));
        }
        else
        {
            gfx_water->SetReflectionPlaneHeight(water->GetStaticWaterHeight());
        }
        gfx_water->FrameStepWater(dt);
    }

    // Terrain - sky
#ifdef USE_CAELUM
    SkyManager* sky = App::GetGameContext()->GetTerrain()->getSkyManager();
    if (sky != nullptr)
    {
        sky->DetectSkyUpdate();
    }
#endif

    SkyXManager* skyx_man = App::GetGameContext()->GetTerrain()->getSkyXManager();
    if (skyx_man != nullptr)
    {
       skyx_man->update(dt); // Light update
    }

    // GUI - race
    if (m_simbuf.simbuf_race_in_progress != m_simbuf.simbuf_race_in_progress_prev)
    {
        if (m_simbuf.simbuf_race_in_progress) // Started
        {
            RoR::App::GetOverlayWrapper()->ShowRacingOverlay();
        }
        else // Ended
        {
            RoR::App::GetOverlayWrapper()->HideRacingOverlay();
        }
    }
    if (m_simbuf.simbuf_race_in_progress)
    {
        RoR::App::GetOverlayWrapper()->UpdateRacingGui(this);
    }

    // GUI - vehicle pressure
    if (m_simbuf.simbuf_player_actor)
    {
        App::GetOverlayWrapper()->UpdatePressureOverlay(m_simbuf.simbuf_player_actor->GetGfxActor());
    }

    // HUD - network labels (always update)
    for (GfxActor* gfx_actor: m_gfx_actor_inventory.Active())
    {
        gfx_actor->UpdateNetLabels(dt);
    }

    // Player avatars
    for (GfxCharacter* a: m_all_gfx_characters)
    {
        a->UpdateCharacterInScene();
    }

    // Actors - update misc visuals
    for (GfxActor* gfx_actor: m_gfx_actor_inventory.Active())
    {
        float dt_actor = (!gfx_actor->GetSimDataBuffer().simbuf_physics_paused) ? dt : 0.f;
        if (gfx_actor->IsActorLive())
        {
            gfx_actor->UpdateRods();
            gfx_actor->UpdateCabMesh();
            gfx_actor->UpdateWingMeshes();
            gfx_actor->UpdateAirbrakes();
            gfx_actor->UpdateCParticles();
            gfx_actor->UpdateExhausts();
            gfx_actor->UpdateAeroEngines();
            gfx_actor->UpdatePropAnimations(dt_actor);
        }
        // Beacon flares must always be updated
        gfx_actor->UpdateProps(dt_actor, (gfx_actor == player_gfx_actor));
        // Blinkers (turn signals) must always be updated
        gfx_actor->UpdateFlares(dt_actor, (gfx_actor == player_gfx_actor));
    }
    if (player_gfx_actor != nullptr)
    {
        float dt_actor = (!player_gfx_actor->GetSimDataBuffer().simbuf_physics_paused) ? dt : 0.f;
        player_gfx_actor->UpdateVideoCameras(dt_actor);

        // The old-style render-to-texture dashboard (based on OGRE overlays)
        if (m_simbuf.simbuf_player_actor->ar_driveable == TRUCK && m_simbuf.simbuf_player_actor->ar_engine != nullptr)
        {
            RoR::App::GetOverlayWrapper()->UpdateLandVehicleHUD(player_gfx_actor);
        }
        else if (m_simbuf.simbuf_player_actor->ar_driveable == AIRPLANE)
        {
            RoR::App::GetOverlayWrapper()->UpdateAerialHUD(player_gfx_actor);
        }
    }

    App::GetGuiManager()->DrawSimGuiBuffered(player_gfx_actor);

    App::GetGameContext()->GetSceneMouse().UpdateVisuals();

    this->UpdateFreeBeamGfx(dt);

    // Actors - finalize threaded tasks
    for (GfxActor* gfx_actor: m_live_gfx_actors)
    {
        gfx_actor->FinishWheelUpdates();
        gfx_actor->FinishFlexbodyTasks();
    }
    // Publish the epoch only after every asynchronous FlexBody/Flexable task
    // has joined and flexitFinal()/updateFlexbodyVertexBuffers() has completed.
    // Capture rejects any BufferSimulationData()/UpdateScene mismatch.
    if (m_ogre_next_demo_capture_enabled &&
        m_ogre14_joined_buffer_ready && m_ogre14_joined_buffer_atomic)
    {
        m_ogre14_post_update_scene_epoch = m_ogre14_joined_buffer_epoch;
    }
}

void GfxScene::SetParticlesVisible(bool visible)
{
    for (auto itor : m_dustpools)
    {
        itor.second->setVisible(visible);
    }
}

DustPool* GfxScene::GetDustPool(const char* name)
{
    auto found = m_dustpools.find(name);
    if (found != m_dustpools.end())
    {
        return found->second;
    }
    else
    {
        return nullptr;
    }
}

bool GfxScene::RegisterGfxActor(RoR::GfxActor* gfx_actor)
{
    if (gfx_actor == nullptr || gfx_actor->GetActorId() < 0)
    {
        LOG("GfxScene: rejected GfxActor registration without a stable ID");
        return false;
    }
    const std::int64_t actor_id = gfx_actor->GetActorId();
    const GfxActorCaptureMutation mutation =
        m_gfx_actor_inventory.Register(actor_id, gfx_actor);
    if (mutation != GfxActorCaptureMutation::APPLIED)
    {
        LOG(fmt::format(
            "GfxScene: rejected GfxActor registration {} ({})",
            actor_id, static_cast<unsigned int>(mutation)));
        return false;
    }
    return true;
}

bool GfxScene::HideGfxActor(RoR::GfxActor* gfx_actor)
{
    if (gfx_actor == nullptr)
        return false;
    const GfxActorCaptureMutation mutation = m_gfx_actor_inventory.Hide(
        gfx_actor->GetActorId(), gfx_actor);
    if (mutation != GfxActorCaptureMutation::APPLIED)
    {
        LOG("GfxScene: rejected hide of an unknown or hidden GfxActor");
        return false;
    }
    m_live_gfx_actors.erase(
        std::remove(m_live_gfx_actors.begin(), m_live_gfx_actors.end(),
                    gfx_actor),
        m_live_gfx_actors.end());
    return true;
}

bool GfxScene::UnhideGfxActor(RoR::GfxActor* gfx_actor)
{
    return RegisterGfxActor(gfx_actor);
}

void GfxScene::BufferSimulationData()
{
    ActorManager* actor_manager = nullptr;
    std::uint64_t simulation_tick_before = 0U;
    float simulation_time_before = 0.0F;
    if (m_ogre_next_demo_capture_enabled)
    {
        m_ogre14_joined_buffer_ready = false;
        m_ogre14_joined_buffer_atomic = false;
        m_ogre14_post_update_scene_epoch = 0U;
        actor_manager = App::GetGameContext()->GetActorManager();
        if (actor_manager == nullptr)
        {
            return;
        }
        simulation_tick_before =
            actor_manager->GetCompletedPhysicsSteps();
        simulation_time_before = actor_manager->GetTotalTime();
    }

    m_simbuf.simbuf_player_actor = App::GetGameContext()->GetPlayerActor();
    m_simbuf.simbuf_character_pos = App::GetGameContext()->GetPlayerCharacter()->getPosition();
    m_simbuf.simbuf_sim_paused = App::GetGameContext()->GetActorManager()->IsSimulationPaused();
    m_simbuf.simbuf_sim_speed = App::GetGameContext()->GetActorManager()->GetSimulationSpeed();
    m_simbuf.simbuf_camera_behavior = App::GetCameraManager()->GetCurrentBehavior();

    // Race system
    m_simbuf.simbuf_race_time = App::GetGameContext()->GetRaceSystem().GetRaceTime();
    m_simbuf.simbuf_race_best_time = App::GetGameContext()->GetRaceSystem().GetRaceBestTime();
    m_simbuf.simbuf_race_time_diff = App::GetGameContext()->GetRaceSystem().GetRaceTimeDiff();
    m_simbuf.simbuf_race_in_progress_prev = m_simbuf.simbuf_race_in_progress;
    m_simbuf.simbuf_race_in_progress = App::GetGameContext()->GetRaceSystem().IsRaceInProgress();
    m_simbuf.simbuf_dir_arrow_target = App::GetGameContext()->GetRaceSystem().GetDirArrowTarget();
    m_simbuf.simbuf_dir_arrow_text = App::GetGameContext()->GetRaceSystem().GetDirArrowText();
    m_simbuf.simbuf_dir_arrow_visible = App::GetGameContext()->GetRaceSystem().IsDirArrowVisible();

    m_live_gfx_actors.clear();
    for (GfxActor* a: m_gfx_actor_inventory.Active())
    {
        if (a->IsActorLive() || !a->IsActorInitialized())
        {
            a->UpdateSimDataBuffer();
            m_live_gfx_actors.push_back(a);
            a->InitializeActor();
        }
    }

    for (GfxCharacter* a: m_all_gfx_characters)
    {
        a->BufferSimulationData();
    }

    if (!m_ogre_next_demo_capture_enabled)
    {
        return;
    }
    const std::uint64_t simulation_tick_after =
        actor_manager->GetCompletedPhysicsSteps();
    const float simulation_time_after = actor_manager->GetTotalTime();
    if (m_ogre14_joined_buffer_epoch ==
        (std::numeric_limits<std::uint64_t>::max)())
    {
        return;
    }
    ++m_ogre14_joined_buffer_epoch;
    m_ogre14_joined_buffer_ready = true;
    m_ogre14_joined_buffer_atomic =
        simulation_tick_before == simulation_tick_after &&
        simulation_time_before == simulation_time_after &&
        std::isfinite(simulation_time_after) &&
        simulation_time_after >= 0.0F;
    if (m_ogre14_joined_buffer_atomic)
    {
        m_ogre14_simulation_tick = simulation_tick_after;
        m_ogre14_simulation_time_seconds =
            static_cast<double>(simulation_time_after);
    }
}

Render::ValidationResult GfxScene::CaptureOgre14DynamicActorInventory(
    Render::Ogre14GraphicsSceneDynamicIdentityRegistry& identity_registry,
    std::map<std::string,
             Render::Ogre14GraphicsSceneDynamicMeshCacheEntry,
             std::less<>>& mesh_cache,
    std::vector<Render::GraphicsSceneAssetInput>& assets,
    std::vector<Render::GraphicsSceneDynamicMeshInput>& dynamic_meshes)
{
    std::vector<Render::Ogre14GraphicsSceneDynamicSectionCaptureInput>
        sections;
    for (const auto& actor_record : m_gfx_actor_inventory.Records())
    {
        GfxActor* const actor = actor_record.second.owner;
        if (actor_record.second.lifecycle ==
            GfxActorCaptureLifecycle::DESTROYED)
        {
            if (actor != nullptr)
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::REVISION_MISMATCH,
                    "dynamic_meshes.actor_lifecycle",
                    "destroyed actor identity retained a live owner");
            }
            continue;
        }
        if (actor == nullptr || actor->GetActorId() < 0)
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_IDENTIFIER,
                "dynamic_meshes.actor_instance_id",
                "actor deformable inventory has no stable actor ID");
        }
        Render::Ogre14GraphicsSceneDynamicSectionIdentity identity;
        identity.actor_instance_id = actor->GetActorId();
        std::vector<Ogre::Vector3> positions;
        std::vector<Ogre::Vector3> normals;
        std::vector<Ogre::Vector2> texcoords0;

        if ((actor->m_cab_mesh == nullptr) !=
            (actor->m_cab_entity == nullptr))
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "dynamic_meshes.cab",
                "cab Entity and joined staging owner must coexist");
        }
        if (actor->m_cab_mesh != nullptr)
        {
            if (!actor->m_cab_mesh->copyJoinedCpuStaging(
                    positions, normals, texcoords0))
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::EMPTY_PAYLOAD,
                    "dynamic_meshes.cab.joined_staging",
                    "cab did not expose completed CPU staging");
            }
            identity.component_kind = Render::
                Ogre14GraphicsSceneDynamicComponentKind::CAB;
            identity.component_id = 0U;
            Render::ValidationResult validation =
                CaptureOgre14DynamicEntitySections(
                    actor->m_cab_entity, identity,
                    actor_record.second.lifecycle,
                    positions, normals, texcoords0,
                    actor->m_cab_mesh->getCpuTopologySections(), false,
                    mesh_cache, sections);
            if (!validation)
                return validation;
        }

        for (FlexBody* const flexbody : actor->m_flexbodies)
        {
            if (flexbody == nullptr)
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::MISSING_REFERENCE,
                    "dynamic_meshes.flexbody",
                    "actor contains a null FlexBody owner");
            }
            if (flexbody->getPlaceholderType() !=
                FlexBody::PlaceholderType::NOT_A_PLACEHOLDER)
            {
                continue;
            }
            const std::int64_t flexbody_id = flexbody->getID();
            if (flexbody_id < 0 ||
                static_cast<std::uint64_t>(flexbody_id) >
                    (std::numeric_limits<std::uint32_t>::max)())
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::INVALID_IDENTIFIER,
                    "dynamic_meshes.flexbody_id",
                    "FlexBody has no stable uint32 creation ID");
            }
            if (!flexbody->copyJoinedCpuStaging(
                    positions, normals, texcoords0))
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::EMPTY_PAYLOAD,
                    "dynamic_meshes.flexbody.joined_staging",
                    "FlexBody did not expose completed CPU staging");
            }
            identity.component_kind = Render::
                Ogre14GraphicsSceneDynamicComponentKind::FLEXBODY;
            identity.component_id = static_cast<std::uint32_t>(flexbody_id);
            Render::ValidationResult validation =
                CaptureOgre14DynamicEntitySections(
                    flexbody->getEntity(), identity,
                    actor_record.second.lifecycle,
                    positions, normals, texcoords0,
                    flexbody->getCpuTopologySections(),
                    flexbody->hasDynamicTextureBlend(), mesh_cache, sections);
            if (!validation)
                return validation;
        }

        for (const WheelGfx& wheel : actor->m_wheels)
        {
            if (wheel.wx_flex_mesh == nullptr)
                continue;
            const std::int64_t wheel_id = wheel.wx_wheel_id;
            if (wheel_id < 0 ||
                static_cast<std::uint64_t>(wheel_id) >
                    (std::numeric_limits<std::uint32_t>::max)())
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::INVALID_IDENTIFIER,
                    "dynamic_meshes.wheel_id",
                    "deformable wheel has no stable uint32 wheel ID");
            }
            Ogre::Entity* entity = nullptr;
            const std::vector<FlexMeshTopologySection>* topology = nullptr;
            if (FlexMesh* const flexmesh =
                    dynamic_cast<FlexMesh*>(wheel.wx_flex_mesh))
            {
                if (!flexmesh->copyJoinedCpuStaging(
                        positions, normals, texcoords0) ||
                    wheel.wx_scenenode == nullptr ||
                    wheel.wx_scenenode->numAttachedObjects() != 1U)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::EMPTY_PAYLOAD,
                        "dynamic_meshes.flexmesh_wheel.joined_staging",
                        "FlexMesh wheel staging or Entity is incomplete");
                }
                entity = dynamic_cast<Ogre::Entity*>(
                    wheel.wx_scenenode->getAttachedObject(0U));
                topology = &flexmesh->getCpuTopologySections();
                identity.component_kind = Render::
                    Ogre14GraphicsSceneDynamicComponentKind::FLEXMESH_WHEEL;
            }
            else if (FlexMeshWheel* const meshwheel =
                         dynamic_cast<FlexMeshWheel*>(wheel.wx_flex_mesh))
            {
                if (!meshwheel->copyJoinedCpuStaging(
                        positions, normals, texcoords0))
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::EMPTY_PAYLOAD,
                        "dynamic_meshes.meshwheel_tire.joined_staging",
                        "MeshWheel tire did not expose completed CPU staging");
                }
                entity = meshwheel->GetTireEntity();
                topology = &meshwheel->getCpuTopologySections();
                identity.component_kind = Render::
                    Ogre14GraphicsSceneDynamicComponentKind::MESHWHEEL_TIRE;
            }
            else
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::UNSUPPORTED_FEATURE,
                    "dynamic_meshes.flexable_kind",
                    "actor wheel uses an unknown Flexable subtype");
            }
            identity.component_id = static_cast<std::uint32_t>(wheel_id);
            Render::ValidationResult validation =
                CaptureOgre14DynamicEntitySections(
                    entity, identity, actor_record.second.lifecycle,
                    positions, normals, texcoords0,
                    *topology,
                    false, mesh_cache, sections);
            if (!validation)
                return validation;
        }
    }

    return Render::BuildOgre14GraphicsSceneDynamicInventory(
        sections, identity_registry, assets, dynamic_meshes);
}

Render::ValidationResult GfxScene::CaptureOgre14GraphicsScene(
    Render::Ogre14GraphicsSceneCapture& capture)
{
    if (m_ogre14_pending_capture != nullptr)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::SEQUENCE_MISMATCH,
            "ogre14_capture.pending",
            "the preceding OGRE 14 capture must be committed or discarded");
    }
    auto pending = std::make_unique<Ogre14PendingCaptureState>();
    OgreNextDemoTerrainPendingGuard terrain_pending_guard(
        m_ogre_next_demo_terrain_source);
    pending->light_registry = m_ogre14_light_identity_registry;
    pending->terrain_page_cache = m_ogre14_terrain_page_cache;
    pending->static_registry = m_ogre14_static_identity_registry;
    pending->static_mesh_cache = m_ogre14_static_mesh_cache;
    pending->admitted_static_objects =
        m_ogre_next_demo_admitted_static_objects;
    pending->dynamic_registry = m_ogre14_dynamic_identity_registry;
    pending->dynamic_mesh_cache = m_ogre14_dynamic_mesh_cache;

    Render::Ogre14GraphicsSceneCapture candidate;
    candidate.joined_buffer_epoch = m_ogre14_joined_buffer_epoch;
    candidate.post_update_scene_epoch = m_ogre14_post_update_scene_epoch;
    if (m_ogre14_joined_buffer_ready && m_ogre14_joined_buffer_atomic &&
        m_ogre14_post_update_scene_epoch != m_ogre14_joined_buffer_epoch)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::SEQUENCE_MISMATCH,
            "post_update_scene_epoch",
            "capture attempted before the matching UpdateScene flex joins");
    }
    if (m_ogre14_joined_buffer_ready && m_ogre14_joined_buffer_atomic)
    {
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    JOINED_BUFFER_ATOMICITY);
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    POST_UPDATE_SCENE_ATOMICITY);
        candidate.frame.simulation_tick = m_ogre14_simulation_tick;
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::SIMULATION_TICK);
        candidate.frame.simulation_time_seconds =
            m_ogre14_simulation_time_seconds;
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    SIMULATION_TIME_SECONDS);

        // OGRE 14 stores the live world directly in simulation coordinates;
        // it has no floating render-origin rebase in this process.
        candidate.frame.absolute_world_origin_meters = {};
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    ABSOLUTE_WORLD_ORIGIN_METERS);

        if (m_scene_manager != nullptr)
        {
            const Ogre::ColourValue ambient =
                m_scene_manager->getAmbientLight();
            const Render::Float3 native_ambient{
                static_cast<float>(ambient.r),
                static_cast<float>(ambient.g),
                static_cast<float>(ambient.b)};
            if (Render::BuildOgre14GraphicsSceneEnvironment(
                    native_ambient, candidate.frame.environment).ok())
            {
                candidate.available_fields |=
                    Render::Ogre14GraphicsSceneCaptureFieldBit(
                        Render::Ogre14GraphicsSceneCaptureField::
                            ENVIRONMENT);
            }

            Terrain* const terrain = App::GetGameContext() != nullptr
                ? App::GetGameContext()->GetTerrain().GetRef()
                : nullptr;
            TerrainObjectManager* const object_manager =
                terrain != nullptr ? terrain->getObjectManager() : nullptr;
            TerrainGeometryManager* const geometry_manager =
                terrain != nullptr ? terrain->getGeometryManager() : nullptr;
            Gfx::Detail::OgreNextDemoTerrainCapture terrain_capture;
            Ogre::TerrainGroup* const terrain_group =
                geometry_manager != nullptr
                    ? geometry_manager->getTerrainGroup()
                    : nullptr;
            Render::ValidationResult static_validation;
            if (m_ogre_next_demo_terrain_source.HasCommittedCapture())
            {
                static_validation =
                    m_ogre_next_demo_terrain_source.CaptureCommitted(
                        terrain_group, terrain_capture);
            }
            else
            {
                std::vector<Gfx::Detail::OgreNextDemoTerrainPageMesh>
                    terrain_pages;
                static_validation = CaptureOgre14TerrainPages(
                    geometry_manager, m_ogre14_terrain_page_cache,
                    pending->terrain_page_cache, terrain_pages);
                if (!static_validation)
                    return static_validation;
                static_validation = m_ogre_next_demo_terrain_source.Capture(
                    terrain_group, terrain_pages, terrain_capture);
            }
            if (!static_validation)
                return static_validation;
            terrain_pending_guard.Arm();
            std::vector<Render::GraphicsSceneAssetInput> static_assets;
            Render::Float3 static_camera_position;
            float static_capture_radius_meters = 0.0F;
            static_validation = CaptureOgreNextDemoStaticAdmissionView(
                static_camera_position, static_capture_radius_meters);
            if (!static_validation)
                return static_validation;
            static_validation =
                CaptureOgre14StaticMeshObjects(
                    object_manager,
                    static_camera_position,
                    static_capture_radius_meters,
                    pending->admitted_static_objects,
                    pending->static_registry,
                    pending->static_mesh_cache,
                    static_assets,
                    candidate.frame.static_meshes);
            if (!static_validation)
            {
                return static_validation;
            }

            // GfxCharacter is the legacy player/network avatar domain. It is
            // neither an authored static MeshObject nor a GfxActor deformable;
            // character.mesh remains legacy-only until its own skeletal-pose
            // adapter exists. Its normal presence therefore cannot poison the
            // complete supported static + actor-dynamic inventory in this
            // transaction.

            std::vector<Render::GraphicsSceneAssetInput> dynamic_assets;
            Render::ValidationResult dynamic_validation =
                CaptureOgre14DynamicActorInventory(
                    pending->dynamic_registry,
                    pending->dynamic_mesh_cache,
                    dynamic_assets, candidate.frame.dynamic_meshes);
            if (!dynamic_validation)
                return dynamic_validation;
            dynamic_validation = Render::MergeOgre14GraphicsSceneAssets(
                static_assets, dynamic_assets, terrain_capture.assets,
                candidate.frame.assets);
            if (!dynamic_validation)
                return dynamic_validation;

            std::set<std::uint64_t> object_ids;
            for (const Render::GraphicsSceneStaticMeshInput& instance :
                 candidate.frame.static_meshes)
            {
                if (!object_ids.insert(instance.source_object_id).second)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::DUPLICATE_IDENTIFIER,
                        "ogre_next_demo.static_meshes.source_object_id",
                        "static object source IDs collide before terrain merge");
                }
            }
            for (const Render::GraphicsSceneDynamicMeshInput& instance :
                 candidate.frame.dynamic_meshes)
            {
                if (!object_ids.insert(instance.source_object_id).second)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::DUPLICATE_IDENTIFIER,
                        "ogre_next_demo.dynamic_meshes.source_object_id",
                        "static and deformable object source IDs collide");
                }
            }
            for (const Render::GraphicsSceneStaticMeshInput& instance :
                 terrain_capture.static_meshes)
            {
                if (!object_ids.insert(instance.source_object_id).second)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::DUPLICATE_IDENTIFIER,
                        "ogre_next_demo.terrain.source_object_id",
                        "terrain object source ID collides with another scene domain");
                }
                candidate.frame.static_meshes.push_back(instance);
            }
            std::sort(candidate.frame.static_meshes.begin(),
                      candidate.frame.static_meshes.end(),
                [](const auto& first, const auto& second)
                {
                    return first.source_object_id < second.source_object_id;
                });

            // These bits stage together only after coverage, native CPU
            // extraction, material translation, identity/lifecycle auditing,
            // and complete inventory conversion have all succeeded. Durable
            // registries advance only after producer acceptance.
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::ASSETS);
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::STATIC_MESHES);
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::DYNAMIC_MESHES);

            Render::ValidationResult light_validation =
                CaptureOgreNextDemoMainShadowLight(
                    *m_scene_manager,
                    terrain != nullptr ? terrain->getMainLight() : nullptr,
                    pending->light_registry,
                    candidate.frame.lights);
            if (!light_validation)
            {
                return light_validation;
            }
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::LIGHTS);
        }

        // OGRE 14 has no authored reflection-probe registry. Its dynamic
        // GfxEnvmap is a vehicle-local compatibility reflection and cannot be
        // promoted to a world-space parallax-corrected probe. The complete
        // authored probe inventory is therefore exactly empty.
        candidate.frame.reflection_probes.clear();
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    REFLECTION_PROBES);

        if (CaptureOgre14MainCamera(candidate.frame.camera))
        {
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::CAMERA);
        }
    }

    m_ogre14_pending_capture = std::move(pending);
    terrain_pending_guard.Release();
    capture = std::move(candidate);
    return Render::ValidationResult::Success();
}

void GfxScene::CommitOgre14GraphicsSceneCapture() noexcept
{
    if (m_ogre14_pending_capture == nullptr)
        return;
    using std::swap;
    swap(m_ogre14_light_identity_registry,
         m_ogre14_pending_capture->light_registry);
    swap(m_ogre14_terrain_page_cache,
         m_ogre14_pending_capture->terrain_page_cache);
    swap(m_ogre14_static_identity_registry,
         m_ogre14_pending_capture->static_registry);
    swap(m_ogre14_static_mesh_cache,
         m_ogre14_pending_capture->static_mesh_cache);
    swap(m_ogre_next_demo_admitted_static_objects,
         m_ogre14_pending_capture->admitted_static_objects);
    swap(m_ogre14_dynamic_identity_registry,
         m_ogre14_pending_capture->dynamic_registry);
    swap(m_ogre14_dynamic_mesh_cache,
         m_ogre14_pending_capture->dynamic_mesh_cache);
    m_ogre_next_demo_terrain_source.Commit();
    m_ogre14_pending_capture.reset();
}

void GfxScene::DiscardOgre14GraphicsSceneCapture() noexcept
{
    m_ogre_next_demo_terrain_source.Discard();
    m_ogre14_pending_capture.reset();
}

void GfxScene::DestroyGfxActor(RoR::GfxActor* remove_me) noexcept
{
    if (remove_me == nullptr)
        return;
    const GfxActorCaptureMutation mutation =
        m_gfx_actor_inventory.Destroy(remove_me->GetActorId(), remove_me);
    if (mutation != GfxActorCaptureMutation::APPLIED &&
        mutation != GfxActorCaptureMutation::ALREADY_DESTROYED)
    {
        return;
    }
    m_live_gfx_actors.erase(
        std::remove(m_live_gfx_actors.begin(), m_live_gfx_actors.end(),
                    remove_me),
        m_live_gfx_actors.end());
}

void GfxScene::ForceUpdateSingleGfxActor(RoR::GfxActor* gfx_actor)
{
    // Do the work `UpdateScene()` would, but for a single actor.
    // Needed for i.e. terrain editor mode.
    // ------------------------------------------------------

    // Start threaded stuff
    gfx_actor->UpdateFlexbodies(); // Push flexbody tasks to threadpool
    gfx_actor->UpdateWheelVisuals(); // Push flexwheel tasks to threadpool

    // Do sync stuff
    gfx_actor->UpdateRods();
    gfx_actor->UpdateCabMesh();
    gfx_actor->UpdateWingMeshes();
    gfx_actor->UpdateAirbrakes();

    // Finish threaded stuff
    gfx_actor->FinishWheelUpdates();
    gfx_actor->FinishFlexbodyTasks();
}

bool GfxScene::ForceUpdateSingleGfxActorForCapture(
    RoR::GfxActor* gfx_actor,
    float dt,
    const Ogre::Vector3& camera_position,
    const Ogre::Quaternion& camera_orientation)
{
    if (gfx_actor == nullptr ||
        !std::isfinite(dt) ||
        dt < 0.0f ||
        App::GetCameraManager() == nullptr ||
        App::GetCameraManager()->GetCameraNode() == nullptr ||
        App::gfx_particles_mode == nullptr ||
        App::gfx_flares_mode == nullptr ||
        App::gfx_enable_videocams == nullptr ||
        App::gfx_particles_mode->getInt() != 0 ||
        App::gfx_flares_mode->getEnum<GfxFlaresMode>() !=
            GfxFlaresMode::NONE ||
        App::gfx_enable_videocams->getBool())
    {
        return false;
    }

    Ogre::SceneNode* const display_camera_node =
        App::GetCameraManager()->GetCameraNode();
    const Ogre::Vector3 saved_position =
        display_camera_node->getPosition();
    const Ogre::Quaternion saved_orientation =
        display_camera_node->getOrientation();
    const int saved_cinecam =
        gfx_actor->GetSimDataBuffer().simbuf_cur_cinecam;

    struct RestoreCaptureOverrides
    {
        Ogre::SceneNode* node;
        Ogre::Vector3 position;
        Ogre::Quaternion orientation;
        ActorSB* sim_buffer;
        int cinecam;

        ~RestoreCaptureOverrides()
        {
            if (sim_buffer != nullptr)
                sim_buffer->simbuf_cur_cinecam = cinecam;
            if (node != nullptr)
            {
                node->setPosition(position);
                node->setOrientation(orientation);
            }
        }
    } restore = {
        display_camera_node,
        saved_position,
        saved_orientation,
        &gfx_actor->GetSimDataBuffer(),
        saved_cinecam};

    try
    {
        display_camera_node->setPosition(camera_position);
        display_camera_node->setOrientation(camera_orientation);
        // Schema 1 always renders cinecam 0 visibility, regardless of what
        // the interactive display currently shows.
        gfx_actor->GetSimDataBuffer().simbuf_cur_cinecam = 0;

        gfx_actor->UpdateFlexbodies();
        gfx_actor->UpdateWheelVisuals();

        // Particle, flare and render-to-texture video-camera state advances on
        // the display frame. The capture profile rejects those systems rather
        // than inheriting their most recently displayed state.
        gfx_actor->UpdateRods();
        gfx_actor->UpdateCabMesh();
        gfx_actor->UpdateWingMeshes();
        gfx_actor->UpdateAirbrakes();
        gfx_actor->UpdateAeroEngines();
        gfx_actor->UpdatePropAnimations(dt);
        gfx_actor->UpdateProps(0.0f, true);

        // A driver character may be visible inside the truck. Synchronize it
        // from the joined simulation buffer instead of rendering whichever
        // pose the interactive display happened to update last.
        for (GfxCharacter* character : m_all_gfx_characters)
        {
            if (character == nullptr)
                return false;
            character->BufferSimulationData();
            character->UpdateCharacterInScene();
        }

        gfx_actor->FinishWheelUpdates();
        gfx_actor->FinishFlexbodyTasks();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void GfxScene::RegisterGfxCharacter(RoR::GfxCharacter* gfx_character)
{
    m_all_gfx_characters.push_back(gfx_character);
}

void GfxScene::RemoveGfxCharacter(RoR::GfxCharacter* remove_me)
{
    auto itor = std::remove(m_all_gfx_characters.begin(), m_all_gfx_characters.end(), remove_me);
    if (itor != m_all_gfx_characters.end())
    {
        m_all_gfx_characters.erase(itor, m_all_gfx_characters.end());
    }
}

void GfxScene::DrawNetLabel(Ogre::Vector3 scene_pos, float cam_dist, std::string const& nick, int colornum)
{
#if USE_SOCKETW

        // this ensures that the nickname is always in a readable size
        float font_size = std::max(0.6, cam_dist / 40.0);
        std::string caption;
        if (cam_dist > 1000) // 1000 ... vlen
        {
            caption =
                nick + " (" + TOSTRING((float)(ceil(cam_dist / 100) / 10.0) ) + " km)";
        }
        else if (cam_dist > 20) // 20 ... vlen ... 1000
        {
            caption =
                nick + " (" + TOSTRING((int)cam_dist) + " m)";
        }
        else // 0 ... vlen ... 20
        {
            caption = nick;
        }

        // draw with DearIMGUI

    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    World2ScreenConverter world2screen(
        App::GetCameraManager()->GetCamera()->getViewMatrix(true), App::GetCameraManager()->GetCamera()->getProjectionMatrix(), Ogre::Vector2(screen_size.x, screen_size.y));

    Ogre::Vector3 pos_xyz = world2screen.Convert(scene_pos);

    // only draw when in front of camera
    if (pos_xyz.z < 0.f)
    {
        // Align position to whole pixels, to minimize jitter.
        ImVec2 pos((int)pos_xyz.x+0.5, (int)pos_xyz.y+0.5);

        ImVec2 text_size = ImGui::CalcTextSize(caption.c_str());
        GUIManager::GuiTheme const& theme = App::GetGuiManager()->GetTheme();

        ImDrawList* drawlist = GetImDummyFullscreenWindow();
        ImGuiContext* g = ImGui::GetCurrentContext();

        ImVec2 text_pos(pos.x - ((text_size.x / 2)), pos.y - ((text_size.y / 2)));

        // Draw background rectangle
        const float PADDING = 4.f;
        drawlist->AddRectFilled(
            text_pos - ImVec2(PADDING, PADDING),
            text_pos + text_size + ImVec2(PADDING, PADDING),
            ImColor(theme.semitransparent_window_bg),
            ImGui::GetStyle().WindowRounding);

        // draw colored text
        Ogre::ColourValue color = App::GetNetwork()->GetPlayerColor(colornum);
        ImVec4 text_color(color.r, color.g, color.b, 1.f);
        drawlist->AddText(g->Font, g->FontSize, text_pos, ImColor(text_color), caption.c_str());
    }

#endif // USE_SOCKETW
}

void GfxScene::AdjustParticleSystemTimeFactor(Ogre::ParticleSystem* psys)
{
    float speed_factor = 0.f;
    if (App::sim_state->getEnum<SimState>() == SimState::RUNNING && !App::GetGameContext()->GetActorManager()->IsSimulationPaused())
    {
        speed_factor = m_simbuf.simbuf_sim_speed;
    }

    psys->setSpeedFactor(speed_factor);
}

void GfxScene::AddFreeBeamGfx(FreeBeamGfxRequest* rq)
{
    auto itor = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [rq](const FreeBeamGfx& obj) { return obj.fbx_id == rq->fbr_id; });
    if (itor != m_gfx_freebeams.end())
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_WARNING,
            fmt::format("FreeBeamGfx with ID %d already exists, ignoring request.",rq->fbr_id));
        return;
    }

    FreeBeamGfx obj;
    obj.fbx_id = rq->fbr_id;
    obj.fbx_freeforce_primary = rq->fbr_freeforce_primary;
    obj.fbx_freeforce_secondary = rq->fbr_freeforce_secondary;
    obj.fbx_diameter = rq->fbr_diameter;

    Ogre::Entity* e = m_scene_manager->createEntity(fmt::format("FreeBeamGfx_{}", rq->fbr_id), rq->fbr_mesh_name);
    e->setMaterialName(rq->fbr_material_name);

    obj.fbx_scenenode = m_gfx_freebeams_grouping_node->createChildSceneNode(fmt::format("FreeBeamGfx_{}", rq->fbr_id));
    obj.fbx_scenenode->setScale(rq->fbr_diameter, -1, rq->fbr_diameter);
    obj.fbx_scenenode->attachObject(e);

    m_gfx_freebeams.push_back(obj);
}

void GfxScene::ModifyFreeBeamGfx(FreeBeamGfxRequest* rq)
{
    auto itor = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [rq](const FreeBeamGfx& obj) { return obj.fbx_id == rq->fbr_id; });
    if (itor == m_gfx_freebeams.end())
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_WARNING,
            fmt::format("FreeBeamGfx with ID %d not found, ignoring request.", rq->fbr_id));
        return;
    }

    FreeBeamGfx& obj = *itor;
    this->RemoveFreeBeamGfx(rq->fbr_id);
    this->AddFreeBeamGfx(rq);
}

void GfxScene::RemoveFreeBeamGfx(FreeBeamGfxID_t id)
{
    auto itor = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [id](const FreeBeamGfx& obj) { return obj.fbx_id == id; });
    if (itor == m_gfx_freebeams.end())
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_WARNING,
            fmt::format("FreeBeamGfx with ID %d not found, ignoring request.", id));
        return;
    }

    FreeBeamGfx& obj = *itor;
    m_scene_manager->destroyEntity((Ogre::Entity*)obj.fbx_scenenode->getAttachedObject(0));
    m_gfx_freebeams_grouping_node->removeChild(obj.fbx_scenenode);
    m_gfx_freebeams.erase(itor);
}

void GfxScene::UpdateFreeBeamGfx(float dt)
{
    for (FreeBeamGfx& freebeam : m_gfx_freebeams)
    {
        // Sanity checks - primary freeforce
        ROR_ASSERT(freebeam.fbx_id != FREEBEAMGFXID_INVALID);
        ROR_ASSERT(freebeam.fbx_freeforce_primary != FREEFORCEID_INVALID);
        ActorManager::FreeForceVec_t::iterator itor;
        const bool exists = App::GetGameContext()->GetActorManager()->FindFreeForce(freebeam.fbx_freeforce_primary, itor);
        ROR_ASSERT(exists);
        if (!exists)
        {
            continue;
        }
        FreeForce& freeforce = *itor;
        
        // Sanity checks - base actor
        ROR_ASSERT(freeforce.ffc_base_actor);
        ROR_ASSERT(freeforce.ffc_base_actor->ar_state != ActorState::DISPOSED);
        GfxActor* gfx_actor_base = freeforce.ffc_base_actor->GetGfxActor();
        ROR_ASSERT(gfx_actor_base);
        ROR_ASSERT(freeforce.ffc_base_node != NODENUM_INVALID);
        ROR_ASSERT(freeforce.ffc_base_node < freeforce.ffc_base_actor->ar_num_nodes);

        // Sanity checks - target actor
        ROR_ASSERT(freeforce.ffc_target_actor);
        ROR_ASSERT(freeforce.ffc_target_actor->ar_state != ActorState::DISPOSED);
        GfxActor* gfx_actor_target = freeforce.ffc_target_actor->GetGfxActor();
        ROR_ASSERT(gfx_actor_target);
        ROR_ASSERT(freeforce.ffc_target_node != NODENUM_INVALID);
        ROR_ASSERT(freeforce.ffc_target_node < freeforce.ffc_target_actor->ar_num_nodes);

        // Get node positions
        Ogre::Vector3 basenode_pos = gfx_actor_base->GetSimNodeBuffer()[freeforce.ffc_base_node].AbsPosition;
        Ogre::Vector3 targetnode_pos = gfx_actor_target->GetSimNodeBuffer()[freeforce.ffc_target_node].AbsPosition;

        // Do the transforms
        freebeam.fbx_scenenode->setPosition(basenode_pos.midPoint(targetnode_pos));
        freebeam.fbx_scenenode->setOrientation(GfxScene::SpecialGetRotationTo(Ogre::Vector3::UNIT_Y, (basenode_pos - targetnode_pos)));
        freebeam.fbx_scenenode->setScale(freebeam.fbx_diameter, basenode_pos.distance(targetnode_pos), freebeam.fbx_diameter);
    }
}

void GfxScene::OnFreeForceRemoved(FreeForceID_t id)
{
    auto itor_secondary = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [id](const FreeBeamGfx& obj) { return obj.fbx_freeforce_secondary == id; });
    if (itor_secondary != m_gfx_freebeams.end())
    {
        // Just clear the freeforce ID
        itor_secondary->fbx_freeforce_secondary = FREEFORCEID_INVALID;
    }
    else
    {
        auto itor_primary = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
            [id](const FreeBeamGfx& obj) { return obj.fbx_freeforce_primary == id; });
        if (itor_primary != m_gfx_freebeams.end())
        {
            // Remove the whole freebeam
            this->RemoveFreeBeamGfx(itor_primary->fbx_id);
        }
    }
}

void GfxScene::OnFreeForceBroken(FreeForceID_t id)
{
    auto itor = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [id](const FreeBeamGfx& obj) { return obj.fbx_freeforce_primary == id || obj.fbx_freeforce_secondary == id; });
    if (itor != m_gfx_freebeams.end())
    {
        // Remove the whole freebeam if either freeforce broke
        this->RemoveFreeBeamGfx(itor->fbx_id);
    }
}

Ogre::Quaternion RoR::GfxScene::SpecialGetRotationTo(const Ogre::Vector3& src, const Ogre::Vector3& dest)
{
    // Based on Stan Melax's article in Game Programming Gems
    Ogre::Quaternion q;
    // Copy, since cannot modify local
    Ogre::Vector3 v0 = src;
    Ogre::Vector3 v1 = dest;
    v0.normalise();
    v1.normalise();

    // NB if the crossProduct approaches zero, we get unstable because ANY axis will do
    // when v0 == -v1
    Ogre::Real d = v0.dotProduct(v1);
    // If dot == 1, vectors are the same
    if (d >= 1.0f)
    {
        return Ogre::Quaternion::IDENTITY;
    }
    if (d < (1e-6f - 1.0f))
    {
        // Generate an axis
        Ogre::Vector3 axis = Ogre::Vector3::UNIT_X.crossProduct(src);
        if (axis.isZeroLength()) // pick another if colinear
            axis = Ogre::Vector3::UNIT_Y.crossProduct(src);
        axis.normalise();
        q.FromAngleAxis(Ogre::Radian(Ogre::Math::PI), axis);
    }
    else
    {
        Ogre::Real s = fast_sqrt((1 + d) * 2);
        if (s == 0)
            return Ogre::Quaternion::IDENTITY;

        Ogre::Vector3 c = v0.crossProduct(v1);
        Ogre::Real invs = 1 / s;

        q.x = c.x * invs;
        q.y = c.y * invs;
        q.z = c.z * invs;
        q.w = s * 0.5;
    }
    return q;
}
