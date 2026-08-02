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

#include "AppContext.h"
#include "Actor.h"
#include "ActorManager.h"
#include "ApproxMath.h"
#include "Console.h"
#include "DustPool.h"
#include "HydraxWater.h"
#include "GameContext.h"
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
#include <set>

using namespace Ogre;
using namespace RoR;

namespace
{

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

RoR::Render::ValidationResult CaptureOgre14ManagedLights(
    Ogre::SceneManager& scene_manager,
    RoR::Render::Ogre14GraphicsSceneLightIdentityRegistry& identity_registry,
    std::vector<RoR::Render::GraphicsSceneLightInput>& output)
{
    std::vector<RoR::Render::Ogre14GraphicsSceneLightCaptureInput> inputs;
    // SceneManager documents this registry view as unsafe during concurrent
    // creation/destruction. Capture runs only on the joined main-thread
    // boundary after BufferSimulationData(), where scene mutation is quiescent.
    const Ogre::SceneManager::MovableObjectMap& managed_lights =
        scene_manager.getMovableObjects(Ogre::MOT_LIGHT);
    inputs.reserve(managed_lights.size());

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

        RoR::Render::Ogre14GraphicsSceneLightCaptureInput input;
        input.exact_name = light->getName();
        switch (light->getType())
        {
        case Ogre::Light::LT_POINT:
            input.kind =
                RoR::Render::Ogre14GraphicsSceneLightKind::POINT;
            break;
        case Ogre::Light::LT_DIRECTIONAL:
            input.kind =
                RoR::Render::Ogre14GraphicsSceneLightKind::DIRECTIONAL;
            break;
        case Ogre::Light::LT_SPOTLIGHT:
            input.kind = RoR::Render::Ogre14GraphicsSceneLightKind::SPOT;
            break;
        case Ogre::Light::LT_RECTLIGHT:
            input.kind =
                RoR::Render::Ogre14GraphicsSceneLightKind::RECTANGLE;
            break;
        default:
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::INVALID_ENUM, "lights.type",
                "OGRE managed-light inventory contains an unknown type");
        }

#ifndef OGRE_NODELESS_POSITIONING
        if (!light->isAttached())
        {
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::MISSING_REFERENCE,
                "lights.parent_scene_node",
                "this OGRE build requires managed lights to be attached");
        }
#endif

        const Ogre::ColourValue diffuse = light->getDiffuseColour();
        const Ogre::ColourValue specular = light->getSpecularColour();
        input.diffuse_linear = {
            static_cast<float>(diffuse.r),
            static_cast<float>(diffuse.g),
            static_cast<float>(diffuse.b)};
        input.specular_linear = {
            static_cast<float>(specular.r),
            static_cast<float>(specular.g),
            static_cast<float>(specular.b)};
        input.power_scale = static_cast<float>(light->getPowerScale());
        // getVisible() is the stable authored enable bit. isVisible() also
        // depends on per-camera masks and mutable render-frame state.
        input.visible = light->getVisible();
        input.visibility_flags = light->getVisibilityFlags();
        input.light_mask = light->getLightMask();
        input.attenuation_range = light->getAttenuationRange();
        input.attenuation_constant = light->getAttenuationConstant();
        input.attenuation_linear = light->getAttenuationLinear();
        input.attenuation_quadratic = light->getAttenuationQuadric();
        input.inner_cone_radians =
            static_cast<float>(light->getSpotlightInnerAngle().valueRadians());
        input.outer_cone_radians =
            static_cast<float>(light->getSpotlightOuterAngle().valueRadians());
        input.spot_falloff =
            static_cast<float>(light->getSpotlightFalloff());
        input.casts_shadows = light->getCastShadows();

        if (input.kind ==
                RoR::Render::Ogre14GraphicsSceneLightKind::POINT ||
            input.kind == RoR::Render::Ogre14GraphicsSceneLightKind::SPOT)
        {
            const Ogre::Vector3 position = light->getDerivedPosition(false);
            input.derived_position = {
                static_cast<float>(position.x),
                static_cast<float>(position.y),
                static_cast<float>(position.z)};
        }
        if (input.kind ==
                RoR::Render::Ogre14GraphicsSceneLightKind::DIRECTIONAL ||
            input.kind == RoR::Render::Ogre14GraphicsSceneLightKind::SPOT)
        {
            const Ogre::Vector3 direction = light->getDerivedDirection();
            input.derived_direction = {
                static_cast<float>(direction.x),
                static_cast<float>(direction.y),
                static_cast<float>(direction.z)};
        }
        inputs.push_back(std::move(input));
    }

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

RoR::Render::ValidationResult CaptureOgre14MaterialFallbackInput(
    const Ogre::MaterialPtr& material,
    RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput& output,
    bool& reverse_winding)
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

    RoR::Render::Ogre14GraphicsSceneMaterialCull cull;
    switch (pass->getCullingMode())
    {
    case Ogre::CULL_NONE:
        cull = RoR::Render::Ogre14GraphicsSceneMaterialCull::NONE;
        reverse_winding = false;
        break;
    case Ogre::CULL_CLOCKWISE:
        cull = RoR::Render::Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
        reverse_winding = false;
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
    candidate.exact_resource_group = material->getGroup();
    candidate.exact_name = material->getName();
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
    candidate.cull = cull;
    candidate.alpha_reject = alpha_reject;
    candidate.alpha_reject_value = pass->getAlphaRejectValue();
    output = std::move(candidate);
    return RoR::Render::ValidationResult::Success();
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
    return RoR::Render::BuildOgre14GraphicsSceneStaticMeshPayload(
        input, payload);
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
    std::vector<RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput>&
        captured_sections)
{
    std::map<std::string,
             RoR::Render::Ogre14GraphicsSceneTerrainPageCacheEntry,
             std::less<>> candidate_cache;
    std::vector<RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput>
        candidate_sections;
    if (geometry_manager == nullptr ||
        geometry_manager->getTerrainGroup() == nullptr)
    {
        captured_cache = std::move(candidate_cache);
        captured_sections = std::move(candidate_sections);
        return RoR::Render::ValidationResult::Success();
    }

    Ogre::TerrainGroup* const group = geometry_manager->getTerrainGroup();
    if (group->getNumTerrainPrepareRequests() != 0U ||
        group->isDerivedDataUpdateInProgress())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::REVISION_MISMATCH,
            "terrain.pages.native_update",
            "TerrainGroup has background preparation or derived-data work");
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
    Ogre::TerrainGlobalOptions* const terrain_options =
        Ogre::TerrainGlobalOptions::getSingletonPtr();
    if (!slots.empty() && terrain_options == nullptr)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "terrain.pages.global_options",
            "loaded OGRE terrain has no TerrainGlobalOptions singleton");
    }

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

        const Ogre::TerrainLayerDeclaration& layer_declaration =
            terrain->getLayerDeclaration();
        if (layer_declaration.size() >
                (std::numeric_limits<std::uint8_t>::max)() ||
            terrain->getBlendTextures().size() >
                (std::numeric_limits<std::uint32_t>::max)())
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::SIZE_MISMATCH,
                "terrain.pages.material.audit",
                "terrain sampler or blend texture inventory exceeds portable bounds");
        }
        input.material_audit.layer_count = terrain->getLayerCount();
        input.material_audit.sampler_count =
            static_cast<std::uint32_t>(layer_declaration.size());
        input.material_audit.layer_world_sizes.reserve(
            input.material_audit.layer_count);
        input.material_audit.layer_texture_names.reserve(
            static_cast<std::size_t>(input.material_audit.layer_count) *
            input.material_audit.sampler_count);
        for (std::uint32_t layer = 0U;
             layer < input.material_audit.layer_count; ++layer)
        {
            input.material_audit.layer_world_sizes.push_back(
                static_cast<float>(terrain->getLayerWorldSize(
                    static_cast<std::uint8_t>(layer))));
            for (std::uint32_t sampler = 0U;
                 sampler < input.material_audit.sampler_count; ++sampler)
            {
                input.material_audit.layer_texture_names.push_back(
                    terrain->getLayerTextureName(
                        static_cast<std::uint8_t>(layer),
                        static_cast<std::uint8_t>(sampler)));
            }
        }
        const std::vector<Ogre::TexturePtr>& blend_textures =
            terrain->getBlendTextures();
        input.material_audit.blend_texture_count =
            static_cast<std::uint32_t>(blend_textures.size());
        input.material_audit.blend_texture_names.reserve(
            blend_textures.size());
        for (const Ogre::TexturePtr& texture : blend_textures)
        {
            input.material_audit.blend_texture_names.push_back(
                texture ? texture->getName() : std::string{});
        }
        const Ogre::TexturePtr& global_colour_map =
            terrain->getGlobalColourMap();
        input.material_audit.global_colour_map_enabled =
            terrain->getGlobalColourMapEnabled();
        if (global_colour_map)
            input.material_audit.exact_global_colour_map_name =
                global_colour_map->getName();
        const Ogre::TexturePtr& lightmap = terrain->getLightmap();
        input.material_audit.has_lightmap = static_cast<bool>(lightmap);
        if (lightmap)
            input.material_audit.exact_lightmap_name = lightmap->getName();
        const Ogre::TexturePtr& composite_map = terrain->getCompositeMap();
        input.material_audit.has_composite_map =
            static_cast<bool>(composite_map);
        if (composite_map)
            input.material_audit.exact_composite_map_name =
                composite_map->getName();

        // This is the render-thread material accessor used by OGRE itself. It
        // may finish lazy material generation, but it does not prepare LODs or
        // mutate height/normal/delta data.
        const Ogre::MaterialPtr material = terrain->getMaterial();
        bool reverse_winding = false;
        validation = CaptureOgre14MaterialFallbackInput(
            material, input.material, reverse_winding);
        if (!validation)
            return validation;
        if (reverse_winding !=
            (input.material.cull == RoR::Render::
                 Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE))
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "terrain.pages.material.winding",
                "terrain material culling and native winding conversion disagree");
        }
        input.visibility_mask = terrain->getVisibilityFlags();
        input.visible = true;
        input.casts_shadows = terrain_options->getCastsDynamicShadows();
        input.receives_shadows = material->getReceiveShadows();
        input.visible_in_reflections = true;

        validation = RoR::Render::
            ValidateOgre14GraphicsSceneTerrainMaterialCapture(
                input.material_audit, input.material);
        if (!validation)
            return validation;

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
    validation =
        RoR::Render::ValidateOgre14GraphicsSceneTerrainPageSet(page_inputs);
    if (!validation)
        return validation;

    candidate_sections.reserve(page_inputs.size());
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

        RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput section;
        validation = RoR::Render::BuildOgre14GraphicsSceneTerrainSection(
            input, cache_entry.mesh_payload, section);
        if (!validation)
            return validation;
        const auto inserted = candidate_cache.emplace(
            cache_key, std::move(cache_entry));
        if (!inserted.second)
        {
            return NativeStaticFailure(
                RoR::Render::ValidationCode::DUPLICATE_IDENTIFIER,
                "terrain.pages.cache_key",
                "distinct terrain pages produced one exact native cache key");
        }
        candidate_sections.push_back(std::move(section));
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
    captured_sections = std::move(candidate_sections);
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::ValidationResult CaptureOgre14StaticMeshObjects(
    RoR::TerrainObjectManager* object_manager,
    bool has_deformable_geometry,
    const std::vector<
        RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput>&
        terrain_sections,
    RoR::Render::Ogre14GraphicsSceneStaticIdentityRegistry& identity_registry,
    std::map<std::string,
             RoR::Render::Ogre14GraphicsSceneStaticMeshCacheEntry,
             std::less<>>& mesh_cache,
    std::vector<RoR::Render::GraphicsSceneAssetInput>& assets,
    std::vector<RoR::Render::GraphicsSceneStaticMeshInput>& static_meshes)
{
    RoR::Render::Ogre14GraphicsSceneUnsupportedGeometry unsupported;
    unsupported.deformable = has_deformable_geometry;
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
        sections = terrain_sections;
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
                validation = CaptureOgre14MaterialFallbackInput(
                    sub_entity->getMaterial(), section.material,
                    reverse_winding);
                if (!validation)
                    return validation;
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

                const std::string cache_key =
                    BuildNativeStaticMeshCacheKey(section.mesh_identity);
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
    m_ogre14_joined_buffer_ready = false;
    m_ogre14_joined_buffer_atomic = false;
    // Native mesh pointers cannot outlive SceneManager resource teardown.
    // Stable/tombstoned source identities intentionally remain in the
    // registry until the owning producer lifetime is explicitly replaced.
    m_ogre14_static_mesh_cache.clear();
    m_ogre14_terrain_page_cache.clear();

    // Delete dustpools
    for (auto itor : m_dustpools)
    {
        itor.second->Discard(m_scene_manager);
        delete itor.second;
    }
    m_dustpools.clear();

    // Delete game elements
    m_all_gfx_actors.clear();
    m_all_gfx_characters.clear();

    // Wipe scene manager
    m_scene_manager->clearScene();
    m_gfx_freebeams_grouping_node = nullptr;

    // Recover from the wipe
    App::GetCameraManager()->ReCreateCameraNode();
    App::GetGuiManager()->DirectionArrow.CreateArrow();
    m_gfx_freebeams_grouping_node = m_scene_manager->getRootSceneNode()->createChildSceneNode("FreeBeam Visuals");
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
        for (GfxActor* gfx_actor: m_all_gfx_actors)
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
    for (GfxActor* gfx_actor: m_all_gfx_actors)
    {
        gfx_actor->UpdateNetLabels(dt);
    }

    // Player avatars
    for (GfxCharacter* a: m_all_gfx_characters)
    {
        a->UpdateCharacterInScene();
    }

    // Actors - update misc visuals
    for (GfxActor* gfx_actor: m_all_gfx_actors)
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

void GfxScene::RegisterGfxActor(RoR::GfxActor* gfx_actor)
{
    m_all_gfx_actors.push_back(gfx_actor);
}

void GfxScene::BufferSimulationData()
{
    ActorManager* actor_manager = nullptr;
    std::uint64_t simulation_tick_before = 0U;
    float simulation_time_before = 0.0F;
    if (m_ogre14_scene_capture_enabled)
    {
        m_ogre14_joined_buffer_ready = false;
        m_ogre14_joined_buffer_atomic = false;
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
    for (GfxActor* a: m_all_gfx_actors)
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

    if (!m_ogre14_scene_capture_enabled)
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

Render::ValidationResult GfxScene::CaptureOgre14GraphicsScene(
    Render::Ogre14GraphicsSceneCapture& capture)
{
    Render::Ogre14GraphicsSceneCapture candidate;
    candidate.joined_buffer_epoch = m_ogre14_joined_buffer_epoch;
    if (m_ogre14_joined_buffer_ready && m_ogre14_joined_buffer_atomic)
    {
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    JOINED_BUFFER_ATOMICITY);
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
            std::map<std::string,
                     Render::Ogre14GraphicsSceneTerrainPageCacheEntry,
                     std::less<>> candidate_terrain_cache;
            std::vector<
                Render::Ogre14GraphicsSceneStaticSectionCaptureInput>
                terrain_sections;
            Render::ValidationResult static_validation =
                CaptureOgre14TerrainPages(
                    geometry_manager, m_ogre14_terrain_page_cache,
                    candidate_terrain_cache, terrain_sections);
            if (!static_validation)
            {
                return static_validation;
            }
            static_validation = CaptureOgre14StaticMeshObjects(
                    object_manager,
                    !m_all_gfx_actors.empty() ||
                        !m_all_gfx_characters.empty(),
                    terrain_sections,
                    m_ogre14_static_identity_registry,
                    m_ogre14_static_mesh_cache,
                    candidate.frame.assets,
                    candidate.frame.static_meshes);
            if (!static_validation)
            {
                return static_validation;
            }
            m_ogre14_terrain_page_cache =
                std::move(candidate_terrain_cache);
            // These bits commit together only after coverage, native CPU
            // extraction, material fallback, identity/lifecycle auditing, and
            // complete inventory conversion have all succeeded.
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::ASSETS);
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::STATIC_MESHES);

            Render::ValidationResult light_validation =
                CaptureOgre14ManagedLights(
                    *m_scene_manager, m_ogre14_light_identity_registry,
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

    capture = std::move(candidate);
    return Render::ValidationResult::Success();
}

void GfxScene::RemoveGfxActor(RoR::GfxActor* remove_me)
{
    auto itor = std::remove(m_all_gfx_actors.begin(), m_all_gfx_actors.end(), remove_me);
    if (itor != m_all_gfx_actors.end())
    {
        m_all_gfx_actors.erase(itor, m_all_gfx_actors.end());
    }
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
