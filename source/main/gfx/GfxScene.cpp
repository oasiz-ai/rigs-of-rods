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

#include <chrono>
#include "gfx/ogre14/detail/OgreNextDemoPrivatePolicy.h"
#include "gfx/render/ogrenext/OgreNextPssmShadowPolicy.h"
#include "system/detail/OgreNextDemoFrameNormalization.h"

#include <cstdio>

#include "AppContext.h"
#if OGRE_VERSION_MAJOR >= 14
#include "ContentManager.h"
#include "gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h"
#include <RTShaderSystem/OgreRTShaderSystem.h>
#endif
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
#include <OgreBillboardParticleRenderer.h>

#include <algorithm>
#include <array>
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

constexpr std::uint64_t kOgreNextDemoMaximumObservedParticleSystems = 4096U;
constexpr std::uint64_t kOgreNextDemoMaximumParticlesPerSystem = 16384U;
constexpr std::uint64_t kOgreNextDemoMaximumObservedParticles = 65536U;

bool IsFiniteOgreRealBits(Ogre::Real value) noexcept
{
    static_assert(sizeof(Ogre::Real) == sizeof(std::uint32_t) ||
                  sizeof(Ogre::Real) == sizeof(std::uint64_t),
                  "native Real must be IEEE binary32 or binary64");
    if constexpr (sizeof(Ogre::Real) == sizeof(std::uint32_t))
    {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
    }
    else
    {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & UINT64_C(0x7ff0000000000000)) !=
               UINT64_C(0x7ff0000000000000);
    }
}

bool HasOgreNextDemoDustSourceAlpha(
    const RoR::Render::TextureResourceDescriptor& texture) noexcept
{
    if (texture.type != RoR::Render::TextureResourceType::TEXTURE_2D ||
        texture.format != RoR::Render::TextureResourceFormat::RGBA8_UNORM ||
        texture.color_space != RoR::Render::TextureColorSpace::SRGB ||
        texture.array_layers != 1U)
    {
        return false;
    }
    for (const auto& mip : texture.mip_levels)
    {
        for (std::uint32_t y = 0U; y < mip.height; ++y)
        {
            const std::uint64_t row = static_cast<std::uint64_t>(y) *
                mip.row_pitch_bytes;
            for (std::uint32_t x = 0U; x < mip.width; ++x)
            {
                const std::uint64_t alpha = row +
                    static_cast<std::uint64_t>(x) * 4U + 3U;
                if (alpha >= mip.bytes.size())
                    return false;
                if (mip.bytes[static_cast<std::size_t>(alpha)] != 255U)
                    return true;
            }
        }
    }
    return false;
}

bool IsOgreNextDemoDustSampler(
    const RoR::Render::SamplerResourceDescriptor& sampler,
    std::size_t mip_count) noexcept
{
    if (mip_count == 0U ||
        sampler.address_u !=
            RoR::Render::SamplerAddressMode::CLAMP_TO_EDGE ||
        sampler.address_v !=
            RoR::Render::SamplerAddressMode::CLAMP_TO_EDGE ||
        sampler.address_w !=
            RoR::Render::SamplerAddressMode::CLAMP_TO_EDGE ||
        sampler.mip_lod_bias != 0.0F || sampler.minimum_lod != 0.0F ||
        sampler.maximum_lod != static_cast<float>(mip_count - 1U) ||
        sampler.compare_enabled)
    {
        return false;
    }
    if (!sampler.anisotropy_enabled)
        return sampler.maximum_anisotropy == 1.0F;
    return sampler.minification_filter == RoR::Render::SamplerFilter::LINEAR &&
        sampler.magnification_filter == RoR::Render::SamplerFilter::LINEAR &&
        sampler.mip_filter == RoR::Render::SamplerFilter::LINEAR &&
        sampler.maximum_anisotropy >= 2.0F &&
        sampler.maximum_anisotropy <= 16.0F &&
        std::floor(sampler.maximum_anisotropy) ==
            sampler.maximum_anisotropy;
}

std::string FormatOgreNextDemoMaterialExclusions(
    const RoR::Gfx::Detail::OgreNextDemoMaterialSourceCounters& counters)
{
    std::string result;
    for (std::size_t index = 1U;
         index < counters.exclusions_by_reason.size(); ++index)
    {
        if (!result.empty())
            result.push_back(',');
        const auto reason = static_cast<
            RoR::Gfx::Detail::OgreNextDemoTextureProjectionExclusion>(index);
        result.append(
            RoR::Gfx::Detail::OgreNextDemoTextureProjectionExclusionName(
                reason));
        result.push_back('=');
        result.append(std::to_string(counters.exclusions_by_reason[index]));
    }
    return result;
}

std::string BuildOgreNextDemoMaterialCoverageSnapshot(
    std::size_t active_projections,
    const RoR::Gfx::Detail::OgreNextDemoMaterialSourceCounters& counters,
    const RoR::Gfx::Detail::OgreNextDemoCuratedCityWorldCoverage& curated)
{
    return fmt::format(
        "active={};candidates={};projected={};matte={};eligible_keys={};"
        "projected_keys={};matte_only_keys={};active_texture_states={};"
        "authored_mips={};generated_mips={};output_mips={};native_mips={};"
        "tus_gamma_nonunit={};texture_gamma_nonunit={};tus_hw_gamma_off={};"
        "texture_hw_gamma_off={};automipmap={};blend_replace={};"
        "blend_source_over={};blend_legacy_alpha={};alpha_disabled={};"
        "alpha_greater={};alpha_greater_equal={};workflow_mr={};"
        "workflow_specular={};anisotropic={};normalized_textures={};"
        "opaque_v2={};straight_alpha_v1={};linear_specular_v1={};"
        "curated_cityworld={}/{};curated_observed={};curated_matte={};"
        "curated_environment_pending={};uncurated_spherical_matte={};"
        "reasons={}",
        active_projections, counters.candidate_sections,
        counters.projected_sections, counters.matte_excluded_sections,
        counters.distinct_eligible_texture_keys,
        counters.distinct_projected_texture_keys,
        counters.distinct_matte_only_texture_keys,
        counters.active_texture_state_observations,
        counters.active_authored_mip_prefix_levels,
        counters.active_generated_mip_tail_levels,
        counters.active_normalized_output_mip_levels,
        counters.active_legacy_native_additional_mip_levels,
        counters.active_legacy_texture_unit_gamma_nonunit_observations,
        counters.active_legacy_texture_gamma_nonunit_observations,
        counters.active_legacy_texture_unit_hardware_gamma_off_observations,
        counters.active_legacy_hardware_gamma_off_observations,
        counters.active_legacy_automipmap_observations,
        counters.active_replace_material_projections,
        counters.active_straight_source_over_material_projections,
        counters.active_legacy_straight_alpha_material_projections,
        counters.active_alpha_test_disabled_material_projections,
        counters.active_alpha_test_greater_material_projections,
        counters.active_alpha_test_greater_equal_material_projections,
        counters.active_metallic_roughness_workflow_projections,
        counters.active_specular_workflow_projections,
        counters.active_anisotropic_sampler_projections,
        counters.active_normalized_texture_observations,
        counters.active_opaque_texture_normalizations,
        counters.active_straight_alpha_texture_normalizations,
        counters.active_linear_specular_texture_normalizations,
        curated.admitted_entries, curated.policy_entries,
        curated.observed_entries, curated.matte_entries,
        curated.environment_pending_entries,
        curated.uncurated_spherical_family_matte_materials,
        FormatOgreNextDemoMaterialExclusions(counters));
}

std::string FormatOgreNextDemoMaterialCounters(
    const RoR::Gfx::Detail::OgreNextDemoMaterialSourceCounters& counters)
{
    std::string result = fmt::format(
        "new_frozen_material_decisions={} candidate_sections={} "
        "projected_sections={} matte_excluded_sections={} projections={} "
        "distinct_eligible_texture_keys={} distinct_projected_texture_keys={} "
        "distinct_matte_only_texture_keys={} "
        "active_replace_material_projections={} "
        "active_straight_source_over_material_projections={} "
        "active_legacy_straight_alpha_material_projections={} "
        "active_alpha_test_disabled_material_projections={} "
        "active_alpha_test_greater_material_projections={} "
        "active_alpha_test_greater_equal_material_projections={} "
        "active_metallic_roughness_workflow_projections={} "
        "active_specular_workflow_projections={} "
        "active_anisotropic_sampler_projections={} ",
        counters.new_frozen_material_decisions, counters.candidate_sections,
        counters.projected_sections, counters.matte_excluded_sections,
        counters.projections, counters.distinct_eligible_texture_keys,
        counters.distinct_projected_texture_keys,
        counters.distinct_matte_only_texture_keys,
        counters.active_replace_material_projections,
        counters.active_straight_source_over_material_projections,
        counters.active_legacy_straight_alpha_material_projections,
        counters.active_alpha_test_disabled_material_projections,
        counters.active_alpha_test_greater_material_projections,
        counters.active_alpha_test_greater_equal_material_projections,
        counters.active_metallic_roughness_workflow_projections,
        counters.active_specular_workflow_projections,
        counters.active_anisotropic_sampler_projections);
    result += fmt::format(
        "active_texture_state_observations={} "
        "active_normalized_texture_observations={} "
        "active_opaque_texture_normalizations={} "
        "active_straight_alpha_texture_normalizations={} "
        "active_linear_specular_texture_normalizations={} "
        "active_authored_mip_prefix_levels={} "
        "active_generated_mip_tail_levels={} "
        "active_normalized_output_mip_levels={} "
        "active_legacy_native_additional_mip_levels={} "
        "active_legacy_texture_unit_gamma_nonunit_observations={} "
        "active_legacy_texture_gamma_nonunit_observations={} "
        "active_legacy_texture_unit_hardware_gamma_off_observations={} "
        "active_legacy_hardware_gamma_off_observations={} "
        "active_legacy_automipmap_observations={} ",
        counters.active_texture_state_observations,
        counters.active_normalized_texture_observations,
        counters.active_opaque_texture_normalizations,
        counters.active_straight_alpha_texture_normalizations,
        counters.active_linear_specular_texture_normalizations,
        counters.active_authored_mip_prefix_levels,
        counters.active_generated_mip_tail_levels,
        counters.active_normalized_output_mip_levels,
        counters.active_legacy_native_additional_mip_levels,
        counters.active_legacy_texture_unit_gamma_nonunit_observations,
        counters.active_legacy_texture_gamma_nonunit_observations,
        counters.active_legacy_texture_unit_hardware_gamma_off_observations,
        counters.active_legacy_hardware_gamma_off_observations,
        counters.active_legacy_automipmap_observations);
    result += fmt::format(
        "authenticated_archive_source_decodes={} "
        "authenticated_generated_source_decodes={} "
        "authenticated_source_decodes={} ordinary_observed_source_decodes={} "
        "source_cache_hits={} source_decode_rejections={} "
        "source_exclusions={} modern_source_normalizations={} "
        "opaque_source_normalizations={} "
        "straight_alpha_source_normalizations={} "
        "authored_specular_source_decodes={} "
        "linear_specular_source_normalizations={} "
        "authored_specular_mip_prefix_levels={} "
        "generated_specular_mip_tail_levels={} "
        "normalized_specular_output_mip_levels={} "
        "alpha_test_material_projections={} "
        "straight_source_over_material_projections={} "
        "legacy_straight_alpha_material_projections={} "
        "specular_workflow_projections={} anisotropic_sampler_projections={} "
        "authored_mip_prefix_levels={} generated_mip_tail_levels={} "
        "normalized_output_mip_levels={} "
        "legacy_native_additional_mip_levels={} "
        "legacy_texture_unit_gamma_nonunit_observations={} "
        "legacy_texture_gamma_nonunit_observations={} "
        "legacy_texture_unit_hardware_gamma_off_observations={} "
        "legacy_hardware_gamma_off_observations={} "
        "legacy_automipmap_observations={} "
        "lossy_material_normalizations={} gpu_readbacks={} "
        "authenticated_gpu_readbacks={} unauthenticated_gpu_readbacks={} "
        "matte_by_reason=[{}]",
        counters.authenticated_archive_source_decodes,
        counters.authenticated_generated_source_decodes,
        counters.authenticated_source_decodes,
        counters.ordinary_observed_source_decodes, counters.source_cache_hits,
        counters.source_decode_rejections, counters.source_exclusions,
        counters.modern_source_normalizations,
        counters.opaque_source_normalizations,
        counters.straight_alpha_source_normalizations,
        counters.authored_specular_source_decodes,
        counters.linear_specular_source_normalizations,
        counters.authored_specular_mip_prefix_levels,
        counters.generated_specular_mip_tail_levels,
        counters.normalized_specular_output_mip_levels,
        counters.alpha_test_material_projections,
        counters.straight_source_over_material_projections,
        counters.legacy_straight_alpha_material_projections,
        counters.specular_workflow_projections,
        counters.anisotropic_sampler_projections,
        counters.authored_mip_prefix_levels,
        counters.generated_mip_tail_levels,
        counters.normalized_output_mip_levels,
        counters.legacy_native_additional_mip_levels,
        counters.legacy_texture_unit_gamma_nonunit_observations,
        counters.legacy_texture_gamma_nonunit_observations,
        counters.legacy_texture_unit_hardware_gamma_off_observations,
        counters.legacy_hardware_gamma_off_observations,
        counters.legacy_automipmap_observations,
        counters.lossy_material_normalizations, counters.gpu_readbacks,
        counters.authenticated_gpu_readbacks,
        counters.unauthenticated_gpu_readbacks,
        FormatOgreNextDemoMaterialExclusions(counters));
    return result;
}

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
            m_source.DiscardMapGenerationCapture();
    }

    void Arm() noexcept { m_armed = true; }
    void Release() noexcept { m_armed = false; }

private:
    RoR::Gfx::Detail::Ogre14ToOgreNextTerrainSource& m_source;
    bool m_armed = false;
};

class OgreNextDemoMaterialPendingGuard final
{
public:
    explicit OgreNextDemoMaterialPendingGuard(
        RoR::Gfx::Detail::OgreNextDemoMaterialSource& source) noexcept:
        m_source(source)
    {
    }

    ~OgreNextDemoMaterialPendingGuard()
    {
        if (m_armed)
            m_source.Discard();
    }

    void Arm() noexcept { m_armed = true; }
    void Release() noexcept { m_armed = false; }

private:
    RoR::Gfx::Detail::OgreNextDemoMaterialSource& m_source;
    bool m_armed = false;
};

class Ogre14RoadMaterialFramePendingGuard final
{
public:
    explicit Ogre14RoadMaterialFramePendingGuard(
        std::unique_ptr<RoR::Render::Ogre14LegacyLiveMaterialCoordinator>&
            coordinator) noexcept:
        m_coordinator(coordinator)
    {
    }

    ~Ogre14RoadMaterialFramePendingGuard()
    {
        if (m_armed && m_coordinator != nullptr)
            m_coordinator->DiscardPreparedFrame();
    }

    void Arm() noexcept { m_armed = true; }
    void Release() noexcept { m_armed = false; }

private:
    std::unique_ptr<RoR::Render::Ogre14LegacyLiveMaterialCoordinator>&
        m_coordinator;
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
    RoR::Render::GraphicsSceneCameraInput& output,
    RoR::Render::Double3& absolute_world_position_meters)
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
    input.far_plane = RoR::Detail::ResolveOgreNextDemoCaptureFarPlane(
        static_cast<float>(camera->getFarClipDistance()));
    // OGRE 14 has no scene-linear view-exposure state. Identity is therefore
    // exact here; optional display postprocessing remains outside the scene.
    input.exposure = 1.0F;
    input.visibility_mask = viewport->getVisibilityMask();
    const RoR::Render::ValidationResult validation =
        RoR::Render::BuildOgre14GraphicsSceneCamera(input, output);
    if (!validation)
        return false;
    const Ogre::Vector3 camera_position = camera->getDerivedPosition();
    absolute_world_position_meters = {
        static_cast<double>(camera_position.x),
        static_cast<double>(camera_position.y),
        static_cast<double>(camera_position.z)};
    return true;
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
            RoR::Render::kOgreNextDemoStaticAdmissionFarMeters,
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
    const bool straight_source_over = source == Ogre::SBF_SOURCE_ALPHA &&
        destination == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
        source_alpha == Ogre::SBF_ONE &&
        destination_alpha == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    const bool legacy_straight_alpha = source == Ogre::SBF_SOURCE_ALPHA &&
        destination == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
        source_alpha == Ogre::SBF_SOURCE_ALPHA &&
        destination_alpha == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    if (!replace && !straight_source_over && !legacy_straight_alpha)
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "assets.material.blend",
            "portable fallback supports replace, Porter-Duff straight "
            "source-over, or exact OGRE legacy straight-alpha blending");
    }
    if (!pass->getDepthCheckEnabled() ||
        pass->getDepthFunction() != Ogre::CMPF_LESS_EQUAL ||
        pass->getDepthBiasConstant() != 0.0F ||
        pass->getDepthBiasSlopeScale() != 0.0F ||
        pass->getIterationDepthBias() != 0.0F ||
        pass->isAlphaToCoverageEnabled())
    {
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "assets.material.depth_alpha_state",
            "portable fallback requires depth-test LESS_EQUAL, zero depth "
            "bias, and disabled alpha-to-coverage");
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
    case Ogre::CMPF_GREATER:
        alpha_reject = RoR::Render::
            Ogre14GraphicsSceneMaterialAlphaReject::GREATER;
        break;
    case Ogre::CMPF_GREATER_EQUAL:
        alpha_reject = RoR::Render::
            Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL;
        break;
    default:
        return NativeStaticFailure(
            RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
            "assets.material.alpha_reject",
            "portable fallback supports always-pass, greater, or "
            "greater-equal alpha rejection");
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
    candidate.blend =
        replace
            ? RoR::Render::Ogre14GraphicsSceneMaterialBlend::REPLACE
            : straight_source_over
                  ? RoR::Render::Ogre14GraphicsSceneMaterialBlend::
                        STRAIGHT_SOURCE_OVER
                  : RoR::Render::Ogre14GraphicsSceneMaterialBlend::
                        LEGACY_STRAIGHT_ALPHA;
    candidate.cull = reference.cull;
    candidate.alpha_reject = alpha_reject;
    candidate.alpha_reject_value = pass->getAlphaRejectValue();
    candidate.depth_write = pass->getDepthWriteEnabled();
    output = std::move(candidate);
    reverse_winding = reference.reverse_winding;
    return RoR::Render::ValidationResult::Success();
}

// Disposable demo-only policy. Authored textures/programs first receive one
// canonical neutral opaque matte so unsupported legacy state cannot erase real
// mesh geometry or live simulation deformation. A later private source step
// may replace that matte with the narrowly admitted opaque TUS0 PBR projection;
// every unsupported section retains this deterministic fallback.
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
    candidate.depth_write = true;

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

bool EqualOgre14ContinuousParticleState(
    const RoR::Render::Ogre14ParticleState& lhs,
    const RoR::Render::Ogre14ParticleState& rhs) noexcept
{
    return lhs.particle_id == rhs.particle_id &&
        lhs.position == rhs.position && lhs.direction == rhs.direction &&
        lhs.velocity == rhs.velocity &&
        lhs.color_linear == rhs.color_linear &&
        lhs.size_meters == rhs.size_meters &&
        lhs.rotation_radians == rhs.rotation_radians &&
        lhs.age_seconds == rhs.age_seconds &&
        lhs.lifetime_seconds == rhs.lifetime_seconds;
}

bool EqualOgre14ContinuousParticleSystem(
    const RoR::Render::Ogre14ParticleSourceSystemCapture& lhs,
    const RoR::Render::Ogre14ParticleSourceSystemCapture& rhs) noexcept
{
    if (lhs.system_id != rhs.system_id || lhs.effect != rhs.effect ||
        lhs.billboard_mode != rhs.billboard_mode ||
        lhs.billboard_rotation_mode != rhs.billboard_rotation_mode ||
        lhs.particles_are_world_space != rhs.particles_are_world_space ||
        lhs.requires_frontend_emitter_evaluation !=
            rhs.requires_frontend_emitter_evaluation ||
        lhs.requires_frontend_affector_evaluation !=
            rhs.requires_frontend_affector_evaluation ||
        lhs.requires_frontend_sorting != rhs.requires_frontend_sorting ||
        lhs.requires_texture_animation != rhs.requires_texture_animation ||
        lhs.system_visible != rhs.system_visible ||
        lhs.parent_visible != rhs.parent_visible ||
        lhs.emitting != rhs.emitting ||
        lhs.material_closure.material_source_asset_id !=
            rhs.material_closure.material_source_asset_id ||
        lhs.material_closure.texture_source_asset_id !=
            rhs.material_closure.texture_source_asset_id ||
        lhs.material_closure.sampler_source_asset_id !=
            rhs.material_closure.sampler_source_asset_id ||
        lhs.material_closure.blend != rhs.material_closure.blend ||
        lhs.material_closure.alpha_reject !=
            rhs.material_closure.alpha_reject ||
        lhs.material_closure.alpha_reject_threshold !=
            rhs.material_closure.alpha_reject_threshold ||
        lhs.material_closure.sort_policy !=
            rhs.material_closure.sort_policy ||
        lhs.material_closure.depth_check !=
            rhs.material_closure.depth_check ||
        lhs.material_closure.depth_write !=
            rhs.material_closure.depth_write ||
        lhs.material_closure.lighting_enabled !=
            rhs.material_closure.lighting_enabled ||
        lhs.material_closure.receives_shadows !=
            rhs.material_closure.receives_shadows ||
        lhs.material_closure.casts_shadows !=
            rhs.material_closure.casts_shadows ||
        lhs.material_closure.vertex_color_modulation !=
            rhs.material_closure.vertex_color_modulation ||
        lhs.material_closure.source_backed_texture !=
            rhs.material_closure.source_backed_texture ||
        lhs.material_closure.gpu_readback_used !=
            rhs.material_closure.gpu_readback_used ||
        lhs.particles.size() != rhs.particles.size())
        return false;
    for (std::size_t index = 0U; index < lhs.particles.size(); ++index)
    {
        if (!EqualOgre14ContinuousParticleState(
                lhs.particles[index], rhs.particles[index]))
            return false;
    }
    return true;
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
    // private demo boundary that prevents a pending SkyX light-map update from
    // starving the one map-generation capture forever.
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
    const RoR::Actor* managed_material_owner,
    const RoR::Render::ManagedMaterialDeclarationSnapshot*
        managed_material_snapshot,
    std::vector<RoR::Render::Ogre14ManagedMaterialDeclarationBinding>&
        projected_managed_material_bindings,
    RoR::Gfx::Detail::OgreNextDemoMaterialSource& material_source,
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
        const bool has_authored_uv0 = !joined_texcoords0.empty() &&
            operation.vertexData->vertexDeclaration != nullptr &&
            operation.vertexData->vertexDeclaration->
                findElementBySemantic(
                    Ogre::VES_TEXTURE_COORDINATES, 0U) != nullptr;
        const std::string material_decision_key =
            "dynamic/" + BuildNativeDynamicMeshCacheKey(identity);
        RoR::Render::Ogre14ManagedMaterialDeclarationBinding managed_binding;
        const RoR::Render::Ogre14ManagedMaterialDeclarationBinding*
            managed_binding_ptr = nullptr;
        if (managed_material_owner != nullptr &&
            managed_material_snapshot != nullptr)
        {
            bool managed_binding_found = false;
            validation =
                managed_material_owner->ResolveManagedMaterialDeclarationBinding(
                    *managed_material_snapshot, sub_entity->getMaterial(),
                    managed_binding, managed_binding_found);
            if (!validation)
            {
                validation.field = "dynamic_meshes." + validation.field;
                return validation;
            }
            if (managed_binding_found)
            {
                managed_binding_ptr = &managed_binding;
            }
        }
        bool projected = false;
        validation = material_source.TryProject(
            material_decision_key, sub_entity->getMaterial(),
            used_demo_matte, has_authored_uv0, managed_binding_ptr,
            section.material, projected);
        if (!validation)
            return validation;
        if (projected && managed_binding_ptr != nullptr)
        {
            const auto already_reachable = std::find_if(
                projected_managed_material_bindings.begin(),
                projected_managed_material_bindings.end(),
                [&managed_binding](const auto& candidate)
                {
                    return candidate.SharesImmutableStateWith(
                        managed_binding);
                });
            if (already_reachable ==
                projected_managed_material_bindings.end())
            {
                projected_managed_material_bindings.push_back(
                    managed_binding);
            }
        }
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
    RoR::Gfx::Detail::OgreNextDemoMaterialSource& material_source,
    std::set<std::uint64_t>& admitted_static_objects,
    RoR::Render::Ogre14GraphicsSceneStaticIdentityRegistry& identity_registry,
    std::map<std::string,
             RoR::Render::Ogre14GraphicsSceneStaticMeshCacheEntry,
             std::less<>>& mesh_cache,
    const std::vector<RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput>&
        adapted_road_sections,
    bool procedural_roads_adapted,
    std::vector<std::pair<std::uint64_t, RoR::Render::Bounds3>>&
        unadmitted_bounds,
    std::vector<RoR::Render::GraphicsSceneAssetInput>& assets,
    std::vector<RoR::Render::GraphicsSceneStaticMeshInput>& static_meshes)
{
    RoR::Render::Ogre14GraphicsSceneUnsupportedGeometry unsupported;
    if (object_manager != nullptr)
    {
        // Roads that arrived through the finalized procedural-road adapter
        // are part of this capture; only unadapted procedural geometry stays
        // an unsupported-coverage failure.
        unsupported.procedural = !procedural_roads_adapted &&
            object_manager->HasProceduralGeometry();
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
                {
                    // Static bounds cannot move. Recording them here lets the
                    // retention gate decide next frame, from a scan of this
                    // list alone, whether a walk could admit anything new.
                    unadmitted_bounds.emplace_back(
                        record.stable_id, world_bounds);
                    continue;
                }
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
                const bool has_authored_uv0 =
                    operation.vertexData->vertexDeclaration != nullptr &&
                    operation.vertexData->vertexDeclaration->
                        findElementBySemantic(
                            Ogre::VES_TEXTURE_COORDINATES, 0U) != nullptr;
                const std::string material_decision_key =
                    "static/" +
                    std::to_string(section.stable_object_id) + "/" +
                    std::to_string(section.section_index);
                bool projected = false;
                validation = material_source.TryProject(
                    material_decision_key, sub_entity->getMaterial(),
                    used_demo_matte, has_authored_uv0,
                    section.material, projected);
                if (!validation)
                    return validation;
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

    // The inventory build replaces the registry's live-key sets wholesale,
    // so every static section - authored objects and adapted procedural
    // roads alike - must flow through this one call.
    sections.insert(sections.end(), adapted_road_sections.begin(),
                    adapted_road_sections.end());
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
    // This is deliberately idempotent: the unload/fatal coordinator must have
    // released capture authority before native owners begin teardown, while
    // ClearScene keeps a final local guard before SceneManager::clearScene().
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
    // Authenticated material cache entries are immutable anti-tombstone owners
    // only while their map generation is alive. Full-scene teardown releases
    // them here; same-map bundle reload retains only unreachable payload owners
    // and must pass a fresh exact authenticated observation before reuse.
    m_ogre_next_demo_material_source.Reset();
    m_ogre_next_demo_material_coverage_log_snapshot.clear();
    m_ogre_next_demo_analytic_sky_log_snapshot.clear();
    m_ogre14_automatic_reflection_probe_state = {};
    // The retained GUI readback belongs to the closing generation; the next
    // map republishes only after a fresh overlay capture.
    m_ogre14_hud_overlay_latest.reset();
    // The retained static scene belongs to the map generation that produced
    // it. A different map with the same object count would otherwise pass
    // every retention gate while carrying the previous map's meshes.
    m_ogre14_static_retention_valid = false;
    m_ogre14_static_retention_inventory = 0U;
    m_ogre14_static_retention_cache_size = 0U;
    m_ogre14_static_retention_frozen_decisions = 0U;
    m_ogre14_static_retention_projections = 0U;
    m_ogre14_static_retention_assets_owner.reset();
    m_ogre14_static_retention_meshes_owner.reset();
    m_ogre14_static_retention_object_ids.reset();
    m_ogre14_static_retention_unadmitted.clear();
    m_ogre14_static_retention_road_live = 0U;
    m_ogre14_static_retention_road_cached = 0U;
    m_ogre14_procedural_road_inventory =
        Render::Ogre14ProceduralRoadInventory();
    // A fresh coordinator per generation: the opaque catalog identity must
    // change even when numeric sequences restart at one. Cached observations
    // carry the old generation's registry receipts, so they go with it.
    m_ogre14_road_material_coordinator.reset();
    m_ogre14_road_observation_cache.clear();
    m_ogre14_joined_buffer_epoch = 0U;
    m_ogre14_joined_buffer_ready = false;
    m_ogre14_joined_buffer_atomic = false;
    m_ogre14_post_update_scene_epoch = 0U;
    m_ogre14_simulation_tick = 0U;
    m_ogre14_simulation_time_seconds = 0.0;
    m_ogre14_particle_update_timings.clear();
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
    m_ogre14_particle_capture_state = {};
    m_ogre14_particle_coverage_log_snapshot.clear();
}

void GfxScene::Init()
{
    ROR_ASSERT(!m_scene_manager);
#if OGRE_VERSION_MAJOR >= 14
    ContentManager* const content_manager = App::GetContentManager();
    const bool authenticated_texture_authority_bound =
        content_manager != nullptr &&
        m_ogre_next_demo_material_source.BindAuthenticatedTextureAuthority(
            *content_manager, *content_manager);
    const bool ordinary_texture_source_bound =
        content_manager != nullptr &&
        m_ogre_next_demo_material_source.
            BindOrdinarySelectedTextureSourceResolver(*content_manager);
    const bool authenticated_material_script_bound =
        content_manager != nullptr &&
        m_ogre_next_demo_material_source.
            BindAuthenticatedMaterialScriptResolver(*content_manager);
    if (!authenticated_texture_authority_bound ||
        !ordinary_texture_source_bound ||
        !authenticated_material_script_bound)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "The OgreNext material source could not bind ContentManager's "
            "authenticated texture/material-script authority and ordinary "
            "selected-source resolver",
            "GfxScene::Init");
    }
#endif
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
    if (m_ogre_next_demo_capture_enabled)
    {
        // In capture ownership the legacy Ogre Root never renders a frame, so
        // its frame-time controller cannot advance ParticleSystem. Reconstruct
        // the controller's real-frame input from simulation time; _update()
        // then applies each native system's already-authored speed factor once.
        // This runs after every emitter mutation above and before the joined
        // epoch is published, making the captured particles realized OGRE14
        // simulation state rather than frontend emitter guesses.
        const float simulation_speed = m_simbuf.simbuf_sim_speed;
        const float particle_frame_seconds =
            dt > 0.0F && std::isfinite(dt) && simulation_speed > 0.0F &&
                    std::isfinite(simulation_speed)
                ? dt / simulation_speed
                : 0.0F;
        const Ogre::SceneManager::MovableObjectMap& particle_objects =
            m_scene_manager->getMovableObjects(Ogre::MOT_PARTICLE_SYSTEM);
        for (const auto& entry : particle_objects)
        {
            Ogre::ParticleSystem* const particle_system =
                dynamic_cast<Ogre::ParticleSystem*>(entry.second);
            if (particle_system != nullptr)
            {
                Ogre14ParticleUpdateTiming& timing =
                    m_ogre14_particle_update_timings[
                        particle_system->getName()];
                Ogre::Real effective_seconds = particle_frame_seconds;
                effective_seconds *= particle_system->getSpeedFactor();
                if (!timing.valid ||
                    timing.native_update_count ==
                        (std::numeric_limits<std::uint64_t>::max)() ||
                    !IsFiniteOgreRealBits(effective_seconds) ||
                    effective_seconds < 0.0F)
                {
                    timing.valid = false;
                }
                else
                {
                    ++timing.native_update_count;
                    timing.latest_effective_interval_seconds =
                        effective_seconds;
                }
                particle_system->_update(particle_frame_seconds);
            }
        }
    }
    // Publish the epoch only after every asynchronous FlexBody/Flexable task
    // has joined, flexitFinal()/updateFlexbodyVertexBuffers() has completed,
    // and the hidden OGRE14 particle simulation has reached this same frame.
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
        const ActorPtr managed_material_owner = actor->GetActor();
        if (!managed_material_owner)
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "dynamic_meshes.managed_material_owner",
                "live GfxActor has no managed-material Actor owner");
        }
        Render::ManagedMaterialDeclarationSnapshot managed_material_snapshot;
        Render::ValidationResult managed_snapshot_validation =
            managed_material_owner->CaptureManagedMaterialDeclarationSnapshot(
                managed_material_snapshot);
        if (!managed_snapshot_validation)
        {
            managed_snapshot_validation.field =
                "dynamic_meshes." + managed_snapshot_validation.field;
            return managed_snapshot_validation;
        }
        std::vector<Ogre::Vector3> positions;
        std::vector<Ogre::Vector3> normals;
        std::vector<Ogre::Vector2> texcoords0;
        std::vector<Render::Ogre14ManagedMaterialDeclarationBinding>
            projected_managed_material_bindings;

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
                    managed_material_owner.GetRef(), &managed_material_snapshot,
                    projected_managed_material_bindings,
                    m_ogre_next_demo_material_source, mesh_cache, sections);
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
                    flexbody->hasDynamicTextureBlend(),
                    managed_material_owner.GetRef(), &managed_material_snapshot,
                    projected_managed_material_bindings,
                    m_ogre_next_demo_material_source, mesh_cache, sections);
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
                    false, managed_material_owner.GetRef(),
                    &managed_material_snapshot,
                    projected_managed_material_bindings,
                    m_ogre_next_demo_material_source,
                    mesh_cache, sections);
            if (!validation)
                return validation;
        }
        managed_snapshot_validation =
            managed_material_owner->
                ValidateManagedMaterialDeclarationSnapshotReachability(
                    managed_material_snapshot,
                    projected_managed_material_bindings);
        if (!managed_snapshot_validation)
        {
            managed_snapshot_validation.field =
                "dynamic_meshes." + managed_snapshot_validation.field;
            return managed_snapshot_validation;
        }
    }

    return Render::BuildOgre14GraphicsSceneDynamicInventory(
        sections, identity_registry, assets, dynamic_meshes);
}

namespace {

/// Adds its own lifetime to a capture-section counter. The OGRE 14 scene read
/// dominates a combined-runtime frame, and the sections differ in what could
/// fix them, so each is measured separately rather than as one total.
class Ogre14CaptureSectionTimer final
{
public:
    explicit Ogre14CaptureSectionTimer(std::uint64_t& accumulator) noexcept
        : m_accumulator(accumulator)
        , m_started(std::chrono::steady_clock::now())
    {
    }

    ~Ogre14CaptureSectionTimer() noexcept
    {
        m_accumulator += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - m_started).count());
    }

    Ogre14CaptureSectionTimer(const Ogre14CaptureSectionTimer&) = delete;
    Ogre14CaptureSectionTimer& operator=(
        const Ogre14CaptureSectionTimer&) = delete;

private:
    std::uint64_t& m_accumulator;
    std::chrono::steady_clock::time_point m_started;
};

} // namespace

Render::ValidationResult GfxScene::EnsureOgre14RoadMaterialCoordinator()
{
    if (m_ogre14_road_material_coordinator != nullptr)
        return Render::ValidationResult::Success();
    ContentManager* const content_manager = App::GetContentManager();
    if (content_manager == nullptr)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::MISSING_REFERENCE,
            "road_material.content_manager",
            "no ContentManager authority for road material translation");
    }
    // Authored compatibility declaration: road2 is the exact legacy lit,
    // diffuse-textured fixed-function road material. The lit election and
    // the sRGB base-color role are declared here, never inferred from pass
    // state or file names.
    std::vector<Render::Ogre14LegacyMaterialSemanticDeclaration>
        declarations(1U);
    declarations[0].material_key.exact_resource_group = "MaterialsRG";
    declarations[0].material_key.exact_name = "road2";
    declarations[0].source = Render::Ogre14LegacyMaterialSemanticSource::
        VERSIONED_COMPATIBILITY_TABLE;
    declarations[0].source_revision = 1U;
    declarations[0].base_color_semantic =
        Render::Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR;
    declarations[0].texture_color_role =
        Render::Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
    Render::Ogre14LegacyMaterialSemanticRegistry semantic_registry;
    Render::ValidationResult validation =
        Render::BuildOgre14LegacyMaterialSemanticRegistry(
            Render::Ogre14LegacyMaterialSemanticRegistryConfiguration{},
            declarations, semantic_registry);
    if (!validation)
        return validation;
    // The textured construction: ContentManager is both the authenticated
    // texture resolver used at native capture and the scene-lifetime
    // authority snapshot provider consulted by every PrepareFrame.
    return Render::CreateOgre14LegacyLiveMaterialCoordinator(
        Render::Ogre14LegacyLiveMaterialCoordinatorConfiguration{},
        semantic_registry, *content_manager,
        m_ogre14_road_material_coordinator);
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
    const std::chrono::steady_clock::time_point ogre14_capture_started =
        std::chrono::steady_clock::now();
    auto pending = std::make_unique<Ogre14PendingCaptureState>();
    // Disjoint per-section spans measured beside the capture-struct sections.
    // They are locals rather than capture fields so the joined-scene capture
    // contract stays untouched; commit copies them onto the transaction.
    std::uint64_t section_retained_copy_ns = 0U;
    std::uint64_t section_asset_merge_ns = 0U;
    std::uint64_t section_object_union_ns = 0U;
    std::uint64_t section_particle_walk_ns = 0U;
    OgreNextDemoMaterialPendingGuard material_pending_guard(
        m_ogre_next_demo_material_source);
    Ogre14RoadMaterialFramePendingGuard road_material_frame_guard(
        m_ogre14_road_material_coordinator);

    // Static objects do not move, yet every capture walked all of them
    // through OGRE and deep-copied the static registries into this pending
    // transaction. Reuse the retained static capture when a walk could not
    // change its bytes: the inventory, cache, material decisions, and road
    // inventory are unchanged, and a scan of the retained unadmitted bounds
    // against the current camera admits nothing new. Full admission is not
    // required - on a 12 km map the capture radius never reaches it, so a
    // gate demanding it would never activate at all. Any failed condition or
    // classifier fault falls back to the full walk, which is the previous
    // behavior unchanged.
    {
        Terrain* const retention_terrain = App::GetGameContext() != nullptr
            ? App::GetGameContext()->GetTerrain().GetRef()
            : nullptr;
        TerrainObjectManager* const retention_objects =
            retention_terrain != nullptr
                ? retention_terrain->getObjectManager()
                : nullptr;
        const Gfx::Detail::OgreNextDemoMaterialSourceCounters
            retention_lifetime =
                m_ogre_next_demo_material_source.LifetimeCounters();
        // Record which condition rejects retention, so a gate that never
        // fires is a numbered fact in the log instead of a guess.
        m_ogre14_static_retention_miss_stage = 0U;
        bool retention_hit = true;
        if (!m_ogre14_static_retention_valid)
            m_ogre14_static_retention_miss_stage = 1U;
        else if (retention_objects == nullptr)
            m_ogre14_static_retention_miss_stage = 2U;
        else if (m_ogre14_static_retention_meshes_owner == nullptr ||
                 m_ogre14_static_retention_meshes_owner->empty() ||
                 m_ogre14_static_retention_assets_owner == nullptr ||
                 m_ogre14_static_retention_object_ids == nullptr)
            m_ogre14_static_retention_miss_stage = 3U;
        else if (m_ogre14_static_retention_inventory !=
                 retention_objects->GetStaticGraphicsObjects().size())
            m_ogre14_static_retention_miss_stage = 4U;
        else if (m_ogre14_static_retention_cache_size !=
                 m_ogre14_static_mesh_cache.size())
            m_ogre14_static_retention_miss_stage = 5U;
        else if (m_ogre14_static_retention_frozen_decisions !=
                 retention_lifetime.new_frozen_material_decisions)
            m_ogre14_static_retention_miss_stage = 6U;
        // The lifetime projections counter advances every capture whether or
        // not anything changed, so it cannot detect change; measured stage-7
        // misses on every frame proved it. Material change is already gated
        // by frozen decisions: decisions are immutable once made, so a new
        // section necessarily mints one.
        else if (m_ogre14_static_retention_road_live !=
                 m_ogre14_procedural_road_inventory.live_identity_count())
            m_ogre14_static_retention_miss_stage = 8U;
        else if (m_ogre14_static_retention_road_cached !=
                 m_ogre14_procedural_road_inventory.cached_mesh_count())
            m_ogre14_static_retention_miss_stage = 9U;
        retention_hit = m_ogre14_static_retention_miss_stage == 0U;
        if (retention_hit)
        {
            Render::Float3 retention_camera{};
            float retention_radius = 0.0F;
            const Render::ValidationResult retention_view =
                CaptureOgreNextDemoStaticAdmissionView(
                    retention_camera, retention_radius);
            if (!retention_view)
            {
                retention_hit = false;
                m_ogre14_static_retention_miss_stage = 10U;
            }
            else
            {
                for (const auto& unadmitted :
                     m_ogre14_static_retention_unadmitted)
                {
                    bool within_capture_radius = false;
                    const Render::ValidationResult classified =
                        Gfx::Detail::ClassifyOgreNextDemoStaticBounds(
                            unadmitted.second, retention_camera,
                            retention_radius, within_capture_radius);
                    if (!classified || within_capture_radius)
                    {
                        retention_hit = false;
                        m_ogre14_static_retention_miss_stage =
                            !classified ? 11U : 12U;
                        break;
                    }
                }
            }
        }
        if (retention_hit)
        {
            // The map-generation terrain is folded into the retained owners,
            // so its native slot identity must still hold before those owners
            // may be handed on. This is the copy-free half of what
            // CaptureCommitted performs on a miss frame.
            TerrainGeometryManager* const retention_geometry =
                retention_terrain != nullptr
                    ? retention_terrain->getGeometryManager()
                    : nullptr;
            if (!m_ogre_next_demo_terrain_source.HasCommittedCapture() ||
                !m_ogre_next_demo_terrain_source.VerifyCommittedIdentity(
                    retention_geometry != nullptr
                        ? retention_geometry->getTerrainGroup()
                        : nullptr))
            {
                retention_hit = false;
                m_ogre14_static_retention_miss_stage = 13U;
            }
        }
        pending->static_state_retained = retention_hit;
    }

    pending->light_registry = m_ogre14_light_identity_registry;
    if (!pending->static_state_retained)
    {
        pending->static_registry = m_ogre14_static_identity_registry;
        pending->static_mesh_cache = m_ogre14_static_mesh_cache;
        pending->admitted_static_objects =
            m_ogre_next_demo_admitted_static_objects;
    }
    pending->dynamic_registry = m_ogre14_dynamic_identity_registry;
    pending->dynamic_mesh_cache = m_ogre14_dynamic_mesh_cache;
    pending->particle_capture_state = m_ogre14_particle_capture_state;
    pending->automatic_reflection_probe_state =
        m_ogre14_automatic_reflection_probe_state;
    pending->particle_capture_state.captured_systems = 0U;
    pending->particle_capture_state.captured_particles = 0U;
    pending->particle_capture_state.observed_systems = 0U;
    pending->particle_capture_state.observed_particles = 0U;
    pending->particle_capture_state.deferred_inactive_systems = 0U;
    pending->particle_capture_state.excluded_systems = 0U;
    pending->particle_capture_state.excluded_particles = 0U;
    pending->particle_capture_state.excluded_non_dust_systems = 0U;
    pending->particle_capture_state.excluded_sparks_systems = 0U;
    pending->particle_capture_state.excluded_ripple_systems = 0U;
    pending->particle_capture_state.excluded_other_non_dust_systems = 0U;
    pending->particle_capture_state.excluded_billboard_modes = 0U;
    pending->particle_capture_state.excluded_local_space_systems = 0U;
    pending->particle_capture_state.excluded_animated_systems = 0U;
    pending->particle_capture_state.excluded_sorted_systems = 0U;
    pending->particle_capture_state.excluded_timing_modes = 0U;
    pending->particle_capture_state.source_backed_textures = 0U;
    pending->particle_capture_state.source_alpha_textures = 0U;
    pending->particle_capture_state.gpu_readbacks = 0U;

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
            if (pending->static_state_retained)
            {
                // Terrain lives inside the retained owners and its native
                // identity was proven by gate stage 13. Rebuilding the
                // committed capture here would copy and re-sort every page
                // asset and instance for a set that is already published.
            }
            else if (m_ogre_next_demo_terrain_source.HasCommittedCapture())
            {
                static_validation =
                    m_ogre_next_demo_terrain_source.CaptureCommitted(
                        terrain_group, terrain_capture);
            }
            else
            {
                auto terrain_page_cache_candidate =
                    m_ogre14_terrain_page_cache;
                std::vector<Gfx::Detail::OgreNextDemoTerrainPageMesh>
                    terrain_pages;
                Ogre14CaptureSectionTimer terrain_timer(candidate.terrain_ns);
                static_validation = CaptureOgre14TerrainPages(
                    geometry_manager, m_ogre14_terrain_page_cache,
                    terrain_page_cache_candidate, terrain_pages);
                if (!static_validation)
                    return static_validation;
                OgreNextDemoTerrainPendingGuard terrain_pending_guard(
                    m_ogre_next_demo_terrain_source);
                terrain_pending_guard.Arm();
                static_validation = m_ogre_next_demo_terrain_source.Capture(
                    terrain_group, terrain_pages, terrain_capture);
                if (!static_validation)
                    return static_validation;
                // Terrain is immutable for the map generation and is not part
                // of the per-frame joined material/object transaction. Commit
                // both owners now so a later material or particle rejection
                // cannot force the expensive terrain extraction and readback
                // to run again on the render thread.
                m_ogre_next_demo_terrain_source.CommitMapGenerationCapture();
                if (!m_ogre_next_demo_terrain_source.HasCommittedCapture())
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::SEQUENCE_MISMATCH,
                        "ogre_next_demo.terrain.commit",
                        "map-generation terrain publication did not commit");
                }
                using std::swap;
                swap(m_ogre14_terrain_page_cache,
                     terrain_page_cache_candidate);
                terrain_pending_guard.Release();
            }
            if (!static_validation)
                return static_validation;
            const bool material_capture_open =
                m_ogre_next_demo_material_source.BeginCapture();
            if (!material_capture_open)
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::SEQUENCE_MISMATCH,
                    "ogre_next_demo.material.pending",
                    "the private material source could not open its capture transaction");
            }
            material_pending_guard.Arm();
            std::vector<Render::GraphicsSceneAssetInput> static_assets;
            if (pending->static_state_retained)
                ++m_ogre14_static_retention_hits;
            else
                ++m_ogre14_static_retention_misses;
            if (((m_ogre14_static_retention_hits +
                  m_ogre14_static_retention_misses) % 300U) == 0U &&
                object_manager != nullptr)
            {
                LOG(fmt::format(
                    "[RoR|SceneSource|Retention] hits={} misses={} "
                    "admitted={} inventory={} cache={} miss_stage={} "
                    "owner_reuse={} owner_assets={} owner_meshes={}",
                    m_ogre14_static_retention_hits,
                    m_ogre14_static_retention_misses,
                    m_ogre_next_demo_admitted_static_objects.size(),
                    object_manager->GetStaticGraphicsObjects().size(),
                    m_ogre14_static_mesh_cache.size(),
                    m_ogre14_static_retention_miss_stage,
                    m_ogre14_static_retention_owner_handoffs,
                    m_ogre14_static_retention_assets_owner != nullptr
                        ? m_ogre14_static_retention_assets_owner->size()
                        : 0U,
                    m_ogre14_static_retention_meshes_owner != nullptr
                        ? m_ogre14_static_retention_meshes_owner->size()
                        : 0U));
            }
            if (pending->static_state_retained)
            {
                // The retained capture is byte-equivalent to what the walk
                // below would produce under the conditions verified above,
                // so the producer receives the exact immutable owners rather
                // than a copy of them. static_assets and frame.static_meshes
                // stay empty: they are the residue vectors now, and the
                // frame's authoritative set is their disjoint union with the
                // owners. Charged to its own section so `static` keeps
                // meaning the full walk.
                Ogre14CaptureSectionTimer retained_timer(
                    section_retained_copy_ns);
                candidate.frame.retained_static_assets =
                    m_ogre14_static_retention_assets_owner;
                candidate.frame.retained_static_meshes =
                    m_ogre14_static_retention_meshes_owner;
                ++m_ogre14_static_retention_owner_handoffs;
            }
            else
            {
            std::vector<std::pair<std::uint64_t, Render::Bounds3>>
                walk_unadmitted;
            Render::Float3 static_camera_position;
            float static_capture_radius_meters = 0.0F;
            static_validation = CaptureOgreNextDemoStaticAdmissionView(
                static_camera_position, static_capture_radius_meters);
            if (!static_validation)
                return static_validation;
            Ogre14CaptureSectionTimer static_timer(
                candidate.static_meshes_ns);

            // Finalized procedural roads join the same static transaction.
            // Every present road must carry its finalized snapshot; a road
            // mid-rebuild is inconsistent state and fails the capture closed.
            std::vector<Render::Ogre14ProceduralRoadCapture> road_captures;
            bool procedural_roads_adapted = false;
            if (object_manager != nullptr &&
                object_manager->HasProceduralGeometry())
            {
                ProceduralManagerPtr procedural_manager =
                    object_manager->getProceduralManager();
                if (procedural_manager != nullptr)
                {
                    const int road_object_count =
                        procedural_manager->getNumObjects();
                    for (int road_index = 0; road_index < road_object_count;
                         ++road_index)
                    {
                        ProceduralObjectPtr road_object =
                            procedural_manager->getObject(road_index);
                        if (road_object == nullptr)
                            continue;
                        ProceduralRoadPtr road = road_object->getRoad();
                        if (road == nullptr)
                            continue; // removed road: a permanent tombstone
                        if (!road->HasFinalizedGraphicsSnapshot())
                        {
                            return Render::ValidationResult::Failure(
                                Render::ValidationCode::SEQUENCE_MISMATCH,
                                "static_meshes.procedural.unfinalized",
                                "a live procedural road has no finalized "
                                "graphics snapshot");
                        }
                        road_captures.push_back(
                            road->CopyFinalizedGraphicsSnapshot());
                    }
                    procedural_roads_adapted = true;
                }
            }
            pending->procedural_road_inventory =
                m_ogre14_procedural_road_inventory;
            std::vector<Render::Ogre14GraphicsSceneStaticSectionCaptureInput>
                road_sections;
            if (procedural_roads_adapted && road_captures.empty())
            {
                // An empty complete inventory still publishes: removed roads
                // become permanent tombstones. No material is admitted here.
                static_validation = Render::BuildOgre14ProceduralRoadInventory(
                    road_captures, pending->procedural_road_inventory,
                    road_sections);
                if (!static_validation)
                    return static_validation;
            }
            else if (procedural_roads_adapted)
            {
                // Textured road2 is admitted only through its exact legacy
                // material closure: observe each live road material through
                // the authenticated native extractor, translate one complete
                // frame, and hand the road inventory that authoritative
                // frame. No factor fallback runs on this path.
                static_validation = EnsureOgre14RoadMaterialCoordinator();
                if (!static_validation)
                    return static_validation;
                ContentManager* const road_content_manager =
                    App::GetContentManager();
                if (road_content_manager == nullptr)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::MISSING_REFERENCE,
                        "road_material.content_manager",
                        "no ContentManager authority for road textures");
                }
                std::vector<Render::Ogre14LegacyMaterialObservation>
                    road_observations;
                bool used_cached_road_observation = false;
                const auto observe_road_materials =
                    [&](const bool allow_cached_observations)
                    -> Render::ValidationResult
                {
                    road_observations.clear();
                    used_cached_road_observation = false;
                    for (const Render::Ogre14ProceduralRoadCapture&
                             road_capture : road_captures)
                    {
                        Render::Ogre14LegacyAssetKey road_material_key;
                        road_material_key.exact_resource_group =
                            road_capture.material.exact_resource_group;
                        road_material_key.exact_name =
                            road_capture.material.exact_name;
                        bool already_observed = false;
                        for (const Render::Ogre14LegacyMaterialObservation&
                                 existing : road_observations)
                        {
                            if (existing.material_key == road_material_key)
                            {
                                already_observed = true;
                                break;
                            }
                        }
                        if (already_observed)
                            continue;
                        const Ogre::MaterialPtr live_road_material =
                            Ogre::MaterialManager::getSingleton().getByName(
                                road_material_key.exact_name,
                                road_material_key.exact_resource_group);
                        if (!live_road_material)
                        {
                            return Render::ValidationResult::Failure(
                                Render::ValidationCode::MISSING_REFERENCE,
                                "road.material.live_lookup",
                                "finalized road material is not loaded");
                        }
                        const std::string observation_cache_key =
                            road_material_key.exact_resource_group + '\n'
                            + road_material_key.exact_name;
                        if (allow_cached_observations)
                        {
                            const auto cached =
                                m_ogre14_road_observation_cache.find(
                                    observation_cache_key);
                            if (cached != m_ogre14_road_observation_cache.end()
                                && cached->second.material_state_count
                                    == static_cast<std::uint64_t>(
                                        live_road_material->getStateCount()))
                            {
                                road_observations.push_back(
                                    cached->second.observation);
                                used_cached_road_observation = true;
                                continue;
                            }
                        }
                        Render::Ogre14LegacyMaterialObservation observation;
                        observation.material_key = road_material_key;
                        Render::ValidationResult observation_validation =
                            m_ogre14_road_material_coordinator->
                                ResolveMaterialSemantics(
                                    road_material_key,
                                    observation.semantic_resolution);
                        if (!observation_validation)
                            return observation_validation;
                        // Producer-side RTT renders (survey map, envmap) let
                        // the RTSS resolver append generated techniques, while
                        // the authenticated extractor requires the authored
                        // single technique. Strip generated techniques through
                        // the RTSS bookkeeping for the duration of this
                        // capture; the resolver regenerates them lazily on the
                        // next render.
                        if (live_road_material->getNumTechniques() > 1)
                        {
                            Ogre::RTShader::ShaderGenerator* const
                                shader_generator = Ogre::RTShader::
                                    ShaderGenerator::getSingletonPtr();
                            if (shader_generator != nullptr)
                            {
                                shader_generator->
                                    removeAllShaderBasedTechniques(
                                        road_material_key.exact_name,
                                        road_material_key
                                            .exact_resource_group);
                            }
                        }
                        observation_validation =
                            Render::CaptureOgre14LegacyNativeMaterial(
                                *live_road_material,
                                observation.semantic_resolution
                                    .native_declaration,
                                *road_content_manager,
                                observation.native_capture);
                        if (!observation_validation)
                            return observation_validation;
                        if (!observation.native_capture.textures.empty() &&
                            !observation.native_capture.textures.front()
                                 .mip_levels.empty())
                        {
                            // Content probe: the readback bytes ship verbatim
                            // into the closure. A decoded-on-readback sRGB
                            // texture would arrive here linear and be decoded
                            // again by the presenter, landing near black.
                            const std::vector<std::uint8_t>& probe_bytes =
                                observation.native_capture.textures.front()
                                    .mip_levels.front().bytes;
                            std::uint64_t probe_sum = 0U;
                            std::size_t probe_samples = 0U;
                            for (std::size_t index = 0U;
                                 index + 3U < probe_bytes.size(); index += 4U)
                            {
                                probe_sum += probe_bytes[index];
                                probe_sum += probe_bytes[index + 1U];
                                probe_sum += probe_bytes[index + 2U];
                                probe_samples += 3U;
                            }
                            LOG(fmt::format(
                                "[RoR|SceneSource|RoadMaterial] observed "
                                "texture '{}' mip0 rgb_mean={}",
                                observation.native_capture.textures.front()
                                    .key.exact_name,
                                probe_samples == 0U
                                    ? 0U
                                    : probe_sum / probe_samples));
                        }
                        // Cache the finished observation for later walks. The
                        // state count is read after the RTSS strip so a hit
                        // means the material is byte-identical to capture
                        // time; any mutation (including regenerated RTSS
                        // techniques) forces a fresh capture.
                        Ogre14RoadMaterialObservationCacheEntry cache_entry;
                        cache_entry.material_state_count =
                            static_cast<std::uint64_t>(
                                live_road_material->getStateCount());
                        cache_entry.observation = observation;
                        m_ogre14_road_observation_cache
                            [observation_cache_key] = std::move(cache_entry);
                        road_observations.push_back(std::move(observation));
                    }
                    return Render::ValidationResult::Success();
                };
                static_validation = observe_road_materials(true);
                if (!static_validation)
                    return static_validation;
                Render::Ogre14LegacyPreparedMaterialFrame road_material_frame;
                static_validation =
                    m_ogre14_road_material_coordinator->PrepareFrame(
                        m_ogre14_road_material_coordinator->source_sequence()
                            + 1U,
                        road_observations, road_material_frame);
                if (!static_validation && used_cached_road_observation)
                {
                    // Fail closed on cache staleness: a receipt-registry
                    // publication since capture time invalidates the cached
                    // loaded-resource resolutions, and PrepareFrame is the
                    // authority that notices. Drop the cache, re-observe
                    // every material live, and retry exactly once; a fresh
                    // failure then propagates normally.
                    m_ogre14_road_observation_cache.clear();
                    static_validation = observe_road_materials(false);
                    if (!static_validation)
                        return static_validation;
                    static_validation =
                        m_ogre14_road_material_coordinator->PrepareFrame(
                            m_ogre14_road_material_coordinator
                                ->source_sequence() + 1U,
                            road_observations, road_material_frame);
                }
                if (!static_validation)
                    return static_validation;
                road_material_frame_guard.Arm();
                for (Render::Ogre14ProceduralRoadCapture& road_capture :
                     road_captures)
                {
                    const Render::Ogre14LegacyPreparedMaterial*
                        prepared_material = nullptr;
                    for (const Render::Ogre14LegacyPreparedMaterial&
                             candidate_material :
                         road_material_frame.materials())
                    {
                        if (candidate_material.material_key.exact_resource_group
                                == road_capture.material.exact_resource_group
                            && candidate_material.material_key.exact_name
                                == road_capture.material.exact_name)
                        {
                            prepared_material = &candidate_material;
                            break;
                        }
                    }
                    if (prepared_material == nullptr)
                    {
                        return Render::ValidationResult::Failure(
                            Render::ValidationCode::MISSING_REFERENCE,
                            "road_material.prepared_lookup",
                            "captured road has no prepared material closure");
                    }
                    // Retain the exact extractor-minted native owner. The
                    // closure's own audit must never be assigned here: a
                    // shared control block is rejected as laundering.
                    road_capture.exact_native_material_audit =
                        prepared_material->native_material_audit;
                }
                const Render::Ogre14LegacyTranslatedFrame* const
                    road_translated_frame =
                        road_material_frame.translated_frame();
                if (road_translated_frame == nullptr)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::MISSING_REFERENCE,
                        "road_material.translated_frame",
                        "prepared road material frame has no translation");
                }
                static_validation = Render::BuildOgre14ProceduralRoadInventory(
                    road_captures, *road_translated_frame,
                    pending->procedural_road_inventory, road_sections);
                if (!static_validation)
                    return static_validation;
                pending->road_material_frame = std::move(road_material_frame);
                pending->has_road_material_frame = true;
            }

            static_validation =
                CaptureOgre14StaticMeshObjects(
                    object_manager,
                    static_camera_position,
                    static_capture_radius_meters,
                    m_ogre_next_demo_material_source,
                    pending->admitted_static_objects,
                    pending->static_registry,
                    pending->static_mesh_cache,
                    road_sections,
                    procedural_roads_adapted,
                    walk_unadmitted,
                    static_assets,
                    candidate.frame.static_meshes);
            if (!static_validation)
            {
                return static_validation;
            }
            // Census: name every admitted material that would render black
            // or exposure-crushed on the presenter - an UNLIT model in the
            // HDR frame, or a near-zero base color factor. These are
            // invisible to every validation because they are legal.
            // Diagnostics only: byte-scanning texture payloads on every
            // walk is measurable frame cost, so the whole block is opt-in.
            static const bool census_enabled =
                std::getenv("ROR_SCENE_CENSUS") != nullptr;
            if (census_enabled)
            {
                std::size_t census_logged = 0U;
                for (const Render::GraphicsSceneAssetInput& census_asset :
                     static_assets)
                {
                    if (census_logged >= 12U)
                        break;
                    if (census_asset.payload == nullptr ||
                        !std::holds_alternative<Render::MaterialDescriptor>(
                            *census_asset.payload))
                        continue;
                    const Render::MaterialDescriptor& census_material =
                        std::get<Render::MaterialDescriptor>(
                            *census_asset.payload);
                    const bool census_unlit = census_material.model ==
                        Render::MaterialModel::UNLIT;
                    const bool census_black =
                        census_material.base_color_factor.x <= 0.05F &&
                        census_material.base_color_factor.y <= 0.05F &&
                        census_material.base_color_factor.z <= 0.05F;
                    if (!census_unlit && !census_black)
                        continue;
                    LOG(fmt::format(
                        "[RoR|SceneSource|MaterialCensus] '{}' model={} "
                        "base_color=({:.3f},{:.3f},{:.3f})",
                        census_material.debug_name,
                        census_unlit ? "UNLIT" : "PBR",
                        census_material.base_color_factor.x,
                        census_material.base_color_factor.y,
                        census_material.base_color_factor.z));
                    ++census_logged;
                }
                // Texture-content census: a projected section carries a
                // white factor, so a dark BOUND texture is the remaining
                // way a legal admitted surface renders near black.
                std::size_t texture_census_logged = 0U;
                for (const Render::GraphicsSceneAssetInput& census_asset :
                     static_assets)
                {
                    if (texture_census_logged >= 10U)
                        break;
                    if (census_asset.payload == nullptr ||
                        !std::holds_alternative<
                            Render::TextureResourceDescriptor>(
                            *census_asset.payload))
                        continue;
                    const Render::TextureResourceDescriptor& census_texture =
                        std::get<Render::TextureResourceDescriptor>(
                            *census_asset.payload);
                    if (census_texture.mip_levels.empty())
                        continue;
                    const std::vector<std::uint8_t>& census_bytes =
                        census_texture.mip_levels.front().bytes;
                    std::uint64_t census_sum = 0U;
                    std::size_t census_samples = 0U;
                    for (std::size_t index = 0U;
                         index + 3U < census_bytes.size(); index += 4U)
                    {
                        census_sum += census_bytes[index];
                        census_sum += census_bytes[index + 1U];
                        census_sum += census_bytes[index + 2U];
                        census_samples += 3U;
                    }
                    const std::uint64_t census_mean = census_samples == 0U
                        ? 0U
                        : census_sum / census_samples;
                    if (census_mean > 48U)
                        continue;
                    LOG(fmt::format(
                        "[RoR|SceneSource|TextureCensus] '{}' {}x{} "
                        "rgb_mean={}",
                        census_texture.debug_name, census_texture.width,
                        census_texture.height, census_mean));
                    ++texture_census_logged;
                }
                // Proximity census: name what actually stands in front of
                // the spawn camera. The black surface fills the near field,
                // so it is one of these instances.
                std::map<std::uint64_t, bool> census_material_bindings;
                std::map<std::uint64_t, const Render::MaterialDescriptor*>
                    census_materials;
                std::map<std::uint64_t, const Render::MeshResourceDescriptor*>
                    census_meshes;
                for (const Render::GraphicsSceneAssetInput& census_asset :
                     static_assets)
                {
                    if (census_asset.payload == nullptr)
                        continue;
                    if (std::holds_alternative<Render::MaterialDescriptor>(
                            *census_asset.payload))
                    {
                        census_materials[census_asset.source_asset_id] =
                            &std::get<Render::MaterialDescriptor>(
                                *census_asset.payload);
                        census_material_bindings[
                            census_asset.source_asset_id] =
                            census_asset.material_bindings[static_cast<
                                std::size_t>(
                                Render::MaterialTextureSlot::BASE_COLOR)]
                                .texture_source_asset_id != 0U;
                    }
                    else if (std::holds_alternative<
                                 Render::MeshResourceDescriptor>(
                                 *census_asset.payload))
                        census_meshes[census_asset.source_asset_id] =
                            &std::get<Render::MeshResourceDescriptor>(
                                *census_asset.payload);
                }
                // One-shot full inventory dump for offline analysis:
                // every instance with joined names, factors, and binding.
                static bool census_dumped = false;
                if (!census_dumped)
                {
                    census_dumped = true;
                    std::FILE* const dump = std::fopen(
                        "/private/tmp/ror-frame-inventory.txt", "w");
                    if (dump != nullptr)
                    {
                        std::fprintf(
                            dump, "camera %.2f %.2f %.2f\n",
                            static_cast<double>(static_camera_position.x),
                            static_cast<double>(static_camera_position.y),
                            static_cast<double>(static_camera_position.z));
                        for (const Render::GraphicsSceneStaticMeshInput&
                                 dump_instance :
                             candidate.frame.static_meshes)
                        {
                            const auto dump_material = census_materials.find(
                                dump_instance.material_source_asset_id);
                            const auto dump_mesh = census_meshes.find(
                                dump_instance.mesh_source_asset_id);
                            const bool dump_textured =
                                census_material_bindings.count(
                                    dump_instance
                                        .material_source_asset_id) != 0U &&
                                census_material_bindings.at(
                                    dump_instance.material_source_asset_id);
                            std::fprintf(
                                dump,
                                "%.1f %.1f %.1f|%s|%s|%s|%.3f %.3f %.3f|%d\n",
                                static_cast<double>(
                                    dump_instance.render_from_object
                                        .elements[12U]),
                                static_cast<double>(
                                    dump_instance.render_from_object
                                        .elements[13U]),
                                static_cast<double>(
                                    dump_instance.render_from_object
                                        .elements[14U]),
                                dump_mesh != census_meshes.end()
                                    ? dump_mesh->second->debug_name.c_str()
                                    : "?",
                                dump_material != census_materials.end()
                                    ? dump_material->second->debug_name
                                          .c_str()
                                    : "?",
                                dump_material != census_materials.end() &&
                                        dump_material->second->model ==
                                            Render::MaterialModel::UNLIT
                                    ? "UNLIT"
                                    : "PBR",
                                dump_material != census_materials.end()
                                    ? static_cast<double>(
                                          dump_material->second
                                              ->base_color_factor.x)
                                    : -1.0,
                                dump_material != census_materials.end()
                                    ? static_cast<double>(
                                          dump_material->second
                                              ->base_color_factor.y)
                                    : -1.0,
                                dump_material != census_materials.end()
                                    ? static_cast<double>(
                                          dump_material->second
                                              ->base_color_factor.z)
                                    : -1.0,
                                dump_textured ? 1 : 0);
                        }
                        std::fclose(dump);
                    }
                }
                std::size_t proximity_logged = 0U;
                for (const Render::GraphicsSceneStaticMeshInput&
                         census_instance : candidate.frame.static_meshes)
                {
                    if (proximity_logged >= 14U)
                        break;
                    const float dx =
                        census_instance.render_from_object.elements[12U] -
                        static_camera_position.x;
                    const float dy =
                        census_instance.render_from_object.elements[13U] -
                        static_camera_position.y;
                    const float dz =
                        census_instance.render_from_object.elements[14U] -
                        static_camera_position.z;
                    const float distance_squared =
                        (dx * dx) + (dy * dy) + (dz * dz);
                    if (distance_squared > 40.0F * 40.0F)
                        continue;
                    const auto census_material = census_materials.find(
                        census_instance.material_source_asset_id);
                    const auto census_mesh = census_meshes.find(
                        census_instance.mesh_source_asset_id);
                    LOG(fmt::format(
                        "[RoR|SceneSource|ProximityCensus] d={:.1f} mesh='{}'"
                        " material='{}' model={} base=({:.2f},{:.2f},{:.2f})"
                        " textured={}",
                        std::sqrt(distance_squared),
                        census_mesh != census_meshes.end()
                            ? census_mesh->second->debug_name
                            : "?",
                        census_material != census_materials.end()
                            ? census_material->second->debug_name
                            : "?",
                        census_material != census_materials.end()
                            ? (census_material->second->model ==
                                       Render::MaterialModel::UNLIT
                                   ? "UNLIT"
                                   : "PBR")
                            : "?",
                        census_material != census_materials.end()
                            ? census_material->second->base_color_factor.x
                            : -1.0F,
                        census_material != census_materials.end()
                            ? census_material->second->base_color_factor.y
                            : -1.0F,
                        census_material != census_materials.end()
                            ? census_material->second->base_color_factor.z
                            : -1.0F,
                        census_material_bindings.count(
                            census_instance.material_source_asset_id) != 0U &&
                            census_material_bindings.at(
                                census_instance.material_source_asset_id)));
                    ++proximity_logged;
                }
            }
            // Stash the retention refresh on the pending transaction. It is
            // applied only when this frame commits, so a capture a later
            // section discards can never leave the retained scene describing
            // state that was never published.
            {
                const Gfx::Detail::OgreNextDemoMaterialSourceCounters
                    retention_lifetime =
                        m_ogre_next_demo_material_source.LifetimeCounters();
                // The owners themselves are built after terrain has joined
                // the union below; only the walk-scoped facts are stashed
                // here, where walk_unadmitted is still in scope.
                pending->retention_unadmitted = std::move(walk_unadmitted);
                pending->retention_inventory = object_manager != nullptr
                    ? object_manager->GetStaticGraphicsObjects().size()
                    : 0U;
                pending->retention_cache_size =
                    pending->static_mesh_cache.size();
                pending->retention_road_live =
                    pending->procedural_road_inventory.live_identity_count();
                pending->retention_road_cached =
                    pending->procedural_road_inventory.cached_mesh_count();
                pending->retention_frozen_decisions =
                    retention_lifetime.new_frozen_material_decisions;
                pending->retention_projections =
                    retention_lifetime.projections;
                pending->has_retention_refresh = true;
            }
            }

            // GfxCharacter is the legacy player/network avatar domain. It is
            // neither an authored static MeshObject nor a GfxActor deformable;
            // character.mesh remains legacy-only until its own skeletal-pose
            // adapter exists. Its normal presence therefore cannot poison the
            // complete supported static + actor-dynamic inventory in this
            // transaction.

            std::vector<Render::GraphicsSceneAssetInput> dynamic_assets;
            Render::ValidationResult dynamic_validation;
            {
                // The section timer must end with the joined staging copy it
                // names. Left unbraced it lived to the end of the enclosing
                // scene-manager block, so `dynamic=` also billed the merges,
                // the particle walk, the duplicate set, the terrain append,
                // the sort, lights, and the environment.
                Ogre14CaptureSectionTimer dynamic_timer(
                    candidate.dynamic_meshes_ns);
                dynamic_validation = CaptureOgre14DynamicActorInventory(
                    pending->dynamic_registry,
                    pending->dynamic_mesh_cache,
                    dynamic_assets, candidate.frame.dynamic_meshes);
            }
            if (!dynamic_validation)
                return dynamic_validation;
            std::vector<Render::GraphicsSceneAssetInput> nonterrain_assets;
            const std::vector<Render::GraphicsSceneAssetInput> empty_assets;
            {
                Ogre14CaptureSectionTimer merge_timer(section_asset_merge_ns);
                dynamic_validation = Render::MergeOgre14GraphicsSceneAssets(
                    static_assets, dynamic_assets, empty_assets,
                    nonterrain_assets);
            }
            if (!dynamic_validation)
                return dynamic_validation;

            const std::chrono::steady_clock::time_point
                particle_walk_started = std::chrono::steady_clock::now();
            std::vector<Render::Ogre14ParticleSourceSystemCapture>
                captured_dust_systems;
            std::uint64_t dust_material_source_id = 0U;
            if (m_scene_manager != nullptr)
            {
                const Ogre::SceneManager::MovableObjectMap& particle_objects =
                    m_scene_manager->getMovableObjects(
                        Ogre::MOT_PARTICLE_SYSTEM);
                for (const auto& entry : particle_objects)
                {
                    Ogre::ParticleSystem* const psys =
                        dynamic_cast<Ogre::ParticleSystem*>(entry.second);
                    if (psys == nullptr)
                        continue;
                    const std::uint64_t native_particle_count =
                        psys->_getActiveParticles().size();
                    if (pending->particle_capture_state.observed_systems >=
                            kOgreNextDemoMaximumObservedParticleSystems ||
                        native_particle_count >
                            kOgreNextDemoMaximumParticlesPerSystem ||
                        pending->particle_capture_state.observed_particles >
                            kOgreNextDemoMaximumObservedParticles -
                                native_particle_count)
                    {
                        return Render::ValidationResult::Failure(
                            Render::ValidationCode::VALUE_OUT_OF_RANGE,
                            "continuous_particles.observed_limits",
                            "OGRE14 particle inventory exceeds the bounded joined capture limits");
                    }
                    ++pending->particle_capture_state.observed_systems;
                    pending->particle_capture_state.observed_particles +=
                        native_particle_count;
                    const std::string& native_name = psys->getName();
                    if (native_name.rfind("Dust tracks/Dust ", 0U) != 0U ||
                        psys->getMaterialName() != "tracks/SmokeMat")
                    {
                        ++pending->particle_capture_state.excluded_systems;
                        pending->particle_capture_state.excluded_particles +=
                            native_particle_count;
                        ++pending->particle_capture_state
                              .excluded_non_dust_systems;
                        if (native_name.rfind("Dust tracks/Sparks ", 0U) ==
                            0U)
                        {
                            ++pending->particle_capture_state
                                  .excluded_sparks_systems;
                        }
                        else if (native_name.rfind(
                                     "Dust tracks/Ripple ", 0U) == 0U)
                        {
                            ++pending->particle_capture_state
                                  .excluded_ripple_systems;
                        }
                        else
                        {
                            ++pending->particle_capture_state
                                  .excluded_other_non_dust_systems;
                        }
                        continue;
                    }
                    Ogre::BillboardParticleRenderer* const renderer =
                        dynamic_cast<Ogre::BillboardParticleRenderer*>(
                            psys->getRenderer());
                    if (renderer == nullptr ||
                        renderer->getBillboardType() != Ogre::BBT_POINT ||
                        renderer->isPointRenderingEnabled() ||
                        renderer->getBillboardOrigin() != Ogre::BBO_CENTER ||
                        renderer->getBillboardRotationType() !=
                            Ogre::BBR_TEXCOORD ||
                        renderer->getUseAccurateFacing())
                    {
                        ++pending->particle_capture_state.excluded_systems;
                        pending->particle_capture_state.excluded_particles +=
                            native_particle_count;
                        ++pending->particle_capture_state
                              .excluded_billboard_modes;
                        continue;
                    }
                    if (psys->getKeepParticlesInLocalSpace())
                    {
                        ++pending->particle_capture_state.excluded_systems;
                        pending->particle_capture_state.excluded_particles +=
                            native_particle_count;
                        ++pending->particle_capture_state
                              .excluded_local_space_systems;
                        continue;
                    }
                    const Ogre::Vector2 stacks_slices =
                        renderer->getTextureStacksAndSlices();
                    if (stacks_slices.x != 1.0F || stacks_slices.y != 1.0F)
                    {
                        ++pending->particle_capture_state.excluded_systems;
                        pending->particle_capture_state.excluded_particles +=
                            native_particle_count;
                        ++pending->particle_capture_state
                              .excluded_animated_systems;
                        continue;
                    }
                    if (psys->getSortingEnabled())
                    {
                        ++pending->particle_capture_state.excluded_systems;
                        pending->particle_capture_state.excluded_particles +=
                            native_particle_count;
                        ++pending->particle_capture_state
                              .excluded_sorted_systems;
                        continue;
                    }
                    const auto timing_observation =
                        m_ogre14_particle_update_timings.find(
                            native_name);
                    if (psys->getIterationInterval() != 0.0F ||
                        Ogre::ParticleSystem::getDefaultIterationInterval() !=
                            0.0F ||
                        !IsFiniteOgreRealBits(psys->getSpeedFactor()) ||
                        psys->getSpeedFactor() < 0.0F ||
                        timing_observation ==
                            m_ogre14_particle_update_timings.end() ||
                        !timing_observation->second.valid)
                    {
                        ++pending->particle_capture_state.excluded_systems;
                        pending->particle_capture_state.excluded_particles +=
                            native_particle_count;
                        ++pending->particle_capture_state
                              .excluded_timing_modes;
                        continue;
                    }
                    const std::uint64_t current_native_update_count =
                        timing_observation->second.native_update_count;
                    const float latest_native_effective_interval_seconds =
                        timing_observation->second
                            .latest_effective_interval_seconds;

                    bool native_emitting = false;
                    for (unsigned short emitter_index = 0U;
                         emitter_index < psys->getNumEmitters();
                         ++emitter_index)
                    {
                        Ogre::ParticleEmitter* const emitter =
                            psys->getEmitter(emitter_index);
                        native_emitting = native_emitting ||
                            (emitter != nullptr && emitter->getEnabled());
                    }

                    auto system_identity =
                        pending->particle_capture_state.systems.find(
                            native_name);
                    const bool was_previously_admitted =
                        system_identity != pending->particle_capture_state
                                               .systems.end();
                    const Render::Ogre14ParticleSystemAdmissionDecision
                        admission = Render::
                            ClassifyOgre14ParticleSystemAdmission(
                                was_previously_admitted,
                                native_emitting, native_particle_count);
                    if (admission == Render::
                            Ogre14ParticleSystemAdmissionDecision::
                                DEFER_INACTIVE_FIRST_OBSERVATION)
                    {
                        ++pending->particle_capture_state
                              .deferred_inactive_systems;
                        continue;
                    }
                    if (system_identity ==
                        pending->particle_capture_state.systems.end())
                    {
                        if (pending->particle_capture_state.next_system_id ==
                            (std::numeric_limits<std::uint64_t>::max)())
                        {
                            return Render::ValidationResult::Failure(
                                Render::ValidationCode::VALUE_OUT_OF_RANGE,
                                "continuous_particles.system_id",
                                "Dust system identity space is exhausted");
                        }
                        GfxScene::Ogre14DustSystemIdentity identity;
                        identity.system_id = pending->particle_capture_state
                                                 .next_system_id++;
                        identity.last_native_update_count =
                            current_native_update_count;
                        system_identity = pending->particle_capture_state
                                              .systems.emplace(
                                                  native_name,
                                                  std::move(identity))
                                              .first;
                    }

                    Render::Ogre14ParticleSourceSystemCapture system;
                    system.system_id = system_identity->second.system_id;
                    system.effect = Render::ParticleEffect::DUST;
                    system.billboard_mode = Render::
                        Ogre14ParticleBillboardMode::CAMERA_FACING_POINT;
                    system.billboard_rotation_mode = Render::
                        Ogre14ParticleBillboardRotationMode::
                            TEXTURE_COORDINATES;
                    system.particles_are_world_space = true;
                    system.requires_frontend_emitter_evaluation = false;
                    system.requires_frontend_affector_evaluation = false;
                    system.requires_frontend_sorting = false;
                    system.requires_texture_animation = false;
                    system.system_visible = psys->getVisible();
                    Ogre::SceneNode* const parent =
                        psys->getParentSceneNode();
                    system.parent_visible = parent != nullptr &&
                        parent->isInSceneGraph();
                    system.emitting = native_emitting;

                    std::map<std::uintptr_t,
                             GfxScene::Ogre14DustParticleIdentity>
                        active_particle_identities;
                    const std::vector<Ogre::Particle*>& native_particles =
                        psys->_getActiveParticles();
                    system.particles.reserve(native_particles.size());
                    bool animated_particle = false;
                    for (Ogre::Particle* const native_particle :
                         native_particles)
                    {
                        if (native_particle == nullptr ||
                            native_particle->mParticleType !=
                                Ogre::Particle::Visual ||
                            native_particle->mTexcoordIndex != 0U ||
                            native_particle->mRandomTexcoordOffset != 0U)
                        {
                            animated_particle = true;
                            break;
                        }
                        const float lifetime = static_cast<float>(
                            native_particle->mTotalTimeToLive);
                        const float age = lifetime - static_cast<float>(
                            native_particle->mTimeToLive);
                        const float remaining = static_cast<float>(
                            native_particle->mTimeToLive);
                        const std::uintptr_t pointer_token =
                            reinterpret_cast<std::uintptr_t>(native_particle);
                        auto prior_particle = system_identity->second
                                                  .active_particles.find(
                                                      pointer_token);
                        std::uint64_t particle_id = 0U;
                        if (prior_particle != system_identity->second
                                                  .active_particles.end() &&
                            Render::CanRetainOgre14ParticlePoolIdentity(
                                prior_particle->second.age_seconds,
                                prior_particle->second.lifetime_seconds,
                                prior_particle->second.remaining_seconds,
                                system_identity->second
                                    .last_native_update_count,
                                age, lifetime, remaining,
                                current_native_update_count,
                                latest_native_effective_interval_seconds))
                        {
                            particle_id =
                                prior_particle->second.particle_id;
                        }
                        else
                        {
                            if (system_identity->second.next_particle_id ==
                                (std::numeric_limits<std::uint64_t>::max)())
                            {
                                return Render::ValidationResult::Failure(
                                    Render::ValidationCode::
                                        VALUE_OUT_OF_RANGE,
                                    "continuous_particles.particle_id",
                                    "Dust particle identity space is exhausted");
                            }
                            particle_id = system_identity->second
                                              .next_particle_id++;
                        }
                        active_particle_identities.emplace(
                            pointer_token,
                            GfxScene::Ogre14DustParticleIdentity{
                                particle_id, age, lifetime, remaining});

                        const Ogre::Vector3 velocity =
                            native_particle->mDirection;
                        Ogre::Vector3 direction = velocity;
                        if (direction.squaredLength() > 0.0F)
                            direction.normalise();
                        else
                            direction = Ogre::Vector3::UNIT_Y;
                        std::array<std::uint8_t, 4U> native_colour_bytes{};
                        static_assert(sizeof(native_particle->mColour) ==
                                      sizeof(native_colour_bytes));
                        std::memcpy(native_colour_bytes.data(),
                                    &native_particle->mColour,
                                    sizeof(native_particle->mColour));
                        Render::Ogre14ParticleState particle;
                        particle.particle_id = particle_id;
                        particle.position = {
                            static_cast<float>(native_particle->mPosition.x),
                            static_cast<float>(native_particle->mPosition.y),
                            static_cast<float>(native_particle->mPosition.z)};
                        particle.direction = {
                            static_cast<float>(direction.x),
                            static_cast<float>(direction.y),
                            static_cast<float>(direction.z)};
                        particle.velocity = {
                            static_cast<float>(velocity.x),
                            static_cast<float>(velocity.y),
                            static_cast<float>(velocity.z)};
                        particle.color_linear = Render::
                            DecodeOgre14ParticleColourBytes(
                                native_colour_bytes);
                        particle.size_meters = {
                            static_cast<float>(native_particle->mWidth),
                            static_cast<float>(native_particle->mHeight)};
                        particle.rotation_radians = static_cast<float>(
                            native_particle->mRotation.valueRadians());
                        particle.age_seconds = age;
                        particle.lifetime_seconds = lifetime;
                        system.particles.push_back(particle);
                    }
                    if (animated_particle)
                    {
                        ++pending->particle_capture_state.excluded_systems;
                        pending->particle_capture_state.excluded_particles +=
                            native_particle_count;
                        ++pending->particle_capture_state
                              .excluded_animated_systems;
                        continue;
                    }
                    system_identity->second.active_particles =
                        std::move(active_particle_identities);
                    system_identity->second.last_native_update_count =
                        current_native_update_count;
                    std::sort(system.particles.begin(),
                              system.particles.end(),
                        [](const auto& lhs, const auto& rhs)
                        {
                            return lhs.particle_id < rhs.particle_id;
                        });
                    captured_dust_systems.push_back(std::move(system));
                }
            }

            if (!captured_dust_systems.empty())
            {
                Ogre::MaterialPtr dust_material =
                    Ogre::MaterialManager::getSingleton().getByName(
                        "tracks/SmokeMat");
                Render::Ogre14GraphicsSceneMaterialCaptureInput
                    placeholder_input;
                bool reverse_winding = false;
                bool used_matte = false;
                dynamic_validation = CaptureOgreNextDemoMaterialInput(
                    dust_material, placeholder_input, reverse_winding,
                    used_matte);
                if (!dynamic_validation)
                    return dynamic_validation;
                bool projected = false;
                dynamic_validation = m_ogre_next_demo_material_source
                    .TryProject("particle/tracks/Dust", dust_material,
                                used_matte, true, placeholder_input,
                                projected);
                if (!dynamic_validation)
                    return dynamic_validation;
                if (!projected)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::UNSUPPORTED_FEATURE,
                        "continuous_particles.tracks_Dust.material",
                        "tracks/Dust requires exact source-backed tracks/SmokeMat; matte and GPU readback are forbidden");
                }
                dynamic_validation = Render::
                    DeriveOgre14GraphicsSceneMaterialAssetId(
                        placeholder_input.exact_resource_group,
                        placeholder_input.exact_name,
                        dust_material_source_id);
                if (!dynamic_validation)
                    return dynamic_validation;
                Render::MaterialDescriptor placeholder;
                dynamic_validation = Render::
                    BuildOgre14GraphicsSceneMaterialFallback(
                        placeholder_input, placeholder);
                if (!dynamic_validation)
                    return dynamic_validation;
                Render::GraphicsSceneAssetInput material_asset;
                material_asset.source_asset_id = dust_material_source_id;
                material_asset.payload =
                    std::make_shared<const Render::RenderAssetPayload>(
                        std::move(placeholder));
                nonterrain_assets.push_back(std::move(material_asset));
            }
            if (material_capture_open)
            {
                dynamic_validation =
                    m_ogre_next_demo_material_source.Apply(
                        nonterrain_assets);
                if (!dynamic_validation)
                    return dynamic_validation;
                pending->new_material_projection_count =
                    m_ogre_next_demo_material_source.NewProjectionCount();
                pending->active_material_projection_count =
                    m_ogre_next_demo_material_source.UsedProjectionCount();
                pending->material_source_counters =
                    m_ogre_next_demo_material_source.CurrentCaptureCounters();
                pending->curated_cityworld_material_coverage =
                    m_ogre_next_demo_material_source
                        .CurrentCuratedCityWorldCoverage();
            }
            if (!captured_dust_systems.empty())
            {
                const auto dust_material_asset = std::find_if(
                    nonterrain_assets.begin(), nonterrain_assets.end(),
                    [dust_material_source_id](const auto& asset)
                    {
                        return asset.source_asset_id ==
                            dust_material_source_id;
                    });
                if (dust_material_asset == nonterrain_assets.end() ||
                    dust_material_asset->payload == nullptr ||
                    Render::RenderAssetPayloadKind(
                        *dust_material_asset->payload) !=
                        Render::RenderAssetKind::MATERIAL)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::MISSING_REFERENCE,
                        "continuous_particles.tracks_Dust.material",
                        "projected SmokeMat asset disappeared before joined publication");
                }
                const Render::GraphicsSceneAssetBinding& base_binding =
                    dust_material_asset->material_bindings[
                        static_cast<std::size_t>(
                            Render::MaterialTextureSlot::BASE_COLOR)];
                if (base_binding.texture_source_asset_id == 0U ||
                    base_binding.sampler_source_asset_id == 0U)
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::MISSING_REFERENCE,
                        "continuous_particles.tracks_Dust.source_assets",
                        "SmokeMat did not retain exact smoke.dds and clamp sampler source identities");
                }
                const auto dust_texture_asset = std::find_if(
                    nonterrain_assets.begin(), nonterrain_assets.end(),
                    [&base_binding](const auto& asset)
                    {
                        return asset.source_asset_id ==
                            base_binding.texture_source_asset_id;
                    });
                const Render::TextureResourceDescriptor* dust_texture =
                    dust_texture_asset != nonterrain_assets.end() &&
                            dust_texture_asset->payload != nullptr
                        ? std::get_if<Render::TextureResourceDescriptor>(
                              dust_texture_asset->payload.get())
                        : nullptr;
                const auto dust_sampler_asset = std::find_if(
                    nonterrain_assets.begin(), nonterrain_assets.end(),
                    [&base_binding](const auto& asset)
                    {
                        return asset.source_asset_id ==
                            base_binding.sampler_source_asset_id;
                    });
                const Render::SamplerResourceDescriptor* dust_sampler =
                    dust_sampler_asset != nonterrain_assets.end() &&
                            dust_sampler_asset->payload != nullptr
                        ? std::get_if<Render::SamplerResourceDescriptor>(
                              dust_sampler_asset->payload.get())
                        : nullptr;
                if (dust_texture == nullptr ||
                    dust_sampler == nullptr ||
                    !HasOgreNextDemoDustSourceAlpha(*dust_texture) ||
                    !IsOgreNextDemoDustSampler(
                        *dust_sampler, dust_texture->mip_levels.size()))
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::REVISION_MISMATCH,
                        "continuous_particles.tracks_Dust.source_alpha",
                        "source-backed smoke.dds lost exact sRGB alpha or clamp/anisotropy state during normalization");
                }
                for (Render::Ogre14ParticleSourceSystemCapture& system :
                     captured_dust_systems)
                {
                    system.material_closure.material_source_asset_id =
                        dust_material_source_id;
                    system.material_closure.texture_source_asset_id =
                        base_binding.texture_source_asset_id;
                    system.material_closure.sampler_source_asset_id =
                        base_binding.sampler_source_asset_id;
                    system.material_closure.blend = Render::
                        ContinuousParticleBlendMode::
                            LEGACY_STRAIGHT_ALPHA;
                    system.material_closure.alpha_reject = Render::
                        ContinuousParticleAlphaReject::GREATER;
                    system.material_closure.alpha_reject_threshold =
                        2.0F / 255.0F;
                    system.material_closure.sort_policy = Render::
                        ContinuousParticleSortPolicy::STABLE_PARTICLE_ID;
                    system.material_closure.depth_check = true;
                    system.material_closure.depth_write = false;
                    system.material_closure.lighting_enabled = false;
                    system.material_closure.receives_shadows = false;
                    system.material_closure.casts_shadows = false;
                    system.material_closure.vertex_color_modulation = true;
                    system.material_closure.source_backed_texture = true;
                    system.material_closure.gpu_readback_used = false;
                }
                pending->particle_capture_state.source_backed_textures = 1U;
                pending->particle_capture_state.source_alpha_textures = 1U;
            }

            std::sort(captured_dust_systems.begin(),
                      captured_dust_systems.end(),
                [](const auto& lhs, const auto& rhs)
                {
                    return lhs.system_id < rhs.system_id;
                });
            Render::Ogre14JoinedParticleSourceFrame particle_frame;
            particle_frame.source_sequence =
                pending->particle_capture_state.next_source_sequence;
            particle_frame.simulation_tick =
                candidate.frame.simulation_tick;
            particle_frame.simulation_time_seconds =
                candidate.frame.simulation_time_seconds;
            particle_frame.absolute_world_origin_meters =
                candidate.frame.absolute_world_origin_meters;
            particle_frame.joined_buffer_epoch =
                candidate.joined_buffer_epoch;
            particle_frame.post_physics_epoch =
                candidate.post_update_scene_epoch;
            particle_frame.complete_inventory = true;
            particle_frame.systems = captured_dust_systems;
            std::map<std::uint64_t,
                     Render::Ogre14ParticleSourceSystemCapture> current_systems;
            for (const auto& system : captured_dust_systems)
            {
                const auto prior = pending->particle_capture_state
                                       .live_systems.find(system.system_id);
                if (prior == pending->particle_capture_state
                                 .live_systems.end() ||
                    !EqualOgre14ContinuousParticleSystem(
                        prior->second, system))
                {
                    if (pending->particle_capture_state.next_event_id ==
                        (std::numeric_limits<std::uint64_t>::max)())
                    {
                        return Render::ValidationResult::Failure(
                            Render::ValidationCode::VALUE_OUT_OF_RANGE,
                            "continuous_particles.event_id",
                            "Dust event identity space is exhausted");
                    }
                    Render::Ogre14ParticleLifecycleEvent event;
                    event.event_id = pending->particle_capture_state
                                         .next_event_id++;
                    event.system_id = system.system_id;
                    event.operation =
                        prior == pending->particle_capture_state
                                     .live_systems.end()
                            ? Render::Ogre14ParticleLifecycleOperation::CREATE
                        : prior->second.emitting && !system.emitting
                            ? Render::Ogre14ParticleLifecycleOperation::STOP
                            : Render::Ogre14ParticleLifecycleOperation::UPDATE;
                    particle_frame.events.push_back(event);
                }
                current_systems.emplace(system.system_id, system);
                ++pending->particle_capture_state.captured_systems;
                pending->particle_capture_state.captured_particles +=
                    system.particles.size();
            }
            for (const auto& prior :
                 pending->particle_capture_state.live_systems)
            {
                if (current_systems.find(prior.first) !=
                    current_systems.end())
                    continue;
                if (pending->particle_capture_state.next_event_id ==
                    (std::numeric_limits<std::uint64_t>::max)())
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::VALUE_OUT_OF_RANGE,
                        "continuous_particles.event_id",
                        "Dust event identity space is exhausted");
                }
                particle_frame.events.push_back({
                    pending->particle_capture_state.next_event_id++,
                    prior.first,
                    Render::Ogre14ParticleLifecycleOperation::DESTROY});
            }
            pending->particle_capture_state.live_systems =
                std::move(current_systems);
            pending->particle_capture_state.lifetime_max_captured_systems =
                (std::max)(pending->particle_capture_state
                               .lifetime_max_captured_systems,
                           pending->particle_capture_state.captured_systems);
            pending->particle_capture_state.lifetime_max_captured_particles =
                (std::max)(pending->particle_capture_state
                               .lifetime_max_captured_particles,
                           pending->particle_capture_state.captured_particles);
            if (pending->particle_capture_state.captured_systems +
                        pending->particle_capture_state
                            .deferred_inactive_systems +
                        pending->particle_capture_state.excluded_systems !=
                    pending->particle_capture_state.observed_systems ||
                pending->particle_capture_state.captured_particles +
                        pending->particle_capture_state.excluded_particles !=
                    pending->particle_capture_state.observed_particles ||
                pending->particle_capture_state.excluded_sparks_systems +
                        pending->particle_capture_state
                            .excluded_ripple_systems +
                        pending->particle_capture_state
                            .excluded_other_non_dust_systems !=
                    pending->particle_capture_state
                        .excluded_non_dust_systems ||
                pending->particle_capture_state.excluded_non_dust_systems +
                        pending->particle_capture_state
                            .excluded_billboard_modes +
                        pending->particle_capture_state
                            .excluded_local_space_systems +
                        pending->particle_capture_state
                            .excluded_animated_systems +
                        pending->particle_capture_state
                            .excluded_sorted_systems +
                        pending->particle_capture_state
                            .excluded_timing_modes !=
                    pending->particle_capture_state.excluded_systems)
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::SIZE_MISMATCH,
                    "continuous_particles.coverage_denominators",
                    "admitted, inactive-deferred, and named-exclusion system counts plus admitted/excluded particle counts do not exactly recount the observed OGRE14 inventory");
            }
            if (pending->particle_capture_state.next_source_sequence ==
                (std::numeric_limits<std::uint64_t>::max)())
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::VALUE_OUT_OF_RANGE,
                    "continuous_particles.source_sequence",
                    "Dust source sequence space is exhausted");
            }
            ++pending->particle_capture_state.next_source_sequence;
            candidate.frame.continuous_particles =
                std::move(particle_frame);
            section_particle_walk_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - particle_walk_started)
                    .count());
            {
                Ogre14CaptureSectionTimer merge_timer(section_asset_merge_ns);
                dynamic_validation = Render::MergeOgre14GraphicsSceneAssets(
                    nonterrain_assets, empty_assets, terrain_capture.assets,
                    candidate.frame.assets);
            }
            if (!dynamic_validation)
                return dynamic_validation;

            {
            Ogre14CaptureSectionTimer union_timer(section_object_union_ns);
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
                // On a retained-hit frame the static domain is not in this
                // set at all, so its disjointness from the dynamic domain is
                // proven against the owner's sorted identities instead.
                if (pending->static_state_retained &&
                    std::binary_search(
                        m_ogre14_static_retention_object_ids->begin(),
                        m_ogre14_static_retention_object_ids->end(),
                        instance.source_object_id))
                {
                    return Render::ValidationResult::Failure(
                        Render::ValidationCode::DUPLICATE_IDENTIFIER,
                        "ogre_next_demo.dynamic_meshes.source_object_id",
                        "retained static and deformable object source IDs collide");
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
            }

            if (!pending->static_state_retained)
            {
                // Mint the next generation of retained owners from this
                // walk's complete static union, terrain included. New
                // vectors every time: a published owner is immutable, and
                // its identity is the only change signal the producer has.
                Ogre14CaptureSectionTimer refresh_timer(
                    section_retained_copy_ns);
                auto refreshed_assets = std::make_shared<
                    std::vector<Render::GraphicsSceneAssetInput>>();
                const Render::ValidationResult refreshed_asset_merge =
                    Render::MergeOgre14GraphicsSceneAssets(
                        static_assets, empty_assets, terrain_capture.assets,
                        *refreshed_assets);
                if (!refreshed_asset_merge)
                    return refreshed_asset_merge;
                auto refreshed_meshes = std::make_shared<
                    std::vector<Render::GraphicsSceneStaticMeshInput>>(
                        candidate.frame.static_meshes);
                auto refreshed_object_ids =
                    std::make_shared<std::vector<std::uint64_t>>();
                refreshed_object_ids->reserve(refreshed_meshes->size());
                for (const Render::GraphicsSceneStaticMeshInput& instance :
                     *refreshed_meshes)
                {
                    refreshed_object_ids->push_back(
                        instance.source_object_id);
                }
                pending->retention_assets_owner =
                    std::move(refreshed_assets);
                pending->retention_meshes_owner =
                    std::move(refreshed_meshes);
                pending->retention_object_ids =
                    std::move(refreshed_object_ids);
            }

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
            // The environment and exact converted sun stage together. This
            // prevents a sky descriptor from ever naming an uncommitted or
            // differently normalized light identity; the pending light
            // registry remains rollback-only until producer acceptance.
            if (candidate.frame.lights.size() != 1U)
            {
                return Render::ValidationResult::Failure(
                    Render::ValidationCode::SIZE_MISMATCH,
                    "ogre_next_demo.environment.sun",
                    "modern analytic sky requires the one captured terrain main light");
            }
            Render::ValidationResult environment_validation =
                Render::BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
                    native_ambient, candidate.frame.lights.front(),
                    candidate.frame.simulation_time_seconds,
                    candidate.frame.environment);
            if (!environment_validation)
            {
                return environment_validation;
            }
            const Render::AnalyticSkyDescriptor& sky =
                candidate.frame.environment.analytic_sky;
            const Render::GraphicsSceneLightInput& committed_sun =
                candidate.frame.lights.front();
            pending->analytic_sky_log_snapshot = fmt::format(
                "policy_v={} enabled={} exact_skyx_pixel_capture=false "
                "radiance_authority="
                "joined_live_ambient_and_exact_converted_main_light "
                "sun_light_id={} sun_direction=[{:.9g},{:.9g},{:.9g}] "
                "sun_intensity={:.9g} ambient=[{:.9g},{:.9g},{:.9g}] "
                "zenith=[{:.9g},{:.9g},{:.9g}] "
                "horizon=[{:.9g},{:.9g},{:.9g}] "
                "ground=[{:.9g},{:.9g},{:.9g}] "
                "sun_disk=[{:.9g},{:.9g},{:.9g}] "
                "sun_angular_radius_radians={:.9g} "
                "cloud_coverage={:.9g} "
                "cloud_radiance=[{:.9g},{:.9g},{:.9g}] "
                "cloud_phase_radians={:.9g}",
                Render::kOgre14ModernAnalyticSkyPolicyVersion,
                sky.enabled, sky.sun_light_id,
                committed_sun.direction.x, committed_sun.direction.y,
                committed_sun.direction.z, committed_sun.intensity,
                native_ambient.x, native_ambient.y, native_ambient.z,
                sky.zenith_radiance.x, sky.zenith_radiance.y,
                sky.zenith_radiance.z, sky.horizon_radiance.x,
                sky.horizon_radiance.y, sky.horizon_radiance.z,
                sky.ground_radiance.x, sky.ground_radiance.y,
                sky.ground_radiance.z, sky.sun_disk_radiance.x,
                sky.sun_disk_radiance.y, sky.sun_disk_radiance.z,
                sky.sun_angular_radius_radians, sky.cloud_coverage,
                sky.cloud_radiance.x, sky.cloud_radiance.y,
                sky.cloud_radiance.z, sky.cloud_phase_radians);
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::ENVIRONMENT);
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::LIGHTS);
        }

        // The transported menu/HUD rides as an optional input; the producer
        // owns its asset identities and revision policy, so an unchanged
        // retained readback costs no asset delta.
        candidate.frame.hud_overlay = m_ogre14_hud_overlay_latest;

        Render::Double3 automatic_probe_camera_position;
        if (CaptureOgre14MainCamera(
                candidate.frame.camera,
                automatic_probe_camera_position))
        {
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::CAMERA);
            // OGRE 14 has no authored reflection-probe registry. Its dynamic
            // GfxEnvmap remains a vehicle-local compatibility effect and is
            // never promoted. The combined-runtime visual policy instead
            // authors one explicitly labelled project-owned PCC probe.
            // The probe policy reads the complete static inventory. On a
            // retained-hit frame that inventory is the owner, not the empty
            // residue vector; handing it the residue would read as the whole
            // static scene having disappeared.
            Render::ValidationResult probe_validation =
                Render::BuildOgre14AutomaticReflectionProbe(
                    automatic_probe_camera_position,
                    pending->static_state_retained &&
                            m_ogre14_static_retention_meshes_owner != nullptr
                        ? *m_ogre14_static_retention_meshes_owner
                        : candidate.frame.static_meshes,
                    m_ogre14_automatic_reflection_probe_state,
                    pending->automatic_reflection_probe_state,
                    candidate.frame.reflection_probes);
            if (!probe_validation)
                return probe_validation;
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::
                        REFLECTION_PROBES);
        }
    }

    {
        const std::uint64_t capture_total_ns =
            static_cast<std::uint64_t>(std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() -
                    ogre14_capture_started).count());
        const std::uint64_t section_sum_ns = candidate.terrain_ns +
            candidate.static_meshes_ns + candidate.dynamic_meshes_ns +
            section_retained_copy_ns + section_asset_merge_ns +
            section_object_union_ns + section_particle_walk_ns;
        pending->section_terrain_ns = candidate.terrain_ns;
        pending->section_static_ns = candidate.static_meshes_ns;
        pending->section_dynamic_ns = candidate.dynamic_meshes_ns;
        pending->section_retained_ns = section_retained_copy_ns;
        pending->section_merge_ns = section_asset_merge_ns;
        pending->section_union_ns = section_object_union_ns;
        pending->section_particles_ns = section_particle_walk_ns;
        pending->section_other_ns = capture_total_ns > section_sum_ns
            ? capture_total_ns - section_sum_ns
            : 0U;
    }
    m_ogre14_pending_capture = std::move(pending);
    material_pending_guard.Release();
    road_material_frame_guard.Release();
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
    if (!m_ogre14_pending_capture->static_state_retained)
    {
        swap(m_ogre14_static_identity_registry,
             m_ogre14_pending_capture->static_registry);
        swap(m_ogre14_static_mesh_cache,
             m_ogre14_pending_capture->static_mesh_cache);
        swap(m_ogre_next_demo_admitted_static_objects,
             m_ogre14_pending_capture->admitted_static_objects);
        swap(m_ogre14_procedural_road_inventory,
             m_ogre14_pending_capture->procedural_road_inventory);
        if (m_ogre14_pending_capture->has_retention_refresh)
        {
            m_ogre14_static_retention_assets_owner =
                std::move(m_ogre14_pending_capture->retention_assets_owner);
            m_ogre14_static_retention_meshes_owner =
                std::move(m_ogre14_pending_capture->retention_meshes_owner);
            m_ogre14_static_retention_object_ids =
                std::move(m_ogre14_pending_capture->retention_object_ids);
            m_ogre14_static_retention_unadmitted =
                std::move(m_ogre14_pending_capture->retention_unadmitted);
            m_ogre14_static_retention_inventory =
                m_ogre14_pending_capture->retention_inventory;
            m_ogre14_static_retention_cache_size =
                m_ogre14_pending_capture->retention_cache_size;
            m_ogre14_static_retention_road_live =
                m_ogre14_pending_capture->retention_road_live;
            m_ogre14_static_retention_road_cached =
                m_ogre14_pending_capture->retention_road_cached;
            m_ogre14_static_retention_frozen_decisions =
                m_ogre14_pending_capture->retention_frozen_decisions;
            m_ogre14_static_retention_projections =
                m_ogre14_pending_capture->retention_projections;
            m_ogre14_static_retention_valid = true;
        }
    }
    swap(m_ogre14_dynamic_identity_registry,
         m_ogre14_pending_capture->dynamic_registry);
    swap(m_ogre14_dynamic_mesh_cache,
         m_ogre14_pending_capture->dynamic_mesh_cache);
    swap(m_ogre14_particle_capture_state,
         m_ogre14_pending_capture->particle_capture_state);
    swap(m_ogre14_automatic_reflection_probe_state,
         m_ogre14_pending_capture->automatic_reflection_probe_state);
    const bool analytic_sky_changed =
        !m_ogre14_pending_capture->analytic_sky_log_snapshot.empty() &&
        m_ogre14_pending_capture->analytic_sky_log_snapshot !=
            m_ogre_next_demo_analytic_sky_log_snapshot;
    swap(m_ogre_next_demo_analytic_sky_log_snapshot,
         m_ogre14_pending_capture->analytic_sky_log_snapshot);
    if (analytic_sky_changed)
    {
        LOG(fmt::format(
            "[RoR|OgreNextDemo|AnalyticSky|Source] {}",
            m_ogre_next_demo_analytic_sky_log_snapshot));
    }
    m_ogre_next_demo_material_source.Commit();
    if (m_ogre14_pending_capture->has_road_material_frame &&
        m_ogre14_road_material_coordinator != nullptr)
    {
        const Render::Ogre14LegacyPreparedMaterialCommitResult
            road_material_commit_result =
                m_ogre14_road_material_coordinator->
                    CommitPreparedFrameAfterAcceptedExposure(
                        m_ogre14_pending_capture->road_material_frame);
        if (road_material_commit_result !=
            Render::Ogre14LegacyPreparedMaterialCommitResult::COMMITTED)
        {
            // The accepted exposure could not advance the translator. Drop
            // the pending frame so the next capture can prepare again, and
            // leave a loud record: a silent wedge here would starve every
            // later road frame with "the preceding material frame must be
            // committed or discarded".
            m_ogre14_road_material_coordinator->DiscardPreparedFrame();
            LOG(fmt::format(
                "[RoR|SceneSource|RoadMaterial] prepared frame refused at "
                "accepted exposure: result={}",
                static_cast<unsigned int>(road_material_commit_result)));
        }
    }
    // Periodically surface the per-section traversal cost. The joined read
    // dominates a combined-runtime frame, and which section owns it decides
    // what a retained native scene must cache. Counting at commit means only
    // accepted exposures are measured.
    ++m_ogre14_section_log_captures;
    m_ogre14_section_log_terrain_ns +=
        m_ogre14_pending_capture->section_terrain_ns;
    m_ogre14_section_log_static_ns +=
        m_ogre14_pending_capture->section_static_ns;
    m_ogre14_section_log_dynamic_ns +=
        m_ogre14_pending_capture->section_dynamic_ns;
    m_ogre14_section_log_retained_ns +=
        m_ogre14_pending_capture->section_retained_ns;
    m_ogre14_section_log_merge_ns +=
        m_ogre14_pending_capture->section_merge_ns;
    m_ogre14_section_log_union_ns +=
        m_ogre14_pending_capture->section_union_ns;
    m_ogre14_section_log_particles_ns +=
        m_ogre14_pending_capture->section_particles_ns;
    m_ogre14_section_log_other_ns +=
        m_ogre14_pending_capture->section_other_ns;
    if ((m_ogre14_section_log_captures % 300U) == 0U)
    {
        LOG(fmt::format(
            "[RoR|SceneSource] captures={} mean_ns terrain={} static={} "
            "dynamic={} retained={} merge={} union={} particles={} other={}",
            m_ogre14_section_log_captures,
            m_ogre14_section_log_terrain_ns / m_ogre14_section_log_captures,
            m_ogre14_section_log_static_ns / m_ogre14_section_log_captures,
            m_ogre14_section_log_dynamic_ns / m_ogre14_section_log_captures,
            m_ogre14_section_log_retained_ns / m_ogre14_section_log_captures,
            m_ogre14_section_log_merge_ns / m_ogre14_section_log_captures,
            m_ogre14_section_log_union_ns / m_ogre14_section_log_captures,
            m_ogre14_section_log_particles_ns / m_ogre14_section_log_captures,
            m_ogre14_section_log_other_ns / m_ogre14_section_log_captures));
    }
    const Gfx::Detail::OgreNextDemoMaterialSourceCounters& capture_counters =
        m_ogre14_pending_capture->material_source_counters;
    const Gfx::Detail::OgreNextDemoCuratedCityWorldCoverage&
        curated_coverage = m_ogre14_pending_capture
            ->curated_cityworld_material_coverage;
    const std::string coverage_snapshot =
        BuildOgreNextDemoMaterialCoverageSnapshot(
            m_ogre14_pending_capture->active_material_projection_count,
            capture_counters, curated_coverage);
    const bool activity_event =
        m_ogre14_pending_capture->new_material_projection_count != 0U ||
        capture_counters.new_frozen_material_decisions != 0U ||
        capture_counters.authenticated_archive_source_decodes != 0U ||
        capture_counters.authenticated_generated_source_decodes != 0U ||
        capture_counters.ordinary_observed_source_decodes != 0U ||
        capture_counters.modern_source_normalizations != 0U ||
        capture_counters.authored_specular_source_decodes != 0U ||
        capture_counters.linear_specular_source_normalizations != 0U ||
        capture_counters.alpha_test_material_projections != 0U ||
        capture_counters.straight_source_over_material_projections != 0U ||
        capture_counters.legacy_straight_alpha_material_projections != 0U ||
        capture_counters.specular_workflow_projections != 0U ||
        capture_counters.anisotropic_sampler_projections != 0U;
    if (activity_event ||
        coverage_snapshot !=
            m_ogre_next_demo_material_coverage_log_snapshot)
    {
        const Gfx::Detail::OgreNextDemoMaterialSourceCounters
            lifetime_counters =
                m_ogre_next_demo_material_source.LifetimeCounters();
        const std::string capture_exclusions =
            FormatOgreNextDemoMaterialExclusions(capture_counters);
        const std::string lifetime_exclusions =
            FormatOgreNextDemoMaterialExclusions(lifetime_counters);
        LOG(fmt::format(
            "[RoR|OgreNextDemo|MaterialSource] "
            "modern_policy=srgb_opaque_authored_prefix_linear_tail_v2 "
            "straight_alpha_policy="
            "srgb_straight_alpha_authored_prefix_premultiplied_linear_tail_v1 "
            "linear_specular_policy=linear_specular_authored_prefix_box_tail_v1 "
            "managed_specular_lowering="
            "linear_rgb_specular_workflow_dielectric_ior1p5_f0p04_"
            "no_metallic_synthesis_v1 "
            "managed_specular_scope=alexis_opaque_4_of_7_v1 "
            "capture_v4=[{}] lifetime_v4=[{}] "
            "committed_new_projections={} active_projections={} capture "
            "new_frozen_material_decisions={} candidate_sections={} "
            "projected_sections={} matte_excluded_sections={} "
            "distinct_eligible_texture_keys={} "
            "distinct_projected_texture_keys={} "
            "distinct_matte_only_texture_keys={} "
            "active_texture_state_observations={} "
            "active_authored_mip_prefix_levels={} "
            "active_generated_mip_tail_levels={} "
            "active_normalized_output_mip_levels={} "
            "active_legacy_native_additional_mip_levels={} "
            "active_legacy_texture_unit_gamma_nonunit_observations={} "
            "active_legacy_texture_gamma_nonunit_observations={} "
            "active_legacy_texture_unit_hardware_gamma_off_observations={} "
            "active_legacy_hardware_gamma_off_observations={} "
            "active_legacy_automipmap_observations={} "
            "authenticated_archive_source_decodes={} "
            "authenticated_generated_source_decodes={} "
            "authenticated_source_decodes={} "
            "ordinary_observed_source_decodes={} source_cache_hits={} "
            "source_decode_rejections={} source_exclusions={} "
            "modern_source_normalizations={} "
            "authored_mip_prefix_levels={} generated_mip_tail_levels={} "
            "normalized_output_mip_levels={} "
            "legacy_native_additional_mip_levels={} "
            "legacy_texture_unit_gamma_nonunit_observations={} "
            "legacy_texture_gamma_nonunit_observations={} "
            "legacy_texture_unit_hardware_gamma_off_observations={} "
            "legacy_hardware_gamma_off_observations={} "
            "legacy_automipmap_observations={} "
            "lossy_material_normalizations={} gpu_readbacks={} "
            "authenticated_gpu_readbacks={} unauthenticated_gpu_readbacks={} "
            "projections={} matte_by_reason=[{}]; lifetime "
            "new_frozen_material_decisions={} candidate_sections={} "
            "projected_sections={} matte_excluded_sections={} "
            "distinct_eligible_texture_keys={} "
            "distinct_projected_texture_keys={} "
            "distinct_matte_only_texture_keys={} "
            "active_texture_state_observations={} "
            "active_authored_mip_prefix_levels={} "
            "active_generated_mip_tail_levels={} "
            "active_normalized_output_mip_levels={} "
            "active_legacy_native_additional_mip_levels={} "
            "active_legacy_texture_unit_gamma_nonunit_observations={} "
            "active_legacy_texture_gamma_nonunit_observations={} "
            "active_legacy_texture_unit_hardware_gamma_off_observations={} "
            "active_legacy_hardware_gamma_off_observations={} "
            "active_legacy_automipmap_observations={} "
            "authenticated_archive_source_decodes={} "
            "authenticated_generated_source_decodes={} "
            "authenticated_source_decodes={} authenticated_gpu_readbacks={} "
            "ordinary_observed_source_decodes={} source_cache_hits={} "
            "source_decode_rejections={} source_exclusions={} "
            "modern_source_normalizations={} "
            "authored_mip_prefix_levels={} generated_mip_tail_levels={} "
            "normalized_output_mip_levels={} "
            "legacy_native_additional_mip_levels={} "
            "legacy_texture_unit_gamma_nonunit_observations={} "
            "legacy_texture_gamma_nonunit_observations={} "
            "legacy_texture_unit_hardware_gamma_off_observations={} "
            "legacy_hardware_gamma_off_observations={} "
            "legacy_automipmap_observations={} "
            "lossy_material_normalizations={} gpu_readbacks={} "
            "unauthenticated_gpu_readbacks={} projections={} "
            "matte_by_reason=[{}]",
            FormatOgreNextDemoMaterialCounters(capture_counters),
            FormatOgreNextDemoMaterialCounters(lifetime_counters),
            m_ogre14_pending_capture->new_material_projection_count,
            m_ogre14_pending_capture->active_material_projection_count,
            capture_counters.new_frozen_material_decisions,
            capture_counters.candidate_sections,
            capture_counters.projected_sections,
            capture_counters.matte_excluded_sections,
            capture_counters.distinct_eligible_texture_keys,
            capture_counters.distinct_projected_texture_keys,
            capture_counters.distinct_matte_only_texture_keys,
            capture_counters.active_texture_state_observations,
            capture_counters.active_authored_mip_prefix_levels,
            capture_counters.active_generated_mip_tail_levels,
            capture_counters.active_normalized_output_mip_levels,
            capture_counters.active_legacy_native_additional_mip_levels,
            capture_counters
                .active_legacy_texture_unit_gamma_nonunit_observations,
            capture_counters
                .active_legacy_texture_gamma_nonunit_observations,
            capture_counters
                .active_legacy_texture_unit_hardware_gamma_off_observations,
            capture_counters.active_legacy_hardware_gamma_off_observations,
            capture_counters.active_legacy_automipmap_observations,
            capture_counters.authenticated_archive_source_decodes,
            capture_counters.authenticated_generated_source_decodes,
            capture_counters.authenticated_source_decodes,
            capture_counters.ordinary_observed_source_decodes,
            capture_counters.source_cache_hits,
            capture_counters.source_decode_rejections,
            capture_counters.source_exclusions,
            capture_counters.modern_source_normalizations,
            capture_counters.authored_mip_prefix_levels,
            capture_counters.generated_mip_tail_levels,
            capture_counters.normalized_output_mip_levels,
            capture_counters.legacy_native_additional_mip_levels,
            capture_counters
                .legacy_texture_unit_gamma_nonunit_observations,
            capture_counters.legacy_texture_gamma_nonunit_observations,
            capture_counters
                .legacy_texture_unit_hardware_gamma_off_observations,
            capture_counters.legacy_hardware_gamma_off_observations,
            capture_counters.legacy_automipmap_observations,
            capture_counters.lossy_material_normalizations,
            capture_counters.gpu_readbacks,
            capture_counters.authenticated_gpu_readbacks,
            capture_counters.unauthenticated_gpu_readbacks,
            capture_counters.projections,
            capture_exclusions,
            lifetime_counters.new_frozen_material_decisions,
            lifetime_counters.candidate_sections,
            lifetime_counters.projected_sections,
            lifetime_counters.matte_excluded_sections,
            lifetime_counters.distinct_eligible_texture_keys,
            lifetime_counters.distinct_projected_texture_keys,
            lifetime_counters.distinct_matte_only_texture_keys,
            lifetime_counters.active_texture_state_observations,
            lifetime_counters.active_authored_mip_prefix_levels,
            lifetime_counters.active_generated_mip_tail_levels,
            lifetime_counters.active_normalized_output_mip_levels,
            lifetime_counters.active_legacy_native_additional_mip_levels,
            lifetime_counters
                .active_legacy_texture_unit_gamma_nonunit_observations,
            lifetime_counters
                .active_legacy_texture_gamma_nonunit_observations,
            lifetime_counters
                .active_legacy_texture_unit_hardware_gamma_off_observations,
            lifetime_counters.active_legacy_hardware_gamma_off_observations,
            lifetime_counters.active_legacy_automipmap_observations,
            lifetime_counters.authenticated_archive_source_decodes,
            lifetime_counters.authenticated_generated_source_decodes,
            lifetime_counters.authenticated_source_decodes,
            lifetime_counters.authenticated_gpu_readbacks,
            lifetime_counters.ordinary_observed_source_decodes,
            lifetime_counters.source_cache_hits,
            lifetime_counters.source_decode_rejections,
            lifetime_counters.source_exclusions,
            lifetime_counters.modern_source_normalizations,
            lifetime_counters.authored_mip_prefix_levels,
            lifetime_counters.generated_mip_tail_levels,
            lifetime_counters.normalized_output_mip_levels,
            lifetime_counters.legacy_native_additional_mip_levels,
            lifetime_counters
                .legacy_texture_unit_gamma_nonunit_observations,
            lifetime_counters.legacy_texture_gamma_nonunit_observations,
            lifetime_counters
                .legacy_texture_unit_hardware_gamma_off_observations,
            lifetime_counters.legacy_hardware_gamma_off_observations,
            lifetime_counters.legacy_automipmap_observations,
            lifetime_counters.lossy_material_normalizations,
            lifetime_counters.gpu_readbacks,
            lifetime_counters.unauthenticated_gpu_readbacks,
            lifetime_counters.projections,
            lifetime_exclusions));
        LOG(fmt::format(
            "[RoR|OgreNextDemo|CuratedCityWorldAsia] policy_v={} "
            "admitted={}/{} observed={} matte={} "
            "environment_pending={} uncurated_spherical_family_matte={} "
            "sampler_profile={} acceptance_config_sha256={} "
            "environment_semantics={} parity_claim=false",
            Gfx::Detail::kOgreNextDemoCuratedCityWorldAsiaPolicyVersion,
            curated_coverage.admitted_entries,
            curated_coverage.policy_entries,
            curated_coverage.observed_entries,
            curated_coverage.matte_entries,
            curated_coverage.environment_pending_entries,
            curated_coverage.uncurated_spherical_family_matte_materials,
            Gfx::Detail::kOgreNextDemoCuratedCityWorldSamplerProfile,
            Gfx::Detail::
                kOgreNextDemoCuratedCityWorldAcceptanceConfigSha256,
            Gfx::Detail::kOgreNextDemoCuratedCityWorldEnvironmentPolicy));
        m_ogre_next_demo_material_coverage_log_snapshot = coverage_snapshot;
    }
    const Ogre14ContinuousParticleCaptureState& particles =
        m_ogre14_particle_capture_state;
    const std::string particle_snapshot = fmt::format(
        "observed_systems={} observed_particles={} admitted_systems={} "
        "admitted_particles={} deferred_inactive_systems={} "
        "excluded_systems={} excluded_particles={} "
        "excluded_non_dust_systems={} excluded_sparks_systems={} "
        "excluded_ripple_systems={} excluded_other_non_dust_systems={} "
        "excluded_billboard_modes={} "
        "excluded_local_space_systems={} excluded_animated_systems={} "
        "excluded_sorted_systems={} excluded_timing_modes={} "
        "distinct_source_textures={} source_alpha_textures={} "
        "gpu_readbacks={} lifetime_max_admitted_systems={} "
        "lifetime_max_admitted_particles={}",
        particles.observed_systems, particles.observed_particles,
        particles.captured_systems, particles.captured_particles,
        particles.deferred_inactive_systems,
        particles.excluded_systems, particles.excluded_particles,
        particles.excluded_non_dust_systems,
        particles.excluded_sparks_systems,
        particles.excluded_ripple_systems,
        particles.excluded_other_non_dust_systems,
        particles.excluded_billboard_modes,
        particles.excluded_local_space_systems,
        particles.excluded_animated_systems,
        particles.excluded_sorted_systems,
        particles.excluded_timing_modes,
        particles.source_backed_textures,
        particles.source_alpha_textures, particles.gpu_readbacks,
        particles.lifetime_max_captured_systems,
        particles.lifetime_max_captured_particles);
    if (particle_snapshot != m_ogre14_particle_coverage_log_snapshot)
    {
        LOG(fmt::format(
            "[RoR|OgreNextDemo|ContinuousParticles|Capture] {}",
            particle_snapshot));
        m_ogre14_particle_coverage_log_snapshot = particle_snapshot;
    }
    m_ogre14_pending_capture.reset();
}

void GfxScene::DiscardOgre14GraphicsSceneCapture() noexcept
{
    m_ogre_next_demo_material_source.Discard();
    if (m_ogre14_road_material_coordinator != nullptr)
        m_ogre14_road_material_coordinator->DiscardPreparedFrame();
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
