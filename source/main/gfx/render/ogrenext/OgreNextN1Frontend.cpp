/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Frontend.h"

#include "OgreNextDisplayDomainUnlit.h"
#include "OgreNextN1MediaIntegrity.h"
#include "OgreNextN1ParticleRuntime.h"
#include "OgreNextN1NativeInterop.h"
#include "OgreNextN1Policy.h"
#include "OgreNextReflectionProbeRuntime.h"
#include "OgreNextSunVisibilityV2Interop.h"
#include "OgreNextUvAffinePbs.h"
#include "ror_ogre_next_n1_config.h"

#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorNode.h"
#include "Compositor/OgreCompositorNodeDef.h"
#include "Compositor/OgreCompositorShadowNode.h"
#include "Compositor/OgreCompositorShadowNodeDef.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "Compositor/OgreCompositorWorkspaceListener.h"
#include "Compositor/OgreCompositorWorkspaceDef.h"
#include "Compositor/OgreCompositorChannel.h"
#include "Compositor/OgreTextureDefinition.h"
#include "Compositor/Pass/OgreCompositorPassDef.h"
#include "Compositor/Pass/OgreCompositorPass.h"
#include "Compositor/Pass/PassClear/OgreCompositorPassClearDef.h"
#include "Compositor/Pass/PassQuad/OgreCompositorPassQuadDef.h"
#include "Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h"
#include "OgreAbiUtils.h"
#include "OgreArchiveManager.h"
#include "OgreCamera.h"
#include "OgreColourValue.h"
#include "OgreDepthBuffer.h"
#include "OgreException.h"
#include "OgreHlmsSamplerblock.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsDatablock.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreHlmsUnlit.h"
#include "OgreHlmsUnlitDatablock.h"
#include "OgreImage2.h"
#include "OgreItem.h"
#include "OgreLight.h"
#include "OgreMaterial.h"
#include "OgreMaterialManager.h"
#include "OgreManualObject2.h"
#include "OgreMatrix4.h"
#include "OgreMesh2.h"
#include "OgreMeshManager2.h"
#include "OgrePixelFormatGpu.h"
#include "OgreQuaternion.h"
#include "OgreRenderSystem.h"
#include "OgreRenderSystemCapabilities.h"
#include "OgreResourceGroupManager.h"
#include "OgreRoot.h"
#include "OgreSceneManager.h"
#include "OgreSceneNode.h"
#include "OgreStagingTexture.h"
#include "OgreSubItem.h"
#include "OgreSubMesh2.h"
#include "OgreTechnique.h"
#include "OgrePass.h"
#include "OgreOverlay.h"
#include "OgreOverlayContainer.h"
#include "OgreOverlayManager.h"
#include "OgreOverlaySystem.h"
#include "OgreGpuProgramParams.h"
#include "OgreTextureBox.h"
#include "OgreTextureGpu.h"
#include "OgreTextureGpuManager.h"
#include "OgreVisibilityFlags.h"
#include "OgreWindow.h"
#include "Vao/OgreVaoManager.h"
#include "Vao/OgreAsyncTicket.h"
#include "Vao/OgreVertexArrayObject.h"
#include "Vao/OgreVertexBufferPacked.h"
#include "Vao/OgreIndexBufferPacked.h"

#if defined(ROR_OGRE_NEXT_N1_METAL)
#include "OgreMetalPlugin.h"
using N1RendererPlugin = Ogre::MetalPlugin;
#elif defined(ROR_OGRE_NEXT_N1_D3D11)
#include "OgreD3D11Plugin.h"
using N1RendererPlugin = Ogre::D3D11Plugin;
#elif defined(ROR_OGRE_NEXT_N1_VULKAN)
#include "OgreVulkanPlugin.h"
using N1RendererPlugin = Ogre::VulkanPlugin;
#else
#error "No reviewed Ogre-Next N1 renderer policy selected"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <map>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace RoR::Render {
namespace {

std::atomic<bool> g_ogre_next_n1_root_claimed{false};
constexpr char kOgreNextPresentationResourceGroup[] =
    "RoROgreNextPresentationCopyV1";

bool TryClaimOgreNextN1Root() noexcept {
  bool expected = false;
  return g_ogre_next_n1_root_claimed.compare_exchange_strong(
      expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

void ReleaseOgreNextN1Root() noexcept {
  g_ogre_next_n1_root_claimed.store(false, std::memory_order_release);
}

std::uint64_t AnalyticSkyCpuGeometryFnv1a64(
    const OgreNextAnalyticSkyNativeMesh &mesh) noexcept {
  constexpr std::uint64_t kOffsetBasis = UINT64_C(14695981039346656037);
  constexpr std::uint64_t kPrime = UINT64_C(1099511628211);
  std::uint64_t digest = kOffsetBasis;
  const auto absorb = [&](const void *data, std::size_t size) {
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    for (std::size_t index = 0U; index < size; ++index) {
      digest ^= bytes[index];
      digest *= kPrime;
    }
  };
  const auto absorb_section = [&](std::uint8_t tag, const void *data,
                                  std::size_t size) {
    absorb(&tag, sizeof(tag));
    absorb(data, size);
  };
  absorb_section(1U, mesh.background_vertices.data(),
                 mesh.background_vertices.size() *
                     sizeof(OgreNextAnalyticSkyNativeVertex));
  absorb_section(2U, mesh.background_indices.data(),
                 mesh.background_indices.size() * sizeof(std::uint32_t));
  absorb_section(3U, mesh.sun_vertices.data(),
                 mesh.sun_vertices.size() *
                     sizeof(OgreNextAnalyticSkyNativeVertex));
  absorb_section(4U, mesh.sun_indices.data(),
                 mesh.sun_indices.size() * sizeof(std::uint32_t));
  return digest;
}

bool SameNativeWindow(const NativeWindowHandle &lhs,
                      const NativeWindowHandle &rhs) noexcept {
  return lhs.system == rhs.system && lhs.connection == rhs.connection &&
         lhs.surface == rhs.surface && lhs.generation == rhs.generation;
}

template <std::size_t Capacity>
bool HasExactPresentationParameters(
    const std::array<OgreNextN1PresentationParameter, Capacity> &parameters,
    std::size_t count,
    std::initializer_list<std::pair<const char *, std::string>> expected) {
  if (count != expected.size() || count > parameters.size()) {
    return false;
  }
  for (const auto &required : expected) {
    std::size_t matches = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
      matches += parameters[index].name == required.first &&
                         parameters[index].value == required.second
                     ? 1U
                     : 0U;
    }
    if (matches != 1U) {
      return false;
    }
  }
  return true;
}

RenderOperationResult ValidatePresentationConfiguration(
    const OgreNextN1PresentationConfiguration &configuration,
    const FrontendInitializationRequest &request) {
  if (configuration.version != kOgreNextN1PresentationContractVersion) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "unsupported Ogre-Next presentation configuration version");
  }
  if (!configuration.enabled) {
    if (configuration.mode !=
            OgreNextN1PresentationMode::EXACT_ONE_FRAME_GATE ||
        !configuration.shader_media_root.empty() ||
        configuration.exact_window.valid() ||
        configuration.renderer_option_count != 0U ||
        configuration.bootstrap_window_parameter_count != 0U ||
        configuration.presentation_window_parameter_count != 0U ||
        configuration.show_callback_context != nullptr ||
        configuration.show_after_workspace_ready != nullptr ||
        configuration.gpu_only_output) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "disabled presentation configuration retained live inputs");
    }
    return RenderOperationResult::Success();
  }
  if (configuration.mode !=
          OgreNextN1PresentationMode::EXACT_ONE_FRAME_GATE &&
      configuration.mode !=
          OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "unknown Ogre-Next presentation lifetime mode");
  }
  if (configuration.gpu_only_output &&
      configuration.mode !=
          OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "GPU-only presentation output requires the production run loop");
  }
  if (configuration.shader_media_root.empty() ||
      !configuration.exact_window.valid() ||
      !SameNativeWindow(configuration.exact_window, request.window) ||
      configuration.show_callback_context == nullptr ||
      configuration.show_after_workspace_ready == nullptr) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "enabled presentation configuration is incomplete or identifies another native window");
  }

  const std::string surface = std::to_string(request.window.surface);
#if defined(ROR_OGRE_NEXT_N1_METAL)
  const bool exact = request.window.system == NativeWindowSystem::COCOA &&
                     HasExactPresentationParameters(
                         configuration.renderer_options,
                         configuration.renderer_option_count, {}) &&
                     HasExactPresentationParameters(
                         configuration.bootstrap_window_parameters,
                         configuration.bootstrap_window_parameter_count, {}) &&
                     HasExactPresentationParameters(
                         configuration.presentation_window_parameters,
                         configuration.presentation_window_parameter_count,
                         {{"externalWindowHandle", surface},
                          {"gamma", "true"},
                          {"FSAA", "0"},
                          {"presentsWithTransaction", "false"}});
#elif defined(ROR_OGRE_NEXT_N1_D3D11)
  const bool exact = request.window.system == NativeWindowSystem::WINDOWS &&
                     HasExactPresentationParameters(
                         configuration.renderer_options,
                         configuration.renderer_option_count, {}) &&
                     HasExactPresentationParameters(
                         configuration.bootstrap_window_parameters,
                         configuration.bootstrap_window_parameter_count, {}) &&
                     HasExactPresentationParameters(
                         configuration.presentation_window_parameters,
                         configuration.presentation_window_parameter_count,
                         {{"externalWindowHandle", surface},
                          {"gamma", "true"},
                          {"FSAA", "0"},
                          {"vsync", "false"},
                          {"vsyncInterval", "0"}});
#elif defined(ROR_OGRE_NEXT_N1_VULKAN)
  bool stable_pair = false;
  if (configuration.presentation_window_parameter_count <=
      configuration.presentation_window_parameters.size()) {
    for (std::size_t index = 0U;
         index < configuration.presentation_window_parameter_count; ++index) {
      const OgreNextN1PresentationParameter &parameter =
          configuration.presentation_window_parameters[index];
      if (parameter.name == "SDL2x11") {
        try {
          stable_pair = std::stoull(parameter.value) != 0ULL;
        } catch (...) {
          stable_pair = false;
        }
      }
    }
  }
  const bool exact = request.window.system == NativeWindowSystem::X11 &&
                     stable_pair &&
                     HasExactPresentationParameters(
                         configuration.renderer_options,
                         configuration.renderer_option_count,
                         {{"Interface", "xcb"}}) &&
                     HasExactPresentationParameters(
                         configuration.bootstrap_window_parameters,
                         configuration.bootstrap_window_parameter_count,
                         {{"windowType", "null"}}) &&
                     configuration.presentation_window_parameter_count == 5U &&
                     HasExactPresentationParameters(
                         configuration.presentation_window_parameters,
                         configuration.presentation_window_parameter_count,
                         {{"SDL2x11",
                           [&configuration]() {
                             for (std::size_t index = 0U;
                                  index < configuration
                                              .presentation_window_parameter_count;
                                  ++index) {
                               if (configuration
                                       .presentation_window_parameters[index]
                                       .name == "SDL2x11") {
                                 return configuration
                                     .presentation_window_parameters[index]
                                     .value;
                               }
                             }
                             return std::string{};
                           }()},
                          {"gamma", "true"},
                          {"FSAA", "0"},
                          {"vsync", "false"},
                          {"vsyncInterval", "0"}});
#else
  constexpr bool exact = false;
#endif
  if (!exact) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "presentation binding differs from the reviewed native platform contract");
  }
  return RenderOperationResult::Success();
}

template <std::size_t Capacity>
Ogre::NameValuePairList ToOgreParameters(
    const std::array<OgreNextN1PresentationParameter, Capacity> &parameters,
    std::size_t count) {
  Ogre::NameValuePairList result;
  for (std::size_t index = 0U; index < count; ++index) {
    const bool inserted =
        result.emplace(parameters[index].name, parameters[index].value).second;
    if (!inserted) {
      throw std::runtime_error(
          "presentation binding contains a duplicate Ogre parameter");
    }
  }
  return result;
}

struct N1Vertex {
  float position[3];
  float normal[3];
};

struct Rt4PbrVertex {
  float position[3];
  float normal[3];
  float tangent[4];
  float texture_coordinates_0[2];
};

static_assert(std::is_standard_layout<N1Vertex>::value,
              "N1 native interop vertex must be standard-layout");
static_assert(sizeof(N1Vertex) ==
                  kOgreNextPositionNormalVertexStrideBytes,
              "reviewed N1 position/normal vertex must remain 24 bytes");
static_assert(offsetof(N1Vertex, position) == 0U &&
                  offsetof(N1Vertex, normal) == 12U,
              "reviewed N1 position/normal offsets changed");
static_assert(std::is_standard_layout<Rt4PbrVertex>::value,
              "RT4 native interop vertex must be standard-layout");
static_assert(sizeof(Rt4PbrVertex) ==
                  kOgreNextPositionNormalTangentUv0VertexStrideBytes,
              "reviewed RT4 position/normal/tangent/UV0 vertex must remain 48 bytes");
static_assert(offsetof(Rt4PbrVertex, position) == 0U &&
                  offsetof(Rt4PbrVertex, normal) == 12U &&
                  offsetof(Rt4PbrVertex, tangent) == 24U &&
                  offsetof(Rt4PbrVertex, texture_coordinates_0) == 40U,
              "reviewed RT4 vertex offsets changed");
static_assert(std::is_nothrow_move_assignable<RenderFrameOutput>::value,
              "frontend publication requires no-throw output ownership transfer");

bool TryResolveNativeVertexLayout(
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextNativeFeatureTier native_feature_tier,
    OgreNextNativeVertexLayout &layout) noexcept {
  layout = OgreNextNativeVertexLayout::INVALID;
  switch (raster_feature_tier) {
  case OgreNextRasterFeatureTier::STATIC_PBR_N1:
    switch (native_feature_tier) {
    case OgreNextNativeFeatureTier::RASTER_N1:
    case OgreNextNativeFeatureTier::METAL_RAY_TRACING_N2:
    case OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3:
      layout = OgreNextNativeVertexLayout::POSITION_NORMAL_FLOAT32_24;
      return true;
    case OgreNextNativeFeatureTier::
        METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW:
    case OgreNextNativeFeatureTier::METAL_RAY_TRACING_V2_SUN_VISIBILITY:
      // N4 consumes the complete RT4 tangent/UV material layout and never
      // silently degrades to the texture-free N1 geometry contract.
      return false;
    }
    return false;
  case OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1:
    switch (native_feature_tier) {
    case OgreNextNativeFeatureTier::RASTER_N1:
    case OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3:
    case OgreNextNativeFeatureTier::
        METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW:
    case OgreNextNativeFeatureTier::METAL_RAY_TRACING_V2_SUN_VISIBILITY:
      layout = OgreNextNativeVertexLayout::
          POSITION_NORMAL_TANGENT_UV0_FLOAT32_48;
      return true;
    case OgreNextNativeFeatureTier::METAL_RAY_TRACING_N2:
      // N2's original checkpoint is intentionally frozen to the 24-byte
      // layout. The simultaneous modern raster/native proof is N3.
      return false;
    }
    return false;
  }
  return false;
}

bool UsesMetalImageInterop(
    OgreNextNativeFeatureTier native_feature_tier) noexcept {
  return native_feature_tier ==
             OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3 ||
         native_feature_tier == OgreNextNativeFeatureTier::
                                    METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW;
}

bool UsesMetalDirectionalHardShadow(
    OgreNextNativeFeatureTier native_feature_tier) noexcept {
  return native_feature_tier == OgreNextNativeFeatureTier::
                                    METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW;
}

bool UsesMetalSunVisibilityV2(
    OgreNextNativeFeatureTier native_feature_tier) noexcept {
  return native_feature_tier ==
         OgreNextNativeFeatureTier::METAL_RAY_TRACING_V2_SUN_VISIBILITY;
}

enum class UploadedTextureChannel : std::uint8_t {
  RGBA,
  DISPLAY_DOMAIN_RGBA,
  LINEAR_RGBA,
  GREEN,
  BLUE,
  NORMAL_RG,
};

struct NativeTextureUsage final {
  bool sampled_rgba = false;
  bool display_domain_rgba = false;
  bool linear_rgba = false;
  bool roughness_g = false;
  bool metallic_b = false;
  bool normal_rg = false;

  [[nodiscard]] bool empty() const noexcept {
    return !sampled_rgba && !display_domain_rgba && !linear_rgba &&
           !roughness_g && !metallic_b && !normal_rg;
  }

  friend bool operator==(const NativeTextureUsage &lhs,
                         const NativeTextureUsage &rhs) noexcept {
    return lhs.sampled_rgba == rhs.sampled_rgba &&
           lhs.display_domain_rgba ==
               rhs.display_domain_rgba &&
           lhs.linear_rgba == rhs.linear_rgba &&
           lhs.roughness_g == rhs.roughness_g &&
           lhs.metallic_b == rhs.metallic_b &&
           lhs.normal_rg == rhs.normal_rg;
  }

  friend bool operator!=(const NativeTextureUsage &lhs,
                         const NativeTextureUsage &rhs) noexcept {
    return !(lhs == rhs);
  }
};

struct ReferencedTextureUsage final {
  RenderAssetReference asset;
  NativeTextureUsage usage;
};

RasterGraphicsApi CompiledRasterApi() noexcept {
#if defined(ROR_OGRE_NEXT_N1_METAL)
  return RasterGraphicsApi::METAL;
#elif defined(ROR_OGRE_NEXT_N1_D3D11)
  return RasterGraphicsApi::DIRECT3D11;
#else
  return RasterGraphicsApi::VULKAN;
#endif
}

std::string AssetName(const char *prefix,
                      const RenderAssetReference &asset) {
  std::ostringstream name;
  name << prefix << '_' << std::hex << asset.id.high() << '_'
       << asset.id.low() << std::dec << "_r" << asset.revision;
  return name.str();
}

std::string TextureAssetName(const RenderAssetReference &asset,
                             const NativeTextureUsage &usage) {
  const unsigned int usage_key =
      (usage.sampled_rgba ? 1U : 0U) |
      (usage.display_domain_rgba ? 2U : 0U) |
      (usage.roughness_g ? 4U : 0U) | (usage.metallic_b ? 8U : 0U) |
      (usage.normal_rg ? 16U : 0U) | (usage.linear_rgba ? 32U : 0U);
  std::ostringstream name;
  name << AssetName("RoRRT4Texture", asset) << "_usage" << std::hex
       << usage_key;
  return name.str();
}

Ogre::Matrix4 ToOgreMatrix(const Matrix4x4 &source) {
  Ogre::Matrix4 result;
  for (std::size_t column = 0U; column < 4U; ++column) {
    for (std::size_t row = 0U; row < 4U; ++row) {
      result[row][column] = source.elements[column * 4U + row];
    }
  }
  return result;
}

Matrix4x4 FromOgreMatrix(const Ogre::Matrix4 &source) {
  Matrix4x4 result;
  for (std::size_t column = 0U; column < 4U; ++column) {
    for (std::size_t row = 0U; row < 4U; ++row) {
      result.elements[column * 4U + row] = source[row][column];
    }
  }
  return result;
}

bool NearlyEqual(float lhs, float rhs) noexcept {
  constexpr float kTolerance = 1.0e-6F;
  return std::isfinite(lhs) && std::isfinite(rhs) &&
         std::fabs(lhs - rhs) <=
             kTolerance * std::max(1.0F, std::max(std::fabs(lhs),
                                                  std::fabs(rhs)));
}

bool NearlyEqual(const Ogre::Vector3 &lhs,
                 const Ogre::Vector3 &rhs) noexcept {
  return NearlyEqual(lhs.x, rhs.x) && NearlyEqual(lhs.y, rhs.y) &&
         NearlyEqual(lhs.z, rhs.z);
}

/// A camera basis reaches the frame consumers through
/// Matrix4::inverseAffine(), which inverts by cofactors rather than by
/// transposing the rotation, so even an exactly rigid view matrix comes back
/// orthonormal only to a few float32 ulps. The generic 1.0e-6 NearlyEqual
/// bound sits right at that noise floor: a live session held it for 3,194
/// consecutive frames and then rejected one, which killed the renderer. This
/// bound stays far below any deviation a real defect produces (shear, mirror,
/// non-unit scale, or a stale basis all move these products by >= 1e-2) while
/// admitting pure rounding.
constexpr float kCameraBasisOrthonormalTolerance = 1.0e-4F;

/// True when the basis is orthonormal within float32 inverse-affine rounding.
/// Non-finite components are never admitted.
[[nodiscard]] bool IsRigidOrthonormalCameraBasis(
    const Ogre::Vector3 &right, const Ogre::Vector3 &up,
    const Ogre::Vector3 &forward) noexcept {
  const auto finite = [](const Ogre::Vector3 &v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  };
  if (!finite(right) || !finite(up) || !finite(forward)) {
    return false;
  }
  return std::fabs(right.squaredLength() - 1.0F) <=
             kCameraBasisOrthonormalTolerance &&
         std::fabs(up.squaredLength() - 1.0F) <=
             kCameraBasisOrthonormalTolerance &&
         std::fabs(forward.squaredLength() - 1.0F) <=
             kCameraBasisOrthonormalTolerance &&
         std::fabs(right.dotProduct(up)) <=
             kCameraBasisOrthonormalTolerance;
}

bool NearlyEqual(const Ogre::Aabb &lhs, const Ogre::Aabb &rhs) noexcept {
  return NearlyEqual(lhs.mCenter, rhs.mCenter) &&
         NearlyEqual(lhs.mHalfSize, rhs.mHalfSize);
}

bool NearlyEqualNativeTransformedAabb(const Ogre::Aabb &expected,
                                      const Ogre::Aabb &observed) noexcept {
  return NearlyEqualOgreNextPssmNativeTransformValue(expected.mCenter.x,
                                                      observed.mCenter.x) &&
         NearlyEqualOgreNextPssmNativeTransformValue(expected.mCenter.y,
                                                      observed.mCenter.y) &&
         NearlyEqualOgreNextPssmNativeTransformValue(expected.mCenter.z,
                                                      observed.mCenter.z) &&
         NearlyEqualOgreNextPssmNativeTransformValue(expected.mHalfSize.x,
                                                      observed.mHalfSize.x) &&
         NearlyEqualOgreNextPssmNativeTransformValue(expected.mHalfSize.y,
                                                      observed.mHalfSize.y) &&
         NearlyEqualOgreNextPssmNativeTransformValue(expected.mHalfSize.z,
                                                      observed.mHalfSize.z);
}

bool NearlyEqualNativeTransform(const Ogre::Matrix4 &expected,
                                const Ogre::Matrix4 &observed) noexcept {
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      if (!NearlyEqualOgreNextPssmNativeTransformValue(
              expected[row][column], observed[row][column])) {
        return false;
      }
    }
  }
  return true;
}

OgreNextPssmNativeAabb ObserveNativeAabb(const Ogre::Aabb &aabb) noexcept {
  const Ogre::Vector3 minimum = aabb.getMinimum();
  const Ogre::Vector3 maximum = aabb.getMaximum();
  OgreNextPssmNativeAabb observed;
  observed.minimum = {minimum.x, minimum.y, minimum.z};
  observed.maximum = {maximum.x, maximum.y, maximum.z};
  return observed;
}

bool NearlyEqual(const Ogre::ColourValue &lhs,
                 const Ogre::ColourValue &rhs) noexcept {
  return NearlyEqual(lhs.r, rhs.r) && NearlyEqual(lhs.g, rhs.g) &&
         NearlyEqual(lhs.b, rhs.b) && NearlyEqual(lhs.a, rhs.a);
}

bool NearlyEqual(const Ogre::Matrix4 &lhs,
                 const Ogre::Matrix4 &rhs) noexcept {
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      if (!NearlyEqual(lhs[row][column], rhs[row][column])) {
        return false;
      }
    }
  }
  return true;
}

Ogre::FilterOptions ToOgreFilter(SamplerFilter filter,
                                 bool anisotropic) {
  if (anisotropic) {
    return Ogre::FO_ANISOTROPIC;
  }
  switch (filter) {
  case SamplerFilter::NEAREST:
    return Ogre::FO_POINT;
  case SamplerFilter::LINEAR:
    return Ogre::FO_LINEAR;
  }
  throw std::logic_error("validated RT4/V1 sampler filter became unknown");
}

Ogre::TextureAddressingMode
ToOgreAddressMode(SamplerAddressMode address_mode) {
  switch (address_mode) {
  case SamplerAddressMode::REPEAT:
    return Ogre::TAM_WRAP;
  case SamplerAddressMode::MIRRORED_REPEAT:
    return Ogre::TAM_MIRROR;
  case SamplerAddressMode::CLAMP_TO_EDGE:
    return Ogre::TAM_CLAMP;
  case SamplerAddressMode::CLAMP_TO_BORDER:
    break;
  }
  throw std::logic_error(
      "validated RT4/V1 sampler address mode became unsupported");
}

Ogre::HlmsSamplerblock
ToOgreSampler(const SamplerResourceDescriptor &descriptor) {
  Ogre::HlmsSamplerblock sampler;
  sampler.mMinFilter = ToOgreFilter(descriptor.minification_filter,
                                    descriptor.anisotropy_enabled);
  sampler.mMagFilter = ToOgreFilter(descriptor.magnification_filter,
                                    descriptor.anisotropy_enabled);
  sampler.mMipFilter =
      ToOgreFilter(descriptor.mip_filter, false);
  sampler.mU = ToOgreAddressMode(descriptor.address_u);
  sampler.mV = ToOgreAddressMode(descriptor.address_v);
  sampler.mW = ToOgreAddressMode(descriptor.address_w);
  sampler.mMipLodBias = descriptor.mip_lod_bias;
  sampler.mMaxAnisotropy = descriptor.anisotropy_enabled
                               ? descriptor.maximum_anisotropy
                               : 1.0F;
  sampler.mCompareFunction = Ogre::NUM_COMPARE_FUNCTIONS;
  sampler.mBorderColour = Ogre::ColourValue(
      descriptor.border_color.x, descriptor.border_color.y,
      descriptor.border_color.z, descriptor.border_color.w);
  sampler.mMinLod = descriptor.minimum_lod;
  sampler.mMaxLod = descriptor.maximum_lod;
  return sampler;
}

void VerifySamplerMapping(const Ogre::HlmsSamplerblock &actual,
                          const Ogre::HlmsSamplerblock &expected) {
  Ogre::HlmsSamplerblock actual_copy = actual;
  if (actual_copy != expected) {
    throw std::runtime_error(
        "Ogre-Next RT4/V1 live sampler differs from the reviewed portable mapping");
  }
}

Ogre::HlmsMacroblock
BuildPbsMacroblock(const MaterialDescriptor &descriptor) {
  Ogre::HlmsMacroblock macroblock;
  macroblock.mCullMode = descriptor.double_sided ? Ogre::CULL_NONE
                                                  : Ogre::CULL_CLOCKWISE;
  macroblock.mDepthWrite = descriptor.depth_write;
  return macroblock;
}

Ogre::HlmsBlendblock
BuildPbsBlendblock(const MaterialDescriptor &descriptor) {
  Ogre::HlmsBlendblock blendblock;
  if (descriptor.blend_mode == MaterialBlendMode::STRAIGHT_SOURCE_OVER) {
    blendblock.mSourceBlendFactor = Ogre::SBF_SOURCE_ALPHA;
    blendblock.mDestBlendFactor = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    blendblock.mSourceBlendFactorAlpha = Ogre::SBF_ONE;
    blendblock.mDestBlendFactorAlpha = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    blendblock.mBlendOperation = Ogre::SBO_ADD;
    blendblock.mBlendOperationAlpha = Ogre::SBO_ADD;
    blendblock.calculateSeparateBlendMode();
  } else if (descriptor.blend_mode ==
             MaterialBlendMode::LEGACY_STRAIGHT_ALPHA) {
    // Exact OGRE `scene_blend alpha_blend`, including its squared-alpha
    // destination equation.
    blendblock.setBlendType(Ogre::SBT_TRANSPARENT_ALPHA);
  } else if (descriptor.blend_mode ==
             MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER) {
    // Porter-Duff source-over for content whose RGB already carries its
    // coverage (the transported HUD overlay texture).
    blendblock.mSourceBlendFactor = Ogre::SBF_ONE;
    blendblock.mDestBlendFactor = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    blendblock.mSourceBlendFactorAlpha = Ogre::SBF_ONE;
    blendblock.mDestBlendFactorAlpha = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    blendblock.mBlendOperation = Ogre::SBO_ADD;
    blendblock.mBlendOperationAlpha = Ogre::SBO_ADD;
    blendblock.calculateSeparateBlendMode();
  }
  return blendblock;
}

void VerifyPbsMapping(const Ogre::HlmsPbsDatablock &datablock,
                      const MaterialDescriptor &descriptor) {
  OgreNextN1PbsUv0AffineTransform uv0_affine;
  const ValidationResult uv0_affine_validation =
      BuildOgreNextN1PbsUv0AffineTransform(descriptor, uv0_affine);
  if (!uv0_affine_validation) {
    throw std::logic_error(
        "validated RT4/V1 UV0 affine profile disappeared before PBS verification");
  }
  const Ogre::Vector4 expected_uv0_affine(
      uv0_affine.scale.x, uv0_affine.scale.y,
      uv0_affine.offset.x, uv0_affine.offset.y);
  const Ogre::Vector3 expected_base_color(
      descriptor.base_color_factor.x, descriptor.base_color_factor.y,
      descriptor.base_color_factor.z);
  const Ogre::Vector3 expected_emissive(
      descriptor.emissive_factor.x * descriptor.emissive_strength,
      descriptor.emissive_factor.y * descriptor.emissive_strength,
      descriptor.emissive_factor.z * descriptor.emissive_strength);
  const bool specular_workflow =
      descriptor.pbr_workflow == MaterialPbrWorkflow::SPECULAR;
  const bool thin_slab_transmission =
      descriptor.transmission_mode ==
      MaterialTransmissionMode::THIN_PARALLEL_SLAB;
  const Ogre::Vector3 expected_specular =
      specular_workflow
          ? Ogre::Vector3(descriptor.specular_factor.x,
                          descriptor.specular_factor.y,
                          descriptor.specular_factor.z)
          : Ogre::Vector3::UNIT_SCALE;
  const Ogre::HlmsMacroblock expected_macroblock =
      BuildPbsMacroblock(descriptor);
  Ogre::HlmsBlendblock expected_blendblock = BuildPbsBlendblock(descriptor);
  if (thin_slab_transmission) {
    expected_blendblock.mSourceBlendFactor = Ogre::SBF_ONE;
    expected_blendblock.mDestBlendFactor = Ogre::SBF_ZERO;
    expected_blendblock.setForceTransparentRenderOrder(true);
  }
  // Pinned PBS compares the threshold on the left and sampled alpha on the
  // right. These are therefore the inverse discard comparisons needed to keep
  // the descriptor's fragments exactly.
  const Ogre::CompareFunction expected_alpha_test =
      descriptor.alpha_test_mode == MaterialAlphaTestMode::GREATER
          ? Ogre::CMPF_GREATER_EQUAL
          : descriptor.alpha_test_mode ==
                    MaterialAlphaTestMode::GREATER_EQUAL
                ? Ogre::CMPF_GREATER
                : Ogre::CMPF_ALWAYS_PASS;
  const Ogre::HlmsPbsDatablock::TransparencyModes expected_transparency =
      thin_slab_transmission
          ? Ogre::HlmsPbsDatablock::Refractive
          : descriptor.blend_mode != MaterialBlendMode::REPLACE
          ? Ogre::HlmsPbsDatablock::Fade
          : Ogre::HlmsPbsDatablock::None;
  const float expected_transparency_value =
      thin_slab_transmission
          ? 1.0F - descriptor.transmission_factor
          : descriptor.blend_mode != MaterialBlendMode::REPLACE
          ? descriptor.base_color_factor.w
          : 1.0F;
  const Ogre::Vector4 expected_transmission_attenuation =
      thin_slab_transmission
          ? Ogre::Vector4(descriptor.attenuation_color.x,
                          descriptor.attenuation_color.y,
                          descriptor.attenuation_color.z,
                          descriptor.attenuation_distance_m)
          : Ogre::Vector4::ZERO;
  const Ogre::Vector4 actual_transmission_parameters =
      datablock.getUserValue(2U);
  const bool exact_transmission_parameters =
      thin_slab_transmission
          ? actual_transmission_parameters.x ==
                    descriptor.index_of_refraction &&
                actual_transmission_parameters.y ==
                    descriptor.slab_thickness_m &&
                actual_transmission_parameters.z ==
                    descriptor.transmission_factor &&
                std::isfinite(actual_transmission_parameters.w) &&
                actual_transmission_parameters.w > 0.0F
          : actual_transmission_parameters == Ogre::Vector4::ZERO;
  const float ior_ratio =
      (1.0F - descriptor.index_of_refraction) /
      (1.0F + descriptor.index_of_refraction);
  const float expected_fresnel = ior_ratio * ior_ratio;
  if (datablock.getBrdf() != Ogre::PbsBrdf::Default ||
      datablock.getWorkflow() !=
          (specular_workflow
               ? Ogre::HlmsPbsDatablock::SpecularWorkflow
               : Ogre::HlmsPbsDatablock::MetallicWorkflow) ||
      datablock.getTwoSidedLighting() != descriptor.double_sided ||
      !NearlyEqual(datablock.getDiffuse(), expected_base_color) ||
      !NearlyEqual(datablock.getSpecular(), expected_specular) ||
      (specular_workflow &&
       (!NearlyEqual(datablock.getFresnel().x, expected_fresnel) ||
        datablock.hasSeparateFresnel())) ||
      (!specular_workflow &&
       !NearlyEqual(datablock.getMetalness(), descriptor.metallic_factor)) ||
      !NearlyEqual(datablock.getRoughness(), descriptor.roughness_factor) ||
      !NearlyEqual(datablock.getEmissive(), expected_emissive) ||
      datablock.getNormalMapWeight() != 1.0F ||
      datablock.getMacroblock() == nullptr ||
      *datablock.getMacroblock() != expected_macroblock ||
      datablock.getBlendblock() == nullptr ||
      *datablock.getBlendblock() != expected_blendblock ||
      datablock.getBlendblock()->isAutoTransparent() !=
          (!thin_slab_transmission &&
           descriptor.blend_mode != MaterialBlendMode::REPLACE) ||
      datablock.getBlendblock()->isForcedTransparent() !=
          thin_slab_transmission ||
      datablock.getAlphaTest() != expected_alpha_test ||
      datablock.getAlphaTestShadowCasterOnly() ||
      !NearlyEqual(datablock.getAlphaTestThreshold(),
                   descriptor.alpha_cutoff) ||
      datablock.getTransparencyMode() != expected_transparency ||
      !NearlyEqual(datablock.getTransparency(),
                   expected_transparency_value) ||
      datablock.getUseAlphaFromTextures() != !thin_slab_transmission ||
      !NearlyEqual(datablock.getRefractionStrength(), 0.0F) ||
      datablock.getUserValue(0U) != expected_uv0_affine ||
      datablock.getUserValue(1U) != expected_transmission_attenuation ||
      !exact_transmission_parameters ||
      !OgreNextUvAffinePbs::SelectsUv0AffineShader(&datablock) ||
      OgreNextUvAffinePbs::SelectsThinSlabTransmissionShader(&datablock) !=
          thin_slab_transmission) {
    throw std::runtime_error(
        "Ogre-Next RT4/V1 live PBS datablock differs from the reviewed workflow, UV0 affine, alpha, depth, cull, or blend mapping");
  }
}

void VerifyDisplayDomainUnlitMapping(
    const Ogre::HlmsUnlitDatablock &datablock,
    const Ogre::HlmsUnlit &expected_creator, Ogre::TextureGpu *texture,
    const Ogre::HlmsSamplerblock &sampler,
    const Ogre::HlmsMacroblock &expected_macroblock,
    const Ogre::HlmsBlendblock &expected_blendblock) {
  const Ogre::String *name = datablock.getNameStr();
  const Ogre::ColourValue colour = datablock.getColour();
  if (datablock.getCreator() != &expected_creator || name == nullptr ||
      name->compare(
          0U, sizeof(kOgreNextDisplayDomainDatablockPrefix) - 1U,
          kOgreNextDisplayDomainDatablockPrefix) != 0 ||
      !datablock.hasColour() || colour != Ogre::ColourValue::White ||
      datablock.getTexture(0U) != texture ||
      datablock.getTextureUvSource(0U) != 0U ||
      datablock.getSamplerblock(0U) == nullptr ||
      *datablock.getSamplerblock(0U) != sampler ||
      datablock.getMacroblock() == nullptr ||
      *datablock.getMacroblock() != expected_macroblock ||
      datablock.getBlendblock() == nullptr ||
      *datablock.getBlendblock() != expected_blendblock) {
    throw std::runtime_error(
        "Ogre-Next RT4/V1 display-domain Unlit datablock differs from the reviewed one-texture mapping");
  }
  for (Ogre::uint8 slot = 0U; slot < Ogre::NUM_UNLIT_TEXTURE_TYPES; ++slot) {
    if ((slot != 0U && datablock.getTexture(slot) != nullptr) ||
        datablock.getTextureUvSource(slot) != 0U ||
        datablock.getBlendMode(slot) != Ogre::UNLIT_BLEND_NORMAL_NON_PREMUL ||
        datablock.getEnableAnimationMatrix(slot) ||
        datablock.getEnablePlanarReflection(slot)) {
      throw std::runtime_error(
          "Ogre-Next RT4/V1 display-domain Unlit enabled an unreviewed texture-layer feature");
    }
  }
}

bool DecomposeTrs(const Matrix4x4 &source, Ogre::Vector3 &position,
                  Ogre::Vector3 &scale, Ogre::Quaternion &orientation,
                  Ogre::Matrix4 &reconstructed) {
  const Ogre::Matrix4 matrix = ToOgreMatrix(source);
  matrix.decomposition(position, scale, orientation);
  reconstructed.makeTransform(position, scale, orientation);
  constexpr float kRoundTripTolerance = 2.0e-4F;
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      const float expected = matrix[row][column];
      const float tolerance =
          kRoundTripTolerance * std::max(1.0F, std::fabs(expected));
      if (!std::isfinite(reconstructed[row][column]) ||
          std::fabs(reconstructed[row][column] - expected) > tolerance) {
        return false;
      }
    }
  }
  return true;
}

/// Bit-exact retained-scene comparator. Every descriptor field participates:
/// asset references embed revisions so content changes are visible here, and
/// previous_render_from_object is compared even though the N1 raster path
/// does not consume it natively — it is forwarded to the interop side and
/// must not silently diverge. Float aggregates compare by bits (memcmp of
/// each member, never the whole struct, to keep padding bytes out), so -0.0
/// differs from 0.0 and a NaN equals only its own bit pattern.
static_assert(kSceneSnapshotVersion == 7U,
              "review SameMeshInstanceDescriptor whenever the snapshot "
              "contract changes MeshInstanceDescriptor state");
static_assert(sizeof(MeshInstanceDescriptor) == 248U,
              "MeshInstanceDescriptor changed size; review the retained-scene "
              "field-by-field comparator before accepting the new layout");
bool SameMeshInstanceDescriptor(const MeshInstanceDescriptor &lhs,
                                const MeshInstanceDescriptor &rhs) noexcept {
  return lhs.instance_id == rhs.instance_id && lhs.mesh == rhs.mesh &&
         lhs.material == rhs.material &&
         lhs.topology_revision == rhs.topology_revision &&
         lhs.deformation_revision == rhs.deformation_revision &&
         std::memcmp(lhs.render_from_object.elements.data(),
                     rhs.render_from_object.elements.data(),
                     sizeof(lhs.render_from_object.elements)) == 0 &&
         std::memcmp(lhs.previous_render_from_object.elements.data(),
                     rhs.previous_render_from_object.elements.data(),
                     sizeof(lhs.previous_render_from_object.elements)) == 0 &&
         std::memcmp(&lhs.local_bounds.minimum, &rhs.local_bounds.minimum,
                     sizeof(lhs.local_bounds.minimum)) == 0 &&
         std::memcmp(&lhs.local_bounds.maximum, &rhs.local_bounds.maximum,
                     sizeof(lhs.local_bounds.maximum)) == 0 &&
         lhs.visibility_mask == rhs.visibility_mask &&
         lhs.flags == rhs.flags;
}

RenderOperationResult ResolveShaderMediaRoot(const std::string &configured,
                                             std::string &resolved) {
  if (configured.empty() || configured.find('\0') != std::string::npos) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "Ogre-Next N1 shader media root is empty or contains NUL");
  }
  try {
    const std::filesystem::path requested = std::filesystem::u8path(configured);
    if (!requested.is_absolute()) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "Ogre-Next N1 shader media root must be an absolute runtime path");
    }
    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(requested, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "Ogre-Next N1 shader media root is not a readable directory");
    }

    error.clear();
    if (!std::filesystem::is_directory(canonical / "Hlms", error) || error) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "Ogre-Next N1 shader media root lacks its HLMS directory");
    }
    resolved = canonical.generic_u8string();
    return RenderOperationResult::Success();
  } catch (const std::filesystem::filesystem_error &) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "Ogre-Next N1 shader media root cannot be resolved");
  }
}

RenderOperationResult ValidateShaderArchives(
    const std::string &resolved_media_root) {
  const auto require_paths = [&](Ogre::String data_path,
                                 Ogre::StringVector library_paths) {
    library_paths.push_back(std::move(data_path));
    for (const Ogre::String &relative : library_paths) {
      std::error_code error;
      const std::filesystem::path archive =
          std::filesystem::u8path(resolved_media_root) / relative;
      if (!std::filesystem::is_directory(archive, error) || error) {
        return false;
      }
    }
    return true;
  };
  Ogre::String pbs_data;
  Ogre::StringVector pbs_libraries;
  Ogre::HlmsPbs::getDefaultPaths(pbs_data, pbs_libraries);
  pbs_libraries.emplace_back(kOgreNextUvAffinePbsMediaPath);
  Ogre::String unlit_data;
  Ogre::StringVector unlit_libraries;
  Ogre::HlmsUnlit::getDefaultPaths(unlit_data, unlit_libraries);
  unlit_libraries.emplace_back(kOgreNextDisplayDomainMediaPath);
  if (!require_paths(std::move(pbs_data), std::move(pbs_libraries)) ||
      !require_paths(std::move(unlit_data), std::move(unlit_libraries))) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "Ogre-Next N1 shader media root lacks required PBS/Unlit HLMS archives");
  }
  return RenderOperationResult::Success();
}

Ogre::HlmsPbs *RegisterPbs(Ogre::Root &root,
                           const std::string &resolved_media_root) {
  Ogre::String data_path;
  Ogre::StringVector library_paths;
  Ogre::HlmsPbs::getDefaultPaths(data_path, library_paths);
  library_paths.emplace_back(kOgreNextUvAffinePbsMediaPath);
  const Ogre::String media_root = Ogre::String(resolved_media_root) + "/";
  Ogre::ArchiveManager &archives = Ogre::ArchiveManager::getSingleton();
  Ogre::Archive *data =
      archives.load(media_root + data_path, "FileSystem", true);
  Ogre::ArchiveVec libraries;
  for (const Ogre::String &library_path : library_paths) {
    libraries.push_back(
        archives.load(media_root + library_path, "FileSystem", true));
  }
  Ogre::HlmsPbs *pbs = OGRE_NEW OgreNextUvAffinePbs(data, &libraries);
  root.getHlmsManager()->registerHlms(pbs);
  return pbs;
}

Ogre::HlmsUnlit *RegisterUnlit(Ogre::Root &root,
                               const std::string &resolved_media_root) {
  Ogre::String data_path;
  Ogre::StringVector library_paths;
  Ogre::HlmsUnlit::getDefaultPaths(data_path, library_paths);
  library_paths.emplace_back(kOgreNextDisplayDomainMediaPath);
  const Ogre::String media_root = Ogre::String(resolved_media_root) + "/";
  Ogre::ArchiveManager &archives = Ogre::ArchiveManager::getSingleton();
  Ogre::Archive *data =
      archives.load(media_root + data_path, "FileSystem", true);
  Ogre::ArchiveVec libraries;
  for (const Ogre::String &library_path : library_paths) {
    libraries.push_back(
        archives.load(media_root + library_path, "FileSystem", true));
  }
  Ogre::HlmsUnlit *unlit = OGRE_NEW OgreNextDisplayDomainUnlit(
      data, &libraries);
  unlit->setPrecisionMode(Ogre::Hlms::PrecisionFull32);
  if (unlit->getPrecisionMode() != Ogre::Hlms::PrecisionFull32) {
    OGRE_DELETE unlit;
    throw std::runtime_error(
        "Ogre-Next rejected full32 precision before Unlit registration");
  }
  root.getHlmsManager()->registerHlms(unlit);
  if (unlit->getPrecisionMode() != Ogre::Hlms::PrecisionFull32 ||
      unlit->getSupportedPrecisionMode() != Ogre::Hlms::PrecisionFull32) {
    throw std::runtime_error(
        "Ogre-Next display-domain Unlit did not retain supported full32 precision");
  }
  return unlit;
}

RenderOperationResult NotInitialized() {
  return RenderOperationResult::Failure(RenderOperationCode::NOT_INITIALIZED,
                                        "Ogre-Next N1 is not initialized");
}

RenderOperationResult WrongThread() {
  return RenderOperationResult::Failure(
      RenderOperationCode::INVALID_ARGUMENT,
      "Ogre-Next N1 call was made from a thread other than its owner");
}

RenderOperationResult FaultedFrontend() {
  return RenderOperationResult::Failure(
      RenderOperationCode::BACKEND_FAILURE,
      "Ogre-Next N1 is fault-latched after incomplete native teardown; Shutdown is required");
}

RenderOperationResult FrameCleanupFailure() {
  return RenderOperationResult::Failure(
      RenderOperationCode::BACKEND_FAILURE,
      "Ogre-Next N1 could not completely tear down its offscreen frame resources and was fault-latched");
}

RenderOperationResult NativeTeardownFailure(const char *operation) {
  return RenderOperationResult::Failure(
      RenderOperationCode::BACKEND_FAILURE,
      std::string(operation) +
          " could not completely release Ogre-Next native resources");
}

RenderOperationResult BackendFailure(const Ogre::Exception &error) {
  return RenderOperationResult::Failure(RenderOperationCode::BACKEND_FAILURE,
                                        error.getFullDescription());
}

bool TryComputeReadbackLayout(std::uint32_t width, std::uint32_t height,
                              PixelFormat format,
                              std::uint64_t &row_pitch_bytes,
                              std::size_t &total_bytes) noexcept {
  const std::uint64_t bytes_per_pixel =
      format == PixelFormat::RGBA16_FLOAT ? 8U : 4U;
  if (width == 0U || height == 0U ||
      static_cast<std::uint64_t>(width) >
          (std::numeric_limits<std::uint64_t>::max)() / bytes_per_pixel) {
    return false;
  }
  row_pitch_bytes = static_cast<std::uint64_t>(width) * bytes_per_pixel;
  if (static_cast<std::uint64_t>(height) >
      (std::numeric_limits<std::uint64_t>::max)() / row_pitch_bytes) {
    return false;
  }
  const std::uint64_t total = row_pitch_bytes * height;
  if (total > (std::numeric_limits<std::size_t>::max)()) {
    return false;
  }
  total_bytes = static_cast<std::size_t>(total);
  return true;
}

void CreateAndVerifyPssmShadowNode(
    Ogre::CompositorManager2 &compositors,
    const Ogre::RenderSystemCapabilities &capabilities,
    const std::string &shadow_node_name,
    std::uint32_t native_visibility_mask) {
  Ogre::ShadowNodeHelper::ShadowParam parameter{};
  parameter.supportedLightTypes = 0U;
  parameter.addLightType(Ogre::Light::LT_DIRECTIONAL);
  parameter.technique = Ogre::SHADOWMAP_PSSM;
  parameter.numPssmSplits =
      static_cast<Ogre::uint8>(kOgreNextPssmCascadeCount);
  parameter.atlasId = 0U;
  for (std::size_t index = 0U; index < kOgreNextPssmCascadeCount;
       ++index) {
    const OgreNextPssmCascadeLayout &layout =
        kOgreNextPssmCascadeLayouts[index];
    parameter.resolution[index] =
        Ogre::ShadowNodeHelper::Resolution(layout.width, layout.height);
    parameter.atlasStart[index] =
        Ogre::ShadowNodeHelper::Resolution(layout.atlas_x, layout.atlas_y);
  }
  Ogre::ShadowNodeHelper::ShadowParamVec parameters;
  parameters.push_back(parameter);
  Ogre::ShadowNodeHelper::createShadowNodeWithSettings(
      &compositors, &capabilities, shadow_node_name, parameters, false,
      1024U, kOgreNextPssmLambda, kOgreNextPssmSplitPaddingMeters,
      kOgreNextPssmSplitBlend, kOgreNextPssmSplitFade,
      kOgreNextPssmStableCascadeCount, native_visibility_mask,
      kOgreNextPssmXyPadding, 0U, 255U);
  // Programmatic definitions remain in Ogre's unfinished maps until this
  // explicit validation boundary. Never let addWorkspace() select or repair
  // an unreviewed fallback node on our behalf.
  compositors.validateAllNodes();

  Ogre::CompositorShadowNodeDef *definition =
      compositors.getShadowNodeDefinitionNonConst(
          Ogre::IdString(shadow_node_name));
  if (definition == nullptr ||
      definition->getNumShadowTextureDefinitions() !=
          kOgreNextPssmCascadeCount ||
      definition->getNumTargetPasses() !=
          kOgreNextPssmCascadeCount + 1U) {
    throw std::runtime_error(
        "Ogre-Next PSSM helper substituted the reviewed three-cascade node topology");
  }
  const auto &textures = definition->getLocalTextureDefinitions();
  if (textures.size() != 1U ||
      textures.front().width != kOgreNextPssmAtlasWidth ||
      textures.front().height != kOgreNextPssmAtlasHeight ||
      textures.front().format != Ogre::PFG_D32_FLOAT ||
      textures.front().depthBufferFormat != Ogre::PFG_D32_FLOAT ||
      textures.front().preferDepthTexture || textures.front().fsaa != "1" ||
      textures.front().textureFlags !=
          (Ogre::TextureFlags::RenderToTexture |
           Ogre::TextureFlags::DiscardableContent)) {
    throw std::runtime_error(
        "Ogre-Next PSSM helper substituted the reviewed 2048x3072 D32_FLOAT atlas");
  }

  for (std::size_t index = 0U; index < kOgreNextPssmCascadeCount;
       ++index) {
    Ogre::ShadowTextureDefinition *shadow =
        definition->getShadowTextureDefinitionNonConst(index);
    shadow->constantBiasScale = kOgreNextPssmConstantBiasScale;
    shadow->normalOffsetBias = kOgreNextPssmNormalOffsetBias;
    shadow->autoConstantBiasScale = kOgreNextPssmAutoConstantBiasScale;
    shadow->autoNormalOffsetBiasScale =
        kOgreNextPssmAutoNormalOffsetBiasScale;
    const OgreNextPssmCascadeLayout &layout =
        kOgreNextPssmCascadeLayouts[index];
    const Ogre::Vector2 expected_offset(
        static_cast<float>(layout.atlas_x) /
            static_cast<float>(kOgreNextPssmAtlasWidth),
        static_cast<float>(layout.atlas_y) /
            static_cast<float>(kOgreNextPssmAtlasHeight));
    const Ogre::Vector2 expected_length(
        static_cast<float>(layout.width) /
            static_cast<float>(kOgreNextPssmAtlasWidth),
        static_cast<float>(layout.height) /
            static_cast<float>(kOgreNextPssmAtlasHeight));
    if (shadow->light != 0U || shadow->split != index ||
        shadow->shadowMapTechnique != Ogre::SHADOWMAP_PSSM ||
        shadow->numSplits != kOgreNextPssmCascadeCount ||
        shadow->numStableSplits != kOgreNextPssmStableCascadeCount ||
        !NearlyEqual(shadow->uvOffset.x, expected_offset.x) ||
        !NearlyEqual(shadow->uvOffset.y, expected_offset.y) ||
        !NearlyEqual(shadow->uvLength.x, expected_length.x) ||
        !NearlyEqual(shadow->uvLength.y, expected_length.y) ||
        !NearlyEqual(shadow->pssmLambda, kOgreNextPssmLambda) ||
        !NearlyEqual(shadow->splitPadding,
                     kOgreNextPssmSplitPaddingMeters) ||
        !NearlyEqual(shadow->splitBlend, kOgreNextPssmSplitBlend) ||
        !NearlyEqual(shadow->splitFade, kOgreNextPssmSplitFade) ||
        !NearlyEqual(shadow->xyPadding, kOgreNextPssmXyPadding) ||
        !NearlyEqual(shadow->constantBiasScale,
                     kOgreNextPssmConstantBiasScale) ||
        !NearlyEqual(shadow->normalOffsetBias,
                     kOgreNextPssmNormalOffsetBias) ||
        !NearlyEqual(shadow->autoConstantBiasScale,
                     kOgreNextPssmAutoConstantBiasScale) ||
        !NearlyEqual(shadow->autoNormalOffsetBiasScale,
                     kOgreNextPssmAutoNormalOffsetBiasScale)) {
      throw std::runtime_error(
          "Ogre-Next PSSM native definition differs from the reviewed cascade policy");
    }
  }

  const std::uint32_t expected_shadow_visibility_mask =
      native_visibility_mask | Ogre::VisibilityFlags::LAYER_SHADOW_CASTER;
  for (std::size_t target_index = 1U;
       target_index < kOgreNextPssmCascadeCount + 1U; ++target_index) {
    const Ogre::CompositorPassDefVec &passes =
        definition->getTargetPass(target_index)->getCompositorPasses();
    const auto *scene_pass =
        passes.size() == 1U
            ? dynamic_cast<const Ogre::CompositorPassSceneDef *>(passes.front())
            : nullptr;
    if (scene_pass == nullptr ||
        scene_pass->mShadowMapIdx != target_index - 1U ||
        scene_pass->mIncludeOverlays ||
        scene_pass->mVisibilityMask != expected_shadow_visibility_mask) {
      std::ostringstream detail;
      detail << "Ogre-Next PSSM caster pass " << target_index
             << " lost stable cascade ordering or UI-free visibility"
             << " (passes=" << passes.size() << ", scene="
             << (scene_pass != nullptr) << ", map="
             << (scene_pass != nullptr ? scene_pass->mShadowMapIdx : 999U)
             << ", overlays="
             << (scene_pass != nullptr && scene_pass->mIncludeOverlays)
             << ", mask="
             << (scene_pass != nullptr ? scene_pass->mVisibilityMask : 0U)
             << ", expected_mask=" << expected_shadow_visibility_mask << ')';
      throw std::runtime_error(detail.str());
    }
  }
}

void BindAndVerifyPssmWorkspace(
    Ogre::CompositorManager2 &compositors,
    const std::string &main_node_name,
    const std::string &shadow_node_name,
    std::uint32_t scene_pass_identifier = 0U) {
  Ogre::CompositorNodeDef *node =
      compositors.getNodeDefinitionNonConst(
          Ogre::IdString(main_node_name));
  if (node == nullptr) {
    throw std::runtime_error(
        "Ogre-Next main workspace topology changed before PSSM binding");
  }
  Ogre::CompositorPassSceneDef *scene_pass = nullptr;
  std::size_t matching_scene_passes = 0U;
  for (std::size_t target_index = 0U;
       target_index < node->getNumTargetPasses(); ++target_index) {
    const Ogre::CompositorPassDefVec &passes =
        node->getTargetPass(target_index)->getCompositorPasses();
    for (Ogre::CompositorPassDef *pass : passes) {
      auto *candidate = dynamic_cast<Ogre::CompositorPassSceneDef *>(pass);
      if (candidate != nullptr &&
          (scene_pass_identifier == 0U ||
           candidate->mIdentifier == scene_pass_identifier)) {
        scene_pass = candidate;
        ++matching_scene_passes;
      }
    }
  }
  if (scene_pass == nullptr || matching_scene_passes != 1U ||
      (scene_pass_identifier == 0U && node->getNumTargetPasses() != 1U)) {
    throw std::runtime_error(
        "Ogre-Next main workspace has no single UI-free scene pass");
  }
  scene_pass->mIncludeOverlays = false;
  scene_pass->mShadowNode = Ogre::IdString(shadow_node_name);
  scene_pass->mShadowNodeRecalculation = Ogre::SHADOW_NODE_FIRST_ONLY;
  std::size_t bound_shadow_scene_passes = 0U;
  for (std::size_t target_index = 0U;
       target_index < node->getNumTargetPasses(); ++target_index) {
    for (Ogre::CompositorPassDef *pass :
         node->getTargetPass(target_index)->getCompositorPasses()) {
      const auto *candidate =
          dynamic_cast<const Ogre::CompositorPassSceneDef *>(pass);
      if (candidate != nullptr &&
          candidate->mShadowNode == Ogre::IdString(shadow_node_name)) {
        ++bound_shadow_scene_passes;
      }
      if (scene_pass_identifier != 0U && candidate != nullptr &&
          candidate->mIdentifier != scene_pass_identifier &&
          candidate->mShadowNode != Ogre::IdString()) {
        throw std::runtime_error(
            "Ogre-Next bound PSSM outside the selected raster-lit scene pass");
      }
    }
  }
  if (scene_pass->mIncludeOverlays ||
      scene_pass->mShadowNode != Ogre::IdString(shadow_node_name) ||
      scene_pass->mShadowNodeRecalculation !=
          Ogre::SHADOW_NODE_FIRST_ONLY ||
      bound_shadow_scene_passes != 1U) {
    throw std::runtime_error(
        "Ogre-Next substituted the programmatic PSSM workspace binding");
  }
}

void UnbindAndVerifyPssmWorkspace(
    Ogre::CompositorManager2 &compositors,
    const std::string &main_node_name,
    std::uint32_t scene_pass_identifier) {
  Ogre::CompositorNodeDef *node = compositors.getNodeDefinitionNonConst(
      Ogre::IdString(main_node_name));
  if (node == nullptr || scene_pass_identifier == 0U) {
    throw std::runtime_error(
        "Ogre-Next HDR node disappeared before staged PSSM unbind");
  }
  Ogre::CompositorPassSceneDef *selected = nullptr;
  std::size_t matching_scene_passes = 0U;
  for (std::size_t target_index = 0U;
       target_index < node->getNumTargetPasses(); ++target_index) {
    for (Ogre::CompositorPassDef *pass :
         node->getTargetPass(target_index)->getCompositorPasses()) {
      auto *scene = dynamic_cast<Ogre::CompositorPassSceneDef *>(pass);
      if (scene != nullptr && scene->mIdentifier == scene_pass_identifier) {
        selected = scene;
        ++matching_scene_passes;
      }
    }
  }
  if (selected == nullptr || matching_scene_passes != 1U) {
    throw std::runtime_error(
        "Ogre-Next single-evaluation HDR scene pass disappeared before PSSM unbind");
  }
  selected->mShadowNode = Ogre::IdString();
  selected->mShadowNodeRecalculation = Ogre::SHADOW_NODE_FIRST_ONLY;
  std::size_t bound_shadow_scene_passes = 0U;
  for (std::size_t target_index = 0U;
       target_index < node->getNumTargetPasses(); ++target_index) {
    for (Ogre::CompositorPassDef *pass :
         node->getTargetPass(target_index)->getCompositorPasses()) {
      const auto *scene = dynamic_cast<const Ogre::CompositorPassSceneDef *>(pass);
      if (scene != nullptr && scene->mShadowNode != Ogre::IdString()) {
        ++bound_shadow_scene_passes;
      }
    }
  }
  if (selected->mShadowNode != Ogre::IdString() ||
      bound_shadow_scene_passes != 0U) {
    throw std::runtime_error(
        "Ogre-Next single-evaluation HDR scene retained a shadow binding after unbind");
  }
}

struct NativePssmReadback final {
  OgreNextPssmSplitPolicy splits;
  std::array<float, kOgreNextPssmCascadeCount> normal_offset_bias{};
};

NativePssmReadback ReadAndVerifyNativePssmState(
    Ogre::CompositorWorkspace &workspace,
    const std::string &shadow_node_name) {
  Ogre::CompositorShadowNode *shadow_node =
      workspace.findShadowNode(Ogre::IdString(shadow_node_name));
  const auto *native_splits =
      shadow_node != nullptr ? shadow_node->getPssmSplits(0U) : nullptr;
  const auto *native_blends =
      shadow_node != nullptr ? shadow_node->getPssmBlends(0U) : nullptr;
  const Ogre::Real *native_fade =
      shadow_node != nullptr ? shadow_node->getPssmFade(0U) : nullptr;
  OgreNextPssmSplitPolicy expected;
  if (shadow_node == nullptr || native_splits == nullptr ||
      native_blends == nullptr || native_fade == nullptr ||
      shadow_node->getNumActiveShadowCastingLights() != 1U ||
      native_splits->size() != kOgreNextPssmCascadeCount + 1U ||
      native_blends->size() != kOgreNextPssmCascadeCount - 1U ||
      !TryBuildOgreNextPssmSplitPolicy(expected)) {
    std::ostringstream detail;
    detail << "Ogre-Next PSSM runtime did not expose the reviewed cascade split state"
           << " (node=" << (shadow_node != nullptr)
           << ", splits="
           << (native_splits != nullptr ? native_splits->size() : 0U)
           << ", blends="
           << (native_blends != nullptr ? native_blends->size() : 0U)
           << ", fade=" << (native_fade != nullptr)
           << ", active_lights="
           << (shadow_node != nullptr
                   ? shadow_node->getNumActiveShadowCastingLights()
                   : 0U)
           << ')';
    throw std::runtime_error(detail.str());
  }
  NativePssmReadback observed;
  for (std::size_t index = 0U; index < native_splits->size(); ++index) {
    observed.splits.split_points[index] = (*native_splits)[index];
    if (!NearlyEqual(observed.splits.split_points[index],
                     expected.split_points[index])) {
      throw std::runtime_error(
          "Ogre-Next PSSM runtime substituted a cascade split point");
    }
  }
  for (std::size_t index = 0U; index < native_blends->size(); ++index) {
    observed.splits.blend_points[index] = (*native_blends)[index];
    if (!NearlyEqual(observed.splits.blend_points[index],
                     expected.blend_points[index])) {
      throw std::runtime_error(
          "Ogre-Next PSSM runtime substituted a cascade blend point");
    }
  }
  observed.splits.fade_point = *native_fade;
  if (!NearlyEqual(observed.splits.fade_point, expected.fade_point)) {
    throw std::runtime_error(
        "Ogre-Next PSSM runtime substituted the terminal fade point");
  }
  for (std::size_t index = 0U; index < kOgreNextPssmCascadeCount; ++index) {
    if (!shadow_node->isShadowMapIdxActive(index)) {
      throw std::runtime_error(
          "Ogre-Next PSSM runtime left a reviewed cascade inactive");
    }
    observed.normal_offset_bias[index] =
        shadow_node->getNormalOffsetBias(index);
    if (!std::isfinite(observed.normal_offset_bias[index]) ||
        observed.normal_offset_bias[index] <
            kOgreNextPssmNormalOffsetBias) {
      throw std::runtime_error(
          "Ogre-Next PSSM runtime normal-offset bias readback is invalid");
    }
  }
  return observed;
}

void ConfigureAndVerifyPssmProjection(
    Ogre::Camera &camera, const OgreNextPssmProjectionExtents &extents,
    const Ogre::Matrix4 &expected_projection) {
  camera.setProjectionType(Ogre::PT_PERSPECTIVE);
  camera.setCustomProjectionMatrix(false);
  camera.setFrustumExtents(extents.left, extents.right, extents.top,
                           extents.bottom, Ogre::FET_TAN_HALF_ANGLES);
  Ogre::Real observed_left = 0.0F;
  Ogre::Real observed_right = 0.0F;
  Ogre::Real observed_top = 0.0F;
  Ogre::Real observed_bottom = 0.0F;
  camera.getFrustumExtents(observed_left, observed_right, observed_top,
                           observed_bottom, Ogre::FET_TAN_HALF_ANGLES);
  if (camera.isCustomProjectionMatrixEnabled() ||
      !NearlyEqual(observed_left, extents.left) ||
      !NearlyEqual(observed_right, extents.right) ||
      !NearlyEqual(observed_top, extents.top) ||
      !NearlyEqual(observed_bottom, extents.bottom) ||
      !NearlyEqual(camera.getProjectionMatrix(), expected_projection)) {
    throw std::runtime_error(
        "Ogre-Next could not reproduce the portable PSSM projection with split-stable tangent extents");
  }
}

struct PssmD32AtlasProbeResult final {
  bool attempted = false;
  bool supported = false;
  bool allocation_verified = false;
  bool readback_verified = false;
  bool cleanup_verified = false;
  std::uint64_t cleanup_absence_checks = 0U;
};

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
void MaybeInjectPssmFailureStage(OgreNextN1PssmFailureStage configured,
                                 bool &pending,
                                 OgreNextN1PssmFailureStage expected,
                                 const char *detail) {
  if (pending && configured == expected) {
    pending = false;
    throw std::runtime_error(detail);
  }
}
#endif

PssmD32AtlasProbeResult
ProbePssmD32Atlas(Ogre::TextureGpuManager &texture_manager
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
                  ,
                  OgreNextN1PssmFailureStage failure_stage,
                  bool &failure_pending,
                  bool retain_content_evidence,
                  std::uint64_t &content_readbacks
#endif
) {
  constexpr char kProbeTextureName[] = "RoRPssmD32AtlasCapabilityProbe";
  if (texture_manager.findTextureNoThrow(Ogre::IdString(kProbeTextureName)) !=
      nullptr) {
    throw std::runtime_error(
        "Ogre-Next PSSM D32 capability-probe name was already registered");
  }

  PssmD32AtlasProbeResult result;
  result.attempted = true;
  Ogre::TextureGpu *texture = nullptr;
  std::exception_ptr operation_failure;
  try {
    texture = texture_manager.createTexture(
        kProbeTextureName, Ogre::GpuPageOutStrategy::Discard,
        Ogre::TextureFlags::RenderToTexture |
            Ogre::TextureFlags::DiscardableContent,
        Ogre::TextureTypes::Type2D);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    MaybeInjectPssmFailureStage(
        failure_stage, failure_pending,
        OgreNextN1PssmFailureStage::AFTER_D32_ATLAS_CREATE,
        "injected PSSM D32 post-create rollback failure");
#endif
    texture->setResolution(kOgreNextPssmAtlasWidth, kOgreNextPssmAtlasHeight);
    texture->setNumMipmaps(1U);
    texture->setPixelFormat(Ogre::PFG_D32_FLOAT);
    texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
    texture_manager.waitForStreamingCompletion();
    if (!texture->isDataReady() ||
        texture->getResidencyStatus() != Ogre::GpuResidency::Resident ||
        texture->getWidth() != kOgreNextPssmAtlasWidth ||
        texture->getHeight() != kOgreNextPssmAtlasHeight ||
        texture->getPixelFormat() != Ogre::PFG_D32_FLOAT) {
      throw std::runtime_error(
          "Ogre-Next did not allocate the exact resident PSSM D32 atlas");
    }
    result.allocation_verified = true;

    // Isolated artifacts may retain a staging-ticket download to prove actual
    // D32 content access. Production proves the exact resident allocation and
    // then exercises that format in the live shadow graph without any content
    // download.
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    if (retain_content_evidence) {
      ++content_readbacks;
      Ogre::Image2 readback;
      readback.convertFromTexture(texture, 0U, 0U);
      const Ogre::TextureBox pixels = readback.getData(0U);
      if (readback.getWidth() != kOgreNextPssmAtlasWidth ||
          readback.getHeight() != kOgreNextPssmAtlasHeight ||
          readback.getPixelFormat() != Ogre::PFG_D32_FLOAT ||
          pixels.width != kOgreNextPssmAtlasWidth ||
          pixels.height != kOgreNextPssmAtlasHeight || pixels.data == nullptr) {
        throw std::runtime_error(
            "Ogre-Next PSSM D32 atlas staging readback metadata changed");
      }
      result.readback_verified = true;
    }
#endif
  } catch (...) {
    operation_failure = std::current_exception();
  }

  bool cleanup_complete = false;
  bool absence_lookup_completed = false;
  bool absence_proven = false;
  if (texture == nullptr) {
    try {
      texture =
          texture_manager.findTextureNoThrow(Ogre::IdString(kProbeTextureName));
      absence_lookup_completed = true;
      absence_proven = texture == nullptr;
    } catch (...) {
      absence_lookup_completed = false;
      absence_proven = false;
    }
  }
  if (texture != nullptr) {
    bool destroy_returned = false;
    try {
      texture_manager.destroyTexture(texture);
      texture = nullptr;
      texture_manager.waitForStreamingCompletion();
      destroy_returned = true;
    } catch (...) {
      texture = nullptr;
      destroy_returned = false;
    }
    if (destroy_returned) {
      try {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        MaybeInjectPssmFailureStage(
            failure_stage, failure_pending,
            OgreNextN1PssmFailureStage::DURING_D32_ATLAS_CLEANUP_LOOKUP,
            "injected PSSM D32 cleanup absence-lookup failure");
#endif
        absence_proven = texture_manager.findTextureNoThrow(
                             Ogre::IdString(kProbeTextureName)) == nullptr;
        absence_lookup_completed = true;
      } catch (...) {
        absence_lookup_completed = false;
        absence_proven = false;
      }
    }
    cleanup_complete =
        destroy_returned && absence_lookup_completed && absence_proven;
  } else if (absence_lookup_completed) {
    cleanup_complete = absence_proven;
  }
  result.cleanup_verified = cleanup_complete;
  result.cleanup_absence_checks = cleanup_complete ? 1U : 0U;
  if (!cleanup_complete) {
    throw std::runtime_error(
        "Ogre-Next PSSM D32 capability probe could not prove its atlas absent");
  }
  if (operation_failure != nullptr) {
    // Native allocation, transition, and readback errors are backend failures,
    // never capability-based skips. The caller may classify only static,
    // explicitly queried device limits as unsupported.
    std::rethrow_exception(operation_failure);
  }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  const bool exact_evidence =
      !retain_content_evidence || result.readback_verified;
#else
  constexpr bool exact_evidence = true;
#endif
  result.supported = result.allocation_verified && exact_evidence &&
                     result.cleanup_verified;
  return result;
}

constexpr const char kOgreNextHdrResourceGroup[] = "RoROgreNextHdrV2";
// The production workspace connects the stock HdrRenderUi node after
// postprocessing so the transported menu/HUD composites post-tonemap. The
// former UI-free workspace name is retired with its topology.
constexpr const char kOgreNextHdrWorkspace[] = "RoRHdrWorkspaceHudV1";
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
constexpr const char kOgreNextHdrUiOverlayControlWorkspace[] =
    "RoRHdrWorkspaceUiOverlayControlV3";
#endif
constexpr const char kOgreNextHdrRenderingNode[] = "HdrRenderingNode";
constexpr const char kOgreNextHdrPostprocessingNode[] =
    "HdrPostprocessingNode";
constexpr const char kOgreNextHdrUiNode[] = "HdrRenderUi";
constexpr const char kOgreNextHdrShadowNode[] = "RoRHdrPssmShadowNodeV1";
constexpr const char kOgreNextHdrBaseTexture[] = "RoRBaseHdr";
constexpr const char kOgreNextHdrSunFullTexture[] = "RoRSunFullHdr";
constexpr const char kOgreNextHdrRasterLitTexture[] = "rt0";
constexpr const char kOgreNextHdrSunDirectSignedTexture[] =
    "RoRSunDirectSignedHdr";
constexpr const char kOgreNextHdrSunDirectTexture[] = "RoRSunDirectHdr";
constexpr const char kOgreNextHdrVisibilityTexture[] = "RoRVisibility";
constexpr const char kOgreNextHdrLitTexture[] = "RoRLitHdr";
constexpr const char kOgreNextHdrOpaqueDepthTexture[] = "RoROpaqueDepth";
constexpr const char kOgreNextHdrHistoryTexture[] = "oldLumRt";
constexpr const char kOgreNextThinSlabNode[] =
    "RoRThinSlabRefractionNodeV1";
constexpr const char kOgreNextThinSlabInputTexture[] =
    "RoRThinSlabInputHdr";
constexpr const char kOgreNextThinSlabDepthInput[] =
    "RoRThinSlabOpaqueDepth";
constexpr const char kOgreNextThinSlabBackgroundTexture[] =
    "RoRThinSlabBackgroundHdr";
constexpr const char kOgreNextThinSlabOutputTexture[] =
    "RoRThinSlabOutputHdr";
constexpr const char kOgreNextAerialHazeNode[] = "RoRAerialHazeNodeV1";
constexpr const char kOgreNextAerialHazeInputTexture[] = "RoRHazeInputHdr";
constexpr const char kOgreNextAerialHazeDepthInput[] = "RoRHazeOpaqueDepth";
constexpr const char kOgreNextAerialHazeOutputTexture[] = "RoRHazeOutputHdr";
constexpr const char kOgreNextAerialHazeMaterial[] = "RoR/HDR/AerialHaze";
constexpr const char kOgreNextHdrSunVisibilityV2ContinuationWorkspace[] =
    "RoRHdrSunVisibilityV2Continuation";
constexpr const char kOgreNextHdrSubtractMaterial[] =
    "RoR/HDR/SunDirectSubtract";
constexpr const char kOgreNextHdrClampMaterial[] = "RoR/HDR/SunDirectClamp";
constexpr std::uint8_t kOgreNextHdrSplitExecutionMask = 0x01U;
constexpr std::uint8_t kOgreNextHdrPostExecutionMask = 0x02U;
constexpr std::uint8_t kOgreNextThinSlabRenderQueue = 200U;
constexpr std::uint32_t kOgreNextHdrBaseScenePassIdentifier = 0x524f5201U;
constexpr std::uint32_t kOgreNextHdrSunFullScenePassIdentifier = 0x524f5202U;
constexpr std::uint32_t kOgreNextHdrRasterLitScenePassIdentifier = 0x524f5203U;
constexpr std::uint32_t kOgreNextHdrSingleScenePassIdentifier = 0x524f5204U;
constexpr std::uint32_t kOgreNextThinSlabScenePassIdentifier = 0x524f5205U;
// RT4 reserves bits 28-29 from authored geometry. The node keeps bit 29 out of
// Base as an explicit topology invariant, but the pinned PBS global
// directional-light path does not honor per-pass light masks. The listener
// below therefore performs the actual exact power-zero/restore transaction.
// Native sun lights remain on the authored visibility mask so Ogre's shadow
// selector sees both an authored layer and its internal caster bit.
constexpr std::uint32_t kOgreNextHdrDirectionalSunLightVisibility = 1U << 29U;

Ogre::MaterialPtr CreateAndVerifyHdrBlendMaterial(
    const char *name, Ogre::SceneBlendOperation operation) {
  Ogre::MaterialManager &materials = Ogre::MaterialManager::getSingleton();
  if (materials.getByName(name, kOgreNextHdrResourceGroup)) {
    throw std::runtime_error(
        "Ogre-Next HDR split material identity is not empty");
  }
  Ogre::MaterialPtr copy = std::static_pointer_cast<Ogre::Material>(
      materials.load("Ogre/Copy/4xFP32",
                     Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME));
  if (!copy || copy->getNumTechniques() != 1U ||
      copy->getTechnique(0U) == nullptr ||
      copy->getTechnique(0U)->getNumPasses() != 1U) {
    throw std::runtime_error(
        "Ogre-Next HDR split copy material topology changed");
  }
  Ogre::MaterialPtr material =
      copy->clone(name, true, kOgreNextHdrResourceGroup);
  Ogre::Pass *pass = material->getTechnique(0U)->getPass(0U);
  if (pass == nullptr || pass->getBlendblock() == nullptr) {
    throw std::runtime_error(
        "Ogre-Next HDR split copy pass has no blend state");
  }
  Ogre::HlmsBlendblock blend = *pass->getBlendblock();
  blend.mSourceBlendFactor = Ogre::SBF_ONE;
  blend.mDestBlendFactor = Ogre::SBF_ONE;
  blend.mSourceBlendFactorAlpha = Ogre::SBF_ONE;
  blend.mDestBlendFactorAlpha = Ogre::SBF_ONE;
  blend.mBlendOperation = operation;
  blend.mBlendOperationAlpha = operation;
  // The split radiance contract owns RGB only. Keeping alpha writes disabled
  // preserves the target clear: signed/direct remain canonical zero while
  // Base/SunFull/RasterLit scene targets remain canonical one.
  blend.mBlendChannelMask = Ogre::HlmsBlendblock::BlendChannelRed |
                            Ogre::HlmsBlendblock::BlendChannelGreen |
                            Ogre::HlmsBlendblock::BlendChannelBlue;
  blend.calculateSeparateBlendMode();
  pass->setBlendblock(blend);
  material->load();

  const Ogre::HlmsBlendblock *observed = pass->getBlendblock();
  if (observed == nullptr ||
      observed->mSourceBlendFactor != Ogre::SBF_ONE ||
      observed->mDestBlendFactor != Ogre::SBF_ONE ||
      observed->mSourceBlendFactorAlpha != Ogre::SBF_ONE ||
      observed->mDestBlendFactorAlpha != Ogre::SBF_ONE ||
      observed->mBlendOperation != operation ||
      observed->mBlendOperationAlpha != operation ||
      observed->mBlendChannelMask !=
          (Ogre::HlmsBlendblock::BlendChannelRed |
           Ogre::HlmsBlendblock::BlendChannelGreen |
           Ogre::HlmsBlendblock::BlendChannelBlue) ||
      pass->getFragmentProgramName() !=
          copy->getTechnique(0U)->getPass(0U)->getFragmentProgramName()) {
    throw std::runtime_error(
        "Ogre-Next HDR split material failed exact native blend readback");
  }
  return material;
}

void CreateAndVerifyHdrLightingSplitNode(
    Ogre::CompositorManager2 &compositors,
    bool &owns_node_definition, bool enable_sun_visibility_v2) {
  const Ogre::IdString node_name(kOgreNextHdrRenderingNode);
  if (owns_node_definition || !compositors.hasNodeDefinition(node_name)) {
    throw std::runtime_error(
        "Ogre-Next stock HDR rendering node was not parsed");
  }
  // Preserve the stable node identity consumed by Ogre's stock HDR post stack,
  // while replacing its single scene evaluation with the exact linear split.
  compositors.removeNodeDefinition(node_name);
  Ogre::CompositorNodeDef *node =
      compositors.addNodeDefinition(kOgreNextHdrRenderingNode);
  owns_node_definition = true;

  (void)CreateAndVerifyHdrBlendMaterial(kOgreNextHdrSubtractMaterial,
                                        Ogre::SBO_SUBTRACT);
  (void)CreateAndVerifyHdrBlendMaterial(kOgreNextHdrClampMaterial,
                                        Ogre::SBO_MAX);

  node->setNumLocalTextureDefinitions(enable_sun_visibility_v2 ? 9U : 7U);
  const auto add_rgba16 = [&](const char *name, bool needs_depth,
                              bool external_uav = false) {
    Ogre::TextureDefinitionBase::TextureDefinition *texture =
        node->addTextureDefinition(name);
    texture->textureType = Ogre::TextureTypes::Type2D;
    texture->width = 0U;
    texture->height = 0U;
    texture->depthOrSlices = 1U;
    texture->numMipmaps = 1U;
    texture->format = Ogre::PFG_RGBA16_FLOAT;
    texture->fsaa = "1";
    texture->textureFlags = Ogre::TextureFlags::RenderToTexture |
                            (external_uav
                                 ? Ogre::TextureFlags::Uav
                                 : Ogre::TextureFlags::DiscardableContent);
    texture->depthBufferId =
        needs_depth ? 1U : Ogre::DepthBuffer::POOL_NO_DEPTH;
    Ogre::RenderTargetViewDef *view = node->addRenderTextureView(name);
    Ogre::RenderTargetViewEntry attachment;
    attachment.textureName = name;
    view->colourAttachments.push_back(attachment);
    view->depthBufferId = texture->depthBufferId;
  };
  add_rgba16(kOgreNextHdrBaseTexture, true, enable_sun_visibility_v2);
  add_rgba16(kOgreNextHdrSunFullTexture, true);
  add_rgba16(kOgreNextHdrRasterLitTexture, true);
  add_rgba16(kOgreNextHdrSunDirectSignedTexture, false);
  add_rgba16(kOgreNextHdrSunDirectTexture, false,
             enable_sun_visibility_v2);
  Ogre::TextureDefinitionBase::TextureDefinition *history =
      node->addTextureDefinition(kOgreNextHdrHistoryTexture);
  history->textureType = Ogre::TextureTypes::Type2D;
  history->width = 1U;
  history->height = 1U;
  history->depthOrSlices = 1U;
  history->numMipmaps = 1U;
  history->format = Ogre::PFG_R16_FLOAT;
  history->fsaa = "1";
  history->textureFlags = Ogre::TextureFlags::RenderToTexture;
  history->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
  Ogre::RenderTargetViewDef *history_view =
      node->addRenderTextureView(kOgreNextHdrHistoryTexture);
  Ogre::RenderTargetViewEntry history_attachment;
  history_attachment.textureName = kOgreNextHdrHistoryTexture;
  history_view->colourAttachments.push_back(history_attachment);
  history_view->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
  if (enable_sun_visibility_v2) {
    Ogre::TextureDefinitionBase::TextureDefinition *visibility =
        node->addTextureDefinition(kOgreNextHdrVisibilityTexture);
    visibility->textureType = Ogre::TextureTypes::Type2D;
    visibility->width = 0U;
    visibility->height = 0U;
    visibility->depthOrSlices = 1U;
    visibility->numMipmaps = 1U;
    visibility->format = Ogre::PFG_R16_FLOAT;
    visibility->fsaa = "1";
    visibility->textureFlags = Ogre::TextureFlags::RenderToTexture |
                               Ogre::TextureFlags::Uav;
    visibility->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
    add_rgba16(kOgreNextHdrLitTexture, false, true);
  }
  Ogre::TextureDefinitionBase::TextureDefinition *opaque_depth =
      node->addTextureDefinition(kOgreNextHdrOpaqueDepthTexture);
  opaque_depth->textureType = Ogre::TextureTypes::Type2D;
  opaque_depth->width = 0U;
  opaque_depth->height = 0U;
  opaque_depth->depthOrSlices = 1U;
  opaque_depth->numMipmaps = 1U;
  opaque_depth->format = Ogre::PFG_D32_FLOAT;
  opaque_depth->fsaa = "1";
  opaque_depth->textureFlags = Ogre::TextureFlags::RenderToTexture;
  opaque_depth->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
  for (const char *target_name : {kOgreNextHdrBaseTexture,
                                  kOgreNextHdrSunFullTexture,
                                  kOgreNextHdrRasterLitTexture}) {
    Ogre::RenderTargetViewDef *view =
        node->getRenderTargetViewDefNonConstNoThrow(
            Ogre::IdString(target_name));
    if (view == nullptr) {
      throw std::runtime_error(
          "Ogre-Next HDR split lost a scene render-target view");
    }
    view->depthAttachment.textureName = kOgreNextHdrOpaqueDepthTexture;
    view->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
  }

  const Ogre::ColourValue clear_hdr(6.667F, 13.333F, 20.0F, 1.0F);
  const auto add_scene = [&](const char *target_name,
                             std::uint32_t identifier,
                             std::uint32_t light_visibility,
                             bool update_lod) {
    Ogre::CompositorTargetDef *target = node->addTargetPass(target_name);
    target->setNumPasses(1U);
    auto *scene = static_cast<Ogre::CompositorPassSceneDef *>(
        target->addPass(Ogre::PASS_SCENE));
    scene->mIdentifier = identifier;
    scene->mExecutionMask = kOgreNextHdrSplitExecutionMask;
    scene->mFirstRQ = 0U;
    scene->mLastRQ = kOgreNextThinSlabRenderQueue;
    scene->mIncludeOverlays = false;
    scene->mEnableForwardPlus = true;
    scene->mUpdateLodLists = update_lod;
    scene->setVisibilityMask(kOgreNextRt4AuthoredVisibilityMask);
    scene->setLightVisibilityMask(light_visibility);
    scene->setAllLoadActions(Ogre::LoadAction::Clear);
    scene->setAllClearColours(clear_hdr);
    scene->mClearDepth = 1.0F;
    scene->mStoreActionColour[0] = Ogre::StoreAction::Store;
    scene->mStoreActionDepth = Ogre::StoreAction::Store;
    scene->mStoreActionStencil = Ogre::StoreAction::DontCare;
  };

  node->setNumTargetPass(6U);
  const std::uint32_t all_lights =
      Ogre::VisibilityFlags::RESERVED_VISIBILITY_FLAGS;
  add_scene(kOgreNextHdrBaseTexture, kOgreNextHdrBaseScenePassIdentifier,
            all_lights & ~kOgreNextHdrDirectionalSunLightVisibility, true);
  add_scene(kOgreNextHdrSunFullTexture,
            kOgreNextHdrSunFullScenePassIdentifier, all_lights, false);
  add_scene(kOgreNextHdrRasterLitTexture,
            kOgreNextHdrRasterLitScenePassIdentifier, all_lights, false);

  Ogre::CompositorTargetDef *signed_target =
      node->addTargetPass(kOgreNextHdrSunDirectSignedTexture);
  signed_target->setNumPasses(2U);
  auto *copy_base = static_cast<Ogre::CompositorPassQuadDef *>(
      signed_target->addPass(Ogre::PASS_QUAD));
  copy_base->mMaterialIsHlms = false;
  copy_base->mExecutionMask = kOgreNextHdrSplitExecutionMask;
  copy_base->mMaterialName = "Ogre/Copy/4xFP32";
  copy_base->mUseQuad = false;
  copy_base->addQuadTextureSource(0U, kOgreNextHdrBaseTexture);
  copy_base->setAllLoadActions(Ogre::LoadAction::Clear);
  copy_base->setAllClearColours(Ogre::ColourValue::Black);
  copy_base->mStoreActionColour[0] = Ogre::StoreAction::Store;
  auto *subtract_sun_full = static_cast<Ogre::CompositorPassQuadDef *>(
      signed_target->addPass(Ogre::PASS_QUAD));
  subtract_sun_full->mMaterialIsHlms = false;
  subtract_sun_full->mExecutionMask = kOgreNextHdrSplitExecutionMask;
  subtract_sun_full->mMaterialName = kOgreNextHdrSubtractMaterial;
  subtract_sun_full->mUseQuad = false;
  subtract_sun_full->addQuadTextureSource(0U, kOgreNextHdrSunFullTexture);
  subtract_sun_full->mLoadActionColour[0] = Ogre::LoadAction::Load;
  subtract_sun_full->mStoreActionColour[0] = Ogre::StoreAction::Store;

  Ogre::CompositorTargetDef *direct_target =
      node->addTargetPass(kOgreNextHdrSunDirectTexture);
  direct_target->setNumPasses(2U);
  auto *clear_direct = static_cast<Ogre::CompositorPassClearDef *>(
      direct_target->addPass(Ogre::PASS_CLEAR));
  clear_direct->setBuffersToClear(Ogre::RenderPassDescriptor::Colour0);
  clear_direct->mExecutionMask = kOgreNextHdrSplitExecutionMask;
  clear_direct->setAllClearColours(
      Ogre::ColourValue(0.0F, 0.0F, 0.0F, 0.0F));
  auto *clamp_direct = static_cast<Ogre::CompositorPassQuadDef *>(
      direct_target->addPass(Ogre::PASS_QUAD));
  clamp_direct->mMaterialIsHlms = false;
  clamp_direct->mExecutionMask = kOgreNextHdrSplitExecutionMask;
  clamp_direct->mMaterialName = kOgreNextHdrClampMaterial;
  clamp_direct->mUseQuad = false;
  clamp_direct->addQuadTextureSource(0U,
                                     kOgreNextHdrSunDirectSignedTexture);
  clamp_direct->mLoadActionColour[0] = Ogre::LoadAction::Load;
  clamp_direct->mStoreActionColour[0] = Ogre::StoreAction::Store;

  Ogre::CompositorTargetDef *history_target =
      node->addTargetPass(kOgreNextHdrHistoryTexture);
  history_target->setNumPasses(1U);
  auto *clear_history = static_cast<Ogre::CompositorPassClearDef *>(
      history_target->addPass(Ogre::PASS_CLEAR));
  clear_history->mNumInitialPasses = 1U;
  clear_history->mExecutionMask = kOgreNextHdrSplitExecutionMask;
  clear_history->setBuffersToClear(Ogre::RenderPassDescriptor::Colour0);
  clear_history->setAllClearColours(
      Ogre::ColourValue(0.01F, 0.01F, 0.01F, 1.0F));

  node->setNumOutputChannels(enable_sun_visibility_v2 ? 7U : 5U);
  node->mapOutputChannel(0U, kOgreNextHdrRasterLitTexture);
  node->mapOutputChannel(1U, kOgreNextHdrHistoryTexture);
  node->mapOutputChannel(2U, kOgreNextHdrBaseTexture);
  node->mapOutputChannel(3U, kOgreNextHdrSunDirectTexture);
  if (enable_sun_visibility_v2) {
    node->mapOutputChannel(4U, kOgreNextHdrVisibilityTexture);
    node->mapOutputChannel(5U, kOgreNextHdrLitTexture);
    node->mapOutputChannel(6U, kOgreNextHdrOpaqueDepthTexture);
  } else {
    node->mapOutputChannel(4U, kOgreNextHdrOpaqueDepthTexture);
  }

  const auto &textures = node->getLocalTextureDefinitions();
  const std::size_t expected_texture_count =
      enable_sun_visibility_v2 ? 9U : 7U;
  const std::uint32_t expected_output_count =
      enable_sun_visibility_v2 ? 7U : 5U;
  if (textures.size() != expected_texture_count ||
      node->getNumTargetPasses() != 6U ||
      node->getNumOutputChannels() != expected_output_count ||
      node->calculateNumPasses() != 8U) {
    throw std::runtime_error(
        "Ogre-Next HDR split node topology failed exact definition readback");
  }
  for (std::size_t index = 0U; index < 5U; ++index) {
    if (textures[index].format != Ogre::PFG_RGBA16_FLOAT ||
        textures[index].width != 0U || textures[index].height != 0U ||
        textures[index].depthOrSlices != 1U ||
        textures[index].numMipmaps != 1U) {
      throw std::runtime_error(
          "Ogre-Next HDR split linear target metadata changed");
    }
  }
  if (textures[5U].getName() != Ogre::IdString(kOgreNextHdrHistoryTexture) ||
      textures[5U].format != Ogre::PFG_R16_FLOAT ||
      textures[5U].width != 1U || textures[5U].height != 1U ||
      textures[5U].textureFlags != Ogre::TextureFlags::RenderToTexture) {
    throw std::runtime_error(
        "Ogre-Next HDR split history target metadata changed");
  }
  if (enable_sun_visibility_v2) {
    const std::uint32_t required = Ogre::TextureFlags::RenderToTexture |
                                   Ogre::TextureFlags::Uav;
    if (textures[0U].textureFlags != required ||
        textures[4U].textureFlags != required ||
        textures[6U].getName() !=
            Ogre::IdString(kOgreNextHdrVisibilityTexture) ||
        textures[6U].format != Ogre::PFG_R16_FLOAT ||
        textures[6U].textureFlags != required ||
        textures[7U].getName() != Ogre::IdString(kOgreNextHdrLitTexture) ||
        textures[7U].format != Ogre::PFG_RGBA16_FLOAT ||
        textures[7U].textureFlags != required ||
        textures[8U].getName() !=
            Ogre::IdString(kOgreNextHdrOpaqueDepthTexture) ||
        textures[8U].format != Ogre::PFG_D32_FLOAT ||
        textures[8U].textureFlags != Ogre::TextureFlags::RenderToTexture) {
      throw std::runtime_error(
          "Ogre-Next sun-visibility V2 target usage or format changed");
    }
  }

  const auto exact_scene = [&](std::size_t target_index,
                               const char *target_name,
                               std::uint32_t identifier,
                               std::uint32_t light_mask,
                               bool update_lod) {
    Ogre::CompositorTargetDef *target = node->getTargetPass(target_index);
    const Ogre::CompositorPassDefVec &passes =
        target->getCompositorPasses();
    const auto *scene =
        passes.size() == 1U
            ? dynamic_cast<const Ogre::CompositorPassSceneDef *>(
                  passes.front())
            : nullptr;
    return target->getRenderTargetName() == Ogre::IdString(target_name) &&
           scene != nullptr && scene->mIdentifier == identifier &&
           scene->mExecutionMask == kOgreNextHdrSplitExecutionMask &&
           scene->mVisibilityMask == kOgreNextRt4AuthoredVisibilityMask &&
           scene->mLightVisibilityMask == light_mask &&
           scene->mShadowNode == Ogre::IdString() &&
           scene->mIncludeOverlays == false &&
           scene->mUpdateLodLists == update_lod &&
           scene->mLoadActionColour[0] == Ogre::LoadAction::Clear &&
           scene->mStoreActionColour[0] == Ogre::StoreAction::Store;
  };
  if (!exact_scene(0U, kOgreNextHdrBaseTexture,
                   kOgreNextHdrBaseScenePassIdentifier,
                   all_lights &
                       ~kOgreNextHdrDirectionalSunLightVisibility,
                   true) ||
      !exact_scene(1U, kOgreNextHdrSunFullTexture,
                   kOgreNextHdrSunFullScenePassIdentifier, all_lights,
                   false) ||
      !exact_scene(2U, kOgreNextHdrRasterLitTexture,
                   kOgreNextHdrRasterLitScenePassIdentifier, all_lights,
                   false)) {
    throw std::runtime_error(
        "Ogre-Next HDR split scene masks or immutable state changed");
  }
  const auto exact_quad = [&](std::size_t target_index,
                              std::size_t pass_index,
                              const char *target_name,
                              const char *material_name,
                              const char *source_name) {
    Ogre::CompositorTargetDef *target = node->getTargetPass(target_index);
    const Ogre::CompositorPassDefVec &passes =
        target->getCompositorPasses();
    const auto *quad =
        pass_index < passes.size()
            ? dynamic_cast<const Ogre::CompositorPassQuadDef *>(
                  passes[pass_index])
            : nullptr;
    return target->getRenderTargetName() == Ogre::IdString(target_name) &&
           quad != nullptr && !quad->mMaterialIsHlms &&
           quad->mExecutionMask == kOgreNextHdrSplitExecutionMask &&
           quad->mMaterialName == material_name &&
           quad->getTextureSources().size() == 1U &&
           quad->getTextureSources().front().texUnitIdx == 0U &&
           quad->getTextureSources().front().textureName ==
               Ogre::IdString(source_name);
  };
  if (!exact_quad(3U, 0U, kOgreNextHdrSunDirectSignedTexture,
                  "Ogre/Copy/4xFP32", kOgreNextHdrBaseTexture) ||
      !exact_quad(3U, 1U, kOgreNextHdrSunDirectSignedTexture,
                  kOgreNextHdrSubtractMaterial,
                  kOgreNextHdrSunFullTexture) ||
      !exact_quad(4U, 1U, kOgreNextHdrSunDirectTexture,
                  kOgreNextHdrClampMaterial,
                  kOgreNextHdrSunDirectSignedTexture)) {
    throw std::runtime_error(
        "Ogre-Next HDR split GPU subtraction or clamp topology changed");
  }
}

void CreateAndVerifyThinSlabRefractionNode(
    Ogre::CompositorManager2 &compositors) {
  const Ogre::IdString node_name(kOgreNextThinSlabNode);
  if (compositors.hasNodeDefinition(node_name)) {
    throw std::runtime_error(
        "Ogre-Next thin-slab refraction node identity is not empty");
  }
  Ogre::CompositorNodeDef *node =
      compositors.addNodeDefinition(kOgreNextThinSlabNode);
  node->addTextureSourceName(
      kOgreNextThinSlabInputTexture, 0U,
      Ogre::TextureDefinitionBase::TEXTURE_INPUT);
  node->addTextureSourceName(
      kOgreNextThinSlabDepthInput, 1U,
      Ogre::TextureDefinitionBase::TEXTURE_INPUT);
  node->setNumLocalTextureDefinitions(2U);
  const auto add_rgba16 = [&](const char *name, bool with_depth) {
    Ogre::TextureDefinitionBase::TextureDefinition *texture =
        node->addTextureDefinition(name);
    texture->textureType = Ogre::TextureTypes::Type2D;
    texture->width = 0U;
    texture->height = 0U;
    texture->depthOrSlices = 1U;
    texture->numMipmaps = 1U;
    texture->format = Ogre::PFG_RGBA16_FLOAT;
    texture->fsaa = "1";
    texture->textureFlags = Ogre::TextureFlags::RenderToTexture |
                            Ogre::TextureFlags::DiscardableContent;
    texture->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
    Ogre::RenderTargetViewDef *view = node->addRenderTextureView(name);
    Ogre::RenderTargetViewEntry colour;
    colour.textureName = name;
    view->colourAttachments.push_back(colour);
    view->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
    if (with_depth) {
      view->depthAttachment.textureName = kOgreNextThinSlabDepthInput;
      view->depthReadOnly = true;
    }
  };
  add_rgba16(kOgreNextThinSlabBackgroundTexture, false);
  add_rgba16(kOgreNextThinSlabOutputTexture, true);

  node->setNumTargetPass(2U);
  Ogre::CompositorTargetDef *background_target =
      node->addTargetPass(kOgreNextThinSlabBackgroundTexture);
  background_target->setNumPasses(1U);
  auto *copy_background = static_cast<Ogre::CompositorPassQuadDef *>(
      background_target->addPass(Ogre::PASS_QUAD));
  copy_background->mMaterialIsHlms = false;
  copy_background->mMaterialName = "Ogre/Copy/4xFP32";
  copy_background->mExecutionMask = kOgreNextHdrPostExecutionMask;
  copy_background->mUseQuad = false;
  copy_background->addQuadTextureSource(0U,
                                        kOgreNextThinSlabInputTexture);
  copy_background->setAllLoadActions(Ogre::LoadAction::DontCare);
  copy_background->mStoreActionColour[0] = Ogre::StoreAction::Store;

  Ogre::CompositorTargetDef *output_target =
      node->addTargetPass(kOgreNextThinSlabOutputTexture);
  output_target->setNumPasses(2U);
  auto *copy_input = static_cast<Ogre::CompositorPassQuadDef *>(
      output_target->addPass(Ogre::PASS_QUAD));
  copy_input->mMaterialIsHlms = false;
  copy_input->mMaterialName = "Ogre/Copy/4xFP32";
  copy_input->mExecutionMask = kOgreNextHdrPostExecutionMask;
  copy_input->mUseQuad = false;
  copy_input->addQuadTextureSource(0U, kOgreNextThinSlabInputTexture);
  copy_input->setAllLoadActions(Ogre::LoadAction::DontCare);
  copy_input->mStoreActionColour[0] = Ogre::StoreAction::Store;

  auto *scene = static_cast<Ogre::CompositorPassSceneDef *>(
      output_target->addPass(Ogre::PASS_SCENE));
  scene->mIdentifier = kOgreNextThinSlabScenePassIdentifier;
  scene->mExecutionMask = kOgreNextHdrPostExecutionMask;
  scene->mFirstRQ = kOgreNextThinSlabRenderQueue;
  scene->mLastRQ = kOgreNextThinSlabRenderQueue + 1U;
  scene->mIncludeOverlays = false;
  scene->mEnableForwardPlus = true;
  scene->mUpdateLodLists = false;
  scene->setVisibilityMask(kOgreNextRt4AuthoredVisibilityMask);
  scene->setLightVisibilityMask(
      Ogre::VisibilityFlags::RESERVED_VISIBILITY_FLAGS);
  scene->mLoadActionColour[0] = Ogre::LoadAction::Load;
  scene->mLoadActionDepth = Ogre::LoadAction::Load;
  scene->mLoadActionStencil = Ogre::LoadAction::DontCare;
  scene->mStoreActionColour[0] = Ogre::StoreAction::Store;
  scene->mStoreActionDepth = Ogre::StoreAction::DontCare;
  scene->mStoreActionStencil = Ogre::StoreAction::DontCare;
  scene->setUseRefractions(Ogre::IdString(kOgreNextThinSlabDepthInput),
                           Ogre::IdString(
                               kOgreNextThinSlabBackgroundTexture));

  node->setNumOutputChannels(1U);
  node->mapOutputChannel(0U, kOgreNextThinSlabOutputTexture);
  const auto &textures = node->getLocalTextureDefinitions();
  const Ogre::CompositorPassDefVec &background_passes =
      node->getTargetPass(0U)->getCompositorPasses();
  const Ogre::CompositorPassDefVec &output_passes =
      node->getTargetPass(1U)->getCompositorPasses();
  const auto *verified_scene =
      output_passes.size() == 2U
          ? dynamic_cast<const Ogre::CompositorPassSceneDef *>(
                output_passes[1U])
          : nullptr;
  if (textures.size() != 2U || node->getNumTargetPasses() != 2U ||
      node->getNumOutputChannels() != 1U ||
      node->calculateNumPasses() != 3U || background_passes.size() != 1U ||
      verified_scene == nullptr ||
      verified_scene->mIdentifier !=
          kOgreNextThinSlabScenePassIdentifier ||
      verified_scene->mFirstRQ != kOgreNextThinSlabRenderQueue ||
      verified_scene->mLastRQ != kOgreNextThinSlabRenderQueue + 1U ||
      verified_scene->mDepthTextureNoMsaa !=
          Ogre::IdString(kOgreNextThinSlabDepthInput) ||
      verified_scene->mRefractionsTexture !=
          Ogre::IdString(kOgreNextThinSlabBackgroundTexture)) {
    throw std::runtime_error(
        "Ogre-Next thin-slab refraction node topology failed exact readback");
  }
}

// Aerial perspective. One full-screen quad between the single-evaluation
// scene node and the stock HDR post node: it consumes the scene's linear
// RGBA16F radiance on channel 0 and the scene's exported D32 depth on
// channel 1, and writes the hazed radiance on output channel 0. The pass is
// still pre-tonemap, so the HUD (which HdrRenderUi composites afterwards) can
// never be hazed, and the auto-exposure chain measures the hazed image -
// exactly as it already adapts to the sky dome.
//
// Deliberately NOT defined for the DIRECTIONAL_SPLIT_V2 showcase: that
// topology's node/probe schemas stay frozen.
void CreateAndVerifyAerialHazeNode(Ogre::CompositorManager2 &compositors) {
  const Ogre::IdString node_name(kOgreNextAerialHazeNode);
  if (compositors.hasNodeDefinition(node_name)) {
    throw std::runtime_error(
        "Ogre-Next aerial haze node identity is not empty");
  }
  Ogre::CompositorNodeDef *node =
      compositors.addNodeDefinition(kOgreNextAerialHazeNode);
  node->addTextureSourceName(kOgreNextAerialHazeInputTexture, 0U,
                             Ogre::TextureDefinitionBase::TEXTURE_INPUT);
  node->addTextureSourceName(kOgreNextAerialHazeDepthInput, 1U,
                             Ogre::TextureDefinitionBase::TEXTURE_INPUT);

  node->setNumLocalTextureDefinitions(1U);
  Ogre::TextureDefinitionBase::TextureDefinition *output =
      node->addTextureDefinition(kOgreNextAerialHazeOutputTexture);
  output->textureType = Ogre::TextureTypes::Type2D;
  output->width = 0U;
  output->height = 0U;
  output->depthOrSlices = 1U;
  output->numMipmaps = 1U;
  // Same format as the input so the shader's sky / zero-haze early-out is a
  // bit-exact point copy rather than a re-quantization.
  output->format = Ogre::PFG_RGBA16_FLOAT;
  output->fsaa = "1";
  output->textureFlags = Ogre::TextureFlags::RenderToTexture |
                         Ogre::TextureFlags::DiscardableContent;
  output->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
  Ogre::RenderTargetViewDef *view =
      node->addRenderTextureView(kOgreNextAerialHazeOutputTexture);
  Ogre::RenderTargetViewEntry colour;
  colour.textureName = kOgreNextAerialHazeOutputTexture;
  view->colourAttachments.push_back(colour);
  view->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;

  node->setNumTargetPass(1U);
  Ogre::CompositorTargetDef *target =
      node->addTargetPass(kOgreNextAerialHazeOutputTexture);
  target->setNumPasses(1U);
  auto *quad = static_cast<Ogre::CompositorPassQuadDef *>(
      target->addPass(Ogre::PASS_QUAD));
  quad->mMaterialIsHlms = false;
  quad->mMaterialName = kOgreNextAerialHazeMaterial;
  quad->mUseQuad = false;
  quad->addQuadTextureSource(0U, kOgreNextAerialHazeInputTexture);
  quad->addQuadTextureSource(1U, kOgreNextAerialHazeDepthInput);
  quad->setAllLoadActions(Ogre::LoadAction::DontCare);
  quad->mStoreActionColour[0] = Ogre::StoreAction::Store;
  // No execution mask: the single-scene workspace runs the full 0xff mask.

  node->setNumOutputChannels(1U);
  node->mapOutputChannel(0U, kOgreNextAerialHazeOutputTexture);

  const auto &textures = node->getLocalTextureDefinitions();
  const Ogre::CompositorPassDefVec &passes =
      node->getTargetPass(0U)->getCompositorPasses();
  const auto *verified_quad =
      passes.size() == 1U ? dynamic_cast<const Ogre::CompositorPassQuadDef *>(
                                passes.front())
                          : nullptr;
  const bool exact_quad_sources =
      verified_quad != nullptr &&
      verified_quad->getTextureSources().size() == 2U &&
      verified_quad->getTextureSources()[0U].texUnitIdx == 0U &&
      verified_quad->getTextureSources()[0U].textureName ==
          Ogre::IdString(kOgreNextAerialHazeInputTexture) &&
      verified_quad->getTextureSources()[1U].texUnitIdx == 1U &&
      verified_quad->getTextureSources()[1U].textureName ==
          Ogre::IdString(kOgreNextAerialHazeDepthInput);
  if (textures.size() != 1U || node->getNumTargetPasses() != 1U ||
      node->getNumOutputChannels() != 1U ||
      node->calculateNumPasses() != 1U ||
      textures[0U].getName() !=
          Ogre::IdString(kOgreNextAerialHazeOutputTexture) ||
      textures[0U].format != Ogre::PFG_RGBA16_FLOAT ||
      textures[0U].width != 0U || textures[0U].height != 0U ||
      textures[0U].depthBufferId != Ogre::DepthBuffer::POOL_NO_DEPTH ||
      verified_quad == nullptr || verified_quad->mMaterialIsHlms ||
      verified_quad->mMaterialName != kOgreNextAerialHazeMaterial ||
      verified_quad->mUseQuad || !exact_quad_sources) {
    throw std::runtime_error(
        "Ogre-Next aerial haze node topology failed exact definition readback");
  }
}

void CreateAndVerifyHdrSingleSceneNode(
    Ogre::CompositorManager2 &compositors,
    bool &owns_node_definition) {
  const Ogre::IdString node_name(kOgreNextHdrRenderingNode);
  if (owns_node_definition || !compositors.hasNodeDefinition(node_name)) {
    throw std::runtime_error(
        "Ogre-Next stock HDR rendering node was not parsed");
  }
  // Keep the stock node identity consumed by HdrPostprocessingNode, but give
  // the production PSSM path one and only one linear scene evaluation.
  compositors.removeNodeDefinition(node_name);
  Ogre::CompositorNodeDef *node =
      compositors.addNodeDefinition(kOgreNextHdrRenderingNode);
  owns_node_definition = true;

  node->setNumLocalTextureDefinitions(3U);
  Ogre::TextureDefinitionBase::TextureDefinition *scene_texture =
      node->addTextureDefinition(kOgreNextHdrRasterLitTexture);
  scene_texture->textureType = Ogre::TextureTypes::Type2D;
  scene_texture->width = 0U;
  scene_texture->height = 0U;
  scene_texture->depthOrSlices = 1U;
  scene_texture->numMipmaps = 1U;
  scene_texture->format = Ogre::PFG_RGBA16_FLOAT;
  scene_texture->fsaa = "1";
  scene_texture->textureFlags = Ogre::TextureFlags::RenderToTexture |
                                Ogre::TextureFlags::DiscardableContent;
  // The scene's depth is an explicit node-owned D32 texture rather than a
  // pooled depth buffer, so a later pass can sample it. This is the same
  // pattern the DIRECTIONAL_SPLIT_V2 node already uses for RoROpaqueDepth.
  scene_texture->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
  Ogre::RenderTargetViewDef *scene_view =
      node->addRenderTextureView(kOgreNextHdrRasterLitTexture);
  Ogre::RenderTargetViewEntry scene_attachment;
  scene_attachment.textureName = kOgreNextHdrRasterLitTexture;
  scene_view->colourAttachments.push_back(scene_attachment);
  scene_view->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;

  Ogre::TextureDefinitionBase::TextureDefinition *history =
      node->addTextureDefinition(kOgreNextHdrHistoryTexture);
  history->textureType = Ogre::TextureTypes::Type2D;
  history->width = 1U;
  history->height = 1U;
  history->depthOrSlices = 1U;
  history->numMipmaps = 1U;
  history->format = Ogre::PFG_R16_FLOAT;
  history->fsaa = "1";
  history->textureFlags = Ogre::TextureFlags::RenderToTexture;
  history->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
  Ogre::RenderTargetViewDef *history_view =
      node->addRenderTextureView(kOgreNextHdrHistoryTexture);
  Ogre::RenderTargetViewEntry history_attachment;
  history_attachment.textureName = kOgreNextHdrHistoryTexture;
  history_view->colourAttachments.push_back(history_attachment);
  history_view->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;

  Ogre::TextureDefinitionBase::TextureDefinition *opaque_depth =
      node->addTextureDefinition(kOgreNextHdrOpaqueDepthTexture);
  opaque_depth->textureType = Ogre::TextureTypes::Type2D;
  opaque_depth->width = 0U;
  opaque_depth->height = 0U;
  opaque_depth->depthOrSlices = 1U;
  opaque_depth->numMipmaps = 1U;
  opaque_depth->format = Ogre::PFG_D32_FLOAT;
  opaque_depth->fsaa = "1";
  opaque_depth->textureFlags = Ogre::TextureFlags::RenderToTexture;
  opaque_depth->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;
  scene_view->depthAttachment.textureName = kOgreNextHdrOpaqueDepthTexture;

  node->setNumTargetPass(2U);
  Ogre::CompositorTargetDef *scene_target =
      node->addTargetPass(kOgreNextHdrRasterLitTexture);
  scene_target->setNumPasses(1U);
  auto *scene = static_cast<Ogre::CompositorPassSceneDef *>(
      scene_target->addPass(Ogre::PASS_SCENE));
  scene->mIdentifier = kOgreNextHdrSingleScenePassIdentifier;
  scene->mFirstRQ = 0U;
  scene->mLastRQ = kOgreNextPccReservedRenderQueue;
  scene->mIncludeOverlays = false;
  scene->mEnableForwardPlus = true;
  scene->mUpdateLodLists = true;
  scene->setVisibilityMask(kOgreNextRt4AuthoredVisibilityMask);
  scene->setLightVisibilityMask(Ogre::VisibilityFlags::RESERVED_VISIBILITY_FLAGS);
  scene->setAllLoadActions(Ogre::LoadAction::Clear);
  scene->setAllClearColours(Ogre::ColourValue(6.667F, 13.333F, 20.0F, 1.0F));
  // The clear stays exactly 1.0 and the store becomes Store: a later pass
  // reads this depth, and the sky dome and particles deliberately never write
  // it (mDepthCheck/mDepthWrite false for the dome), so a background texel is
  // bit-exactly the 1.0 clear value. That exactness is what lets the aerial
  // haze pass identify sky pixels with a plain d >= 1.0 test.
  scene->mClearDepth = 1.0F;
  scene->mStoreActionColour[0] = Ogre::StoreAction::Store;
  scene->mStoreActionDepth = Ogre::StoreAction::Store;
  scene->mStoreActionStencil = Ogre::StoreAction::DontCare;

  Ogre::CompositorTargetDef *history_target =
      node->addTargetPass(kOgreNextHdrHistoryTexture);
  history_target->setNumPasses(1U);
  auto *clear_history = static_cast<Ogre::CompositorPassClearDef *>(
      history_target->addPass(Ogre::PASS_CLEAR));
  clear_history->mNumInitialPasses = 1U;
  clear_history->setBuffersToClear(Ogre::RenderPassDescriptor::Colour0);
  clear_history->setAllClearColours(
      Ogre::ColourValue(0.01F, 0.01F, 0.01F, 1.0F));

  node->setNumOutputChannels(3U);
  node->mapOutputChannel(0U, kOgreNextHdrRasterLitTexture);
  node->mapOutputChannel(1U, kOgreNextHdrHistoryTexture);
  node->mapOutputChannel(2U, kOgreNextHdrOpaqueDepthTexture);

  const auto &textures = node->getLocalTextureDefinitions();
  const Ogre::CompositorPassDefVec &scene_passes =
      node->getTargetPass(0U)->getCompositorPasses();
  const Ogre::CompositorPassDefVec &history_passes =
      node->getTargetPass(1U)->getCompositorPasses();
  const auto *verified_scene =
      scene_passes.size() == 1U
          ? dynamic_cast<const Ogre::CompositorPassSceneDef *>(
                scene_passes.front())
          : nullptr;
  const auto *verified_history =
      history_passes.size() == 1U
          ? dynamic_cast<const Ogre::CompositorPassClearDef *>(
                history_passes.front())
          : nullptr;
  // Depth adds a texture and an output channel but no pass, so the target and
  // pass counts below stay exactly 2.
  if (textures.size() != 3U || node->getNumTargetPasses() != 2U ||
      node->getNumOutputChannels() != 3U || node->calculateNumPasses() != 2U ||
      textures[0U].getName() !=
          Ogre::IdString(kOgreNextHdrRasterLitTexture) ||
      textures[0U].format != Ogre::PFG_RGBA16_FLOAT ||
      textures[0U].width != 0U || textures[0U].height != 0U ||
      textures[0U].depthBufferId != Ogre::DepthBuffer::POOL_NO_DEPTH ||
      textures[1U].getName() != Ogre::IdString(kOgreNextHdrHistoryTexture) ||
      textures[1U].format != Ogre::PFG_R16_FLOAT ||
      textures[1U].width != 1U || textures[1U].height != 1U ||
      textures[2U].getName() !=
          Ogre::IdString(kOgreNextHdrOpaqueDepthTexture) ||
      textures[2U].format != Ogre::PFG_D32_FLOAT ||
      textures[2U].width != 0U || textures[2U].height != 0U ||
      textures[2U].textureFlags != Ogre::TextureFlags::RenderToTexture ||
      textures[2U].depthBufferId != Ogre::DepthBuffer::POOL_NO_DEPTH ||
      scene_view->depthAttachment.textureName !=
          Ogre::IdString(kOgreNextHdrOpaqueDepthTexture) ||
      verified_scene == nullptr ||
      verified_scene->mIdentifier != kOgreNextHdrSingleScenePassIdentifier ||
      verified_scene->mVisibilityMask != kOgreNextRt4AuthoredVisibilityMask ||
      verified_scene->mShadowNode != Ogre::IdString() ||
      verified_scene->mIncludeOverlays || !verified_scene->mEnableForwardPlus ||
      !verified_scene->mUpdateLodLists ||
      verified_scene->mClearDepth != 1.0F ||
      verified_scene->mStoreActionDepth != Ogre::StoreAction::Store ||
      verified_history == nullptr ||
      verified_history->mNumInitialPasses != 1U) {
    throw std::runtime_error(
        "Ogre-Next single-evaluation HDR node topology failed exact definition readback");
  }
}

void ConfigureAndVerifyHdrPostExecutionMask(
    Ogre::CompositorManager2 &compositors) {
  Ogre::CompositorNodeDef *post = compositors.getNodeDefinitionNonConst(
      Ogre::IdString(kOgreNextHdrPostprocessingNode));
  if (post == nullptr || post->getNumTargetPasses() != 15U ||
      post->calculateNumPasses() != 15U) {
    throw std::runtime_error(
        "Ogre-Next stock HDR post node topology changed before V2 split");
  }
  std::size_t configured = 0U;
  for (std::size_t target_index = 0U;
       target_index < post->getNumTargetPasses(); ++target_index) {
    Ogre::CompositorTargetDef *target = post->getTargetPass(target_index);
    if (target == nullptr || target->getCompositorPasses().size() != 1U ||
        target->getCompositorPasses().front() == nullptr) {
      throw std::runtime_error(
          "Ogre-Next stock HDR post target is no longer one exact GPU pass");
    }
    target->getCompositorPasses().front()->mExecutionMask =
        kOgreNextHdrPostExecutionMask;
    if (target->getCompositorPasses().front()->mExecutionMask !=
        kOgreNextHdrPostExecutionMask) {
      throw std::runtime_error(
          "Ogre-Next HDR post execution mask failed native readback");
    }
    ++configured;
  }
  if (configured != 15U) {
    throw std::runtime_error(
        "Ogre-Next HDR post execution-mask closure is incomplete");
  }
}
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
constexpr const char kOgreNextHdrUiOverlayName[] = "RoRHdrUiOverlayControl";
constexpr const char kOgreNextHdrUiPanelName[] = "RoRHdrUiOverlayPanel";
constexpr const char kOgreNextHdrUiDatablockName[] =
    "RoRHdrUiOverlayMagenta";
#endif
// Production menu/HUD overlay composited by the stock HdrRenderUi node. The
// panel datablock carries the reserved display-domain prefix so its shader
// piece applies the exact sRGB EOTF after filtering; into the sRGB output the
// round trip is an identity for display-referred GUI pixels.
constexpr const char kOgreNextHudOverlayName[] = "RoRHdrHudOverlayV1";
constexpr const char kOgreNextHudPanelName[] = "RoRHdrHudOverlayPanelV1";
constexpr const char kOgreNextHudDatablockName[] =
    "RoRDisplayDomainUnlit_HudOverlayPanelV1";

// Scene-free GUI-only presentation (PresentUiOverlayFrame). Deliberately a
// second, separately named overlay/panel/datablock rather than a second user
// of the production HUD ones: the two paths interleave within one session and
// neither may ever observe or rebind the other's texture. They share only the
// scene manager's single v1::OverlaySystem render-queue listener, because a
// second registered listener would draw every overlay twice.
constexpr const char kOgreNextMenuOverlayName[] = "RoRMenuOverlayV1";
constexpr const char kOgreNextMenuPanelName[] = "RoRMenuOverlayPanelV1";
constexpr const char kOgreNextMenuDatablockName[] =
    "RoRDisplayDomainUnlit_MenuOverlayPanelV1";
constexpr const char kOgreNextMenuOverlayTextureName[] =
    "RoRN1MenuOverlayImage";
/// v1::OverlayManager places overlays in render queue 254 by default, and the
/// stock HdrRenderUi node renders exactly [254, 255) with overlays enabled
/// (mLastRQ is not inclusive). The GUI-only node reproduces that window so no
/// scene renderable can enter a menu frame even if one were visible.
constexpr Ogre::uint8 kOgreNextOverlayFirstRenderQueue = 254U;
constexpr Ogre::uint8 kOgreNextOverlayLastRenderQueue = 255U;

RenderOperationResult HdrBackendFailure(const std::string &detail) {
  return RenderOperationResult::Failure(
      RenderOperationCode::BACKEND_FAILURE,
      "Ogre-Next RT4 HDR compositor failure: " + detail);
}

bool TryReadFiniteR16Texture(Ogre::TextureGpu *texture,
                             std::uint32_t expected_width,
                             std::uint32_t expected_height,
                             std::vector<HdrR16Float> &output,
                             std::uint64_t &content_readbacks) {
  if (texture == nullptr || texture->getPixelFormat() != Ogre::PFG_R16_FLOAT ||
      texture->getWidth() != expected_width ||
      texture->getHeight() != expected_height || texture->getDepth() != 1U) {
    return false;
  }
  ++content_readbacks;
  Ogre::Image2 image;
  image.convertFromTexture(texture, 0U, 0U);
  const Ogre::TextureBox pixels = image.getData(0U);
  std::vector<HdrR16Float> candidate;
  candidate.reserve(static_cast<std::size_t>(expected_width) *
                    expected_height);
  for (std::uint32_t row = 0U; row < expected_height; ++row) {
    for (std::uint32_t column = 0U; column < expected_width; ++column) {
      std::uint16_t bits = 0U;
      std::memcpy(&bits, pixels.at(column, row, 0U), sizeof(bits));
      HdrR16Float canonical;
      const ValidationResult validation =
          DecodeFiniteHdrR16Float(bits, canonical);
      if (!validation || canonical.bits != bits) {
        return false;
      }
      candidate.push_back(canonical);
    }
  }
  output = std::move(candidate);
  return true;
}

bool TryReadPositiveR16Texture(Ogre::TextureGpu *texture,
                               std::uint32_t expected_width,
                               std::uint32_t expected_height,
                               std::vector<HdrR16Float> &output,
                               std::uint64_t &content_readbacks) {
  std::vector<HdrR16Float> candidate;
  if (!TryReadFiniteR16Texture(texture, expected_width, expected_height,
                              candidate, content_readbacks)) {
    return false;
  }
  for (const HdrR16Float &value : candidate) {
    if (!(value.decoded > 0.0F)) {
      return false;
    }
  }
  output = std::move(candidate);
  return true;
}

bool TryComputeHdrAverageLogLuminance(
    const std::vector<HdrR16Float> &iterative_luminance,
    float &average) noexcept {
  if (iterative_luminance.size() != 16U) {
    return false;
  }
  const auto quadrant = [&](std::size_t x, std::size_t y) {
    float value = iterative_luminance[y * 4U + x].decoded;
    value += iterative_luminance[y * 4U + x + 1U].decoded;
    value += iterative_luminance[(y + 1U) * 4U + x].decoded;
    value += iterative_luminance[(y + 1U) * 4U + x + 1U].decoded;
    return value * 0.25F;
  };
  float candidate = quadrant(0U, 0U);
  candidate += quadrant(2U, 0U);
  candidate += quadrant(0U, 2U);
  candidate += quadrant(2U, 2U);
  candidate *= 0.25F;
  if (!std::isfinite(candidate)) {
    return false;
  }
  average = candidate;
  return true;
}

/// The pinned PBS implementation gathers directional lights from its global
/// list and does not apply CompositorPassSceneDef::mLightVisibilityMask to
/// that list. This listener is therefore the smallest exact compositor seam:
/// it transactionally zeroes only directional power for the Base pass and
/// restores the exact authored values before every sun-bearing pass. It never
/// mutates materials, geometry, camera, ambient/environment, or temporal state.
class HdrDirectionalSplitListener final
    : public Ogre::CompositorWorkspaceListener {
public:
  void BeginFrame(
      const std::vector<std::pair<Ogre::Light *, Ogre::SceneNode *>> &lights) {
    if (active_) {
      throw std::logic_error(
          "HDR directional split listener already owns a frame");
    }
    bindings_.clear();
    bindings_.reserve(lights.size());
    for (const auto &entry : lights) {
      if (entry.first == nullptr ||
          entry.first->getType() != Ogre::Light::LT_DIRECTIONAL) {
        throw std::logic_error(
            "HDR directional split received a non-directional native light");
      }
      bindings_.push_back({entry.first, entry.first->getPowerScale()});
    }
    base_pre_count_ = 0U;
    base_post_count_ = 0U;
    sun_full_pre_count_ = 0U;
    raster_lit_pre_count_ = 0U;
    failure_ = false;
    active_ = true;
  }

  [[nodiscard]] bool EndFrame() noexcept {
    const bool restored = RestoreExact();
    const bool valid = active_ && !failure_ && restored &&
                       base_pre_count_ == 1U && base_post_count_ == 1U &&
                       sun_full_pre_count_ == 1U &&
                       raster_lit_pre_count_ == 1U;
    active_ = false;
    bindings_.clear();
    return valid;
  }

  [[nodiscard]] bool AbortFrame() noexcept {
    const bool restored = RestoreExact();
    active_ = false;
    bindings_.clear();
    return restored;
  }

  [[nodiscard]] bool active() const noexcept { return active_; }

  void passPreExecute(Ogre::CompositorPass *pass) override {
    if (!active_ || pass == nullptr || pass->getDefinition() == nullptr) {
      return;
    }
    const std::uint32_t identifier = pass->getDefinition()->mIdentifier;
    if (identifier == kOgreNextHdrBaseScenePassIdentifier) {
      ++base_pre_count_;
      if (!RestoreExact()) {
        failure_ = true;
      }
      for (const Binding &binding : bindings_) {
        binding.light->setPowerScale(0.0F);
        if (binding.light->getPowerScale() != 0.0F) {
          failure_ = true;
        }
      }
    } else if (identifier == kOgreNextHdrSunFullScenePassIdentifier) {
      ++sun_full_pre_count_;
      if (!RestoreExact()) {
        failure_ = true;
      }
    } else if (identifier == kOgreNextHdrRasterLitScenePassIdentifier) {
      ++raster_lit_pre_count_;
      if (!RestoreExact()) {
        failure_ = true;
      }
    }
  }

  void passPosExecute(Ogre::CompositorPass *pass) override {
    if (!active_ || pass == nullptr || pass->getDefinition() == nullptr ||
        pass->getDefinition()->mIdentifier !=
            kOgreNextHdrBaseScenePassIdentifier) {
      return;
    }
    ++base_post_count_;
    if (!RestoreExact()) {
      failure_ = true;
    }
  }

  void workspacePosUpdate(Ogre::CompositorWorkspace *) override {
    if (active_ && !RestoreExact()) {
      failure_ = true;
    }
  }

private:
  struct Binding final {
    Ogre::Light *light = nullptr;
    float power_scale = 0.0F;
  };

  [[nodiscard]] bool RestoreExact() noexcept {
    bool restored = true;
    for (const Binding &binding : bindings_) {
      if (binding.light == nullptr) {
        restored = false;
        continue;
      }
      binding.light->setPowerScale(binding.power_scale);
      restored = binding.light->getPowerScale() == binding.power_scale &&
                 restored;
    }
    return restored;
  }

  std::vector<Binding> bindings_;
  std::uint32_t base_pre_count_ = 0U;
  std::uint32_t base_post_count_ = 0U;
  std::uint32_t sun_full_pre_count_ = 0U;
  std::uint32_t raster_lit_pre_count_ = 0U;
  bool failure_ = false;
  bool active_ = false;
};

} // namespace

namespace {
constexpr const char *kProductionPresentationTargetName =
    "RoRN1ProductionPresentationTarget";
constexpr const char *kProductionPresentationNodeName =
    "RoRN1ProductionPresentationNode";
constexpr const char *kProductionPresentationWorkspaceName =
    "RoRN1ProductionPresentationWorkspace";
constexpr const char *kProductionPresentationShadowNodeName =
    "RoRN1ProductionPresentationPssmShadowNode";
constexpr const char *kBootstrapPresentationNodeName =
    "RoRN1BootstrapPresentationNode";
constexpr const char *kBootstrapPresentationWorkspaceName =
    "RoRN1BootstrapPresentationWorkspace";
constexpr const char *kMenuPresentationNodeName =
    "RoRN1MenuPresentationNode";
constexpr const char *kMenuPresentationWorkspaceName =
    "RoRN1MenuPresentationWorkspace";
} // namespace

class OgreNextN1Frontend::Impl final
    : public OgreNextSunVisibilityV2PresentationContinuation {
public:
  explicit Impl(OgreNextN1Configuration configuration)
      : raster_feature_tier(configuration.raster_feature_tier),
        directional_shadow_mode(configuration.directional_shadow_mode),
        hdr_scene_topology(configuration.hdr_scene_topology),
        configured_shader_media_root(
            std::move(configuration.shader_media_root)),
        presentation_configuration(std::move(configuration.presentation)),
        hdr_configuration(configuration.hdr_temporal_configuration),
        hdr_enabled(configuration.enable_hdr_compositor),
        retain_analytic_sky_geometry_content_evidence(
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
            configuration.retain_analytic_sky_geometry_content_evidence
#else
            false
#endif
            ),
        retain_native_lighting_content_evidence(
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
            configuration.retain_native_lighting_content_evidence
#else
            false
#endif
            ),
        retain_sun_visibility_v2_content_evidence(
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
            configuration.retain_sun_visibility_v2_content_evidence
#else
            false
#endif
            )
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        , texture_upload_failure_stage(
              configuration.texture_upload_failure_stage),
        retain_reflection_capture_evidence(
              configuration.retain_reflection_capture_evidence),
        pssm_failure_stage(configuration.pssm_failure_stage),
        hdr_failure_stage(configuration.hdr_failure_stage),
        analytic_sky_failure_stage(
            configuration.analytic_sky_failure_stage),
        hdr_ui_overlay_control(configuration.hdr_ui_overlay_control)
#endif
  {}

  [[nodiscard]] bool SunVisibilityV2Enabled() const noexcept {
    return UsesMetalSunVisibilityV2(native_feature_tier);
  }

  [[nodiscard]] bool SingleSceneHdrPssmEnabled() const noexcept {
    return hdr_enabled &&
           hdr_scene_topology ==
               OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1;
  }

  [[nodiscard]] NativeSunVisibilityV2Result V2PresentationResult(
      NativeSunVisibilityV2Code code, std::uint64_t frame_id,
      std::uint64_t snapshot_id, const char *detail) const {
    NativeSunVisibilityV2Result result;
    result.code = code;
    result.stage = NativeSunVisibilityV2Stage::PRESENT_CONTINUATION;
    result.frame_id = frame_id;
    result.snapshot_id = snapshot_id;
    result.detail = code == NativeSunVisibilityV2Code::OK ? "ok" : detail;
    return result;
  }

  struct NativeMesh {
    RenderAssetReference asset;
    Ogre::MeshPtr mesh;
    Ogre::VertexBufferPacked *vertex_buffer = nullptr;
    Ogre::IndexBufferPacked *index_buffer = nullptr;
    OgreNextNativeVertexLayout vertex_layout =
        OgreNextNativeVertexLayout::INVALID;
    std::uint32_t vertex_stride_bytes = 0U;
    std::uint64_t native_storage_generation = 0U;
    std::string name;
  };

  struct NativeAnalyticSkySection final {
    Ogre::MeshPtr mesh;
    Ogre::VertexBufferPacked *vertex_buffer = nullptr;
    Ogre::IndexBufferPacked *index_buffer = nullptr;
    Ogre::VertexArrayObject *vao = nullptr;
    std::string name;
    std::size_t vertex_count = 0U;
    std::size_t index_count = 0U;
    bool mesh_owns_native_buffers = false;
  };

  struct NativeMaterial {
    enum class Kind : std::uint8_t {
      PBS,
      DISPLAY_DOMAIN_UNLIT,
    };

    RenderAssetReference asset;
    Kind kind = Kind::PBS;
    Ogre::HlmsPbsDatablock *pbs_datablock = nullptr;
    Ogre::HlmsUnlitDatablock *display_domain_unlit_datablock = nullptr;
    std::string name;
    OgreNextN1PbsUv0AffineTransform uv0_affine;
    std::uint32_t native_texture_slot_mask = 0U;

    [[nodiscard]] Ogre::HlmsDatablock *Datablock() const noexcept {
      return kind == Kind::PBS
                 ? static_cast<Ogre::HlmsDatablock *>(pbs_datablock)
                 : static_cast<Ogre::HlmsDatablock *>(
                       display_domain_unlit_datablock);
    }
  };

  /// One retained native directional light. The descriptor is re-applied and
  /// read back every present; the record exists so set changes and teardown
  /// have exactly one owner per native Light/SceneNode pair.
  struct RetainedLight final {
    LightDescriptor descriptor;
    Ogre::Light *light = nullptr;
    Ogre::SceneNode *node = nullptr;
  };

  /// One retained native mesh instance keyed by instance_id. The record is
  /// the single owner of every native allocation it names: the per-present
  /// diff mutates records in place, and the failure path tears the whole map
  /// down to the empty state so a half-applied diff is never observable.
  struct RetainedInstance final {
    /// Exact descriptor bytes this native state was built from.
    MeshInstanceDescriptor descriptor;
    Ogre::Item *item = nullptr;
    Ogre::SceneNode *node = nullptr;
    /// PSSM non-receiver clone, retained for the instance lifetime.
    Ogre::HlmsPbsDatablock *receiver_clone = nullptr;
    std::string receiver_clone_name;
    /// Deformed native mesh owned while deformation_revision > 1.
    NativeMesh deformed_mesh;
    /// Item render queue before any thin-slab override.
    std::uint8_t base_render_queue = 0U;
    /// Cached audit contributions for O(changed) aggregate maintenance.
    /// Shadow flags are stored plan-conditioned (PBS and plan enabled).
    bool pbs = false;
    bool transmission = false;
    bool normal_mapped = false;
    bool emissive = false;
    bool casts_shadow = false;
    bool receives_shadow = false;
    bool dynamic_mesh = false;
    std::uint32_t material_descriptor_version = 0U;
    /// Retained PSSM evidence, refreshed on create, update, and native
    /// re-verification.
    OgreNextPssmNativeBoundsObservation bounds;
    bool bounds_valid = false;
    /// Present id of the last native mutation or verification of this
    /// record, so one present never verifies the same state twice.
    std::uint64_t verified_frame_id = 0U;
  };

  struct NativeTexture {
    RenderAssetReference asset;
    NativeTextureUsage usage;
    Ogre::TextureGpu *sampled = nullptr;
    Ogre::TextureGpu *linear = nullptr;
    Ogre::TextureGpu *roughness = nullptr;
    Ogre::TextureGpu *metallic = nullptr;
    Ogre::TextureGpu *normal = nullptr;
    std::string sampled_name;
    std::string linear_name;
    std::string roughness_name;
    std::string metallic_name;
    std::string normal_name;
  };

  FrontendCapabilityReport Capabilities() const {
    FrontendCapabilityReport report = BuildOgreNextN1CapabilityReport(
        CompiledRasterApi(), ROR_OGRE_NEXT_N1_VERSION);
    report.maximum_texture_dimension_2d = maximum_texture_dimension;
    if (native_interop) {
      native_interop->DecorateFrontendCapabilities(report);
    }
    return report;
  }

  OgreNextN1TextureAllocationAudit TextureAllocationAudit() const noexcept {
    OgreNextN1TextureAllocationAudit audit;
    audit.live_source_textures =
        static_cast<std::uint32_t>(textures.size());
    audit.exact_usage = initialized && !faulted;
    for (const auto &entry : textures) {
      const NativeTexture &texture = entry.second;
      audit.sampled_rgba_allocations += texture.sampled != nullptr ? 1U : 0U;
      audit.linear_rgba_allocations += texture.linear != nullptr ? 1U : 0U;
      audit.roughness_r8_allocations +=
          texture.roughness != nullptr ? 1U : 0U;
      audit.metallic_r8_allocations += texture.metallic != nullptr ? 1U : 0U;
      audit.normal_rg8_allocations += texture.normal != nullptr ? 1U : 0U;
      audit.exact_usage =
          audit.exact_usage && !texture.usage.empty() &&
          (texture.sampled != nullptr) ==
              (texture.usage.sampled_rgba ||
               texture.usage.display_domain_rgba) &&
          (texture.linear != nullptr) == texture.usage.linear_rgba &&
          (texture.roughness != nullptr) == texture.usage.roughness_g &&
          (texture.metallic != nullptr) == texture.usage.metallic_b &&
          (texture.normal != nullptr) == texture.usage.normal_rg;
    }
    audit.live_native_allocations =
        static_cast<std::uint64_t>(audit.sampled_rgba_allocations) +
        static_cast<std::uint64_t>(audit.linear_rgba_allocations) +
        static_cast<std::uint64_t>(audit.roughness_r8_allocations) +
        static_cast<std::uint64_t>(audit.metallic_r8_allocations) +
        static_cast<std::uint64_t>(audit.normal_rg8_allocations);
    audit.native_allocation_creates = texture_allocation_creates;
    audit.native_allocation_destroys = texture_allocation_destroys;
    audit.retired_name_lookups = texture_retired_name_lookups;
    audit.retired_name_rejections = texture_retired_name_rejections;
    audit.exact_usage =
        audit.exact_usage &&
        audit.native_allocation_creates >= audit.native_allocation_destroys &&
        audit.native_allocation_creates - audit.native_allocation_destroys ==
            audit.live_native_allocations &&
        audit.retired_name_lookups == audit.native_allocation_destroys &&
        audit.retired_name_rejections == audit.retired_name_lookups;
    return audit;
  }

  OgreNextN1PbsUv0AffineState PbsUv0AffineState(
      RenderAssetReference requested) const noexcept {
    OgreNextN1PbsUv0AffineState state;
    state.material = requested;
    const auto found = materials.find(requested.id);
    if (!initialized || faulted || !requested.valid() ||
        requested.kind != RenderAssetKind::MATERIAL ||
        found == materials.end() || found->second.asset != requested) {
      return state;
    }
    const NativeMaterial &native = found->second;
    state.live = true;
    state.pbs = native.kind == NativeMaterial::Kind::PBS &&
                native.pbs_datablock != nullptr;
    if (!state.pbs) {
      return state;
    }
    state.scale = native.uv0_affine.scale;
    state.offset = native.uv0_affine.offset;
    state.portable_texture_binding_count =
        native.uv0_affine.portable_texture_binding_count;
    // Detail slots are bound too, and the readback loop below counts every
    // bound slot, so the expected total has to include them.
    state.native_texture_slot_count =
        native.uv0_affine.native_texture_slot_count +
        native.uv0_affine.native_detail_texture_slot_count;
    state.transformed = native.uv0_affine.transformed;
    state.positive_scale = state.scale.x > 0.0F && state.scale.y > 0.0F;
    state.rotation_zero = true;
    state.shared_across_bound_slots = true;
    state.shader_piece_selected =
        OgreNextUvAffinePbs::SelectsUv0AffineShader(
            native.pbs_datablock);

    bool exact_slots = true;
    for (std::uint32_t slot = 0U;
         slot < static_cast<std::uint32_t>(Ogre::NUM_PBSM_TEXTURE_TYPES);
         ++slot) {
      if ((native.native_texture_slot_mask & (1U << slot)) == 0U) {
        continue;
      }
      const auto pbs_slot = static_cast<Ogre::PbsTextureTypes>(slot);
      exact_slots = exact_slots &&
                    native.pbs_datablock->getTexture(
                        static_cast<Ogre::uint8>(slot)) != nullptr &&
                    native.pbs_datablock->getTextureUvSource(pbs_slot) == 0U;
      ++state.native_texture_slot_readbacks;
    }
    state.uv0_only = exact_slots &&
                     state.native_texture_slot_readbacks ==
                         state.native_texture_slot_count;

    const Ogre::Vector4 expected(
        state.scale.x, state.scale.y, state.offset.x, state.offset.y);
    const Ogre::Vector4 actual0 =
        native.pbs_datablock->getUserValue(0U);
    const Ogre::Vector4 actual1 =
        native.pbs_datablock->getUserValue(1U);
    const Ogre::Vector4 actual2 =
        native.pbs_datablock->getUserValue(2U);
    state.native_user_value_readbacks = 3U;
    state.exact_native_state =
        state.uv0_only && state.positive_scale && state.rotation_zero &&
        state.shared_across_bound_slots && state.shader_piece_selected &&
        actual0 == expected && actual1 == Ogre::Vector4::ZERO &&
        actual2 == Ogre::Vector4::ZERO;
    return state;
  }

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  OgreNextN1NormalUploadAudit NormalUploadAudit() const noexcept {
    OgreNextN1NormalUploadAudit audit;
    audit.verified_uploads = normal_uploads_verified;
    audit.verified_mip_levels = normal_upload_mip_levels_verified;
    audit.verified_rows = normal_upload_rows_verified;
    audit.verified_texels = normal_upload_texels_verified;
    audit.verified_rg_bytes = normal_upload_rg_bytes_verified;
    audit.verified_padded_source_rows =
        normal_upload_padded_source_rows_verified;
    audit.exact_source_rg_to_native_image =
        audit.verified_uploads > 0U && audit.verified_mip_levels > 0U &&
        audit.verified_rows >= audit.verified_mip_levels &&
        audit.verified_texels > 0U &&
        audit.verified_texels <=
            (std::numeric_limits<std::uint64_t>::max)() / 2U &&
        audit.verified_rg_bytes == audit.verified_texels * 2U &&
        audit.verified_padded_source_rows <= audit.verified_rows;
    return audit;
  }

  OgreNextN1DisplayDomainUploadAudit DisplayDomainUploadAudit() const {
    OgreNextN1DisplayDomainUploadAudit audit;
    if (!initialized || faulted || registry == nullptr) {
      return audit;
    }
    for (const auto &entry : textures) {
      const NativeTexture &native = entry.second;
      if (!native.usage.display_domain_rgba) {
        continue;
      }
      ++audit.source_textures;
      const TextureResourceDescriptor *descriptor =
          registry->ResolveTexture(native.asset);
      if (descriptor == nullptr || native.sampled == nullptr ||
          native.sampled->getPixelFormat() != Ogre::PFG_RGBA8_UNORM ||
          descriptor->mip_levels.empty()) {
        return audit;
      }
      audit.expected_mip_levels += descriptor->mip_levels.size();

      Ogre::Image2 readback;
      readback.convertFromTexture(
          native.sampled, 0U,
          static_cast<Ogre::uint8>(descriptor->mip_levels.size() - 1U));
      if (readback.getPixelFormat() != Ogre::PFG_RGBA8_UNORM ||
          readback.getWidth() != descriptor->width ||
          readback.getHeight() != descriptor->height) {
        return audit;
      }
      ++audit.native_readbacks;

      for (std::size_t mip_index = 0U;
           mip_index < descriptor->mip_levels.size(); ++mip_index) {
        const TextureMipLevelDescriptor &source =
            descriptor->mip_levels[mip_index];
        const Ogre::TextureBox downloaded =
            readback.getData(static_cast<Ogre::uint8>(mip_index));
        if (downloaded.data == nullptr || downloaded.width != source.width ||
            downloaded.height != source.height ||
            downloaded.bytesPerPixel != 4U ||
            downloaded.bytesPerRow <
                static_cast<std::size_t>(source.width) * 4U) {
          return audit;
        }
        for (std::uint32_t row = 0U; row < source.height; ++row) {
          const std::uint8_t *source_row =
              source.bytes.data() + static_cast<std::size_t>(row) *
                                        source.row_pitch_bytes;
          const auto *downloaded_row = static_cast<const std::uint8_t *>(
              downloaded.at(0U, row, 0U));
          const std::size_t row_bytes =
              static_cast<std::size_t>(source.width) * 4U;
          if (std::memcmp(downloaded_row, source_row, row_bytes) != 0) {
            return audit;
          }
          ++audit.verified_rows;
          audit.verified_texels += source.width;
          audit.verified_rgba_bytes += row_bytes;
        }
        ++audit.verified_mip_levels;
      }
    }
    audit.exact_source_rgba_to_native_texture =
        audit.source_textures > 0U &&
        audit.native_readbacks == audit.source_textures &&
        audit.verified_mip_levels == audit.expected_mip_levels &&
        audit.verified_rows >= audit.verified_mip_levels &&
        audit.verified_texels > 0U &&
        audit.verified_texels <=
            (std::numeric_limits<std::uint64_t>::max)() / 4U &&
        audit.verified_rgba_bytes == audit.verified_texels * 4U;
    return audit;
  }
#endif

  OgreNextPssmShadowRuntimeAudit DirectionalShadowAudit() const noexcept {
    OgreNextPssmShadowRuntimeAudit audit = shadow_audit;
    audit.configured_mode = directional_shadow_mode;
    return audit;
  }

  OgreNextN1PresentationAudit PresentationAudit() const noexcept {
    return presentation_audit;
  }

  bool OnOwnerThread() const noexcept {
    return initialized && std::this_thread::get_id() == owner_thread;
  }

  RenderOperationResult ValidateSamplerDeviceLimits(
      const RenderAssetRegistry &candidate_registry) const {
    return OgreNextN1OperationFromValidation(
        ValidateOgreNextN1SamplerDeviceLimits(
            candidate_registry, maximum_anisotropy, raster_feature_tier));
  }

  NativeMesh CreateMesh(const RenderAssetReference &asset,
                        const MeshResourceDescriptor &descriptor,
                        const std::string &name_suffix = {}) {
    NativeMesh native;
    if (next_native_storage_generation == 0U ||
        next_native_storage_generation ==
            (std::numeric_limits<std::uint64_t>::max)()) {
      throw std::overflow_error(
          "Ogre-Next native mesh storage generation exhausted");
    }
    native.native_storage_generation = next_native_storage_generation++;
    native.asset = asset;
    native.vertex_layout = native_vertex_layout;
    native.name = AssetName("RoRN1Mesh", asset) + name_suffix;
    native.mesh = Ogre::MeshManager::getSingleton().createManual(
        native.name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    Ogre::VaoManager *vao_manager = renderer->getVaoManager();
    Ogre::VertexBufferPacked *vertex_buffer = nullptr;
    Ogre::IndexBufferPacked *index_buffer = nullptr;
    Ogre::VertexArrayObject *vao = nullptr;
    bool attached = false;
    void *vertices = nullptr;
    void *indices = nullptr;
    try {
      Ogre::VertexElement2Vec elements;
      if (native.vertex_layout == OgreNextNativeVertexLayout::
                                      POSITION_NORMAL_TANGENT_UV0_FLOAT32_48) {
        const std::size_t vertex_bytes =
            sizeof(Rt4PbrVertex) * descriptor.positions.size();
        auto *pbr_vertices = static_cast<Rt4PbrVertex *>(OGRE_MALLOC_SIMD(
            vertex_bytes, Ogre::MEMCATEGORY_GEOMETRY));
        vertices = pbr_vertices;
        for (std::size_t index = 0U; index < descriptor.positions.size();
             ++index) {
          const Float3 &position = descriptor.positions[index];
          const Float3 &normal = descriptor.normals[index];
          const Float4 &tangent = descriptor.tangents[index];
          const Float2 &uv = descriptor.texture_coordinates_0[index];
          pbr_vertices[index] = {
              {position.x, position.y, position.z},
              {normal.x, normal.y, normal.z},
              {tangent.x, tangent.y, tangent.z, tangent.w},
              {uv.x, uv.y},
          };
        }
        elements.push_back(
            Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_POSITION));
        elements.push_back(
            Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_NORMAL));
        elements.push_back(
            Ogre::VertexElement2(Ogre::VET_FLOAT4, Ogre::VES_TANGENT));
        elements.push_back(Ogre::VertexElement2(
            Ogre::VET_FLOAT2, Ogre::VES_TEXTURE_COORDINATES));
      } else if (native.vertex_layout ==
                 OgreNextNativeVertexLayout::POSITION_NORMAL_FLOAT32_24) {
        const std::size_t vertex_bytes =
            sizeof(N1Vertex) * descriptor.positions.size();
        auto *n1_vertices = static_cast<N1Vertex *>(OGRE_MALLOC_SIMD(
            vertex_bytes, Ogre::MEMCATEGORY_GEOMETRY));
        vertices = n1_vertices;
        for (std::size_t index = 0U; index < descriptor.positions.size();
             ++index) {
          const Float3 &position = descriptor.positions[index];
          const Float3 &normal = descriptor.normals[index];
          n1_vertices[index] = {{position.x, position.y, position.z},
                                {normal.x, normal.y, normal.z}};
        }
        elements.push_back(
            Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_POSITION));
        elements.push_back(
            Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_NORMAL));
      } else {
        throw std::logic_error(
            "Ogre-Next mesh creation has no reviewed native vertex layout");
      }
      vertex_buffer = vao_manager->createVertexBuffer(
          elements, descriptor.positions.size(), Ogre::BT_IMMUTABLE, vertices,
          true);
      vertices = nullptr;
      native.vertex_stride_bytes = static_cast<std::uint32_t>(
          vertex_buffer->getBytesPerElement());
      const std::uint32_t expected_stride =
          native.vertex_layout == OgreNextNativeVertexLayout::
                                      POSITION_NORMAL_FLOAT32_24
              ? kOgreNextPositionNormalVertexStrideBytes
              : kOgreNextPositionNormalTangentUv0VertexStrideBytes;
      if (native.vertex_stride_bytes != expected_stride) {
        throw std::runtime_error(
            "Ogre-Next vertex buffer stride differs from its reviewed layout");
      }

      const bool use_u16 =
          descriptor.index_format == MeshIndexFormat::UINT16;
      const std::size_t index_stride =
          use_u16 ? sizeof(Ogre::uint16) : sizeof(Ogre::uint32);
      indices = OGRE_MALLOC_SIMD(index_stride * descriptor.indices.size(),
                                 Ogre::MEMCATEGORY_GEOMETRY);
      if (use_u16) {
        auto *destination = static_cast<Ogre::uint16 *>(indices);
        for (std::size_t index = 0U; index < descriptor.indices.size();
             ++index) {
          destination[index] =
              static_cast<Ogre::uint16>(descriptor.indices[index]);
        }
      } else {
        std::memcpy(indices, descriptor.indices.data(),
                    index_stride * descriptor.indices.size());
      }
      index_buffer = vao_manager->createIndexBuffer(
          use_u16 ? Ogre::IndexBufferPacked::IT_16BIT
                  : Ogre::IndexBufferPacked::IT_32BIT,
          descriptor.indices.size(), Ogre::BT_IMMUTABLE, indices, true);
      indices = nullptr;

      native.vertex_buffer = vertex_buffer;
      native.index_buffer = index_buffer;

      Ogre::VertexBufferPackedVec vertex_buffers;
      vertex_buffers.push_back(vertex_buffer);
      vao = vao_manager->createVertexArrayObject(
          vertex_buffers, index_buffer, Ogre::OT_TRIANGLE_LIST);
      Ogre::SubMesh *submesh = native.mesh->createSubMesh();
      submesh->mVao[Ogre::VpNormal].push_back(vao);
      attached = true;
      submesh->mVao[Ogre::VpShadow].push_back(vao);

      OgreNextN1NativeMeshBounds bounds;
      if (!TryBuildOgreNextN1NativeMeshBounds(descriptor.local_bounds,
                                              bounds)) {
        throw std::logic_error(
            "validated N1 mesh bounds became non-finite before Ogre allocation");
      }
      native.mesh->_setBounds(
          Ogre::Aabb(Ogre::Vector3(bounds.center.x, bounds.center.y,
                                  bounds.center.z),
                     Ogre::Vector3(bounds.half_size.x, bounds.half_size.y,
                                   bounds.half_size.z)),
          false);
      native.mesh->_setBoundingSphereRadius(bounds.radius);
      return native;
    } catch (...) {
      const std::exception_ptr creation_failure = std::current_exception();
      bool clean = true;
      if (vertices != nullptr) {
        OGRE_FREE_SIMD(vertices, Ogre::MEMCATEGORY_GEOMETRY);
      }
      if (indices != nullptr) {
        OGRE_FREE_SIMD(indices, Ogre::MEMCATEGORY_GEOMETRY);
      }
      if (!attached) {
        bool vao_destroyed = true;
        if (vao != nullptr) {
          try {
            vao_manager->destroyVertexArrayObject(vao);
          } catch (...) {
            clean = false;
            vao_destroyed = false;
          }
        }
        if (vao_destroyed) {
          if (vertex_buffer != nullptr) {
            try {
              vao_manager->destroyVertexBuffer(vertex_buffer);
            } catch (...) {
              clean = false;
            }
          }
          if (index_buffer != nullptr) {
            try {
              vao_manager->destroyIndexBuffer(index_buffer);
            } catch (...) {
              clean = false;
            }
          }
        }
      }
      clean = DestroyMesh(native) && clean;
      if (!clean) {
        faulted = true;
      }
      std::rethrow_exception(creation_failure);
    }
  }

  NativeAnalyticSkySection CreateAnalyticSkySection(
      const std::string &name,
      const std::vector<OgreNextAnalyticSkyNativeVertex> &vertices,
      const std::vector<std::uint32_t> &indices, float radius,
      bool background) {
    static_cast<void>(background);
    if (name.empty() || vertices.empty() || indices.empty() ||
        !std::isfinite(radius) || radius <= 0.0F ||
        vertices.size() > (std::numeric_limits<Ogre::uint32>::max)() ||
        indices.size() > (std::numeric_limits<Ogre::uint32>::max)()) {
      throw std::logic_error(
          "validated analytic-sky geometry became unrepresentable before native allocation");
    }
    static_assert(sizeof(OgreNextAnalyticSkyNativeVertex) == 28U,
                  "analytic-sky native vertex layout drifted");

    NativeAnalyticSkySection section;
    section.name = name;
    section.vertex_count = vertices.size();
    section.index_count = indices.size();
    Ogre::MeshManager &mesh_manager = Ogre::MeshManager::getSingleton();
    if (mesh_manager.getByName(
            name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)) {
      throw std::logic_error(
          "analytic-sky native mesh name was still live before allocation");
    }

    Ogre::VaoManager *vao_manager = renderer->getVaoManager();
    void *vertex_data = nullptr;
    void *index_data = nullptr;
    bool mesh_counted = false;
    try {
      section.mesh = mesh_manager.createManual(
          section.name,
          Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
      if (!section.mesh) {
        throw std::logic_error(
            "analytic-sky native mesh allocation returned null");
      }
      ++analytic_sky_audit.native_mesh_creates;
      mesh_counted = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectAnalyticSkyFailure(
          background ? OgreNextN1AnalyticSkyFailureStage::AFTER_BACKGROUND_MESH
                     : OgreNextN1AnalyticSkyFailureStage::AFTER_SUN_MESH,
          background
              ? "injected analytic-sky background-mesh rollback failure"
              : "injected analytic-sky sun-mesh rollback failure");
#endif

      const std::size_t vertex_bytes =
          vertices.size() * sizeof(OgreNextAnalyticSkyNativeVertex);
      vertex_data =
          OGRE_MALLOC_SIMD(vertex_bytes, Ogre::MEMCATEGORY_GEOMETRY);
      if (vertex_data == nullptr) {
        throw std::bad_alloc();
      }
      std::memcpy(vertex_data, vertices.data(), vertex_bytes);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectAnalyticSkyFailure(
          background
              ? OgreNextN1AnalyticSkyFailureStage::
                    AFTER_BACKGROUND_CPU_VERTEX_ALLOCATION
              : OgreNextN1AnalyticSkyFailureStage::
                    AFTER_SUN_CPU_VERTEX_ALLOCATION,
          background
              ? "injected analytic-sky background CPU-vertex rollback failure"
              : "injected analytic-sky sun CPU-vertex rollback failure");
#endif
      Ogre::VertexElement2Vec elements;
      elements.push_back(
          Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_POSITION));
      elements.push_back(
          Ogre::VertexElement2(Ogre::VET_FLOAT4, Ogre::VES_DIFFUSE));
      section.vertex_buffer = vao_manager->createVertexBuffer(
          elements, vertices.size(), Ogre::BT_IMMUTABLE, vertex_data, true);
      vertex_data = nullptr;
      if (section.vertex_buffer == nullptr) {
        throw std::logic_error(
            "analytic-sky native vertex-buffer allocation returned null");
      }
      ++analytic_sky_audit.native_vertex_buffer_creates;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectAnalyticSkyFailure(
          background
              ? OgreNextN1AnalyticSkyFailureStage::
                    AFTER_BACKGROUND_VERTEX_BUFFER
              : OgreNextN1AnalyticSkyFailureStage::AFTER_SUN_VERTEX_BUFFER,
          background
              ? "injected analytic-sky background vertex-buffer rollback failure"
              : "injected analytic-sky sun vertex-buffer rollback failure");
#endif

      const std::size_t index_bytes =
          indices.size() * sizeof(std::uint32_t);
      index_data = OGRE_MALLOC_SIMD(index_bytes, Ogre::MEMCATEGORY_GEOMETRY);
      if (index_data == nullptr) {
        throw std::bad_alloc();
      }
      std::memcpy(index_data, indices.data(), index_bytes);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectAnalyticSkyFailure(
          background
              ? OgreNextN1AnalyticSkyFailureStage::
                    AFTER_BACKGROUND_CPU_INDEX_ALLOCATION
              : OgreNextN1AnalyticSkyFailureStage::
                    AFTER_SUN_CPU_INDEX_ALLOCATION,
          background
              ? "injected analytic-sky background CPU-index rollback failure"
              : "injected analytic-sky sun CPU-index rollback failure");
#endif
      section.index_buffer = vao_manager->createIndexBuffer(
          Ogre::IndexBufferPacked::IT_32BIT, indices.size(),
          Ogre::BT_IMMUTABLE, index_data, true);
      index_data = nullptr;
      if (section.index_buffer == nullptr) {
        throw std::logic_error(
            "analytic-sky native index-buffer allocation returned null");
      }
      ++analytic_sky_audit.native_index_buffer_creates;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectAnalyticSkyFailure(
          background
              ? OgreNextN1AnalyticSkyFailureStage::
                    AFTER_BACKGROUND_INDEX_BUFFER
              : OgreNextN1AnalyticSkyFailureStage::AFTER_SUN_INDEX_BUFFER,
          background
              ? "injected analytic-sky background index-buffer rollback failure"
              : "injected analytic-sky sun index-buffer rollback failure");
#endif

      Ogre::VertexBufferPackedVec vertex_buffers;
      vertex_buffers.push_back(section.vertex_buffer);
      section.vao = vao_manager->createVertexArrayObject(
          vertex_buffers, section.index_buffer, Ogre::OT_TRIANGLE_LIST);
      if (section.vao == nullptr) {
        throw std::logic_error(
            "analytic-sky native VAO allocation returned null");
      }
      ++analytic_sky_audit.native_vao_creates;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectAnalyticSkyFailure(
          background ? OgreNextN1AnalyticSkyFailureStage::AFTER_BACKGROUND_VAO
                     : OgreNextN1AnalyticSkyFailureStage::AFTER_SUN_VAO,
          background
              ? "injected analytic-sky background VAO rollback failure"
              : "injected analytic-sky sun VAO rollback failure");
#endif

      Ogre::SubMesh *submesh = section.mesh->createSubMesh();
      if (submesh == nullptr) {
        throw std::logic_error(
            "analytic-sky native submesh allocation returned null");
      }
      submesh->mVao[Ogre::VpNormal].push_back(section.vao);
      section.mesh_owns_native_buffers = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectAnalyticSkyFailure(
          background
              ? OgreNextN1AnalyticSkyFailureStage::
                    AFTER_BACKGROUND_SUBMESH_ATTACH
              : OgreNextN1AnalyticSkyFailureStage::AFTER_SUN_SUBMESH_ATTACH,
          background
              ? "injected analytic-sky background submesh-attach rollback failure"
              : "injected analytic-sky sun submesh-attach rollback failure");
#endif

      section.mesh->_setBounds(
          Ogre::Aabb(Ogre::Vector3::ZERO,
                     Ogre::Vector3(radius, radius, radius)),
          false);
      section.mesh->_setBoundingSphereRadius(radius);
      return section;
    } catch (...) {
      const std::exception_ptr creation_failure = std::current_exception();
      bool clean = true;
      if (vertex_data != nullptr) {
        OGRE_FREE_SIMD(vertex_data, Ogre::MEMCATEGORY_GEOMETRY);
      }
      if (index_data != nullptr) {
        OGRE_FREE_SIMD(index_data, Ogre::MEMCATEGORY_GEOMETRY);
      }
      if (!section.mesh && !mesh_counted) {
        try {
          section.mesh = mesh_manager.getByName(
              section.name,
              Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
          if (section.mesh) {
            ++analytic_sky_audit.native_mesh_creates;
            mesh_counted = true;
          }
        } catch (...) {
          clean = false;
        }
      }
      if (!section.mesh_owns_native_buffers) {
        bool vao_destroyed = section.vao == nullptr;
        if (section.vao != nullptr) {
          try {
            vao_manager->destroyVertexArrayObject(section.vao);
            ++analytic_sky_audit.native_vao_destroys;
            section.vao = nullptr;
            vao_destroyed = true;
          } catch (...) {
            clean = false;
          }
        }
        if (vao_destroyed && section.vertex_buffer != nullptr) {
          try {
            vao_manager->destroyVertexBuffer(section.vertex_buffer);
            ++analytic_sky_audit.native_vertex_buffer_destroys;
            section.vertex_buffer = nullptr;
          } catch (...) {
            clean = false;
          }
        }
        if (vao_destroyed && section.index_buffer != nullptr) {
          try {
            vao_manager->destroyIndexBuffer(section.index_buffer);
            ++analytic_sky_audit.native_index_buffer_destroys;
            section.index_buffer = nullptr;
          } catch (...) {
            clean = false;
          }
        }
      }
      clean = DestroyAnalyticSkySection(section) && clean;
      if (!clean) {
        faulted = true;
      }
      std::rethrow_exception(creation_failure);
    }
  }

  [[nodiscard]] bool DestroyAnalyticSkySection(
      NativeAnalyticSkySection &section) noexcept {
    if (!section.mesh && section.name.empty()) {
      return true;
    }
    bool clean = true;
    Ogre::MeshManager &mesh_manager = Ogre::MeshManager::getSingleton();
    Ogre::MeshPtr registered;
    try {
      registered = mesh_manager.getByName(
          section.name,
          Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    } catch (...) {
      clean = false;
    }
    if (!section.mesh && registered) {
      section.mesh = registered;
    }
    const bool owned_buffers = section.mesh_owns_native_buffers;
    bool removed = !section.mesh;
    if (section.mesh) {
      try {
        mesh_manager.remove(section.mesh);
        section.mesh.reset();
        removed = true;
        ++analytic_sky_audit.native_mesh_destroys;
        if (owned_buffers) {
          ++analytic_sky_audit.native_vertex_buffer_destroys;
          ++analytic_sky_audit.native_index_buffer_destroys;
          ++analytic_sky_audit.native_vao_destroys;
        }
      } catch (...) {
        clean = false;
        removed = false;
      }
    }
    try {
      if (!removed || mesh_manager.getByName(
                          section.name,
                          Ogre::ResourceGroupManager::
                              DEFAULT_RESOURCE_GROUP_NAME)) {
        clean = false;
      } else {
        ++analytic_sky_audit.native_mesh_absence_checks;
      }
    } catch (...) {
      clean = false;
    }
    section.mesh.reset();
    section.vertex_buffer = nullptr;
    section.index_buffer = nullptr;
    section.vao = nullptr;
    section.name.clear();
    section.vertex_count = 0U;
    section.index_count = 0U;
    section.mesh_owns_native_buffers = false;
    return clean;
  }

  [[nodiscard]] bool ExactAnalyticSkyBufferContents(
      Ogre::BufferPacked *buffer, const void *expected,
      std::size_t expected_elements,
      std::size_t expected_bytes_per_element) {
    if (buffer == nullptr || expected == nullptr || expected_elements == 0U ||
        buffer->getNumElements() != expected_elements ||
        buffer->getBytesPerElement() != expected_bytes_per_element) {
      return false;
    }
    ++LightingContentReadbackCounter();
    Ogre::AsyncTicketPtr ticket =
        buffer->readRequest(0U, expected_elements);
    if (!ticket) {
      return false;
    }
    bool mapped = false;
    try {
      const void *actual = ticket->map();
      mapped = true;
      const bool exact =
          actual != nullptr &&
          std::memcmp(actual, expected,
                      expected_elements * expected_bytes_per_element) == 0;
      ticket->unmap();
      mapped = false;
      if (exact) {
        ++analytic_sky_audit.native_gpu_content_readbacks;
      }
      return exact;
    } catch (...) {
      if (mapped) {
        try {
          ticket->unmap();
        } catch (...) {
          faulted = true;
        }
      }
      throw;
    }
  }

  [[nodiscard]] bool AnalyticSkyItemIsAbsent(Ogre::IdType id) const {
    Ogre::SceneManager::MovableObjectIterator objects =
        scene_manager->getMovableObjectIterator(
            Ogre::ItemFactory::FACTORY_TYPE_NAME);
    while (objects.hasMoreElements()) {
      Ogre::MovableObject *object = objects.getNext();
      if (object != nullptr && object->getId() == id) {
        return false;
      }
    }
    return true;
  }

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  void MaybeInjectAnalyticSkyFailure(
      OgreNextN1AnalyticSkyFailureStage expected,
      const char *message) {
    if (analytic_sky_failure_pending &&
        analytic_sky_failure_stage == expected) {
      analytic_sky_failure_pending = false;
      throw std::runtime_error(message);
    }
  }
#endif

  [[nodiscard]] bool RetireNativeTextureAllocation(
      Ogre::TextureGpu *&texture, const std::string &name) noexcept {
    if (texture == nullptr) {
      return true;
    }
    bool destroy_returned = false;
    bool absence_proven = false;
    if (renderer != nullptr) {
      Ogre::TextureGpuManager *manager = renderer->getTextureGpuManager();
      try {
        manager->destroyTexture(texture);
        // destroyTexture() is asynchronous when an upload or residency
        // transition is pending.  findTextureNoThrow() deliberately hides a
        // destroy-requested entry before its name has actually left Ogre's
        // registry, so it is not sufficient proof that a same-name retry is
        // safe.  Drain the streaming/task queues before auditing absence.
        manager->waitForStreamingCompletion();
        destroy_returned = true;
      } catch (...) {
        // A throwing destroy is ambiguous. The name lookup below still proves
        // whether the allocation actually retired and keeps the ledger exact.
      }
      ++texture_retired_name_lookups;
      try {
        absence_proven =
            manager->findTextureNoThrow(Ogre::IdString(name)) == nullptr;
      } catch (...) {
        absence_proven = false;
      }
      if (absence_proven) {
        ++texture_retired_name_rejections;
        ++texture_allocation_destroys;
      }
    }
    texture = nullptr;
    const bool clean = destroy_returned && absence_proven;
    if (!clean) {
      faulted = true;
    }
    return clean;
  }

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  void MaybeInjectTextureUploadFailure(
      OgreNextN1TextureUploadFailureStage stage) {
    if (texture_upload_failure_pending &&
        texture_upload_failure_stage == stage) {
      texture_upload_failure_pending = false;
      throw std::runtime_error(
          "injected RT4/V1 post-create texture upload failure");
    }
  }
#endif

  Ogre::TextureGpu *CreateUploadedTexture(
      const TextureResourceDescriptor &descriptor, const std::string &name,
      UploadedTextureChannel channel) {
    const bool display_domain_rgba =
        channel == UploadedTextureChannel::DISPLAY_DOMAIN_RGBA;
    const bool linear_rgba =
        channel == UploadedTextureChannel::LINEAR_RGBA;
    const bool rgba = channel == UploadedTextureChannel::RGBA ||
                      display_domain_rgba || linear_rgba;
    const bool normal_rg = channel == UploadedTextureChannel::NORMAL_RG;
    const Ogre::PixelFormatGpu pixel_format =
        rgba ? (display_domain_rgba || linear_rgba
                    ? Ogre::PFG_RGBA8_UNORM
                    : descriptor.color_space == TextureColorSpace::SRGB
                    ? Ogre::PFG_RGBA8_UNORM_SRGB
                    : Ogre::PFG_RGBA8_UNORM)
             : normal_rg ? Ogre::PFG_RG8_UNORM : Ogre::PFG_R8_UNORM;
    auto *image = new Ogre::Image2();
    Ogre::TextureGpu *texture = nullptr;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    std::uint64_t verified_mip_levels = 0U;
    std::uint64_t verified_rows = 0U;
    std::uint64_t verified_texels = 0U;
    std::uint64_t verified_rg_bytes = 0U;
    std::uint64_t verified_padded_source_rows = 0U;
#endif
    try {
      image->createEmptyImage(
          descriptor.width, descriptor.height, 1U,
          Ogre::TextureTypes::Type2D, pixel_format,
          static_cast<Ogre::uint8>(descriptor.mip_levels.size()));
      for (std::size_t mip_index = 0U;
           mip_index < descriptor.mip_levels.size(); ++mip_index) {
        const TextureMipLevelDescriptor &source =
            descriptor.mip_levels[mip_index];
        const Ogre::TextureBox destination =
            image->getData(static_cast<Ogre::uint8>(mip_index));
        if (destination.width != source.width ||
            destination.height != source.height) {
          throw std::logic_error(
              "validated RT4/V1 mip dimensions changed during Ogre upload");
        }
        if (normal_rg &&
            (destination.bytesPerPixel != 2U ||
             destination.bytesPerRow < source.width * 2U)) {
          throw std::runtime_error(
              "Ogre-Next RT4/V1 normal Image2 has an unexpected RG8 row layout");
        }
        for (std::uint32_t row = 0U; row < source.height; ++row) {
          const auto *source_row = source.bytes.data() +
                                   static_cast<std::size_t>(row) *
                                       source.row_pitch_bytes;
          auto *destination_row = static_cast<std::uint8_t *>(
              destination.at(0U, row, 0U));
          if (rgba) {
            std::memcpy(destination_row, source_row,
                        static_cast<std::size_t>(source.width) * 4U);
          } else if (normal_rg) {
            for (std::uint32_t column = 0U; column < source.width; ++column) {
              const std::size_t source_offset =
                  static_cast<std::size_t>(column) * 4U;
              const std::size_t destination_offset =
                  static_cast<std::size_t>(column) * 2U;
              destination_row[destination_offset] = source_row[source_offset];
              destination_row[destination_offset + 1U] =
                  source_row[source_offset + 1U];
            }
            for (std::uint32_t column = 0U; column < source.width; ++column) {
              const std::size_t source_offset =
                  static_cast<std::size_t>(column) * 4U;
              const std::size_t destination_offset =
                  static_cast<std::size_t>(column) * 2U;
              if (destination_row[destination_offset] !=
                      source_row[source_offset] ||
                  destination_row[destination_offset + 1U] !=
                      source_row[source_offset + 1U]) {
                throw std::runtime_error(
                    "Ogre-Next RT4/V1 normal Image2 did not preserve exact source RG bytes");
              }
            }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
            ++verified_rows;
            verified_texels += source.width;
            verified_rg_bytes += static_cast<std::uint64_t>(source.width) * 2U;
            if (source.row_pitch_bytes > source.width * 4U) {
              ++verified_padded_source_rows;
            }
#endif
          } else {
            const std::size_t source_channel =
                channel == UploadedTextureChannel::GREEN ? 1U : 2U;
            for (std::uint32_t column = 0U; column < source.width; ++column) {
              destination_row[column] =
                  source_row[static_cast<std::size_t>(column) * 4U +
                             source_channel];
            }
          }
        }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        if (normal_rg) {
          ++verified_mip_levels;
        }
#endif
      }

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      if (normal_rg) {
        ++normal_uploads_verified;
        normal_upload_mip_levels_verified += verified_mip_levels;
        normal_upload_rows_verified += verified_rows;
        normal_upload_texels_verified += verified_texels;
        normal_upload_rg_bytes_verified += verified_rg_bytes;
        normal_upload_padded_source_rows_verified +=
            verified_padded_source_rows;
      }
#endif

      Ogre::TextureGpuManager *texture_manager =
          renderer->getTextureGpuManager();
      if (texture_manager->findTextureNoThrow(Ogre::IdString(name)) != nullptr) {
        throw std::runtime_error(
            "Ogre-Next RT4/V1 texture name survived before replacement allocation");
      }
      texture = texture_manager->createTexture(
          name, Ogre::GpuPageOutStrategy::Discard, 0U,
          Ogre::TextureTypes::Type2D);
      ++texture_allocation_creates;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectTextureUploadFailure(
          OgreNextN1TextureUploadFailureStage::AFTER_CREATE);
#endif
      texture->setResolution(descriptor.width, descriptor.height);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectTextureUploadFailure(
          OgreNextN1TextureUploadFailureStage::AFTER_SET_RESOLUTION);
#endif
      texture->setNumMipmaps(
          static_cast<Ogre::uint8>(descriptor.mip_levels.size()));
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectTextureUploadFailure(
          OgreNextN1TextureUploadFailureStage::AFTER_SET_MIPMAPS);
#endif
      texture->setPixelFormat(pixel_format);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectTextureUploadFailure(
          OgreNextN1TextureUploadFailureStage::AFTER_SET_PIXEL_FORMAT);
#endif
      texture->scheduleTransitionTo(Ogre::GpuResidency::Resident, image, true);
      image = nullptr;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectTextureUploadFailure(
          OgreNextN1TextureUploadFailureStage::AFTER_SCHEDULE_TRANSITION);
#endif
      return texture;
    } catch (...) {
      delete image;
      static_cast<void>(RetireNativeTextureAllocation(texture, name));
      throw;
    }
  }

  NativeTexture CreateTexture(const RenderAssetReference &asset,
                              const TextureResourceDescriptor &descriptor,
                              NativeTextureUsage usage) {
    NativeTexture native;
    native.asset = asset;
    native.usage = usage;
    const bool aliases_display_domain =
        usage.display_domain_rgba &&
        (usage.sampled_rgba || usage.linear_rgba || usage.roughness_g ||
         usage.metallic_b || usage.normal_rg);
    const bool aliases_linear_role =
        usage.linear_rgba &&
        (usage.sampled_rgba || usage.display_domain_rgba ||
         usage.roughness_g || usage.metallic_b || usage.normal_rg);
    const bool aliases_normal_role =
        usage.normal_rg &&
        (usage.sampled_rgba || usage.display_domain_rgba ||
         usage.linear_rgba ||
         usage.roughness_g || usage.metallic_b);
    if (usage.empty() ||
        (usage.sampled_rgba && (usage.roughness_g || usage.metallic_b)) ||
        aliases_display_domain ||
        aliases_linear_role ||
        aliases_normal_role ||
        ((usage.sampled_rgba || usage.display_domain_rgba) &&
         descriptor.color_space != TextureColorSpace::SRGB) ||
        ((usage.linear_rgba || usage.roughness_g || usage.metallic_b ||
          usage.normal_rg) &&
         descriptor.color_space != TextureColorSpace::LINEAR)) {
      throw std::logic_error(
          "RT4/V1 texture alias or usage is incompatible with its sampled color-space role");
    }
    native.sampled_name = TextureAssetName(asset, usage);
    native.linear_name = native.sampled_name + "_linear_rgba";
    native.roughness_name = native.sampled_name + "_roughness_g";
    native.metallic_name = native.sampled_name + "_metallic_b";
    native.normal_name = native.sampled_name + "_normal_rg";
    try {
      if (usage.sampled_rgba) {
        native.sampled = CreateUploadedTexture(
            descriptor, native.sampled_name, UploadedTextureChannel::RGBA);
      }
      if (usage.display_domain_rgba) {
        native.sampled = CreateUploadedTexture(
            descriptor, native.sampled_name,
            UploadedTextureChannel::DISPLAY_DOMAIN_RGBA);
      }
      if (usage.linear_rgba) {
        native.linear = CreateUploadedTexture(
            descriptor, native.linear_name,
            UploadedTextureChannel::LINEAR_RGBA);
      }
      if (usage.roughness_g) {
        native.roughness = CreateUploadedTexture(
            descriptor, native.roughness_name,
            UploadedTextureChannel::GREEN);
      }
      if (usage.metallic_b) {
        native.metallic = CreateUploadedTexture(
            descriptor, native.metallic_name, UploadedTextureChannel::BLUE);
      }
      if (usage.normal_rg) {
        native.normal = CreateUploadedTexture(
            descriptor, native.normal_name,
            UploadedTextureChannel::NORMAL_RG);
      }
      return native;
    } catch (...) {
      if (!DestroyTexture(native)) {
        faulted = true;
      }
      throw;
    }
  }

  void VerifyTexture(const NativeTexture &native,
                     const TextureResourceDescriptor &descriptor,
                     NativeTextureUsage expected_usage) const {
    const auto verify_one = [&](const Ogre::TextureGpu *texture,
                                Ogre::PixelFormatGpu format) {
      if (texture == nullptr || !texture->isDataReady() ||
          texture->getResidencyStatus() != Ogre::GpuResidency::Resident ||
          texture->getWidth() != descriptor.width ||
          texture->getHeight() != descriptor.height ||
          texture->getNumMipmaps() != descriptor.mip_levels.size() ||
          texture->getPixelFormat() != format) {
        throw std::runtime_error(
            "Ogre-Next RT4/V1 texture upload failed strict metadata or residency validation");
      }
    };
    if (native.usage != expected_usage || expected_usage.empty()) {
      throw std::runtime_error(
          "Ogre-Next RT4/V1 native texture usage differs from the current material graph");
    }
    if (expected_usage.sampled_rgba) {
      verify_one(native.sampled, Ogre::PFG_RGBA8_UNORM_SRGB);
    } else if (expected_usage.display_domain_rgba) {
      verify_one(native.sampled, Ogre::PFG_RGBA8_UNORM);
    } else if (native.sampled != nullptr) {
      throw std::runtime_error(
          "Ogre-Next RT4/V1 allocated an unused sampled RGBA texture");
    }
    if (expected_usage.linear_rgba) {
      verify_one(native.linear, Ogre::PFG_RGBA8_UNORM);
    } else if (native.linear != nullptr) {
      throw std::runtime_error(
          "Ogre-Next RT4/V1 allocated an unused linear RGBA texture");
    }
    if (expected_usage.roughness_g) {
      verify_one(native.roughness, Ogre::PFG_R8_UNORM);
    } else if (native.roughness != nullptr) {
      throw std::runtime_error(
          "Ogre-Next RT4/V1 allocated an unused roughness derivative");
    }
    if (expected_usage.metallic_b) {
      verify_one(native.metallic, Ogre::PFG_R8_UNORM);
    } else if (native.metallic != nullptr) {
      throw std::runtime_error(
          "Ogre-Next RT4/V1 allocated an unused metallic derivative");
    }
    if (expected_usage.normal_rg) {
      verify_one(native.normal, Ogre::PFG_RG8_UNORM);
    } else if (native.normal != nullptr) {
      throw std::runtime_error(
          "Ogre-Next RT4/V1 allocated an unused normal RG derivative");
    }
  }

  NativeMaterial CreateMaterial(const RenderAssetReference &asset,
                                const MaterialDescriptor &descriptor,
                                const RenderAssetRegistry &candidate_registry,
                                const std::map<RenderAssetId, NativeTexture>
                                    &candidate_textures) {
    NativeMaterial native;
    native.asset = asset;
    if (descriptor.model == MaterialModel::UNLIT) {
      native.kind = NativeMaterial::Kind::DISPLAY_DOMAIN_UNLIT;
      native.name = AssetName("RoRDisplayDomainUnlit", asset);
      // Honor the validated blend/depth profile at creation: the opaque scene
      // profile reproduces the historical default blocks exactly, while the
      // HUD overlay profile carries premultiplied source-over without depth
      // writes.
      const Ogre::HlmsMacroblock unlit_macroblock =
          BuildPbsMacroblock(descriptor);
      const Ogre::HlmsBlendblock unlit_blendblock =
          BuildPbsBlendblock(descriptor);
      try {
        native.display_domain_unlit_datablock =
            static_cast<Ogre::HlmsUnlitDatablock *>(unlit->createDatablock(
                native.name, native.name, unlit_macroblock,
                unlit_blendblock, Ogre::HlmsParamVec()));
        const auto found =
            candidate_textures.find(descriptor.base_color_texture.texture.id);
        const SamplerResourceDescriptor *sampler_descriptor =
            candidate_registry.ResolveSampler(
                descriptor.base_color_texture.sampler);
        if (found == candidate_textures.end() ||
            found->second.asset != descriptor.base_color_texture.texture ||
            sampler_descriptor == nullptr || found->second.sampled == nullptr ||
            !found->second.usage.display_domain_rgba ||
            found->second.usage.sampled_rgba ||
            found->second.usage.linear_rgba ||
            found->second.usage.roughness_g ||
            found->second.usage.metallic_b || found->second.usage.normal_rg) {
          throw std::logic_error(
              "validated RT4/V1 display-domain Unlit dependency disappeared before native binding");
        }
        const Ogre::HlmsSamplerblock sampler =
            ToOgreSampler(*sampler_descriptor);
        native.display_domain_unlit_datablock->setUseColour(true);
        native.display_domain_unlit_datablock->setColour(
            Ogre::ColourValue::White);
        native.display_domain_unlit_datablock->setTexture(
            0U, found->second.sampled, &sampler);
        native.display_domain_unlit_datablock->setTextureUvSource(0U,
                                                                         0U);
        VerifyDisplayDomainUnlitMapping(
            *native.display_domain_unlit_datablock, *unlit,
            found->second.sampled, sampler, unlit_macroblock,
            unlit_blendblock);
        return native;
      } catch (...) {
        if (!DestroyMaterial(native)) {
          faulted = true;
        }
        throw;
      }
    }
    const bool thin_slab_transmission =
        descriptor.transmission_mode ==
        MaterialTransmissionMode::THIN_PARALLEL_SLAB;
    native.name = AssetName(thin_slab_transmission
                                ? "RoRN1TransmissionMaterial"
                                : "RoRN1Material",
                            asset);
    const ValidationResult uv0_affine_validation =
        BuildOgreNextN1PbsUv0AffineTransform(descriptor,
                                             native.uv0_affine);
    if (!uv0_affine_validation) {
      throw std::logic_error(
          "validated RT4/V1 PBS UV0 affine profile disappeared before native material creation");
    }
    const Ogre::HlmsMacroblock macroblock = BuildPbsMacroblock(descriptor);
    const Ogre::HlmsBlendblock blendblock = BuildPbsBlendblock(descriptor);
    try {
      native.pbs_datablock = static_cast<Ogre::HlmsPbsDatablock *>(
          pbs->createDatablock(native.name, native.name, macroblock,
                               blendblock, Ogre::HlmsParamVec()));
      native.pbs_datablock->setUserValue(
          0U, Ogre::Vector4(native.uv0_affine.scale.x,
                            native.uv0_affine.scale.y,
                            native.uv0_affine.offset.x,
                            native.uv0_affine.offset.y));
      native.pbs_datablock->setUserValue(
          1U,
          thin_slab_transmission
              ? Ogre::Vector4(descriptor.attenuation_color.x,
                              descriptor.attenuation_color.y,
                              descriptor.attenuation_color.z,
                              descriptor.attenuation_distance_m)
              : Ogre::Vector4::ZERO);
      const float projection_y_scale =
          thin_slab_transmission && camera != nullptr
              ? 1.0F / std::tan(camera->getFOVy().valueRadians() * 0.5F)
              : 0.0F;
      native.pbs_datablock->setUserValue(
          2U,
          thin_slab_transmission
              ? Ogre::Vector4(descriptor.index_of_refraction,
                              descriptor.slab_thickness_m,
                              descriptor.transmission_factor,
                              projection_y_scale)
              : Ogre::Vector4::ZERO);
      native.pbs_datablock->setBrdf(Ogre::PbsBrdf::Default);
      const bool specular_workflow =
          descriptor.pbr_workflow == MaterialPbrWorkflow::SPECULAR;
      native.pbs_datablock->setWorkflow(
          specular_workflow ? Ogre::HlmsPbsDatablock::SpecularWorkflow
                            : Ogre::HlmsPbsDatablock::MetallicWorkflow);
      if (specular_workflow) {
        // The pinned PBS constructor leaves F0 at 0.818 and changing workflow
        // does not reset it. The portable descriptor's reviewed dielectric
        // IOR is the authority; one scalar IOR deliberately produces one
        // shared RGB Fresnel term (IOR 1.5 -> F0 0.04).
        native.pbs_datablock->setIndexOfRefraction(
            Ogre::Vector3(descriptor.index_of_refraction), false);
      }
      native.pbs_datablock->setDiffuse(
          Ogre::Vector3(descriptor.base_color_factor.x,
                        descriptor.base_color_factor.y,
                        descriptor.base_color_factor.z));
      native.pbs_datablock->setSpecular(
          specular_workflow
              ? Ogre::Vector3(descriptor.specular_factor.x,
                              descriptor.specular_factor.y,
                              descriptor.specular_factor.z)
              : Ogre::Vector3::UNIT_SCALE);
      if (!specular_workflow) {
        // Pinned PBS explicitly forbids setMetalness in SpecularWorkflow.
        native.pbs_datablock->setMetalness(descriptor.metallic_factor);
      }
      native.pbs_datablock->setRoughness(descriptor.roughness_factor);
      native.pbs_datablock->setEmissive(
          Ogre::Vector3(descriptor.emissive_factor.x,
                        descriptor.emissive_factor.y,
                        descriptor.emissive_factor.z) *
          descriptor.emissive_strength);
      // Pinned Ogre applies this value as a lerp from (0, 0, 1), not as the
      // glTF x/y normal scale. The admission policy therefore requires the
      // exact identity value and we write/read it explicitly here.
      native.pbs_datablock->setNormalMapWeight(1.0F);
      native.pbs_datablock->setTwoSidedLighting(descriptor.double_sided, false);
      native.pbs_datablock->setTransparency(
          thin_slab_transmission
              ? 1.0F - descriptor.transmission_factor
              : descriptor.blend_mode != MaterialBlendMode::REPLACE
              ? descriptor.base_color_factor.w
              : 1.0F,
          thin_slab_transmission
              ? Ogre::HlmsPbsDatablock::Refractive
              : descriptor.blend_mode != MaterialBlendMode::REPLACE
              ? Ogre::HlmsPbsDatablock::Fade
              : Ogre::HlmsPbsDatablock::None,
          !thin_slab_transmission, thin_slab_transmission);
      native.pbs_datablock->setRefractionStrength(0.0F);
      native.pbs_datablock->setAlphaTest(
          descriptor.alpha_test_mode == MaterialAlphaTestMode::GREATER
              ? Ogre::CMPF_GREATER_EQUAL
              : descriptor.alpha_test_mode ==
                        MaterialAlphaTestMode::GREATER_EQUAL
                    ? Ogre::CMPF_GREATER
                    : Ogre::CMPF_ALWAYS_PASS,
          false, !thin_slab_transmission);
      native.pbs_datablock->setAlphaTestThreshold(descriptor.alpha_cutoff);
      if (directional_shadow_mode ==
          OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1) {
        // Do not inherit an upstream default implicitly: this bias is part of
        // the reviewed checkpoint and is read back again before submission.
        native.pbs_datablock->mShadowConstantBias =
            kOgreNextPssmMaterialConstantBias;
      }
      const auto bind_texture =
          [&](const TextureBinding &binding, Ogre::PbsTextureTypes slot,
              Ogre::TextureGpu *NativeTexture::*native_member) {
            if (!binding.texture.valid()) {
              return;
            }
            const auto found = candidate_textures.find(binding.texture.id);
            const SamplerResourceDescriptor *sampler_descriptor =
                candidate_registry.ResolveSampler(binding.sampler);
            if (found == candidate_textures.end() ||
                found->second.asset != binding.texture ||
                sampler_descriptor == nullptr) {
              throw std::logic_error(
                  "validated RT4/V1 material dependency disappeared before native binding");
            }
            Ogre::TextureGpu *texture = found->second.*native_member;
            if (texture == nullptr) {
              throw std::logic_error(
                  "validated RT4/V1 texture lacks its required uploaded channel");
            }
            const Ogre::HlmsSamplerblock sampler =
                ToOgreSampler(*sampler_descriptor);
            const auto slot_index = static_cast<Ogre::uint8>(slot);
            const std::uint32_t slot_bit =
                1U << static_cast<std::uint32_t>(slot_index);
            if ((native.native_texture_slot_mask & slot_bit) != 0U) {
              throw std::logic_error(
                  "validated RT4/V1 material aliases one native PBS texture slot");
            }
            native.pbs_datablock->setTexture(slot_index, texture, &sampler);
            native.pbs_datablock->setTextureUvSource(
                slot, binding.texture_coordinate_set);
            const Ogre::HlmsSamplerblock *actual_sampler =
                native.pbs_datablock->getSamplerblock(slot_index);
            if (native.pbs_datablock->getTexture(slot_index) != texture ||
                native.pbs_datablock->getTextureUvSource(slot) !=
                    binding.texture_coordinate_set ||
                actual_sampler == nullptr) {
              throw std::runtime_error(
                  "Ogre-Next RT4/V1 live PBS texture binding differs from the reviewed mapping");
            }
            VerifySamplerMapping(*actual_sampler, sampler);
            native.native_texture_slot_mask |= slot_bit;
          };
      if (raster_feature_tier ==
          OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
        bind_texture(descriptor.base_color_texture, Ogre::PBSM_DIFFUSE,
                     &NativeTexture::sampled);
        bind_texture(descriptor.metallic_roughness_texture,
                     Ogre::PBSM_ROUGHNESS, &NativeTexture::roughness);
        bind_texture(descriptor.metallic_roughness_texture,
                     Ogre::PBSM_METALLIC, &NativeTexture::metallic);
        bind_texture(descriptor.normal_texture, Ogre::PBSM_NORMAL,
                     &NativeTexture::normal);
        bind_texture(descriptor.emissive_texture, Ogre::PBSM_EMISSIVE,
                     &NativeTexture::sampled);
        bind_texture(descriptor.specular_texture, Ogre::PBSM_SPECULAR,
                     &NativeTexture::linear);
        // Weighted detail layers. Unlike every slot above, each detail map
        // keeps its own UV scale in the datablock: the custom UV macro piece
        // rewrites only the five non-detail lookups, so the weight mask stays
        // unscaled across the surface while each layer repeats at the rate its
        // texture was authored for. That separation is the entire point of the
        // v6 profile and is what lets a page-sized surface show authored-
        // density material instead of one stretched wash.
        bind_texture(descriptor.detail_weight_texture, Ogre::PBSM_DETAIL_WEIGHT,
                     &NativeTexture::linear);
        constexpr std::array<Ogre::PbsTextureTypes, kMaterialDetailMapCount>
            kDetailSlots{Ogre::PBSM_DETAIL0, Ogre::PBSM_DETAIL1,
                         Ogre::PBSM_DETAIL2, Ogre::PBSM_DETAIL3};
        for (std::size_t layer = 0U; layer < kMaterialDetailMapCount;
             ++layer) {
          const TextureBinding &detail = descriptor.detail_textures[layer];
          bind_texture(detail, kDetailSlots[layer], &NativeTexture::sampled);
          if (!detail.texture.valid()) {
            continue;
          }
          const auto detail_index = static_cast<Ogre::uint8>(layer);
          // Sequential lerp over the running result, which is exactly how the
          // legacy terrain material composited its layers.
          native.pbs_datablock->setDetailMapBlendMode(
              detail_index, Ogre::PBSM_BLEND_NORMAL_NON_PREMUL);
          native.pbs_datablock->setDetailMapWeight(
              detail_index, descriptor.detail_weights[layer]);
          // Ogre packs this as XY = offset, ZW = scale, which is the opposite
          // order from the userValue[0] affine packing above.
          native.pbs_datablock->setDetailMapOffsetScale(
              detail_index,
              Ogre::Vector4(detail.offset.x, detail.offset.y, detail.scale.x,
                            detail.scale.y));
          if (native.pbs_datablock->getDetailMapBlendMode(detail_index) !=
                  Ogre::PBSM_BLEND_NORMAL_NON_PREMUL ||
              native.pbs_datablock->getDetailMapOffsetScale(detail_index) !=
                  Ogre::Vector4(detail.offset.x, detail.offset.y,
                                detail.scale.x, detail.scale.y)) {
            throw std::runtime_error(
                "Ogre-Next RT4/V1 live PBS detail state differs from the reviewed mapping");
          }
        }
      }
      std::uint32_t native_texture_slot_count = 0U;
      for (std::uint32_t slot = 0U;
           slot < static_cast<std::uint32_t>(Ogre::NUM_PBSM_TEXTURE_TYPES);
           ++slot) {
        native_texture_slot_count +=
            (native.native_texture_slot_mask & (1U << slot)) != 0U ? 1U : 0U;
      }
      if (native_texture_slot_count !=
          native.uv0_affine.native_texture_slot_count +
              native.uv0_affine.native_detail_texture_slot_count) {
        throw std::runtime_error(
            "Ogre-Next RT4/V1 did not bind every authored UV0 affine texture slot");
      }
      VerifyPbsMapping(*native.pbs_datablock, descriptor);
      return native;
    } catch (...) {
      if (!DestroyMaterial(native)) {
        faulted = true;
      }
      throw;
    }
  }

  [[nodiscard]] bool DestroyMesh(NativeMesh &native) noexcept {
    if (!native.mesh) {
      return true;
    }
    bool clean = true;
    try {
      Ogre::MeshManager::getSingleton().remove(native.mesh);
    } catch (...) {
      clean = false;
    }
    native.mesh.reset();
    native.vertex_buffer = nullptr;
    native.index_buffer = nullptr;
    return clean;
  }

  [[nodiscard]] bool DestroyTexture(NativeTexture &native) noexcept {
    bool clean = true;
    const auto destroy_one = [&](Ogre::TextureGpu *&texture,
                                 const std::string &name) {
      clean = RetireNativeTextureAllocation(texture, name) && clean;
    };
    destroy_one(native.normal, native.normal_name);
    destroy_one(native.metallic, native.metallic_name);
    destroy_one(native.roughness, native.roughness_name);
    destroy_one(native.linear, native.linear_name);
    destroy_one(native.sampled, native.sampled_name);
    return clean;
  }

  [[nodiscard]] bool DestroyMaterial(NativeMaterial &native) noexcept {
    if (native.pbs_datablock == nullptr &&
        native.display_domain_unlit_datablock == nullptr) {
      return true;
    }
    bool clean = !(native.pbs_datablock != nullptr &&
                   native.display_domain_unlit_datablock != nullptr);
    if (native.pbs_datablock != nullptr) {
      clean = pbs != nullptr && clean;
      try {
        if (pbs != nullptr) {
          pbs->destroyDatablock(Ogre::IdString(native.name));
          clean = pbs->getDatablock(Ogre::IdString(native.name)) == nullptr &&
                  clean;
        }
      } catch (...) {
        clean = false;
      }
    }
    if (native.display_domain_unlit_datablock != nullptr) {
      clean = unlit != nullptr && clean;
      try {
        if (unlit != nullptr) {
          unlit->destroyDatablock(Ogre::IdString(native.name));
          clean =
              unlit->getDatablock(Ogre::IdString(native.name)) == nullptr &&
              clean;
        }
      } catch (...) {
        clean = false;
      }
    }
    native.pbs_datablock = nullptr;
    native.display_domain_unlit_datablock = nullptr;
    return clean;
  }

  [[nodiscard]] bool DestroyCatalog() noexcept {
    bool clean = true;
    for (auto &entry : materials) {
      clean = DestroyMaterial(entry.second) && clean;
    }
    materials.clear();
    for (auto &entry : textures) {
      clean = DestroyTexture(entry.second) && clean;
    }
    textures.clear();
    for (auto &entry : meshes) {
      clean = DestroyMesh(entry.second) && clean;
    }
    meshes.clear();
    registry.reset();
    return clean;
  }

  [[nodiscard]] bool DestroyFrameMeshes() noexcept {
    bool clean = true;
    for (NativeMesh &native : frame_meshes) {
      clean = DestroyMesh(native) && clean;
    }
    frame_meshes.clear();
    return clean;
  }

  [[nodiscard]] bool DestroyRetainedLights() noexcept {
    bool clean = true;
    for (auto iterator = retained_lights.rbegin();
         iterator != retained_lights.rend(); ++iterator) {
      if (iterator->node != nullptr && iterator->light != nullptr) {
        try {
          iterator->node->detachObject(iterator->light);
        } catch (...) {
          clean = false;
        }
      }
      if (iterator->light != nullptr) {
        try {
          scene_manager->destroyLight(iterator->light);
        } catch (...) {
          clean = false;
        }
        iterator->light = nullptr;
      }
      if (iterator->node != nullptr) {
        try {
          scene_manager->destroySceneNode(iterator->node);
        } catch (...) {
          clean = false;
        }
        iterator->node = nullptr;
      }
    }
    retained_lights.clear();
    retained_audit.retained_lights = 0U;
    return clean;
  }

  void AddRetainedContribution(const RetainedInstance &record) noexcept {
    retained_pbs_items += record.pbs ? 1U : 0U;
    retained_transmission_items += record.transmission ? 1U : 0U;
    retained_normal_mapped_items += record.normal_mapped ? 1U : 0U;
    retained_emissive_items += record.emissive ? 1U : 0U;
    retained_shadow_casters += record.casts_shadow ? 1U : 0U;
    retained_shadow_receivers += record.receives_shadow ? 1U : 0U;
  }

  void SubtractRetainedContribution(const RetainedInstance &record) noexcept {
    retained_pbs_items -= record.pbs ? 1U : 0U;
    retained_transmission_items -= record.transmission ? 1U : 0U;
    retained_normal_mapped_items -= record.normal_mapped ? 1U : 0U;
    retained_emissive_items -= record.emissive ? 1U : 0U;
    retained_shadow_casters -= record.casts_shadow ? 1U : 0U;
    retained_shadow_receivers -= record.receives_shadow ? 1U : 0U;
  }

  void ResetRetainedContributions() noexcept {
    retained_pbs_items = 0U;
    retained_transmission_items = 0U;
    retained_normal_mapped_items = 0U;
    retained_emissive_items = 0U;
    retained_shadow_casters = 0U;
    retained_shadow_receivers = 0U;
  }

  /// Destroys a retained instance's PSSM non-receiver clone and proves its
  /// native absence. The clone pointer is nulled before the fallible absence
  /// lookup so a later full teardown never double-destroys the datablock.
  [[nodiscard]] bool
  DestroyRetainedReceiverClone(RetainedInstance &record) noexcept {
    if (record.receiver_clone == nullptr) {
      return true;
    }
    bool clean = true;
    const Ogre::IdString clone_name(record.receiver_clone_name);
    record.receiver_clone = nullptr;
    record.receiver_clone_name.clear();
    bool destroy_returned = false;
    try {
      pbs->destroyDatablock(clone_name);
      ++shadow_audit.receiver_datablock_destroys;
      destroy_returned = true;
    } catch (...) {
      clean = false;
    }
    try {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      MaybeInjectPssmFailureStage(
          pssm_failure_stage, pssm_failure_pending,
          OgreNextN1PssmFailureStage::
              DURING_RECEIVER_DATABLOCK_CLEANUP_LOOKUP,
          "injected PSSM receiver-datablock cleanup absence-lookup failure");
#endif
      if (!destroy_returned || pbs->getDatablock(clone_name) != nullptr) {
        clean = false;
      } else {
        ++shadow_audit.receiver_datablock_cleanup_absence_checks;
      }
    } catch (...) {
      clean = false;
    }
    return clean;
  }

  /// Reverse-order native teardown of one retained instance: Item before
  /// SceneNode before receiver clone before deformed mesh — a datablock must
  /// never die while a live renderable links to it, and a mesh never while an
  /// Item holds its MeshPtr. With native interop the deformed mesh is parked
  /// on the retiring list and destroyed after the next published-frame
  /// discard; without interop Ogre's VaoManager already defers the actual GPU
  /// free by frame count.
  [[nodiscard]] bool
  DestroyRetainedInstanceNative(RetainedInstance &record) noexcept {
    bool clean = true;
    if (record.node != nullptr && record.item != nullptr) {
      try {
        record.node->detachObject(record.item);
      } catch (...) {
        clean = false;
      }
    }
    if (record.item != nullptr) {
      try {
        scene_manager->destroyItem(record.item);
      } catch (...) {
        clean = false;
      }
      record.item = nullptr;
    }
    if (record.node != nullptr) {
      try {
        scene_manager->destroySceneNode(record.node);
      } catch (...) {
        clean = false;
      }
      record.node = nullptr;
    }
    clean = DestroyRetainedReceiverClone(record) && clean;
    if (record.deformed_mesh.mesh) {
      if (native_interop) {
        try {
          frame_meshes.push_back(std::move(record.deformed_mesh));
        } catch (...) {
          clean = DestroyMesh(record.deformed_mesh) && clean;
        }
      } else {
        clean = DestroyMesh(record.deformed_mesh) && clean;
      }
      record.deformed_mesh = NativeMesh{};
    }
    if (record.bounds_valid) {
      record.bounds_valid = false;
      if (retained_audit.bounds_entries != 0U) {
        --retained_audit.bounds_entries;
      }
    }
    return clean;
  }

  /// Destroys every retained instance whose mesh or material the candidate
  /// catalog retires or replaces, so no datablock or mesh is ever destroyed
  /// while a live Item still links to it. The next present's diff recreates
  /// the instances from the new catalog.
  [[nodiscard]] bool DestroyRetainedInstancesForAssetReplacement(
      const std::map<RenderAssetId, NativeMesh> &candidate_meshes,
      const std::map<RenderAssetId, NativeMaterial> &candidate_materials)
      noexcept {
    bool clean = true;
    for (auto iterator = retained_instances.begin();
         iterator != retained_instances.end();) {
      const MeshInstanceDescriptor &descriptor = iterator->second.descriptor;
      const auto mesh = candidate_meshes.find(descriptor.mesh.id);
      const auto material = candidate_materials.find(descriptor.material.id);
      if (mesh != candidate_meshes.end() &&
          mesh->second.asset == descriptor.mesh &&
          material != candidate_materials.end() &&
          material->second.asset == descriptor.material) {
        ++iterator;
        continue;
      }
      clean = DestroyRetainedInstanceNative(iterator->second) && clean;
      SubtractRetainedContribution(iterator->second);
      ++retained_audit.destroyed;
      iterator = retained_instances.erase(iterator);
    }
    retained_audit.retained_instances =
        static_cast<std::uint64_t>(retained_instances.size());
    return clean;
  }

  /// Tears the retained native scene down to the empty state. Used by the
  /// present failure path, asset retirement, generation reset, and shutdown;
  /// the next present rebuilds it from scratch at first-frame cost.
  [[nodiscard]] bool DestroyRetainedScene() noexcept {
    bool clean = true;
    for (auto iterator = retained_instances.rbegin();
         iterator != retained_instances.rend(); ++iterator) {
      clean = DestroyRetainedInstanceNative(iterator->second) && clean;
    }
    retained_instances.clear();
    retained_audit.retained_instances = 0U;
    retained_audit.bounds_entries = 0U;
    ResetRetainedContributions();
    clean = DestroyRetainedLights() && clean;
    clean = DestroyFrameMeshes() && clean;
    return clean;
  }

  [[nodiscard]] bool DestroyRetainedOutputTarget() noexcept {
    if (retained_output_target == nullptr) {
      return true;
    }
    if (renderer == nullptr || renderer->getTextureGpuManager() == nullptr) {
      return false;
    }
    try {
      renderer->getTextureGpuManager()->destroyTexture(
          retained_output_target);
      retained_output_target = nullptr;
      return true;
    } catch (...) {
      return false;
    }
  }

  [[nodiscard]] OgreNextHdrCompositorAudit HdrCompositorAudit() const noexcept {
    OgreNextHdrCompositorAudit audit;
    audit.scene_topology = hdr_scene_topology;
    audit.enabled = hdr_enabled;
    audit.native_workspace_live = hdr_workspace != nullptr &&
                                  hdr_output_target != nullptr;
    audit.deterministic_delta_bound = hdr_manual_delta_bound;
    audit.native_r16_history_validated = hdr_native_history_validated;
    audit.exact_current_to_old_copy_verified =
        hdr_exact_current_to_old_copy_verified;
    audit.hud_workspace_verified = hdr_hud_workspace_verified;
    audit.opaque_depth_export_verified = hdr_opaque_depth_export_verified;
    audit.aerial_haze_workspace_verified = hdr_aerial_haze_workspace_verified;
    audit.aerial_haze_constants_bound = hdr_aerial_haze_constants_bound;
    audit.aerial_haze_applied = hdr_aerial_haze_applied;
    audit.aerial_haze_basis_rejections = hdr_aerial_haze_basis_rejections;
    audit.aerial_haze_extinction_per_meter =
        hdr_aerial_haze_extinction_per_meter;
    audit.aerial_haze_inscatter = hdr_aerial_haze_inscatter;
    audit.width = hdr_width;
    audit.height = hdr_height;
    audit.warmup_frames = hdr_warmup_frames;
    audit.committed_frames = hdr_temporal_state.committed_frame_id();
    audit.pssm_finalization_attempts = hdr_pssm_finalization_attempts;
    audit.pssm_finalization_commits = hdr_pssm_finalization_commits;
    audit.pssm_finalization_rollbacks = hdr_pssm_finalization_rollbacks;
    audit.pssm_warmup_native_absence_checks =
        hdr_pssm_warmup_native_absence_checks;
    audit.pssm_deferred_until_scene_population =
        hdr_pssm_deferred_until_scene_population_verified;
    audit.pssm_finalized_with_populated_scene =
        hdr_pssm_finalized_with_populated_scene;
    audit.zero_light_pssm_warmup_avoided =
        hdr_zero_light_pssm_warmup_avoided_verified;
    audit.previous_inverse_luminance_r16_bits =
        hdr_temporal_state.previous_inverse_luminance().bits;
    audit.history_validation_mode = hdr_history_comparison.mode;
    audit.reference_inverse_luminance_r16_bits =
        hdr_history_comparison.reference_inverse_luminance_r16.bits;
    audit.history_ogre_exposure = hdr_history_comparison.ogre_exposure;
    audit.history_minimum_auto_exposure =
        hdr_history_comparison.minimum_auto_exposure;
    audit.history_maximum_auto_exposure =
        hdr_history_comparison.maximum_auto_exposure;
    audit.history_average_log_luminance =
        hdr_history_comparison.average_log_luminance;
    audit.history_previous_inverse_luminance_r16_bits =
        hdr_history_comparison.previous_inverse_luminance_r16.bits;
    audit.history_delta_seconds = hdr_history_comparison.delta_seconds;
    audit.history_absolute_error = hdr_history_comparison.absolute_error;
    audit.history_allowed_error = hdr_history_comparison.allowed_error;
    audit.history_conditioning_bound =
        hdr_history_comparison.conditioning_bound;
    audit.history_binary32_rounding_bound =
        hdr_history_comparison.binary32_rounding_bound;
    audit.history_storage_ulp = hdr_history_comparison.storage_ulp;
    audit.history_r16_ulp_distance =
        hdr_history_comparison.r16_ulp_distance;
    return audit;
  }

  [[nodiscard]] OgreNextNativeLightingPassAudit
  NativeLightingPassAudit() const noexcept {
    return lighting_audit;
  }

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  [[nodiscard]] OgreNextHdrLightingSplitContentEvidence
  CaptureHdrLightingSplitContentEvidence() {
    if (!initialized || faulted || !hdr_enabled || hdr_workspace == nullptr ||
        !retain_native_lighting_content_evidence ||
        ProductionPresentationEnabled() || lighting_audit.completed_frames == 0U) {
      throw std::logic_error(
          "HDR split content evidence is unavailable outside an opted-in isolated artifact after a committed frame");
    }
    Ogre::CompositorNode *rendering =
        hdr_workspace->findNode(kOgreNextHdrRenderingNode);
    if (rendering == nullptr) {
      throw std::runtime_error(
          "HDR split rendering node disappeared before artifact capture");
    }
    OgreNextHdrLightingSplitContentEvidence evidence;
    evidence.frame_id = lighting_audit.last_frame_id;
    evidence.width = hdr_width;
    evidence.height = hdr_height;
    const auto download = [&](const char *name,
                              std::vector<std::uint16_t> &output) {
      Ogre::TextureGpu *texture = rendering->getDefinedTexture(name);
      if (texture == nullptr ||
          texture->getPixelFormat() != Ogre::PFG_RGBA16_FLOAT ||
          texture->getWidth() != hdr_width ||
          texture->getHeight() != hdr_height || texture->getDepth() != 1U ||
          texture->getNumMipmaps() != 1U) {
        throw std::runtime_error(
            "HDR split artifact target metadata changed before download");
      }
      ++lighting_audit.test_artifact_content_readbacks;
      Ogre::Image2 image;
      image.convertFromTexture(texture, 0U, 0U);
      const Ogre::TextureBox pixels = image.getData(0U);
      if (pixels.data == nullptr || pixels.width != hdr_width ||
          pixels.height != hdr_height || pixels.depth != 1U ||
          pixels.numSlices != 1U ||
          pixels.bytesPerPixel != 4U * sizeof(std::uint16_t) ||
          pixels.bytesPerRow <
              static_cast<std::size_t>(hdr_width) *
                  4U * sizeof(std::uint16_t)) {
        throw std::runtime_error(
            "HDR split artifact staging layout changed");
      }
      output.resize(static_cast<std::size_t>(hdr_width) * hdr_height * 4U);
      for (std::uint32_t row = 0U; row < hdr_height; ++row) {
        for (std::uint32_t column = 0U; column < hdr_width; ++column) {
          const std::size_t offset =
              (static_cast<std::size_t>(row) * hdr_width + column) * 4U;
          std::memcpy(output.data() + offset,
                      pixels.at(column, row, 0U),
                      4U * sizeof(std::uint16_t));
          for (std::size_t channel = 0U; channel < 4U; ++channel) {
            HdrR16Float canonical;
            const ValidationResult finite = DecodeFiniteHdrR16Float(
                output[offset + channel], canonical);
            if (!finite || canonical.bits != output[offset + channel]) {
              throw std::runtime_error(
                  "HDR split artifact contains a non-finite RGBA16 channel");
            }
          }
        }
      }
    };
    download(kOgreNextHdrBaseTexture, evidence.base_hdr_rgba16);
    download(kOgreNextHdrSunFullTexture, evidence.sun_full_hdr_rgba16);
    download(kOgreNextHdrSunDirectTexture, evidence.sun_direct_hdr_rgba16);
    download(kOgreNextHdrRasterLitTexture, evidence.raster_lit_hdr_rgba16);
    return evidence;
  }

  [[nodiscard]] OgreNextSunVisibilityV2ContentEvidence
  CaptureSunVisibilityV2ContentEvidence() {
    if (!initialized || faulted || !SunVisibilityV2Enabled() ||
        !retain_sun_visibility_v2_content_evidence ||
        sun_visibility_v2_frame_awaiting_continuation ||
        sun_visibility_v2_hdr_commit_pending || hdr_workspace == nullptr ||
        hdr_base_hdr_target == nullptr ||
        hdr_sun_direct_hdr_target == nullptr ||
        hdr_visibility_target == nullptr || hdr_lit_target == nullptr ||
        lighting_audit.completed_frames == 0U ||
        lighting_audit.last_frame_id !=
            hdr_temporal_state.committed_frame_id()) {
      throw std::logic_error(
          "sun-visibility V2 content evidence is unavailable before an opted-in isolated continuation completes");
    }
    OgreNextSunVisibilityV2ContentEvidence evidence;
    evidence.frame_id = lighting_audit.last_frame_id;
    evidence.width = hdr_width;
    evidence.height = hdr_height;

    const auto exact_texture = [&](Ogre::TextureGpu *texture,
                                   Ogre::PixelFormatGpu format) {
      return texture != nullptr && texture->isUav() &&
             texture->getPixelFormat() == format &&
             texture->getWidth() == hdr_width &&
             texture->getHeight() == hdr_height && texture->getDepth() == 1U &&
             texture->getNumMipmaps() == 1U &&
             texture->getNumSlices() == 1U &&
             texture->getSampleDescription().getColourSamples() == 1U;
    };
    if (!exact_texture(hdr_base_hdr_target, Ogre::PFG_RGBA16_FLOAT) ||
        !exact_texture(hdr_sun_direct_hdr_target,
                       Ogre::PFG_RGBA16_FLOAT) ||
        !exact_texture(hdr_visibility_target, Ogre::PFG_R16_FLOAT) ||
        !exact_texture(hdr_lit_target, Ogre::PFG_RGBA16_FLOAT)) {
      throw std::runtime_error(
          "sun-visibility V2 image-set metadata changed before artifact capture");
    }

    const auto download_rgba16 = [&](Ogre::TextureGpu *texture,
                                     float required_alpha,
                                     const char *role,
                                     std::vector<std::uint16_t> &output) {
      ++lighting_audit.test_artifact_content_readbacks;
      Ogre::Image2 image;
      image.convertFromTexture(texture, 0U, 0U);
      const Ogre::TextureBox box = image.getData(0U);
      if (box.data == nullptr || box.width != hdr_width ||
          box.height != hdr_height || box.depth != 1U ||
          box.numSlices != 1U ||
          box.bytesPerPixel != 4U * sizeof(std::uint16_t) ||
          box.bytesPerRow < static_cast<std::size_t>(hdr_width) * 4U *
                                sizeof(std::uint16_t)) {
        throw std::runtime_error(std::string("sun-visibility V2 ") + role +
                                 " staging layout changed");
      }
      output.resize(static_cast<std::size_t>(hdr_width) * hdr_height * 4U);
      for (std::uint32_t row = 0U; row < hdr_height; ++row) {
        for (std::uint32_t column = 0U; column < hdr_width; ++column) {
          const std::size_t offset =
              (static_cast<std::size_t>(row) * hdr_width + column) * 4U;
          std::memcpy(output.data() + offset, box.at(column, row, 0U),
                      4U * sizeof(std::uint16_t));
          for (std::size_t channel = 0U; channel < 4U; ++channel) {
            HdrR16Float canonical;
            const ValidationResult finite =
                DecodeFiniteHdrR16Float(output[offset + channel], canonical);
            if (!finite || canonical.bits != output[offset + channel] ||
                (channel < 3U && canonical.decoded < 0.0F) ||
                (channel == 3U && canonical.decoded != required_alpha)) {
              throw std::runtime_error(std::string("sun-visibility V2 ") +
                                       role +
                                       " contains invalid RGBA16 radiance");
            }
          }
        }
      }
    };
    download_rgba16(hdr_base_hdr_target, 1.0F, "BaseHdr",
                    evidence.base_hdr_rgba16);
    download_rgba16(hdr_sun_direct_hdr_target, 0.0F, "SunDirectHdr",
                    evidence.sun_direct_hdr_rgba16);

    ++lighting_audit.test_artifact_content_readbacks;
    Ogre::Image2 visibility_image;
    visibility_image.convertFromTexture(hdr_visibility_target, 0U, 0U);
    const Ogre::TextureBox visibility = visibility_image.getData(0U);
    if (visibility.data == nullptr || visibility.width != hdr_width ||
        visibility.height != hdr_height || visibility.depth != 1U ||
        visibility.numSlices != 1U ||
        visibility.bytesPerPixel != sizeof(std::uint16_t) ||
        visibility.bytesPerRow <
            static_cast<std::size_t>(hdr_width) * sizeof(std::uint16_t)) {
      throw std::runtime_error(
          "sun-visibility V2 visibility staging layout changed");
    }
    evidence.visibility_r16.resize(
        static_cast<std::size_t>(hdr_width) * hdr_height);
    for (std::uint32_t row = 0U; row < hdr_height; ++row) {
      for (std::uint32_t column = 0U; column < hdr_width; ++column) {
        const std::size_t offset =
            static_cast<std::size_t>(row) * hdr_width + column;
        std::memcpy(evidence.visibility_r16.data() + offset,
                    visibility.at(column, row, 0U), sizeof(std::uint16_t));
        if (!IsCanonicalNativeSunVisibilityV2R16(
                evidence.visibility_r16[offset])) {
          throw std::runtime_error(
              "sun-visibility V2 artifact contains noncanonical visibility");
        }
      }
    }

    download_rgba16(hdr_lit_target, 1.0F, "LitHdr",
                    evidence.lit_hdr_rgba16);
    return evidence;
  }
#endif

  [[nodiscard]] std::uint64_t &LightingContentReadbackCounter() noexcept {
    return ProductionPresentationEnabled()
               ? lighting_audit.production_content_readbacks
               : lighting_audit.test_artifact_content_readbacks;
  }

  [[nodiscard]] const char *HdrWorkspaceName() const noexcept {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    if (hdr_ui_overlay_control) {
      return kOgreNextHdrUiOverlayControlWorkspace;
    }
#endif
    return kOgreNextHdrWorkspace;
  }

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  [[nodiscard]] RenderOperationResult CreateHdrUiOverlayControl() {
    if (!hdr_ui_overlay_control) {
      return RenderOperationResult::Success();
    }
    if (scene_manager == nullptr || unlit == nullptr || hdr_overlay_system ||
        hdr_overlay != nullptr || hdr_overlay_panel != nullptr) {
      return HdrBackendFailure(
          "real UI-overlay control lifecycle is not empty");
    }

    hdr_overlay_system = std::make_unique<Ogre::v1::OverlaySystem>();
    scene_manager->addRenderQueueListener(hdr_overlay_system.get());
    hdr_overlay_listener_registered = true;

    Ogre::HlmsMacroblock macroblock;
    macroblock.mDepthCheck = false;
    macroblock.mDepthWrite = false;
    macroblock.mCullMode = Ogre::CULL_NONE;
    Ogre::HlmsDatablock *base_datablock = unlit->createDatablock(
        Ogre::IdString(kOgreNextHdrUiDatablockName),
        kOgreNextHdrUiDatablockName, macroblock, Ogre::HlmsBlendblock(),
        Ogre::HlmsParamVec());
    auto *datablock = dynamic_cast<Ogre::HlmsUnlitDatablock *>(base_datablock);
    if (datablock == nullptr) {
      return HdrBackendFailure(
          "real UI-overlay control did not create an Unlit datablock");
    }
    datablock->setUseColour(true);
    datablock->setColour(Ogre::ColourValue(1.0F, 0.0F, 1.0F, 1.0F));

    Ogre::v1::OverlayManager &manager =
        Ogre::v1::OverlayManager::getSingleton();
    hdr_overlay = manager.create(kOgreNextHdrUiOverlayName);
    Ogre::v1::OverlayElement *element = manager.createOverlayElement(
        "Panel", kOgreNextHdrUiPanelName);
    hdr_overlay_panel =
        dynamic_cast<Ogre::v1::OverlayContainer *>(element);
    if (hdr_overlay == nullptr || hdr_overlay_panel == nullptr) {
      return HdrBackendFailure(
          "real UI-overlay control did not create an Overlay panel");
    }
    hdr_overlay_panel->setMetricsMode(Ogre::v1::GMM_RELATIVE);
    hdr_overlay_panel->setPosition(0.0F, 0.0F);
    hdr_overlay_panel->setDimensions(1.0F, 1.0F);
    hdr_overlay_panel->setMaterialName(kOgreNextHdrUiDatablockName);
    hdr_overlay->add2D(hdr_overlay_panel);
    hdr_overlay->show();
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool DestroyHdrUiOverlayControl() noexcept {
    bool clean = true;
    if (hdr_overlay != nullptr && hdr_overlay_panel != nullptr) {
      try {
        hdr_overlay->remove2D(hdr_overlay_panel);
      } catch (...) {
        clean = false;
      }
    }
    if (Ogre::v1::OverlayManager::getSingletonPtr() != nullptr) {
      Ogre::v1::OverlayManager &manager =
          Ogre::v1::OverlayManager::getSingleton();
      if (hdr_overlay_panel != nullptr) {
        try {
          manager.destroyOverlayElement(hdr_overlay_panel);
        } catch (...) {
          clean = false;
        }
      }
      if (hdr_overlay != nullptr) {
        try {
          manager.destroy(hdr_overlay);
        } catch (...) {
          clean = false;
        }
      }
    } else if (hdr_overlay_panel != nullptr || hdr_overlay != nullptr) {
      clean = false;
    }
    hdr_overlay_panel = nullptr;
    hdr_overlay = nullptr;
    if (hdr_overlay_listener_registered && scene_manager != nullptr &&
        hdr_overlay_system) {
      try {
        scene_manager->removeRenderQueueListener(hdr_overlay_system.get());
      } catch (...) {
        clean = false;
      }
    } else if (hdr_overlay_listener_registered) {
      clean = false;
    }
    hdr_overlay_listener_registered = false;
    hdr_overlay_system.reset();
    return clean;
  }
#endif

  [[nodiscard]] bool HudOverlayControlSelected() const noexcept {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    return hdr_ui_overlay_control;
#else
    return false;
#endif
  }

  /// The scene manager may hold exactly one v1::OverlaySystem render-queue
  /// listener: a second one would inject every overlay twice into the same
  /// queue. Both the production HUD panel and the GUI-only menu panel need
  /// one, and their lifetimes are independent (the HUD runtime dies with the
  /// HDR compositor, the menu runtime does not), so ownership lives with the
  /// frontend instead of with either runtime.
  [[nodiscard]] RenderOperationResult EnsureOverlaySystem() {
    if (overlay_system) {
      return RenderOperationResult::Success();
    }
    if (scene_manager == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "overlay render-queue listener requires a live scene manager");
    }
    overlay_system = std::make_unique<Ogre::v1::OverlaySystem>();
    scene_manager->addRenderQueueListener(overlay_system.get());
    overlay_listener_registered = true;
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool DestroyOverlaySystem() noexcept {
    bool clean = true;
    if (overlay_listener_registered && scene_manager != nullptr &&
        overlay_system) {
      try {
        scene_manager->removeRenderQueueListener(overlay_system.get());
      } catch (...) {
        clean = false;
      }
    } else if (overlay_listener_registered) {
      clean = false;
    }
    overlay_listener_registered = false;
    overlay_system.reset();
    return clean;
  }

  [[nodiscard]] RenderOperationResult CreateHudOverlayRuntime() {
    if (HudOverlayControlSelected()) {
      // The isolated magenta negative-control owns the HdrRenderUi node in
      // this configuration; the production HUD runtime stays absent.
      return RenderOperationResult::Success();
    }
    if (scene_manager == nullptr || unlit == nullptr ||
        hud_overlay != nullptr || hud_overlay_panel != nullptr ||
        hud_overlay_datablock != nullptr) {
      return HdrBackendFailure("HUD overlay runtime lifecycle is not empty");
    }

    const RenderOperationResult overlays = EnsureOverlaySystem();
    if (!overlays) {
      return overlays;
    }

    Ogre::HlmsMacroblock macroblock;
    macroblock.mDepthCheck = false;
    macroblock.mDepthWrite = false;
    macroblock.mCullMode = Ogre::CULL_NONE;
    // Premultiplied source-over: the HUD texture accumulated its GUI over a
    // zero-cleared target, so RGB already carries coverage.
    Ogre::HlmsBlendblock blendblock;
    blendblock.mSourceBlendFactor = Ogre::SBF_ONE;
    blendblock.mDestBlendFactor = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    blendblock.mSourceBlendFactorAlpha = Ogre::SBF_ONE;
    blendblock.mDestBlendFactorAlpha = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    blendblock.mBlendOperation = Ogre::SBO_ADD;
    blendblock.mBlendOperationAlpha = Ogre::SBO_ADD;
    blendblock.calculateSeparateBlendMode();
    Ogre::HlmsDatablock *base_datablock = unlit->createDatablock(
        Ogre::IdString(kOgreNextHudDatablockName), kOgreNextHudDatablockName,
        macroblock, blendblock, Ogre::HlmsParamVec());
    hud_overlay_datablock =
        dynamic_cast<Ogre::HlmsUnlitDatablock *>(base_datablock);
    if (hud_overlay_datablock == nullptr) {
      return HdrBackendFailure(
          "HUD overlay runtime did not create an Unlit datablock");
    }
    hud_overlay_datablock_created = true;
    hud_overlay_datablock->setUseColour(true);
    hud_overlay_datablock->setColour(Ogre::ColourValue::White);

    Ogre::v1::OverlayManager &manager =
        Ogre::v1::OverlayManager::getSingleton();
    hud_overlay = manager.create(kOgreNextHudOverlayName);
    Ogre::v1::OverlayElement *element = manager.createOverlayElement(
        "Panel", kOgreNextHudPanelName);
    hud_overlay_panel =
        dynamic_cast<Ogre::v1::OverlayContainer *>(element);
    if (hud_overlay == nullptr || hud_overlay_panel == nullptr) {
      return HdrBackendFailure(
          "HUD overlay runtime did not create an Overlay panel");
    }
    hud_overlay_panel->setMetricsMode(Ogre::v1::GMM_RELATIVE);
    hud_overlay_panel->setPosition(0.0F, 0.0F);
    hud_overlay_panel->setDimensions(1.0F, 1.0F);
    hud_overlay_panel->setMaterialName(kOgreNextHudDatablockName);
    hud_overlay->add2D(hud_overlay_panel);
    // Hidden until a validated snapshot enables the transported HUD.
    hud_overlay->hide();
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool DestroyHudOverlayRuntime() noexcept {
    bool clean = true;
    if (hud_overlay != nullptr && hud_overlay_panel != nullptr) {
      try {
        hud_overlay->remove2D(hud_overlay_panel);
      } catch (...) {
        clean = false;
      }
    }
    if (Ogre::v1::OverlayManager::getSingletonPtr() != nullptr) {
      Ogre::v1::OverlayManager &manager =
          Ogre::v1::OverlayManager::getSingleton();
      if (hud_overlay_panel != nullptr) {
        try {
          manager.destroyOverlayElement(hud_overlay_panel);
        } catch (...) {
          clean = false;
        }
      }
      if (hud_overlay != nullptr) {
        try {
          manager.destroy(hud_overlay);
        } catch (...) {
          clean = false;
        }
      }
    } else if (hud_overlay_panel != nullptr || hud_overlay != nullptr) {
      clean = false;
    }
    hud_overlay_panel = nullptr;
    hud_overlay = nullptr;
    if (hud_overlay_datablock != nullptr) {
      try {
        hud_overlay_datablock->setTexture(0U, nullptr);
      } catch (...) {
        clean = false;
      }
    }
    hud_overlay_bound_texture = {};
    if (hud_overlay_datablock_created && unlit != nullptr) {
      try {
        unlit->destroyDatablock(Ogre::IdString(kOgreNextHudDatablockName));
      } catch (...) {
        clean = false;
      }
    } else if (hud_overlay_datablock_created) {
      clean = false;
    }
    hud_overlay_datablock = nullptr;
    hud_overlay_datablock_created = false;
    // The v1::OverlaySystem listener is frontend-owned and outlives this
    // runtime: the GUI-only menu panel keeps using it after an HDR compositor
    // rebuild retires the HUD panel. DestroyOverlaySystem() retires it.
    return clean;
  }

  /// GUI-only presentation panel. Structurally identical to the HUD panel -
  /// same reserved display-domain datablock prefix, so the same exact-sRGB-EOTF
  /// shader piece runs, and the same premultiplied source-over blend for GUI
  /// pixels drawn over a zero-cleared target - but with its own overlay,
  /// element, and datablock identities so neither path can rebind the other's
  /// texture. Unlike the HUD runtime it has no HDR dependency: it composites
  /// straight onto the window through its own overlay-only node.
  [[nodiscard]] RenderOperationResult CreateMenuOverlayRuntime() {
    if (scene_manager == nullptr || unlit == nullptr ||
        menu_overlay != nullptr || menu_overlay_panel != nullptr ||
        menu_overlay_datablock != nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "GUI-only overlay runtime lifecycle is not empty");
    }
    const RenderOperationResult overlays = EnsureOverlaySystem();
    if (!overlays) {
      return overlays;
    }

    Ogre::HlmsMacroblock macroblock;
    macroblock.mDepthCheck = false;
    macroblock.mDepthWrite = false;
    macroblock.mCullMode = Ogre::CULL_NONE;
    Ogre::HlmsBlendblock blendblock;
    blendblock.mSourceBlendFactor = Ogre::SBF_ONE;
    blendblock.mDestBlendFactor = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    blendblock.mSourceBlendFactorAlpha = Ogre::SBF_ONE;
    blendblock.mDestBlendFactorAlpha = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    blendblock.mBlendOperation = Ogre::SBO_ADD;
    blendblock.mBlendOperationAlpha = Ogre::SBO_ADD;
    blendblock.calculateSeparateBlendMode();
    Ogre::HlmsDatablock *base_datablock = unlit->createDatablock(
        Ogre::IdString(kOgreNextMenuDatablockName), kOgreNextMenuDatablockName,
        macroblock, blendblock, Ogre::HlmsParamVec());
    menu_overlay_datablock =
        dynamic_cast<Ogre::HlmsUnlitDatablock *>(base_datablock);
    if (menu_overlay_datablock == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "GUI-only overlay runtime did not create an Unlit datablock");
    }
    menu_overlay_datablock_created = true;
    menu_overlay_datablock->setUseColour(true);
    menu_overlay_datablock->setColour(Ogre::ColourValue::White);

    Ogre::v1::OverlayManager &manager =
        Ogre::v1::OverlayManager::getSingleton();
    menu_overlay = manager.create(kOgreNextMenuOverlayName);
    Ogre::v1::OverlayElement *element =
        manager.createOverlayElement("Panel", kOgreNextMenuPanelName);
    menu_overlay_panel = dynamic_cast<Ogre::v1::OverlayContainer *>(element);
    if (menu_overlay == nullptr || menu_overlay_panel == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "GUI-only overlay runtime did not create an Overlay panel");
    }
    menu_overlay_panel->setMetricsMode(Ogre::v1::GMM_RELATIVE);
    menu_overlay_panel->setPosition(0.0F, 0.0F);
    menu_overlay_panel->setDimensions(1.0F, 1.0F);
    menu_overlay_panel->setMaterialName(kOgreNextMenuDatablockName);
    menu_overlay->add2D(menu_overlay_panel);
    // Shown only for the duration of one GUI-only present, so a scene frame
    // rendered through HdrRenderUi can never pick this panel up.
    menu_overlay->hide();
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool DestroyMenuOverlayRuntime() noexcept {
    bool clean = true;
    if (menu_overlay != nullptr && menu_overlay_panel != nullptr) {
      try {
        menu_overlay->remove2D(menu_overlay_panel);
      } catch (...) {
        clean = false;
      }
    }
    if (Ogre::v1::OverlayManager::getSingletonPtr() != nullptr) {
      Ogre::v1::OverlayManager &manager =
          Ogre::v1::OverlayManager::getSingleton();
      if (menu_overlay_panel != nullptr) {
        try {
          manager.destroyOverlayElement(menu_overlay_panel);
        } catch (...) {
          clean = false;
        }
      }
      if (menu_overlay != nullptr) {
        try {
          manager.destroy(menu_overlay);
        } catch (...) {
          clean = false;
        }
      }
    } else if (menu_overlay_panel != nullptr || menu_overlay != nullptr) {
      clean = false;
    }
    menu_overlay_panel = nullptr;
    menu_overlay = nullptr;
    // The datablock must release the image before the image is destroyed.
    if (menu_overlay_datablock != nullptr) {
      try {
        menu_overlay_datablock->setTexture(0U, nullptr);
      } catch (...) {
        clean = false;
      }
    }
    menu_overlay_texture_bound = false;
    clean = DestroyMenuOverlayImage() && clean;
    if (menu_overlay_datablock_created && unlit != nullptr) {
      try {
        unlit->destroyDatablock(Ogre::IdString(kOgreNextMenuDatablockName));
      } catch (...) {
        clean = false;
      }
    } else if (menu_overlay_datablock_created) {
      clean = false;
    }
    menu_overlay_datablock = nullptr;
    menu_overlay_datablock_created = false;
    return clean;
  }

  [[nodiscard]] bool DestroyMenuOverlayImage() noexcept {
    if (menu_overlay_texture == nullptr) {
      menu_overlay_texture_width = 0U;
      menu_overlay_texture_height = 0U;
      menu_overlay_texture_content_hash = 0U;
      return true;
    }
    bool clean = true;
    if (menu_overlay_datablock != nullptr && menu_overlay_texture_bound) {
      try {
        menu_overlay_datablock->setTexture(0U, nullptr);
      } catch (...) {
        clean = false;
      }
    }
    menu_overlay_texture_bound = false;
    if (renderer != nullptr) {
      try {
        renderer->getTextureGpuManager()->destroyTexture(menu_overlay_texture);
        ++presentation_audit.ui_overlay_image_destroys;
      } catch (...) {
        clean = false;
      }
    } else {
      clean = false;
    }
    menu_overlay_texture = nullptr;
    menu_overlay_texture_width = 0U;
    menu_overlay_texture_height = 0U;
    menu_overlay_texture_content_hash = 0U;
    return clean;
  }

  /// Allocates (or re-allocates on an extent change) the frontend-private
  /// display-domain image and uploads the borrowed GUI rows into it. The
  /// image never enters the portable asset catalog: no RenderAssetId, no
  /// revision, no registry entry, so it cannot perturb scene asset lineage.
  /// An unchanged content hash performs no GPU work at all.
  [[nodiscard]] RenderOperationResult
  EnsureMenuOverlayImage(const UiOverlayFrameRequest &request) {
    if (renderer == nullptr || menu_overlay_datablock == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "GUI-only overlay image requires the menu overlay runtime");
    }
    Ogre::TextureGpuManager *texture_manager =
        renderer->getTextureGpuManager();
    if (texture_manager == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "GUI-only overlay image has no texture manager");
    }
    if (menu_overlay_texture != nullptr &&
        (menu_overlay_texture_width != request.width ||
         menu_overlay_texture_height != request.height)) {
      if (!DestroyMenuOverlayImage()) {
        faulted = true;
        return NativeTeardownFailure("Ogre-Next GUI-only overlay image resize");
      }
    }
    if (menu_overlay_texture == nullptr) {
      if (texture_manager->findTextureNoThrow(
              Ogre::IdString(kOgreNextMenuOverlayTextureName)) != nullptr) {
        return RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "GUI-only overlay image name survived its allocation");
      }
      // PFG_RGBA8_UNORM, exactly what CreateUploadedTexture picks for the
      // DISPLAY_DOMAIN_RGBA channel: no hardware sRGB decode, because the
      // reserved-prefix Unlit shader applies the exact EOTF after filtering.
      menu_overlay_texture = texture_manager->createTexture(
          kOgreNextMenuOverlayTextureName, Ogre::GpuPageOutStrategy::Discard,
          Ogre::TextureFlags::ManualTexture, Ogre::TextureTypes::Type2D);
      if (menu_overlay_texture == nullptr) {
        return RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "GUI-only overlay image allocation returned no texture");
      }
      menu_overlay_texture->setResolution(request.width, request.height);
      menu_overlay_texture->setNumMipmaps(1U);
      menu_overlay_texture->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
      menu_overlay_texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
      menu_overlay_texture_width = request.width;
      menu_overlay_texture_height = request.height;
      menu_overlay_texture_content_hash = 0U;
      menu_overlay_texture_bound = false;
      ++presentation_audit.ui_overlay_image_creates;
    }
    if (menu_overlay_texture_content_hash != request.content_hash) {
      Ogre::StagingTexture *staging = texture_manager->getStagingTexture(
          request.width, request.height, 1U, 1U, Ogre::PFG_RGBA8_UNORM);
      if (staging == nullptr) {
        return RenderOperationResult::Failure(
            RenderOperationCode::OUT_OF_MEMORY,
            "GUI-only overlay image could not stage its upload");
      }
      try {
        staging->startMapRegion();
        Ogre::TextureBox box = staging->mapRegion(
            request.width, request.height, 1U, 1U, Ogre::PFG_RGBA8_UNORM);
        box.copyFrom(request.rgba8_bytes, request.width, request.height,
                     request.width * 4U);
        staging->stopMapRegion();
        staging->upload(box, menu_overlay_texture, 0U, nullptr, nullptr);
      } catch (...) {
        texture_manager->removeStagingTexture(staging);
        throw;
      }
      texture_manager->removeStagingTexture(staging);
      menu_overlay_texture_content_hash = request.content_hash;
      ++presentation_audit.ui_overlay_image_uploads;
    }
    if (!menu_overlay_texture_bound) {
      Ogre::HlmsSamplerblock sampler;
      sampler.mMinFilter = Ogre::FO_LINEAR;
      sampler.mMagFilter = Ogre::FO_LINEAR;
      sampler.mMipFilter = Ogre::FO_NONE;
      sampler.setAddressingMode(Ogre::TAM_CLAMP);
      menu_overlay_datablock->setTexture(0U, menu_overlay_texture, &sampler);
      menu_overlay_datablock->setTextureUvSource(0U, 0U);
      menu_overlay_texture_bound = true;
    }
    return RenderOperationResult::Success();
  }

  /// Detaches the panel's bound native texture before an asset transaction
  /// may retire/replace it. The next validated frame rebinds and shows the
  /// overlay again through CommitHudOverlay().
  [[nodiscard]] bool UnbindHudOverlayTextureBeforeAssetReplacement() noexcept {
    if (hud_overlay_datablock == nullptr ||
        !hud_overlay_bound_texture.valid()) {
      return true;
    }
    try {
      if (hud_overlay != nullptr) {
        hud_overlay->hide();
      }
      hud_overlay_datablock->setTexture(0U, nullptr);
      hud_overlay_bound_texture = {};
      return true;
    } catch (...) {
      return false;
    }
  }

  /// Per-frame HUD commit: binds the validated display-domain HUD texture to
  /// the panel only when its reference (id + revision) changed and toggles
  /// overlay visibility. Fail-closed on absent or mis-shaped native state.
  /// An extent mismatch is deliberately NOT fatal: it is the ordinary
  /// transient of a rate-capped HUD readback against a per-frame camera
  /// extent, and it degrades to a hidden overlay -- see the counted branch.
  [[nodiscard]] RenderOperationResult CommitHudOverlay(
      const SceneSnapshot &snapshot, const CameraViewRequest &view) {
    if (HudOverlayControlSelected()) {
      return RenderOperationResult::Success();
    }
    const HudOverlayDescriptor &hud = snapshot.hud_overlay();
    if (!hdr_enabled) {
      if (hud.enabled) {
        return RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "the transported HUD overlay requires the persistent HDR "
            "compositor's post-tonemap UI node");
      }
      return RenderOperationResult::Success();
    }
    if (hud_overlay == nullptr || hud_overlay_panel == nullptr ||
        hud_overlay_datablock == nullptr) {
      return HdrBackendFailure(
          "HUD overlay runtime is absent for a validated frame");
    }
    if (!hud.enabled) {
      hud_overlay->hide();
      return RenderOperationResult::Success();
    }
    const auto material = materials.find(hud.material.id);
    const MaterialDescriptor *portable_material =
        registry->ResolveMaterial(hud.material);
    if (material == materials.end() ||
        material->second.asset != hud.material ||
        material->second.kind != NativeMaterial::Kind::DISPLAY_DOMAIN_UNLIT ||
        material->second.display_domain_unlit_datablock == nullptr ||
        portable_material == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::RESOURCE_STALE,
          "HUD overlay material has no native display-domain allocation");
    }
    const auto texture =
        textures.find(portable_material->base_color_texture.texture.id);
    if (texture == textures.end() ||
        texture->second.asset !=
            portable_material->base_color_texture.texture ||
        !texture->second.usage.display_domain_rgba ||
        texture->second.sampled == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::RESOURCE_STALE,
          "HUD overlay texture has no native display-domain allocation");
    }
    if (texture->second.sampled->getWidth() != view.width ||
        texture->second.sampled->getHeight() != view.height) {
      // F4. This is not a corrupt frame, it is a stale HUD readback. The HUD
      // capture is rate-capped at 30 Hz while the camera extent re-normalizes
      // every frame, so for ~33 ms after ANY window resize the old-extent HUD
      // arrives with a new-extent camera. Failing here ended the session:
      // resizing the window killed the game.
      //
      // The correct response is the no-op this function already implements
      // for a disabled HUD a few lines above -- hide the overlay and present
      // the frame. Stretching the stale texture over the new extent would be
      // the wrong picture; one HUD-less frame is not. The next capture at the
      // new extent rebinds and shows it again.
      ++degrade_audit.hud_extent_mismatch_frames;
      hud_overlay->hide();
      return RenderOperationResult::Success();
    }
    if (hud_overlay_bound_texture !=
        portable_material->base_color_texture.texture) {
      const SamplerResourceDescriptor *sampler_descriptor =
          registry->ResolveSampler(
              portable_material->base_color_texture.sampler);
      if (sampler_descriptor == nullptr) {
        return RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "HUD overlay sampler disappeared before native binding");
      }
      const Ogre::HlmsSamplerblock sampler =
          ToOgreSampler(*sampler_descriptor);
      hud_overlay_datablock->setTexture(0U, texture->second.sampled,
                                        &sampler);
      hud_overlay_datablock->setTextureUvSource(0U, 0U);
      hud_overlay_bound_texture =
          portable_material->base_color_texture.texture;
    }
    hud_overlay->show();
    return RenderOperationResult::Success();
  }

  [[nodiscard]] RenderOperationResult MaybeInjectHdrFailure(
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      OgreNextN1HdrFailureStage stage
#else
      std::uint8_t /*stage*/
#endif
  ) {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    if (hdr_failure_pending && hdr_failure_stage == stage) {
      hdr_failure_pending = false;
      return HdrBackendFailure(
          stage == OgreNextN1HdrFailureStage::AFTER_FRAME_COMMIT_PREPARE
              ? "injected frame commit-prepare failure"
              : "injected staged initialization failure");
    }
#endif
    return RenderOperationResult::Success();
  }

  [[nodiscard]] RenderOperationResult CreateHdrWorkspaceDefinition() {
    Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
    const char *workspace_name = HdrWorkspaceName();
    if (compositors == nullptr ||
        compositors->hasWorkspaceDefinition(Ogre::IdString(workspace_name)) ||
        (SunVisibilityV2Enabled() &&
         compositors->hasWorkspaceDefinition(Ogre::IdString(
             kOgreNextHdrSunVisibilityV2ContinuationWorkspace)))) {
      return HdrBackendFailure(
          "UI-free workspace definition lifecycle is not empty");
    }

    Ogre::CompositorWorkspaceDef *definition =
        compositors->addWorkspaceDefinition(workspace_name);
    hdr_workspace_definition_created = true;
    // Single-scene production inserts the aerial-haze quad between the scene
    // node and the stock HDR post node: scene radiance on haze channel 0, the
    // scene's exported D32 depth on haze channel 1, hazed radiance into post
    // channel 0. Everything downstream of post - including the required
    // HdrRenderUi edge below - is untouched, so the HUD stays post-tonemap
    // and unhazed.
    if (SingleSceneHdrPssmEnabled()) {
      definition->connect(Ogre::IdString(kOgreNextHdrRenderingNode), 0U,
                          Ogre::IdString(kOgreNextAerialHazeNode), 0U);
      definition->connect(Ogre::IdString(kOgreNextHdrRenderingNode), 2U,
                          Ogre::IdString(kOgreNextAerialHazeNode), 1U);
      definition->connect(Ogre::IdString(kOgreNextAerialHazeNode), 0U,
                          Ogre::IdString(kOgreNextHdrPostprocessingNode), 0U);
    } else {
      definition->connect(Ogre::IdString(kOgreNextHdrRenderingNode), 0U,
                          SunVisibilityV2Enabled()
                              ? Ogre::IdString(kOgreNextThinSlabNode)
                              : Ogre::IdString(
                                    kOgreNextHdrPostprocessingNode),
                          0U);
    }
    definition->connect(Ogre::IdString(kOgreNextHdrRenderingNode), 1U,
                        Ogre::IdString(kOgreNextHdrPostprocessingNode), 1U);
    if (SunVisibilityV2Enabled()) {
      definition->connect(Ogre::IdString(kOgreNextHdrRenderingNode), 6U,
                          Ogre::IdString(kOgreNextThinSlabNode), 1U);
      definition->connect(Ogre::IdString(kOgreNextThinSlabNode), 0U,
                          Ogre::IdString(kOgreNextHdrPostprocessingNode), 0U);
    }
    definition->connectExternal(
        0U, Ogre::IdString(kOgreNextHdrPostprocessingNode), 2U);
    // Production and the isolated UI-overlay control both terminate on the
    // stock HdrRenderUi node so the transported menu/HUD composites after
    // tone mapping into the sRGB output.
    definition->connect(Ogre::IdString(kOgreNextHdrPostprocessingNode), 0U,
                        Ogre::IdString(kOgreNextHdrUiNode), 0U);

    const auto &aliases = definition->getNodeAliasMap();
    const bool has_rendering =
        aliases.find(Ogre::IdString(kOgreNextHdrRenderingNode)) != aliases.end();
    const bool has_postprocessing =
        aliases.find(Ogre::IdString(kOgreNextHdrPostprocessingNode)) != aliases.end();
    const bool has_refraction =
        aliases.find(Ogre::IdString(kOgreNextThinSlabNode)) != aliases.end();
    const bool has_haze =
        aliases.find(Ogre::IdString(kOgreNextAerialHazeNode)) != aliases.end();
    const bool has_upstream_ui =
        aliases.find(Ogre::IdString(kOgreNextHdrUiNode)) != aliases.end();
    // The HDR UI node is now a required production member of the closure.
    constexpr bool expected_ui = true;
    // Haze is single-scene only; DIRECTIONAL_SPLIT_V2 keeps rendering -> post
    // (or -> thin slab -> post) exactly as before.
    if (!has_rendering || !has_postprocessing ||
        has_refraction != SunVisibilityV2Enabled() ||
        has_haze != SingleSceneHdrPssmEnabled() ||
        has_upstream_ui != expected_ui) {
      return HdrBackendFailure(
          "programmatic HDR workspace node closure is not exact");
    }
    if (SunVisibilityV2Enabled()) {
      Ogre::CompositorWorkspaceDef *continuation =
          compositors->addWorkspaceDefinition(
              kOgreNextHdrSunVisibilityV2ContinuationWorkspace);
      hdr_v2_continuation_workspace_definition_created = true;
      continuation->connectExternal(
          0U, Ogre::IdString(kOgreNextThinSlabNode), 0U);
      continuation->connectExternal(
          3U, Ogre::IdString(kOgreNextThinSlabNode), 1U);
      continuation->connect(Ogre::IdString(kOgreNextThinSlabNode), 0U,
                            Ogre::IdString(kOgreNextHdrPostprocessingNode),
                            0U);
      continuation->connectExternal(
          1U, Ogre::IdString(kOgreNextHdrPostprocessingNode), 1U);
      continuation->connectExternal(
          2U, Ogre::IdString(kOgreNextHdrPostprocessingNode), 2U);
      const auto &continuation_aliases = continuation->getNodeAliasMap();
      if (continuation_aliases.size() != 2U ||
          continuation_aliases.find(Ogre::IdString(kOgreNextThinSlabNode)) ==
              continuation_aliases.end() ||
          continuation_aliases.find(
              Ogre::IdString(kOgreNextHdrPostprocessingNode)) ==
              continuation_aliases.end()) {
        return HdrBackendFailure(
            "sun-visibility V2 continuation node closure is not exact");
      }
    }
    hdr_hud_workspace_verified = has_upstream_ui;
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool
  DestroyHdrCompositor(bool destroy_definitions_and_resources = true) noexcept {
    bool clean = true;
    if (production_source_target == hdr_output_target &&
        production_source_target != nullptr) {
      // The HDR output is borrowed by the copy-only presentation workspace.
      // Its graph must be retired before the owning HDR texture.
      clean = DestroyProductionPresentationGraph() && clean;
    }
    if (root && hdr_v2_continuation_workspace != nullptr) {
      try {
        root->getCompositorManager2()->removeWorkspace(
            hdr_v2_continuation_workspace);
      } catch (...) {
        clean = false;
      }
    } else if (hdr_v2_continuation_workspace != nullptr) {
      clean = false;
    }
    hdr_v2_continuation_workspace = nullptr;
    if (root && hdr_workspace != nullptr) {
      try {
        if (!SingleSceneHdrPssmEnabled()) {
          clean = hdr_directional_split_listener.AbortFrame() && clean;
          hdr_workspace->removeListener(&hdr_directional_split_listener);
        }
        root->getCompositorManager2()->removeWorkspace(hdr_workspace);
      } catch (...) {
        clean = false;
      }
    }
    hdr_workspace = nullptr;
    hdr_base_hdr_target = nullptr;
    hdr_sun_direct_hdr_target = nullptr;
    hdr_visibility_target = nullptr;
    hdr_lit_target = nullptr;
    hdr_opaque_depth_target = nullptr;
    hdr_history_target = nullptr;
    if (!destroy_definitions_and_resources && SingleSceneHdrPssmEnabled() &&
        root != nullptr) {
      try {
        Ogre::CompositorManager2 *compositors =
            root->getCompositorManager2();
        UnbindAndVerifyPssmWorkspace(
            *compositors, kOgreNextHdrRenderingNode,
            kOgreNextHdrSingleScenePassIdentifier);
        const Ogre::IdString shadow_name(kOgreNextHdrShadowNode);
        if (compositors->hasShadowNodeDefinition(shadow_name)) {
          if (!hdr_shadow_node_definition_created) {
            hdr_shadow_node_definition_created = true;
            ++shadow_audit.shadow_node_creates;
          }
          compositors->removeShadowNodeDefinition(shadow_name);
          ++shadow_audit.shadow_node_destroys;
        }
        clean = !compositors->hasShadowNodeDefinition(shadow_name) && clean;
      } catch (...) {
        clean = false;
      }
      hdr_shadow_node_definition_created = false;
      hdr_pssm_finalized_with_populated_scene = false;
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    if (destroy_definitions_and_resources) {
      clean = DestroyHdrUiOverlayControl() && clean;
    }
#endif
    if (destroy_definitions_and_resources) {
      // The HUD overlay's v1::OverlaySystem lifetime precedes workspace
      // definition removal, mirroring the isolated UI-control ordering.
      clean = DestroyHudOverlayRuntime() && clean;
    }
    if (renderer != nullptr && hdr_output_target != nullptr) {
      try {
        renderer->getTextureGpuManager()->destroyTexture(hdr_output_target);
      } catch (...) {
        clean = false;
      }
    } else if (hdr_output_target != nullptr) {
      clean = false;
    }
    hdr_output_target = nullptr;
    if (destroy_definitions_and_resources && root &&
        hdr_v2_continuation_workspace_definition_created) {
      try {
        root->getCompositorManager2()->removeWorkspaceDefinition(
            Ogre::IdString(
                kOgreNextHdrSunVisibilityV2ContinuationWorkspace));
      } catch (...) {
        clean = false;
      }
    } else if (destroy_definitions_and_resources &&
               hdr_v2_continuation_workspace_definition_created) {
      clean = false;
    }
    if (destroy_definitions_and_resources) {
      hdr_v2_continuation_workspace_definition_created = false;
    }
    if (destroy_definitions_and_resources && root &&
        hdr_workspace_definition_created) {
      try {
        root->getCompositorManager2()->removeWorkspaceDefinition(
            Ogre::IdString(HdrWorkspaceName()));
      } catch (...) {
        clean = false;
      }
    } else if (destroy_definitions_and_resources &&
               hdr_workspace_definition_created) {
      clean = false;
    }
    if (destroy_definitions_and_resources) {
      hdr_workspace_definition_created = false;
    }
    if (destroy_definitions_and_resources && root &&
        hdr_refraction_node_definition_created) {
      try {
        Ogre::CompositorManager2 *compositors =
            root->getCompositorManager2();
        const Ogre::IdString refraction_name(kOgreNextThinSlabNode);
        if (!compositors->hasNodeDefinition(refraction_name)) {
          clean = false;
        } else {
          compositors->removeNodeDefinition(refraction_name);
        }
        clean = !compositors->hasNodeDefinition(refraction_name) && clean;
      } catch (...) {
        clean = false;
      }
    } else if (destroy_definitions_and_resources &&
               hdr_refraction_node_definition_created) {
      clean = false;
    }
    if (destroy_definitions_and_resources) {
      hdr_refraction_node_definition_created = false;
    }
    if (destroy_definitions_and_resources && root &&
        hdr_haze_node_definition_created) {
      try {
        Ogre::CompositorManager2 *compositors =
            root->getCompositorManager2();
        const Ogre::IdString haze_name(kOgreNextAerialHazeNode);
        if (!compositors->hasNodeDefinition(haze_name)) {
          clean = false;
        } else {
          compositors->removeNodeDefinition(haze_name);
        }
        clean = !compositors->hasNodeDefinition(haze_name) && clean;
      } catch (...) {
        clean = false;
      }
    } else if (destroy_definitions_and_resources &&
               hdr_haze_node_definition_created) {
      clean = false;
    }
    if (destroy_definitions_and_resources) {
      hdr_haze_node_definition_created = false;
    }
    if (destroy_definitions_and_resources && root &&
        hdr_split_node_definition_created) {
      try {
        Ogre::CompositorManager2 *compositors =
            root->getCompositorManager2();
        const Ogre::IdString split_name(kOgreNextHdrRenderingNode);
        if (!compositors->hasNodeDefinition(split_name)) {
          clean = false;
        } else {
          compositors->removeNodeDefinition(split_name);
        }
        if (compositors->hasNodeDefinition(split_name)) {
          clean = false;
        }
      } catch (...) {
        clean = false;
      }
    } else if (destroy_definitions_and_resources &&
               hdr_split_node_definition_created) {
      clean = false;
    }
    if (destroy_definitions_and_resources) {
      hdr_split_node_definition_created = false;
    }
    Ogre::CompositorManager2 *const compositor_manager =
        root ? root->getCompositorManager2() : nullptr;
    if (destroy_definitions_and_resources && compositor_manager != nullptr) {
      try {
        const Ogre::IdString shadow_name(kOgreNextHdrShadowNode);
        const bool shadow_exists =
            compositor_manager->hasShadowNodeDefinition(shadow_name);
        if (hdr_shadow_node_definition_created && !shadow_exists) {
          clean = false;
        }
        if (shadow_exists) {
          if (!hdr_shadow_node_definition_created) {
            hdr_shadow_node_definition_created = true;
            ++shadow_audit.shadow_node_creates;
          }
          compositor_manager->removeShadowNodeDefinition(shadow_name);
          ++shadow_audit.shadow_node_destroys;
        }
        if (compositor_manager->hasShadowNodeDefinition(shadow_name)) {
          clean = false;
        }
      } catch (...) {
        clean = false;
      }
    } else if (destroy_definitions_and_resources &&
               hdr_shadow_node_definition_created) {
      clean = false;
    }
    if (destroy_definitions_and_resources) {
      hdr_shadow_node_definition_created = false;
    }
    if (destroy_definitions_and_resources && hdr_resource_group_created) {
      try {
        Ogre::ResourceGroupManager::getSingleton().destroyResourceGroup(
            kOgreNextHdrResourceGroup);
      } catch (...) {
        clean = false;
      }
    }
    if (destroy_definitions_and_resources) {
      hdr_resource_group_created = false;
      hdr_resources_initialized = false;
    }
    if (destroy_definitions_and_resources) {
      hdr_temporal_state.Reset();
      hdr_pssm_finalization_prepared = false;
      hdr_pssm_finalized_with_populated_scene = false;
      hdr_pssm_finalization_deferred = false;
      sun_visibility_v2_frame_awaiting_continuation = false;
      sun_visibility_v2_hdr_commit_pending = false;
      sun_visibility_v2_pending_frame_id = 0U;
      sun_visibility_v2_pending_snapshot_id = 0U;
      sun_visibility_v2_pending_view_id = 0U;
      sun_visibility_v2_pending_surface_revision = 0U;
      sun_visibility_v2_pending_width = 0U;
      sun_visibility_v2_pending_height = 0U;
      sun_visibility_v2_pending_lighting = {};
      sun_visibility_v2_pending_hdr_plan = {};
    }
    hdr_width = 0U;
    hdr_height = 0U;
    hdr_warmup_frames = 0U;
    hdr_pssm_finalization_prepared = false;
    hdr_pssm_warmup_native_absence_checks = 0U;
    hdr_pssm_deferred_until_scene_population_verified = false;
    hdr_zero_light_pssm_warmup_avoided_verified = false;
    hdr_manual_delta_bound = false;
    hdr_native_history_validated = false;
    hdr_exact_current_to_old_copy_verified = false;
    hdr_hud_workspace_verified = false;
    hdr_linear_scene_target_verified = false;
    hdr_base_hdr_target_verified = false;
    hdr_sun_full_hdr_target_verified = false;
    hdr_sun_direct_hdr_target_verified = false;
    hdr_visibility_target_verified = false;
    hdr_lit_target_verified = false;
    hdr_gpu_sun_direct_split_verified = false;
    hdr_opaque_depth_export_verified = false;
    hdr_aerial_haze_workspace_verified = false;
    hdr_aerial_haze_constants_bound = false;
    hdr_aerial_haze_applied = false;
    hdr_auto_exposure_graph_verified = false;
    hdr_bloom_graph_verified = false;
    hdr_tone_map_graph_verified = false;
    hdr_srgb_output_verified = false;
    if (destroy_definitions_and_resources) {
      hdr_history_comparison = OgreNextHdrHistoryComparison{};
    }
    return clean;
  }

  [[nodiscard]] RenderOperationResult ConfigureHdrParameters(
      float ogre_exposure, float minimum_auto_exposure,
      float maximum_auto_exposure, float bloom_minimum_threshold,
      float bloom_inverse_transition_width, float delta_seconds) {
    Ogre::MaterialPtr luminance_material =
        std::static_pointer_cast<Ogre::Material>(
            Ogre::MaterialManager::getSingleton().load(
                "HDR/DownScale03_SumLumEnd",
                Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME));
    Ogre::Pass *luminance_pass =
        luminance_material->getTechnique(0U)->getPass(0U);
    Ogre::GpuProgramParametersSharedPtr luminance_parameters =
        luminance_pass->getFragmentProgramParameters();
    luminance_parameters->clearNamedAutoConstant("timeSinceLast");
    if (luminance_parameters->findAutoConstantEntry("timeSinceLast") !=
        nullptr) {
      return HdrBackendFailure(
          "wall-clock frame_time remained bound after deterministic override");
    }
    const float exposure_exponent = ogre_exposure - 2.0F;
    const float exposure_numerator =
        1024.0F * std::exp(exposure_exponent);
    const Ogre::Vector3 exposure(
        exposure_numerator, 7.5F - maximum_auto_exposure,
        7.5F - minimum_auto_exposure);
    luminance_parameters->setNamedConstant("exposure", exposure);
    luminance_parameters->setNamedConstant("timeSinceLast", delta_seconds);

    float observed_exposure[3U]{};
    float observed_delta = -1.0F;
    const Ogre::GpuConstantDefinition &exposure_definition =
        luminance_parameters->getConstantDefinition("exposure");
    const Ogre::GpuConstantDefinition &delta_definition =
        luminance_parameters->getConstantDefinition("timeSinceLast");
    luminance_parameters->_readRawConstants(
        exposure_definition.physicalIndex, 3U, observed_exposure);
    luminance_parameters->_readRawConstants(
        delta_definition.physicalIndex, 1U, &observed_delta);
    if (observed_exposure[0U] != exposure.x ||
        observed_exposure[1U] != exposure.y ||
        observed_exposure[2U] != exposure.z ||
        observed_delta != delta_seconds) {
      return HdrBackendFailure(
          "native exposure or simulation-delta constant changed after binding");
    }

    Ogre::MaterialPtr bloom_material =
        std::static_pointer_cast<Ogre::Material>(
            Ogre::MaterialManager::getSingleton().load(
                "HDR/BrightPass_Start",
                Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME));
    Ogre::Pass *bloom_pass = bloom_material->getTechnique(0U)->getPass(0U);
    Ogre::GpuProgramParametersSharedPtr bloom_parameters =
        bloom_pass->getFragmentProgramParameters();
    const Ogre::Vector2 threshold(bloom_minimum_threshold,
                                  bloom_inverse_transition_width);
    bloom_parameters->setNamedConstant("brightThreshold", threshold);
    float observed_threshold[2U]{};
    const Ogre::GpuConstantDefinition &threshold_definition =
        bloom_parameters->getConstantDefinition("brightThreshold");
    bloom_parameters->_readRawConstants(
        threshold_definition.physicalIndex, 2U, observed_threshold);
    if (observed_threshold[0U] != threshold.x ||
        observed_threshold[1U] != threshold.y) {
      return HdrBackendFailure(
          "native bloom threshold changed after deterministic binding");
    }
    hdr_manual_delta_bound = true;
    return RenderOperationResult::Success();
  }

  /// Per-frame aerial-haze constants, bound exactly like the HDR exposure and
  /// bloom constants: setNamedConstant followed by a _readRawConstants
  /// readback, HdrBackendFailure on any mismatch. The caller latches faulted.
  ///
  /// Every value is derived here, never in the shader: the presenter consumes
  /// transported policy, it does not re-derive it. `identity` binds the exact
  /// canonical no-haze state (extinction zero), which the shader answers with
  /// a bit-exact RGBA16F point copy - the same payload a disabled sky produces
  /// on the wire.
  [[nodiscard]] RenderOperationResult ConfigureAerialHazeParameters(
      const Ogre::Vector4 &coefficients, const Ogre::Vector4 &inscatter,
      const Ogre::Vector4 &projection, const Ogre::Vector4 &ray_forward,
      const Ogre::Vector4 &ray_right_scaled,
      const Ogre::Vector4 &ray_up_scaled) {
    const std::array<std::pair<const char *, const Ogre::Vector4 *>, 6U>
        bindings{{{"hazeCoefficients", &coefficients},
                  {"hazeInscatter", &inscatter},
                  {"hazeProjection", &projection},
                  {"hazeRayForward", &ray_forward},
                  {"hazeRayRightScaled", &ray_right_scaled},
                  {"hazeRayUpScaled", &ray_up_scaled}}};
    for (const auto &binding : bindings) {
      const Ogre::Vector4 &value = *binding.second;
      if (!IsFinite(static_cast<float>(value.x)) ||
          !IsFinite(static_cast<float>(value.y)) ||
          !IsFinite(static_cast<float>(value.z)) ||
          !IsFinite(static_cast<float>(value.w))) {
        return HdrBackendFailure(
            "aerial haze constant is not representable as finite binary32");
      }
    }
    Ogre::MaterialPtr haze_material =
        std::static_pointer_cast<Ogre::Material>(
            Ogre::MaterialManager::getSingleton().load(
                kOgreNextAerialHazeMaterial, kOgreNextHdrResourceGroup));
    if (!haze_material || haze_material->getNumTechniques() != 1U ||
        haze_material->getTechnique(0U) == nullptr ||
        haze_material->getTechnique(0U)->getNumPasses() != 1U) {
      return HdrBackendFailure(
          "aerial haze material lost its exact single-technique quad pass");
    }
    Ogre::Pass *haze_pass = haze_material->getTechnique(0U)->getPass(0U);
    Ogre::GpuProgramParametersSharedPtr haze_parameters =
        haze_pass->getFragmentProgramParameters();
    if (!haze_parameters) {
      return HdrBackendFailure(
          "aerial haze material exposes no fragment parameters");
    }
    for (const auto &binding : bindings) {
      const Ogre::Vector4 &value = *binding.second;
      haze_parameters->setNamedConstant(binding.first, value);
      float observed[4U]{};
      const Ogre::GpuConstantDefinition &definition =
          haze_parameters->getConstantDefinition(binding.first);
      haze_parameters->_readRawConstants(definition.physicalIndex, 4U,
                                         observed);
      if (observed[0U] != static_cast<float>(value.x) ||
          observed[1U] != static_cast<float>(value.y) ||
          observed[2U] != static_cast<float>(value.z) ||
          observed[3U] != static_cast<float>(value.w)) {
        return HdrBackendFailure(
            "aerial haze constant changed after deterministic binding");
      }
    }
    hdr_aerial_haze_constants_bound = true;
    return RenderOperationResult::Success();
  }

  /// The canonical no-haze binding. Used for the warmup frames before any
  /// snapshot exists and whenever the committed sky is disabled, so the pass
  /// is an exact pass-through rather than an approximation.
  [[nodiscard]] RenderOperationResult BindIdentityAerialHazeParameters() {
    const Ogre::Vector4 zero(0.0F, 0.0F, 0.0F, 0.0F);
    // A unit forward with zero right/up keeps the ray finite and normalized;
    // extinction zero makes every other term unreachable in the shader.
    const RenderOperationResult bound = ConfigureAerialHazeParameters(
        zero, zero, zero, Ogre::Vector4(0.0F, 0.0F, 1.0F, 0.0F), zero, zero);
    if (bound) {
      hdr_aerial_haze_applied = false;
      hdr_aerial_haze_extinction_per_meter = 0.0F;
      hdr_aerial_haze_inscatter = Float3{};
    }
    return bound;
  }

  /// Derives this frame's haze constants from the committed environment and
  /// the validated camera, then binds them with readback verification.
  ///
  /// The inscatter color is deliberately NOT a wire field: it is
  /// horizon_radiance * environment_intensity, the exact product the analytic
  /// sky dome uses for its horizon ring (OgreNextN1Policy's scaled_radiance),
  /// and the cloud layer already fades to zero at that ring. A fully
  /// extinguished pixel therefore equals the dome by construction and no seam
  /// can open at the horizon.
  [[nodiscard]] RenderOperationResult ConfigureAerialHazeForFrame(
      const SceneEnvironmentDescriptor &environment,
      const CameraViewRequest &view) {
    const AnalyticSkyDescriptor &sky = environment.analytic_sky;
    if (!sky.enabled || !(sky.haze_extinction_per_meter > 0.0F)) {
      // Disabled sky, or the validated canonical-zero / zero-extinction
      // payload: exactly no haze, not approximately none.
      return BindIdentityAerialHazeParameters();
    }
    // The snapshot passed ValidateSceneSnapshot and the per-frame analytic-sky
    // admission gate before reaching here; re-check only the derived products,
    // which are what the binding readback below can actually observe.
    const Ogre::Matrix4 native_view = ToOgreMatrix(view.view_from_render);
    const Ogre::Matrix4 render_from_view = native_view.inverseAffine();
    const Ogre::Vector3 camera_position = render_from_view.getTrans();
    // Ogre's view basis is right/up/backward, so forward is -Z.
    const Ogre::Vector3 camera_right(render_from_view[0U][0U],
                                     render_from_view[1U][0U],
                                     render_from_view[2U][0U]);
    const Ogre::Vector3 camera_up(render_from_view[0U][1U],
                                  render_from_view[1U][1U],
                                  render_from_view[2U][1U]);
    const Ogre::Vector3 camera_forward(-render_from_view[0U][2U],
                                       -render_from_view[1U][2U],
                                       -render_from_view[2U][2U]);
    if (!IsRigidOrthonormalCameraBasis(camera_right, camera_up,
                                       camera_forward)) {
      // Haze needs a rigid basis to build its view ray, but a basis this
      // frame cannot justify tearing the renderer down: identity is an exact,
      // fully defined state (bit-identical to a haze-free frame), so the
      // frame presents unhazed and the next frame re-derives normally. Only
      // the derived haze is skipped; nothing else in the scene is affected.
      ++hdr_aerial_haze_basis_rejections;
      return BindIdentityAerialHazeParameters();
    }
    const float projection_x = view.clip_from_view.elements[0U];
    const float projection_y = view.clip_from_view.elements[5U];
    if (!IsFinite(projection_x) || !IsFinite(projection_y) ||
        projection_x == 0.0F || projection_y == 0.0F) {
      return HdrBackendFailure(
          "aerial haze cannot invert a degenerate projection scale");
    }
    // Frustum half-extents at unit forward distance, so
    // fwd + ndc.x*right + ndc.y*up has an exact unit forward component and
    // view_z * length(ray) is the true slant distance to the sampled pixel.
    const float tan_half_fov_x = 1.0F / std::fabs(projection_x);
    const float tan_half_fov_y = 1.0F / std::fabs(projection_y);
    const float near_plane = view.near_plane;
    const float far_plane = view.far_plane;
    const float plane_span = far_plane - near_plane;
    if (!IsFinite(near_plane) || !IsFinite(far_plane) || near_plane <= 0.0F ||
        !(plane_span > 0.0F)) {
      return HdrBackendFailure(
          "aerial haze cannot linearize a degenerate depth range");
    }
    // Non-reversed [0, 1] depth: d = A + B / view_z, so view_z = B / (d - A).
    const float projection_a = far_plane / plane_span;
    const float projection_b = -(far_plane * near_plane) / plane_span;
    const Ogre::Vector4 coefficients(
        sky.haze_extinction_per_meter,
        sky.haze_inverse_scale_height_per_meter,
        static_cast<float>(camera_position.y) - sky.haze_base_height_meters,
        0.0F);
    const Ogre::Vector4 inscatter(
        sky.horizon_radiance.x * environment.environment_intensity,
        sky.horizon_radiance.y * environment.environment_intensity,
        sky.horizon_radiance.z * environment.environment_intensity, 0.0F);
    const RenderOperationResult bound = ConfigureAerialHazeParameters(
        coefficients, inscatter,
        Ogre::Vector4(projection_a, projection_b, 0.0F, 0.0F),
        Ogre::Vector4(camera_forward.x, camera_forward.y, camera_forward.z,
                      0.0F),
        Ogre::Vector4(camera_right.x * tan_half_fov_x,
                      camera_right.y * tan_half_fov_x,
                      camera_right.z * tan_half_fov_x, 0.0F),
        Ogre::Vector4(camera_up.x * tan_half_fov_y,
                      camera_up.y * tan_half_fov_y,
                      camera_up.z * tan_half_fov_y, 0.0F));
    if (bound) {
      hdr_aerial_haze_applied = true;
      hdr_aerial_haze_extinction_per_meter = sky.haze_extinction_per_meter;
      hdr_aerial_haze_inscatter = Float3{
          static_cast<float>(inscatter.x), static_cast<float>(inscatter.y),
          static_cast<float>(inscatter.z)};
    }
    return bound;
  }

  [[nodiscard]] bool ReadHdrHistory(HdrR16Float &history) {
    if (hdr_workspace == nullptr) {
      return false;
    }
    Ogre::CompositorNode *rendering =
        hdr_workspace->findNode(kOgreNextHdrRenderingNode);
    if (rendering == nullptr) {
      return false;
    }
    std::vector<HdrR16Float> values;
    if (!TryReadPositiveR16Texture(rendering->getDefinedTexture("oldLumRt"),
                                   1U, 1U, values,
                                   LightingContentReadbackCounter()) ||
        values.size() != 1U) {
      return false;
    }
    history = values.front();
    return true;
  }

  [[nodiscard]] RenderOperationResult InitializeExactHdrHistory(
      const HdrR16Float &initial_history, HdrR16Float &observed_history) {
    if (hdr_workspace == nullptr) {
      return HdrBackendFailure(
          "persistent workspace is unavailable for exact history initialization");
    }
    Ogre::CompositorNode *rendering =
        hdr_workspace->findNode(kOgreNextHdrRenderingNode);
    Ogre::TextureGpu *history_texture =
        rendering != nullptr ? rendering->getDefinedTexture("oldLumRt")
                             : nullptr;
    if (history_texture == nullptr ||
        history_texture->getPixelFormat() != Ogre::PFG_R16_FLOAT ||
        history_texture->getWidth() != 1U ||
        history_texture->getHeight() != 1U ||
        history_texture->getDepth() != 1U ||
        history_texture->getNumMipmaps() != 1U) {
      return HdrBackendFailure(
          "persistent history texture changed before exact initialization");
    }

    // Shader expressions such as mix(newLum, oldLum, pow(0.25, 0.0)) are
    // mathematically identity operations, but backend compilers are not
    // required to preserve the source R16 payload exactly. Seed the temporal
    // boundary from the validated CPU binary16 bits after shader warmup, then
    // prove those same bits survived a real staging upload and GPU readback.
    Ogre::Image2 initial_image;
    initial_image.createEmptyImage(1U, 1U, 1U,
                                   Ogre::TextureTypes::Type2D,
                                   Ogre::PFG_R16_FLOAT, 1U);
    const Ogre::TextureBox pixels = initial_image.getData(0U);
    if (pixels.data == nullptr || pixels.width != 1U || pixels.height != 1U ||
        pixels.depth != 1U || pixels.numSlices != 1U ||
        pixels.bytesPerPixel != sizeof(initial_history.bits) ||
        pixels.bytesPerRow < sizeof(initial_history.bits)) {
      return HdrBackendFailure(
          "exact initial history staging layout changed");
    }
    std::memcpy(pixels.at(0U, 0U, 0U), &initial_history.bits,
                sizeof(initial_history.bits));
    std::uint16_t staged_bits = 0U;
    std::memcpy(&staged_bits, pixels.at(0U, 0U, 0U), sizeof(staged_bits));
    if (staged_bits != initial_history.bits) {
      return HdrBackendFailure(
          "exact initial history staging changed the R16 payload");
    }
    initial_image.uploadTo(history_texture, 0U, 0U);

    if (retain_native_lighting_content_evidence) {
      HdrR16Float candidate;
      if (!ReadHdrHistory(candidate) ||
          candidate.bits != initial_history.bits ||
          candidate.decoded != initial_history.decoded) {
        return HdrBackendFailure(
            "exact initial R16 history upload did not round-trip");
      }
      observed_history = candidate;
    } else {
      observed_history = initial_history;
    }
    return RenderOperationResult::Success();
  }

  [[nodiscard]] RenderOperationResult RefreshSingleSceneHdrRuntimeTargets(
      bool require_pssm_runtime) {
    if (!SingleSceneHdrPssmEnabled() || hdr_workspace == nullptr ||
        hdr_output_target == nullptr || hdr_width == 0U || hdr_height == 0U) {
      return HdrBackendFailure(
          "single-evaluation HDR runtime refresh has an invalid lifecycle");
    }
    Ogre::CompositorNode *rendering =
        hdr_workspace->findNode(kOgreNextHdrRenderingNode);
    Ogre::CompositorNode *postprocessing =
        hdr_workspace->findNode(kOgreNextHdrPostprocessingNode);
    Ogre::TextureGpu *linear_scene =
        rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrRasterLitTexture)
            : nullptr;
    Ogre::TextureGpu *old_luminance =
        rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrHistoryTexture)
            : nullptr;
    Ogre::TextureGpu *opaque_depth =
        rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrOpaqueDepthTexture)
            : nullptr;
    Ogre::TextureGpu *iterative_luminance =
        postprocessing != nullptr
            ? postprocessing->getDefinedTexture("rtIter2")
            : nullptr;
    Ogre::TextureGpu *current_luminance =
        postprocessing != nullptr
            ? postprocessing->getDefinedTexture("lumRt0")
            : nullptr;
    Ogre::TextureGpu *bloom_horizontal =
        postprocessing != nullptr
            ? postprocessing->getDefinedTexture("rtBlur0")
            : nullptr;
    Ogre::TextureGpu *bloom_vertical =
        postprocessing != nullptr
            ? postprocessing->getDefinedTexture("rtBlur1")
            : nullptr;
    const Ogre::MaterialPtr tone_map =
        Ogre::MaterialManager::getSingleton().getByName(
            "HDR/FinalToneMapping", kOgreNextHdrResourceGroup);
    const Ogre::CompositorNodeDef *definition =
        rendering != nullptr ? rendering->getDefinition() : nullptr;
    hdr_linear_scene_target_verified =
        linear_scene != nullptr &&
        linear_scene->getPixelFormat() == Ogre::PFG_RGBA16_FLOAT &&
        linear_scene->getWidth() == hdr_width &&
        linear_scene->getHeight() == hdr_height &&
        linear_scene->getDepth() == 1U &&
        linear_scene->getNumMipmaps() == 1U;
    // PSSM finalize and rollback both call recreateAllNodes(), which replaces
    // every node instance and therefore every TextureGpu behind it. Re-resolve
    // and re-verify the exported depth and the haze node here so no stale
    // pointer can survive into the aerial-haze pass.
    const bool opaque_depth_verified =
        opaque_depth != nullptr &&
        opaque_depth->getPixelFormat() == Ogre::PFG_D32_FLOAT &&
        opaque_depth->getWidth() == hdr_width &&
        opaque_depth->getHeight() == hdr_height &&
        opaque_depth->getDepth() == 1U &&
        opaque_depth->getNumMipmaps() == 1U;
    hdr_opaque_depth_export_verified = opaque_depth_verified;
    Ogre::CompositorNode *haze =
        hdr_workspace->findNode(kOgreNextAerialHazeNode);
    Ogre::TextureGpu *haze_output =
        haze != nullptr
            ? haze->getDefinedTexture(kOgreNextAerialHazeOutputTexture)
            : nullptr;
    hdr_aerial_haze_workspace_verified =
        haze != nullptr && haze_output != nullptr &&
        haze_output->getPixelFormat() == Ogre::PFG_RGBA16_FLOAT &&
        haze_output->getWidth() == hdr_width &&
        haze_output->getHeight() == hdr_height &&
        haze_output->getDepth() == 1U &&
        haze_output->getNumMipmaps() == 1U;
    hdr_base_hdr_target_verified = false;
    hdr_sun_full_hdr_target_verified = false;
    hdr_sun_direct_hdr_target_verified = false;
    hdr_gpu_sun_direct_split_verified = false;
    hdr_auto_exposure_graph_verified =
        old_luminance != nullptr && iterative_luminance != nullptr &&
        current_luminance != nullptr &&
        old_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT &&
        old_luminance->getWidth() == 1U && old_luminance->getHeight() == 1U &&
        iterative_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT &&
        current_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT;
    hdr_bloom_graph_verified =
        bloom_horizontal != nullptr && bloom_vertical != nullptr;
    hdr_tone_map_graph_verified =
        tone_map && tone_map->getNumTechniques() == 1U &&
        tone_map->getTechnique(0U) != nullptr &&
        tone_map->getTechnique(0U)->getNumPasses() == 1U &&
        tone_map->getTechnique(0U)->getPass(0U)->getFragmentProgramName() ==
            "HDR/FinalToneMapping_ps";
    hdr_srgb_output_verified =
        hdr_output_target->getPixelFormat() == Ogre::PFG_RGBA8_UNORM_SRGB;
    const bool exact_topology =
        definition != nullptr && definition->getNumTargetPasses() == 2U &&
        definition->calculateNumPasses() == 2U &&
        definition->getNumOutputChannels() == 3U;
    const bool exact_shadow_runtime =
        !require_pssm_runtime ||
        (hdr_shadow_node_definition_created &&
         hdr_workspace->findShadowNode(
             Ogre::IdString(kOgreNextHdrShadowNode)) != nullptr);
    if (!hdr_linear_scene_target_verified || !exact_topology ||
        !opaque_depth_verified || !hdr_aerial_haze_workspace_verified ||
        !hdr_auto_exposure_graph_verified || !hdr_bloom_graph_verified ||
        !hdr_tone_map_graph_verified || !hdr_srgb_output_verified ||
        !exact_shadow_runtime) {
      return HdrBackendFailure(
          "single-evaluation HDR runtime differs from the reviewed RGBA16F scene, D32 opaque depth, aerial-haze node, R16F history, exposure, bloom, filmic, sRGB, or staged PSSM topology");
    }
    hdr_base_hdr_target = nullptr;
    hdr_sun_direct_hdr_target = nullptr;
    hdr_visibility_target = nullptr;
    hdr_lit_target = nullptr;
    hdr_opaque_depth_target = opaque_depth;
    hdr_history_target = old_luminance;
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool VerifySingleSceneHdrWarmupShadowAbsence(
      std::uint64_t expected_shadow_node_creates,
      std::uint64_t expected_shadow_node_destroys) noexcept {
    if (!SingleSceneHdrPssmEnabled() || root == nullptr ||
        hdr_workspace == nullptr) {
      return false;
    }
    try {
      Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
      const Ogre::IdString shadow_name(kOgreNextHdrShadowNode);
      Ogre::CompositorNodeDef *definition =
          compositors != nullptr
              ? compositors->getNodeDefinitionNonConst(
                    Ogre::IdString(kOgreNextHdrRenderingNode))
              : nullptr;
      std::size_t selected_scene_passes = 0U;
      std::size_t bound_shadow_scene_passes = 0U;
      if (definition != nullptr) {
        for (std::size_t target_index = 0U;
             target_index < definition->getNumTargetPasses();
             ++target_index) {
          for (const Ogre::CompositorPassDef *pass :
               definition->getTargetPass(target_index)
                   ->getCompositorPasses()) {
            const auto *scene =
                dynamic_cast<const Ogre::CompositorPassSceneDef *>(pass);
            if (scene == nullptr) {
              continue;
            }
            if (scene->mIdentifier ==
                kOgreNextHdrSingleScenePassIdentifier) {
              ++selected_scene_passes;
            }
            if (scene->mShadowNode != Ogre::IdString()) {
              ++bound_shadow_scene_passes;
            }
          }
        }
      }
      const bool absent =
          compositors != nullptr && definition != nullptr &&
          selected_scene_passes == 1U && bound_shadow_scene_passes == 0U &&
          !compositors->hasShadowNodeDefinition(shadow_name) &&
          hdr_workspace->findShadowNode(shadow_name) == nullptr &&
          !hdr_shadow_node_definition_created &&
          shadow_audit.shadow_node_creates == expected_shadow_node_creates &&
          shadow_audit.shadow_node_destroys ==
              expected_shadow_node_destroys;
      if (absent) {
        ++hdr_pssm_warmup_native_absence_checks;
      }
      return absent;
    } catch (...) {
      return false;
    }
  }

  [[nodiscard]] bool RollbackSingleSceneHdrPssm() noexcept {
    bool clean = true;
    try {
      Ogre::CompositorManager2 *compositors =
          root != nullptr ? root->getCompositorManager2() : nullptr;
      if (compositors == nullptr || hdr_workspace == nullptr) {
        clean = false;
      } else {
        UnbindAndVerifyPssmWorkspace(
            *compositors, kOgreNextHdrRenderingNode,
            kOgreNextHdrSingleScenePassIdentifier);
        hdr_workspace->recreateAllNodes();
        const Ogre::IdString shadow_name(kOgreNextHdrShadowNode);
        if (compositors->hasShadowNodeDefinition(shadow_name)) {
          if (!hdr_shadow_node_definition_created) {
            hdr_shadow_node_definition_created = true;
            ++shadow_audit.shadow_node_creates;
          }
          compositors->removeShadowNodeDefinition(shadow_name);
          ++shadow_audit.shadow_node_destroys;
        }
        clean = !compositors->hasShadowNodeDefinition(shadow_name) &&
                hdr_workspace->findShadowNode(shadow_name) == nullptr;
      }
    } catch (...) {
      clean = false;
    }
    hdr_shadow_node_definition_created = false;
    hdr_pssm_finalization_prepared = false;
    hdr_pssm_finalized_with_populated_scene = false;
    hdr_pssm_finalization_deferred = false;
    if (clean) {
      const RenderOperationResult refreshed =
          RefreshSingleSceneHdrRuntimeTargets(false);
      HdrR16Float observed;
      const RenderOperationResult history = refreshed
          ? InitializeExactHdrHistory(
                hdr_temporal_state.previous_inverse_luminance(), observed)
          : refreshed;
      clean = static_cast<bool>(refreshed) && static_cast<bool>(history);
    }
    ++hdr_pssm_finalization_rollbacks;
    return clean;
  }

  [[nodiscard]] RenderOperationResult FinalizeSingleSceneHdrPssm(
      std::uint32_t directional_lights, std::uint32_t shadow_casters,
      std::uint32_t shadow_receivers,
      std::uint32_t authored_view_visibility) {
    // A scene that has not yet streamed any shadow-casting or -receiving
    // geometry is an ordinary early frame, not a backend failure. Terrain that
    // pages in, as the pinned simple2 validation terrains do, submits its
    // first frames before any caster exists, while a mesh terrain such as
    // CityWorld is already populated on frame one. Treating the empty case as
    // fatal made single-evaluation PSSM refuse every paged terrain outright,
    // and because the failure is terminal the session never got a second
    // frame. Defer instead, below, once the genuine contract violations here
    // have been rejected.
    if (!SingleSceneHdrPssmEnabled() || hdr_workspace == nullptr ||
        directional_lights != 1U ||
        authored_view_visibility != kOgreNextRt4AuthoredVisibilityMask) {
      // Four independent preconditions used to share one message, so a
      // failure named the contract but never the observed scene. Report the
      // exact values: which one is unmet decides whether this is a scene the
      // renderer cannot light or merely a frame whose content has not
      // streamed in yet.
      // Lead with the observed values. Relay buffers truncate, and the
      // static contract text is readable here in the source, while the
      // observed scene is the only part that identifies the failure.
      return HdrBackendFailure(
          "single-evaluation PSSM finalization unmet: enabled=" +
          std::to_string(SingleSceneHdrPssmEnabled() ? 1 : 0) +
          " workspace=" + std::to_string(hdr_workspace != nullptr ? 1 : 0) +
          " directional_lights=" + std::to_string(directional_lights) +
          " shadow_casters=" + std::to_string(shadow_casters) +
          " shadow_receivers=" + std::to_string(shadow_receivers) +
          " authored_view_visibility=" +
          std::to_string(authored_view_visibility) + " expected_visibility=" +
          std::to_string(kOgreNextRt4AuthoredVisibilityMask) +
          "; requires exactly one populated directional light and non-empty "
          "caster/receiver sets on the reviewed RT4 visibility mask");
    }
    // Once finalized, keep refreshing the runtime targets every frame even if
    // the scene later empties, so an unloaded map cannot strand stale targets.
    if (hdr_pssm_finalized_with_populated_scene) {
      hdr_pssm_finalization_deferred = false;
      return RefreshSingleSceneHdrRuntimeTargets(true);
    }
    // Not finalized yet and nothing to shadow: wait for geometry. Single
    // evaluation is once-only, so finalizing against an empty scene would
    // permanently bind a shadow setup with no casters or receivers.
    if (shadow_casters == 0U || shadow_receivers == 0U) {
      ++hdr_pssm_finalization_deferrals;
      hdr_pssm_finalization_deferred = true;
      return RenderOperationResult::Success();
    }
    hdr_pssm_finalization_deferred = false;
    if (hdr_pssm_finalization_prepared) {
      return HdrBackendFailure(
          "single-evaluation PSSM finalization was prepared twice before frame publication");
    }
    ++hdr_pssm_finalization_attempts;
    const auto rollback_failure = [&](RenderOperationResult failure) {
      if (!RollbackSingleSceneHdrPssm()) {
        faulted = true;
        return NativeTeardownFailure(
            "Ogre-Next single-evaluation HDR/PSSM finalization rollback");
      }
      return failure;
    };
    try {
      Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
      const Ogre::RenderSystemCapabilities *capabilities =
          renderer->getCapabilities();
      if (compositors == nullptr || capabilities == nullptr ||
          hdr_shadow_node_definition_created ||
          compositors->hasShadowNodeDefinition(
              Ogre::IdString(kOgreNextHdrShadowNode))) {
        return rollback_failure(HdrBackendFailure(
            "single-evaluation PSSM finalization lifecycle is not empty"));
      }
      CreateAndVerifyPssmShadowNode(
          *compositors, *capabilities, kOgreNextHdrShadowNode,
          authored_view_visibility);
      hdr_shadow_node_definition_created = true;
      ++shadow_audit.shadow_node_creates;
      BindAndVerifyPssmWorkspace(
          *compositors, kOgreNextHdrRenderingNode,
          kOgreNextHdrShadowNode, kOgreNextHdrSingleScenePassIdentifier);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      RenderOperationResult injected = MaybeInjectHdrFailure(
          OgreNextN1HdrFailureStage::AFTER_SINGLE_SCENE_PSSM_DEFINITION);
      if (!injected) {
        return rollback_failure(injected);
      }
#endif
      // This is deliberately delayed until native lights, items, bounds, and
      // caster/receiver flags have all passed their per-frame readbacks.  The
      // initialization warmup therefore never instantiates a zero-light PSSM
      // runtime or reports active_lights=0 as if it were a valid shadow frame.
      hdr_workspace->recreateAllNodes();
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      injected = MaybeInjectHdrFailure(
          OgreNextN1HdrFailureStage::AFTER_SINGLE_SCENE_PSSM_WORKSPACE_RECREATE);
      if (!injected) {
        return rollback_failure(injected);
      }
#endif
      const RenderOperationResult refreshed =
          RefreshSingleSceneHdrRuntimeTargets(true);
      if (!refreshed) {
        return rollback_failure(refreshed);
      }
      HdrR16Float observed_history;
      const RenderOperationResult history = InitializeExactHdrHistory(
          hdr_temporal_state.previous_inverse_luminance(), observed_history);
      if (!history) {
        return rollback_failure(history);
      }
      // The native graph is now usable for this frame, but this remains a
      // prepared transaction. Public finalized/commit audit state is emitted
      // only at the final no-fail frame publication point.
      hdr_pssm_finalization_prepared = true;
      return RenderOperationResult::Success();
    } catch (const Ogre::Exception &error) {
      return rollback_failure(BackendFailure(error));
    } catch (const std::exception &error) {
      return rollback_failure(HdrBackendFailure(error.what()));
    }
  }

  [[nodiscard]] bool CanCommitPreparedSingleSceneHdrPssm() const noexcept {
    if (!SingleSceneHdrPssmEnabled()) {
      return !hdr_pssm_finalization_prepared;
    }
    if (hdr_pssm_finalized_with_populated_scene) {
      return !hdr_pssm_finalization_prepared;
    }
    // A frame that deferred finalization prepared nothing on purpose. That is
    // the intended topology for a scene with no shadow geometry yet, not a
    // topology that changed between preparation and publication.
    if (hdr_pssm_finalization_deferred) {
      return !hdr_pssm_finalization_prepared;
    }
    return hdr_pssm_finalization_prepared &&
           hdr_shadow_node_definition_created && hdr_workspace != nullptr;
  }

  void CommitPreparedSingleSceneHdrPssm() noexcept {
    if (!hdr_pssm_finalization_prepared ||
        hdr_pssm_finalized_with_populated_scene) {
      std::terminate();
    }
    hdr_pssm_finalization_prepared = false;
    hdr_pssm_finalized_with_populated_scene = true;
    ++hdr_pssm_finalization_commits;
  }

  [[nodiscard]] RenderOperationResult CreateHdrCompositor(
      std::uint32_t width, std::uint32_t height) {
    if (!hdr_enabled || root == nullptr || renderer == nullptr ||
        scene_manager == nullptr || camera == nullptr ||
        hdr_workspace != nullptr || hdr_output_target != nullptr) {
      return HdrBackendFailure("invalid persistent-workspace lifecycle");
    }
    hdr_pssm_finalization_prepared = false;
    hdr_pssm_deferred_until_scene_population_verified = false;
    hdr_zero_light_pssm_warmup_avoided_verified = false;
    hdr_pssm_warmup_native_absence_checks = 0U;
    Ogre::ResourceGroupManager &resources =
        Ogre::ResourceGroupManager::getSingleton();
    const bool first_resource_initialization = !hdr_resources_initialized;
    if (first_resource_initialization) {
      resources.createResourceGroup(kOgreNextHdrResourceGroup, true);
      hdr_resource_group_created = true;
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    RenderOperationResult injected = first_resource_initialization
                                         ? MaybeInjectHdrFailure(
                                               OgreNextN1HdrFailureStage::
                                                   AFTER_RESOURCE_GROUP_CREATE)
                                         : RenderOperationResult::Success();
    if (!injected) {
      return injected;
    }
#endif
    const std::filesystem::path media_root =
        std::filesystem::u8path(resolved_shader_media_root);
    // FileSystem archives do not resolve shader source files from nested
    // directories by basename, even when recursive enumeration is enabled.
    // Match Ogre-Next's own resources2.cfg/HDR sample registration exactly so
    // every backend can resolve the source selected by the unified program.
    const std::array<const char *, 15U> relative_locations{{
        "2.0/scripts/Compositors",
        "2.0/scripts/materials/Common",
        "2.0/scripts/materials/Common/Any",
        "2.0/scripts/materials/Common/GLSL",
        "2.0/scripts/materials/Common/GLSLES",
        "2.0/scripts/materials/Common/HLSL",
        "2.0/scripts/materials/Common/Metal",
        "2.0/scripts/materials/HDR",
        "2.0/scripts/materials/HDR/GLSL",
        "2.0/scripts/materials/HDR/HLSL",
        "2.0/scripts/materials/HDR/Metal",
        // RoR-owned aerial-haze material and its per-backend shader sources.
        // The same flat-per-directory rule applies: the unified program names
        // its source by basename, so every backend subdirectory needs its own
        // FileSystem archive entry.
        "2.0/scripts/materials/RoRHaze",
        "2.0/scripts/materials/RoRHaze/GLSL",
        "2.0/scripts/materials/RoRHaze/HLSL",
        "2.0/scripts/materials/RoRHaze/Metal"}};
    if (first_resource_initialization) {
      for (const char *relative : relative_locations) {
        resources.addResourceLocation(
            (media_root / relative).generic_u8string(), "FileSystem",
            kOgreNextHdrResourceGroup, true, true);
      }
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    injected = first_resource_initialization
                   ? MaybeInjectHdrFailure(
                         OgreNextN1HdrFailureStage::AFTER_RESOURCE_LOCATIONS)
                   : RenderOperationResult::Success();
    if (!injected) {
      return injected;
    }
#endif
    if (first_resource_initialization) {
      resources.initialiseResourceGroup(kOgreNextHdrResourceGroup, true);
      hdr_resources_initialized = true;
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    injected = first_resource_initialization
                   ? MaybeInjectHdrFailure(
                         OgreNextN1HdrFailureStage::
                             AFTER_RESOURCE_GROUP_INITIALIZE)
                   : RenderOperationResult::Success();
    if (!injected) {
      return injected;
    }
#endif

    if (first_resource_initialization) {
      if (SingleSceneHdrPssmEnabled()) {
        CreateAndVerifyHdrSingleSceneNode(
            *root->getCompositorManager2(),
            hdr_split_node_definition_created);
        CreateAndVerifyAerialHazeNode(*root->getCompositorManager2());
        hdr_haze_node_definition_created = true;
      } else {
        CreateAndVerifyHdrLightingSplitNode(
            *root->getCompositorManager2(), hdr_split_node_definition_created,
            SunVisibilityV2Enabled());
        if (SunVisibilityV2Enabled()) {
          CreateAndVerifyThinSlabRefractionNode(
              *root->getCompositorManager2());
          hdr_refraction_node_definition_created = true;
        }
      }
      if (SunVisibilityV2Enabled()) {
        ConfigureAndVerifyHdrPostExecutionMask(
            *root->getCompositorManager2());
      }
    } else {
      Ogre::CompositorManager2 *compositors =
          root->getCompositorManager2();
      if (!hdr_resource_group_created ||
          !hdr_split_node_definition_created ||
          !hdr_workspace_definition_created ||
          !compositors->hasNodeDefinition(
              Ogre::IdString(kOgreNextHdrRenderingNode)) ||
          !compositors->hasWorkspaceDefinition(
              Ogre::IdString(HdrWorkspaceName())) ||
          (SunVisibilityV2Enabled() &&
          (!hdr_v2_continuation_workspace_definition_created ||
            !compositors->hasWorkspaceDefinition(Ogre::IdString(
                kOgreNextHdrSunVisibilityV2ContinuationWorkspace)) ||
            !hdr_refraction_node_definition_created ||
            !compositors->hasNodeDefinition(
                Ogre::IdString(kOgreNextThinSlabNode)))) ||
          (SingleSceneHdrPssmEnabled() &&
           (!hdr_haze_node_definition_created ||
            !compositors->hasNodeDefinition(
                Ogre::IdString(kOgreNextAerialHazeNode))))) {
        return HdrBackendFailure(
            "retained HDR definitions disappeared before resize rebuild");
      }
      Ogre::CompositorWorkspaceDef *retained_workspace =
          compositors->getWorkspaceDefinition(
              Ogre::IdString(HdrWorkspaceName()));
      const auto &aliases = retained_workspace->getNodeAliasMap();
      const bool has_rendering =
          aliases.find(Ogre::IdString(kOgreNextHdrRenderingNode)) !=
          aliases.end();
      const bool has_postprocessing =
          aliases.find(Ogre::IdString(kOgreNextHdrPostprocessingNode)) !=
          aliases.end();
      const bool has_ui =
          aliases.find(Ogre::IdString(kOgreNextHdrUiNode)) != aliases.end();
      const bool has_haze =
          aliases.find(Ogre::IdString(kOgreNextAerialHazeNode)) !=
          aliases.end();
      // The HDR UI node is a required production member of the closure.
      constexpr bool expected_ui = true;
      if (!has_rendering || !has_postprocessing || has_ui != expected_ui ||
          has_haze != SingleSceneHdrPssmEnabled()) {
        return HdrBackendFailure(
            "retained HDR workspace topology changed before resize rebuild");
      }
      if (SunVisibilityV2Enabled()) {
        Ogre::CompositorWorkspaceDef *continuation =
            compositors->getWorkspaceDefinition(Ogre::IdString(
                kOgreNextHdrSunVisibilityV2ContinuationWorkspace));
        const auto &continuation_aliases = continuation->getNodeAliasMap();
        if (continuation_aliases.size() != 2U ||
            continuation_aliases.find(
                Ogre::IdString(kOgreNextThinSlabNode)) ==
                continuation_aliases.end() ||
            continuation_aliases.find(
                Ogre::IdString(kOgreNextHdrPostprocessingNode)) ==
                continuation_aliases.end()) {
          return HdrBackendFailure(
              "retained sun-visibility V2 continuation topology changed before resize rebuild");
        }
      }
      hdr_hud_workspace_verified = has_ui;
    }

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    if (first_resource_initialization) {
      const RenderOperationResult overlay_control =
          CreateHdrUiOverlayControl();
      if (!overlay_control) {
        return overlay_control;
      }
    }
#endif
    if (first_resource_initialization) {
      const RenderOperationResult hud_runtime = CreateHudOverlayRuntime();
      if (!hud_runtime) {
        return hud_runtime;
      }
    }

    if (first_resource_initialization) {
      const RenderOperationResult workspace_definition =
          CreateHdrWorkspaceDefinition();
      if (!workspace_definition) {
        return workspace_definition;
      }
    }
    if (first_resource_initialization && !SingleSceneHdrPssmEnabled() &&
        directional_shadow_mode ==
        OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1) {
      const Ogre::RenderSystemCapabilities *capabilities =
          renderer->getCapabilities();
      if (capabilities == nullptr) {
        return HdrBackendFailure(
            "device capabilities disappeared before HDR/PSSM binding");
      }
      CreateAndVerifyPssmShadowNode(
          *root->getCompositorManager2(), *capabilities,
          kOgreNextHdrShadowNode, kOgreNextRt4AuthoredVisibilityMask);
      hdr_shadow_node_definition_created = true;
      ++shadow_audit.shadow_node_creates;
      BindAndVerifyPssmWorkspace(*root->getCompositorManager2(),
                                 kOgreNextHdrRenderingNode,
                                 kOgreNextHdrShadowNode,
                                 kOgreNextHdrRasterLitScenePassIdentifier);
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    injected = MaybeInjectHdrFailure(
        OgreNextN1HdrFailureStage::AFTER_WORKSPACE_DEFINITION);
    if (!injected) {
      return injected;
    }
#endif

    hdr_output_target = renderer->getTextureGpuManager()->createTexture(
        "RoRN1HdrOutput", Ogre::GpuPageOutStrategy::Discard,
        Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    injected = MaybeInjectHdrFailure(
        OgreNextN1HdrFailureStage::AFTER_OUTPUT_CREATE);
    if (!injected) {
      return injected;
    }
#endif
    hdr_output_target->setResolution(width, height);
    hdr_output_target->setPixelFormat(Ogre::PFG_RGBA8_UNORM_SRGB);
    hdr_output_target->scheduleTransitionTo(Ogre::GpuResidency::Resident);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    injected = MaybeInjectHdrFailure(
        OgreNextN1HdrFailureStage::AFTER_OUTPUT_CONFIGURE);
    if (!injected) {
      return injected;
    }
#endif
    hdr_workspace = root->getCompositorManager2()->addWorkspace(
        scene_manager, hdr_output_target, camera, HdrWorkspaceName(), true,
        -1, nullptr, nullptr, Ogre::Vector4::ZERO, 0x00U,
        SunVisibilityV2Enabled()
            ? kOgreNextHdrSplitExecutionMask
            : static_cast<std::uint8_t>(0xffU));
    if (!SingleSceneHdrPssmEnabled()) {
      hdr_workspace->addListener(&hdr_directional_split_listener);
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    injected = MaybeInjectHdrFailure(
        OgreNextN1HdrFailureStage::AFTER_WORKSPACE_CREATE);
    if (!injected) {
      return injected;
    }
#endif
    hdr_width = width;
    hdr_height = height;

    Ogre::CompositorNode *rendering =
        hdr_workspace->findNode(kOgreNextHdrRenderingNode);
    Ogre::CompositorNode *postprocessing =
        hdr_workspace->findNode(kOgreNextHdrPostprocessingNode);
    Ogre::CompositorNode *haze =
        SingleSceneHdrPssmEnabled()
            ? hdr_workspace->findNode(kOgreNextAerialHazeNode)
            : nullptr;
    Ogre::TextureGpu *haze_output =
        haze != nullptr
            ? haze->getDefinedTexture(kOgreNextAerialHazeOutputTexture)
            : nullptr;
    Ogre::TextureGpu *linear_scene =
        rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrRasterLitTexture)
            : nullptr;
    Ogre::TextureGpu *base_hdr =
        !SingleSceneHdrPssmEnabled() && rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrBaseTexture)
            : nullptr;
    Ogre::TextureGpu *sun_full_hdr =
        !SingleSceneHdrPssmEnabled() && rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrSunFullTexture)
            : nullptr;
    Ogre::TextureGpu *sun_direct_hdr =
        !SingleSceneHdrPssmEnabled() && rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrSunDirectTexture)
            : nullptr;
    Ogre::TextureGpu *visibility =
        SunVisibilityV2Enabled() && rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrVisibilityTexture)
            : nullptr;
    Ogre::TextureGpu *lit_hdr =
        SunVisibilityV2Enabled() && rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrLitTexture)
            : nullptr;
    // Both production topologies now export RoROpaqueDepth: the V2 split node
    // for its ray-traced continuation, the single-evaluation node for the
    // aerial-haze pass. Resolving it for only one of them would leave the
    // other's depth unverified.
    Ogre::TextureGpu *opaque_depth =
        (SunVisibilityV2Enabled() || SingleSceneHdrPssmEnabled()) &&
                rendering != nullptr
            ? rendering->getDefinedTexture(kOgreNextHdrOpaqueDepthTexture)
            : nullptr;
    Ogre::TextureGpu *old_luminance = rendering != nullptr
                                           ? rendering->getDefinedTexture(
                                                 "oldLumRt")
                                           : nullptr;
    Ogre::TextureGpu *iterative_luminance =
        postprocessing != nullptr
            ? postprocessing->getDefinedTexture("rtIter2")
            : nullptr;
    Ogre::TextureGpu *current_luminance =
        postprocessing != nullptr
            ? postprocessing->getDefinedTexture("lumRt0")
            : nullptr;
    Ogre::TextureGpu *bloom_horizontal =
        postprocessing != nullptr
            ? postprocessing->getDefinedTexture("rtBlur0")
            : nullptr;
    Ogre::TextureGpu *bloom_vertical =
        postprocessing != nullptr
            ? postprocessing->getDefinedTexture("rtBlur1")
            : nullptr;
    const Ogre::MaterialPtr tone_map =
        Ogre::MaterialManager::getSingleton().getByName(
            "HDR/FinalToneMapping", kOgreNextHdrResourceGroup);
    hdr_linear_scene_target_verified =
        linear_scene != nullptr &&
        linear_scene->getPixelFormat() == Ogre::PFG_RGBA16_FLOAT &&
        linear_scene->getWidth() == width && linear_scene->getHeight() == height;
    const auto exact_linear_target = [width, height](Ogre::TextureGpu *texture) {
      return texture != nullptr &&
             texture->getPixelFormat() == Ogre::PFG_RGBA16_FLOAT &&
             texture->getWidth() == width && texture->getHeight() == height &&
             texture->getDepth() == 1U && texture->getNumMipmaps() == 1U;
    };
    hdr_base_hdr_target_verified =
        !SingleSceneHdrPssmEnabled() && exact_linear_target(base_hdr);
    hdr_sun_full_hdr_target_verified =
        !SingleSceneHdrPssmEnabled() && exact_linear_target(sun_full_hdr);
    hdr_sun_direct_hdr_target_verified =
        !SingleSceneHdrPssmEnabled() && exact_linear_target(sun_direct_hdr);
    hdr_visibility_target_verified =
        !SunVisibilityV2Enabled() ||
        (visibility != nullptr && visibility->isUav() &&
         visibility->getPixelFormat() == Ogre::PFG_R16_FLOAT &&
         visibility->getWidth() == width &&
         visibility->getHeight() == height &&
         visibility->getDepth() == 1U &&
         visibility->getNumMipmaps() == 1U);
    hdr_lit_target_verified =
        !SunVisibilityV2Enabled() ||
        (exact_linear_target(lit_hdr) && lit_hdr->isUav());
    const bool opaque_depth_verified =
        (!SunVisibilityV2Enabled() && !SingleSceneHdrPssmEnabled()) ||
        (opaque_depth != nullptr &&
         opaque_depth->getPixelFormat() == Ogre::PFG_D32_FLOAT &&
         opaque_depth->getWidth() == width &&
         opaque_depth->getHeight() == height &&
         opaque_depth->getDepth() == 1U &&
         opaque_depth->getNumMipmaps() == 1U);
    hdr_opaque_depth_export_verified = opaque_depth_verified;
    // The haze node instance and its RGBA16F output must exist at the exact
    // reviewed extent. This is the fail-closed gate for the whole pass: there
    // is no present-without-haze fallback, so a missing or mismatched node
    // takes down the workspace.
    hdr_aerial_haze_workspace_verified =
        !SingleSceneHdrPssmEnabled() ||
        (haze != nullptr && exact_linear_target(haze_output));
    if (SunVisibilityV2Enabled()) {
      hdr_base_hdr_target_verified =
          hdr_base_hdr_target_verified && base_hdr->isUav();
      hdr_sun_direct_hdr_target_verified =
          hdr_sun_direct_hdr_target_verified && sun_direct_hdr->isUav();
    }
    hdr_gpu_sun_direct_split_verified =
        !SingleSceneHdrPssmEnabled() && hdr_base_hdr_target_verified &&
        hdr_sun_full_hdr_target_verified &&
        hdr_sun_direct_hdr_target_verified;
    hdr_auto_exposure_graph_verified =
        old_luminance != nullptr && iterative_luminance != nullptr &&
        current_luminance != nullptr &&
        old_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT &&
        iterative_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT &&
        current_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT;
    hdr_bloom_graph_verified =
        bloom_horizontal != nullptr && bloom_vertical != nullptr;
    hdr_tone_map_graph_verified =
        tone_map && tone_map->getNumTechniques() == 1U &&
        tone_map->getTechnique(0U) != nullptr &&
        tone_map->getTechnique(0U)->getNumPasses() == 1U &&
        tone_map->getTechnique(0U)->getPass(0U)->getFragmentProgramName() ==
            "HDR/FinalToneMapping_ps";
    hdr_srgb_output_verified =
        hdr_output_target->getPixelFormat() == Ogre::PFG_RGBA8_UNORM_SRGB;
    if (SunVisibilityV2Enabled() && hdr_visibility_target_verified &&
        hdr_lit_target_verified && hdr_base_hdr_target_verified &&
        hdr_sun_direct_hdr_target_verified && opaque_depth_verified &&
        old_luminance != nullptr) {
      Ogre::CompositorChannelVec continuation_channels;
      continuation_channels.reserve(4U);
      continuation_channels.push_back(lit_hdr);
      continuation_channels.push_back(old_luminance);
      continuation_channels.push_back(hdr_output_target);
      continuation_channels.push_back(opaque_depth);
      hdr_v2_continuation_workspace =
          root->getCompositorManager2()->addWorkspace(
              scene_manager, continuation_channels, camera,
              kOgreNextHdrSunVisibilityV2ContinuationWorkspace, false, -1,
              nullptr, nullptr, Ogre::Vector4::ZERO, 0x00U,
              kOgreNextHdrPostExecutionMask);
      const Ogre::CompositorChannelVec &observed =
          hdr_v2_continuation_workspace->getExternalRenderTargets();
      if (observed.size() != 4U || observed[0U] != lit_hdr ||
          observed[1U] != old_luminance ||
          observed[2U] != hdr_output_target ||
          observed[3U] != opaque_depth ||
          hdr_v2_continuation_workspace->getEnabled() ||
          hdr_v2_continuation_workspace->getExecutionMask() !=
              kOgreNextHdrPostExecutionMask ||
          hdr_workspace->getExecutionMask() !=
              kOgreNextHdrSplitExecutionMask) {
        return HdrBackendFailure(
            "sun-visibility V2 continuation lost its exact LitHdr/history/output channel order");
      }
    }
    const bool exact_scene_topology =
        SingleSceneHdrPssmEnabled()
            ? (base_hdr == nullptr && sun_full_hdr == nullptr &&
               sun_direct_hdr == nullptr &&
               rendering != nullptr &&
               rendering->getDefinition()->getNumTargetPasses() == 2U &&
               rendering->getDefinition()->calculateNumPasses() == 2U)
            : (hdr_base_hdr_target_verified &&
               hdr_sun_full_hdr_target_verified &&
               hdr_sun_direct_hdr_target_verified &&
               hdr_gpu_sun_direct_split_verified);
    if (!hdr_linear_scene_target_verified || !exact_scene_topology ||
        !hdr_visibility_target_verified || !hdr_lit_target_verified ||
        !opaque_depth_verified || !hdr_aerial_haze_workspace_verified ||
        (SunVisibilityV2Enabled() &&
         hdr_v2_continuation_workspace == nullptr) ||
        !hdr_auto_exposure_graph_verified || !hdr_bloom_graph_verified ||
        !hdr_tone_map_graph_verified || !hdr_srgb_output_verified ||
        (hdr_shadow_node_definition_created &&
         hdr_workspace->findShadowNode(Ogre::IdString(kOgreNextHdrShadowNode)) ==
             nullptr)) {
      return HdrBackendFailure(
          "native HDR lighting graph differs from the reviewed linear-scene, aerial-haze, exposure, bloom, filmic-tone-map, sRGB, or PSSM topology");
    }
    hdr_base_hdr_target = base_hdr;
    hdr_sun_direct_hdr_target = sun_direct_hdr;
    hdr_visibility_target = visibility;
    hdr_lit_target = lit_hdr;
    hdr_opaque_depth_target = opaque_depth;
    hdr_history_target = old_luminance;

    HdrR16Float history_seed;
    if (first_resource_initialization) {
      const ValidationResult initial_quantization = QuantizeHdrR16Float(
          hdr_configuration.initial_inverse_luminance, history_seed);
      if (!initial_quantization) {
        return HdrBackendFailure(
            "validated initial exposure history became unrepresentable");
      }
    } else {
      history_seed = hdr_temporal_state.previous_inverse_luminance();
      HdrR16Float canonical_seed;
      if (!DecodeFiniteHdrR16Float(history_seed.bits, canonical_seed) ||
          canonical_seed.bits != history_seed.bits ||
          canonical_seed.decoded != history_seed.decoded ||
          !(history_seed.decoded > 0.0F)) {
        return HdrBackendFailure(
            "committed exposure history became invalid before resize rebuild");
      }
    }
    const float inverse_width =
        1.0F / (hdr_configuration.bloom_full_colour_threshold -
                hdr_configuration.bloom_minimum_threshold);
    const RenderOperationResult parameters = ConfigureHdrParameters(
        0.0F, hdr_configuration.minimum_auto_exposure,
        hdr_configuration.maximum_auto_exposure,
        hdr_configuration.bloom_minimum_threshold, inverse_width, 0.0F);
    if (!parameters) {
      return parameters;
    }
    if (SingleSceneHdrPssmEnabled()) {
      // The warmup frames execute the workspace before any snapshot exists.
      // Bind the canonical identity now so the quad is an exact pass-through
      // rather than reading whatever the material's defaults happen to be.
      const RenderOperationResult haze_identity =
          BindIdentityAerialHazeParameters();
      if (!haze_identity) {
        return haze_identity;
      }
    }
    if (SunVisibilityV2Enabled()) {
      hdr_workspace->setExecutionMask(
          kOgreNextHdrSplitExecutionMask |
          kOgreNextHdrPostExecutionMask);
      if (hdr_workspace->getExecutionMask() !=
          (kOgreNextHdrSplitExecutionMask |
           kOgreNextHdrPostExecutionMask)) {
        return HdrBackendFailure(
            "sun-visibility V2 warmup could not enable the complete HDR graph");
      }
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    injected = MaybeInjectHdrFailure(
        OgreNextN1HdrFailureStage::AFTER_PARAMETER_BINDING);
    if (!injected) {
      return injected;
    }
#endif
    const std::uint64_t warmup_shadow_node_creates =
        shadow_audit.shadow_node_creates;
    const std::uint64_t warmup_shadow_node_destroys =
        shadow_audit.shadow_node_destroys;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    if (SingleSceneHdrPssmEnabled() && hdr_failure_pending &&
        hdr_failure_stage ==
            OgreNextN1HdrFailureStage::
                BEFORE_SINGLE_SCENE_WARMUP_ABSENCE_CHECK_COUNTER_DRIFT) {
      hdr_failure_pending = false;
      ++shadow_audit.shadow_node_creates;
    }
#endif
    if (SingleSceneHdrPssmEnabled() &&
        !VerifySingleSceneHdrWarmupShadowAbsence(
            warmup_shadow_node_creates, warmup_shadow_node_destroys)) {
      return HdrBackendFailure(
          "single-evaluation HDR warmup began with a shadow definition, binding, runtime instance, ownership marker, or counter drift");
    }
    // Compile and allocate the complete graph before the first public frame.
    // A zero simulation delta prevents launch duration from advancing the
    // intended exposure timeline. Exact history initialization follows the
    // warmup because shader arithmetic is not an exact cross-backend copy.
    for (std::uint64_t warmup = 0U; warmup < 2U; ++warmup) {
      if (!root->renderOneFrame()) {
        return HdrBackendFailure("Ogre-Next ended the HDR warmup loop");
      }
      ++hdr_warmup_frames;
      if (SingleSceneHdrPssmEnabled() &&
          !VerifySingleSceneHdrWarmupShadowAbsence(
              warmup_shadow_node_creates, warmup_shadow_node_destroys)) {
        return HdrBackendFailure(
            "single-evaluation HDR warmup instantiated or recorded a zero-light PSSM runtime");
      }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      const OgreNextN1HdrFailureStage warmup_stage =
          warmup == 0U
              ? OgreNextN1HdrFailureStage::AFTER_WARMUP_FRAME_ONE
              : OgreNextN1HdrFailureStage::AFTER_WARMUP_FRAME_TWO;
      injected = MaybeInjectHdrFailure(warmup_stage);
      if (!injected) {
        return injected;
      }
#endif
    }
    if (SingleSceneHdrPssmEnabled()) {
      const bool exact_absence_receipt =
          hdr_pssm_warmup_native_absence_checks == 3U &&
          shadow_audit.shadow_node_creates == warmup_shadow_node_creates &&
          shadow_audit.shadow_node_destroys == warmup_shadow_node_destroys;
      hdr_pssm_deferred_until_scene_population_verified =
          exact_absence_receipt;
      hdr_zero_light_pssm_warmup_avoided_verified = exact_absence_receipt;
      if (!exact_absence_receipt) {
        return HdrBackendFailure(
            "single-evaluation HDR warmup did not complete its exact native shadow-absence receipt");
      }
    }
    HdrR16Float observed_history;
    const RenderOperationResult exact_history =
        InitializeExactHdrHistory(history_seed, observed_history);
    if (!exact_history) {
      return exact_history;
    }
    if (SunVisibilityV2Enabled()) {
      hdr_workspace->setExecutionMask(kOgreNextHdrSplitExecutionMask);
      if (hdr_workspace->getExecutionMask() !=
              kOgreNextHdrSplitExecutionMask ||
          hdr_v2_continuation_workspace == nullptr ||
          hdr_v2_continuation_workspace->getEnabled()) {
        return HdrBackendFailure(
            "sun-visibility V2 did not restore split-only preparation after warmup");
      }
    }
    if (first_resource_initialization) {
      hdr_history_comparison = OgreNextHdrHistoryComparison{};
    } else {
      hdr_history_comparison =
          hdr_temporal_state.last_history_comparison();
    }
    if (retain_native_lighting_content_evidence) {
      if (first_resource_initialization) {
        hdr_history_comparison.mode = OgreNextHdrHistoryValidationMode::
            NATIVE_AUTHORITATIVE_CONDITIONING_PLUS_ONE_R16_ULP;
        hdr_history_comparison.native_inverse_luminance_r16 =
            observed_history;
        hdr_history_comparison.reference_inverse_luminance_r16 =
            history_seed;
        hdr_history_comparison.accepted = true;
      }
      hdr_native_history_validated = true;
    }
    return RenderOperationResult::Success();
  }

  [[nodiscard]] RenderOperationResult VerifyAndPrepareHdrFrame(
      const OgreNextHdrTemporalFramePlan &plan) {
    if (hdr_workspace == nullptr) {
      return HdrBackendFailure("persistent workspace is unavailable");
    }
    Ogre::CompositorNode *postprocessing =
        hdr_workspace->findNode(kOgreNextHdrPostprocessingNode);
    Ogre::CompositorNode *rendering =
        hdr_workspace->findNode(kOgreNextHdrRenderingNode);
    if (postprocessing == nullptr || rendering == nullptr) {
      return HdrBackendFailure("required upstream HDR nodes are unavailable");
    }
    std::vector<HdrR16Float> iterative;
    std::vector<HdrR16Float> current;
    std::vector<HdrR16Float> copied;
    if (!TryReadFiniteR16Texture(postprocessing->getDefinedTexture("rtIter2"),
                                 4U, 4U, iterative,
                                 LightingContentReadbackCounter()) ||
        !TryReadPositiveR16Texture(
            postprocessing->getDefinedTexture("lumRt0"), 1U, 1U, current,
            LightingContentReadbackCounter()) ||
        !TryReadPositiveR16Texture(
            rendering->getDefinedTexture("oldLumRt"), 1U, 1U, copied,
            LightingContentReadbackCounter()) ||
        current.size() != 1U || copied.size() != 1U ||
        current.front().bits != copied.front().bits ||
        current.front().decoded != copied.front().decoded) {
      return HdrBackendFailure(
          "native luminance reduction is invalid or current-to-old copy bits differ");
    }
    float average_log_luminance = 0.0F;
    if (!TryComputeHdrAverageLogLuminance(iterative,
                                          average_log_luminance)) {
      return HdrBackendFailure(
          "native 4x4 log-luminance reduction is not finite");
    }
    const ValidationResult prepared = hdr_temporal_state.PrepareCommit(
        plan, average_log_luminance, copied.front());
    if (!prepared) {
      return HdrBackendFailure("native history exceeded the reference bound: " +
                               prepared.field + ": " + prepared.detail);
    }
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool RollbackCandidateAllocations(
      std::map<RenderAssetId, NativeMesh> &candidate_meshes,
      std::map<RenderAssetId, NativeMaterial> &candidate_materials,
      std::map<RenderAssetId, NativeTexture> &candidate_textures) noexcept {
    bool clean = true;
    for (auto &entry : candidate_materials) {
      const auto existing = materials.find(entry.first);
      if (existing == materials.end() ||
          existing->second.asset != entry.second.asset) {
        clean = DestroyMaterial(entry.second) && clean;
      }
    }
    candidate_materials.clear();
    for (auto &entry : candidate_textures) {
      const auto existing = textures.find(entry.first);
      if (existing == textures.end() ||
          existing->second.asset != entry.second.asset ||
          existing->second.usage != entry.second.usage) {
        clean = DestroyTexture(entry.second) && clean;
      }
    }
    candidate_textures.clear();
    for (auto &entry : candidate_meshes) {
      const auto existing = meshes.find(entry.first);
      if (existing == meshes.end() ||
          existing->second.asset != entry.second.asset) {
        clean = DestroyMesh(entry.second) && clean;
      }
    }
    candidate_meshes.clear();
    return clean;
  }

  [[nodiscard]] RenderOperationResult CreatePresentationResourceGroup() {
    if (!presentation_configuration.enabled || root == nullptr ||
        renderer == nullptr || presentation_resource_group_created) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "invalid presentation-copy resource-group lifecycle");
    }
    Ogre::ResourceGroupManager &resources =
        Ogre::ResourceGroupManager::getSingleton();
    Ogre::MaterialManager &material_manager =
        Ogre::MaterialManager::getSingleton();
    if (resources.resourceGroupExists(kOgreNextPresentationResourceGroup) ||
        material_manager.getByName("Ogre/Copy/4xFP32")) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "presentation-copy resource identity already exists");
    }

    const std::filesystem::path media_root = std::filesystem::u8path(
        presentation_configuration.shader_media_root);
    resources.createResourceGroup(kOgreNextPresentationResourceGroup, true);
    presentation_resource_group_created = true;
    resources.addResourceLocation(
        (media_root / "CommonCopy").generic_u8string(), "FileSystem",
        kOgreNextPresentationResourceGroup, false, true);
#if defined(ROR_OGRE_NEXT_N1_METAL)
    constexpr const char *platform_directory = "Metal";
#elif defined(ROR_OGRE_NEXT_N1_D3D11)
    constexpr const char *platform_directory = "HLSL";
#elif defined(ROR_OGRE_NEXT_N1_VULKAN)
    constexpr const char *platform_directory = "GLSL";
#else
#error "No reviewed Ogre-Next N1 renderer policy selected"
#endif
    resources.addResourceLocation(
        (media_root / "CommonCopy" / platform_directory).generic_u8string(),
        "FileSystem", kOgreNextPresentationResourceGroup, false, true);
    resources.initialiseResourceGroup(kOgreNextPresentationResourceGroup,
                                      true);

    Ogre::MaterialPtr copy_material = material_manager.getByName(
        "Ogre/Copy/4xFP32", kOgreNextPresentationResourceGroup);
    if (!copy_material || copy_material->getNumTechniques() != 1U) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "exact Ogre/Copy/4xFP32 material was not registered once");
    }
    Ogre::Technique *technique = copy_material->getTechnique(0U);
    if (technique == nullptr || technique->getNumPasses() != 1U) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "exact Ogre/Copy/4xFP32 technique/pass closure changed");
    }
    Ogre::Pass *pass = technique->getPass(0U);
    if (pass == nullptr || pass->getNumTextureUnitStates() != 1U ||
        pass->getVertexProgramName() != "Ogre/Compositor/Quad_vs" ||
        pass->getFragmentProgramName() != "Ogre/Copy/4xFP32_ps") {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "exact Ogre/Copy/4xFP32 GPU-copy binding changed");
    }
    return RenderOperationResult::Success();
  }

  [[nodiscard]] RenderOperationResult RefreshPresentationWindowExtent(
      std::uint32_t expected_width, std::uint32_t expected_height,
      bool notify_window) {
    if (!presentation_configuration.enabled || presentation_window == nullptr ||
        expected_width == 0U || expected_height == 0U) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "invalid active presentation-window extent transaction");
    }
    if (notify_window) {
      presentation_window->windowMovedOrResized();
      ++presentation_audit.window_moved_or_resized_calls;
    }
    const Ogre::TextureGpu *window_texture = presentation_window->getTexture();
    std::uint32_t observed_width = 0U;
    std::uint32_t observed_height = 0U;
    std::int32_t observed_left = 0;
    std::int32_t observed_top = 0;
    presentation_window->getMetrics(observed_width, observed_height,
                                    observed_left, observed_top);
    if (window_texture == nullptr || observed_width != expected_width ||
        observed_height != expected_height ||
        window_texture->getWidth() != expected_width ||
        window_texture->getHeight() != expected_height) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "native presentation window pixel extent differs from the acknowledged host extent");
    }
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool ProductionPresentationEnabled() const noexcept {
    return presentation_configuration.enabled &&
           presentation_configuration.mode ==
               OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP;
  }

  /// The startup graph deliberately has no source texture and no PASS_SCENE.
  /// It clears the exact borrowed window target directly, so making the app
  /// visible cannot manufacture or admit portable world state.
  [[nodiscard]] bool DestroyBootstrapPresentationGraph() noexcept {
    if (root == nullptr) {
      return bootstrap_workspace == nullptr &&
             bootstrap_window_texture == nullptr &&
             !bootstrap_workspace_definition_created &&
             !bootstrap_node_definition_created;
    }
    Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
    if (compositors == nullptr) {
      return false;
    }
    if (bootstrap_workspace != nullptr) {
      try {
        compositors->removeWorkspace(bootstrap_workspace);
        bootstrap_workspace = nullptr;
        bootstrap_window_texture = nullptr;
        ++presentation_audit.bootstrap_workspace_destroys;
      } catch (...) {
        return false;
      }
    }
    if (bootstrap_workspace_definition_created) {
      try {
        const Ogre::IdString name(kBootstrapPresentationWorkspaceName);
        if (!compositors->hasWorkspaceDefinition(name)) {
          return false;
        }
        compositors->removeWorkspaceDefinition(name);
        bootstrap_workspace_definition_created = false;
      } catch (...) {
        return false;
      }
    }
    if (bootstrap_node_definition_created) {
      try {
        const Ogre::IdString name(kBootstrapPresentationNodeName);
        if (!compositors->hasNodeDefinition(name)) {
          return false;
        }
        compositors->removeNodeDefinition(name);
        bootstrap_node_definition_created = false;
        ++presentation_audit.bootstrap_node_definition_destroys;
      } catch (...) {
        return false;
      }
    }
    bootstrap_window_texture = nullptr;
    return true;
  }

  /// The GUI-only graph is a third, independent presentation graph. It has no
  /// source texture, no shadow node, and one overlay-only PASS_SCENE, so it
  /// needs no lights and never reaches the PSSM admission gate. It is kept
  /// separate from the bootstrap graph on purpose: the bootstrap graph's
  /// refusal to exist once portable renderer state is live is the clear-only
  /// startup contract, and the menu must present after scenes have rendered.
  [[nodiscard]] bool DestroyMenuPresentationGraph() noexcept {
    if (root == nullptr) {
      return menu_workspace == nullptr && menu_window_texture == nullptr &&
             !menu_workspace_definition_created &&
             !menu_node_definition_created;
    }
    Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
    if (compositors == nullptr) {
      return false;
    }
    if (menu_workspace != nullptr) {
      try {
        compositors->removeWorkspace(menu_workspace);
        menu_workspace = nullptr;
        menu_window_texture = nullptr;
        ++presentation_audit.ui_overlay_workspace_destroys;
      } catch (...) {
        return false;
      }
    }
    if (menu_workspace_definition_created) {
      try {
        const Ogre::IdString name(kMenuPresentationWorkspaceName);
        if (!compositors->hasWorkspaceDefinition(name)) {
          return false;
        }
        compositors->removeWorkspaceDefinition(name);
        menu_workspace_definition_created = false;
      } catch (...) {
        return false;
      }
    }
    if (menu_node_definition_created) {
      try {
        const Ogre::IdString name(kMenuPresentationNodeName);
        if (!compositors->hasNodeDefinition(name)) {
          return false;
        }
        compositors->removeNodeDefinition(name);
        menu_node_definition_created = false;
      } catch (...) {
        return false;
      }
    }
    menu_window_texture = nullptr;
    return true;
  }

  [[nodiscard]] RenderOperationResult
  RebindMenuPresentationWorkspace(Ogre::TextureGpu *window_texture) {
    if (window_texture == nullptr || root == nullptr ||
        scene_manager == nullptr || camera == nullptr ||
        !menu_workspace_definition_created || !menu_node_definition_created) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "GUI-only presentation rebind has an incomplete overlay graph");
    }
    Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
    try {
      if (menu_workspace != nullptr) {
        compositors->removeWorkspace(menu_workspace);
        menu_workspace = nullptr;
        menu_window_texture = nullptr;
        ++presentation_audit.ui_overlay_workspace_destroys;
      }
      Ogre::CompositorChannelVec channels;
      channels.reserve(1U);
      channels.push_back(window_texture);
      // Created DISABLED: a GUI-only workspace must never execute inside a
      // scene frame's renderOneFrame(). PresentUiOverlayFrame enables it for
      // exactly one call and disables it again.
      menu_workspace =
          compositors->addWorkspace(scene_manager, channels, camera,
                                    kMenuPresentationWorkspaceName, false);
      ++presentation_audit.ui_overlay_workspace_creates;
      const Ogre::CompositorChannelVec &observed =
          menu_workspace->getExternalRenderTargets();
      if (menu_workspace->getEnabled() || observed.size() != 1U ||
          observed[0U] != window_texture) {
        return RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "GUI-only Compositor2 workspace lost its exact disabled window-only channel");
      }
      menu_window_texture = window_texture;
      return RenderOperationResult::Success();
    } catch (const Ogre::Exception &error) {
      return BackendFailure(error);
    } catch (const std::exception &error) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE, error.what());
    }
  }

  [[nodiscard]] RenderOperationResult EnsureMenuPresentationGraph() {
    if (!ProductionPresentationEnabled() || presentation_window == nullptr ||
        !presentation_resource_group_created || surface.suspended) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "GUI-only presentation requires an active production native window");
    }
    Ogre::TextureGpu *window_texture = presentation_window->getTexture();
    if (window_texture == nullptr ||
        window_texture->getWidth() != surface.pixel_width ||
        window_texture->getHeight() != surface.pixel_height) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "GUI-only presentation window lost its acknowledged pixel extent");
    }
    if (menu_workspace == nullptr) {
      Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
      const Ogre::IdString node_name(kMenuPresentationNodeName);
      const Ogre::IdString workspace_name(kMenuPresentationWorkspaceName);
      if (menu_node_definition_created || menu_workspace_definition_created ||
          compositors->hasNodeDefinition(node_name) ||
          compositors->hasWorkspaceDefinition(workspace_name)) {
        return RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "GUI-only presentation compositor identity is not empty");
      }
      Ogre::CompositorNodeDef *node =
          compositors->addNodeDefinition(kMenuPresentationNodeName);
      menu_node_definition_created = true;
      node->addTextureSourceName("PresentationRT", 0U,
                                 Ogre::TextureDefinitionBase::TEXTURE_INPUT);
      node->setNumTargetPass(1U);
      Ogre::CompositorTargetDef *target = node->addTargetPass("PresentationRT");
      target->setNumPasses(1U);
      auto *scene = static_cast<Ogre::CompositorPassSceneDef *>(
          target->addPass(Ogre::PASS_SCENE));
      // Exactly the stock HdrRenderUi overlay window, plus the bootstrap
      // graph's clear so the menu never composites over a stale scene frame.
      scene->mFirstRQ = kOgreNextOverlayFirstRenderQueue;
      scene->mLastRQ = kOgreNextOverlayLastRenderQueue;
      scene->mIncludeOverlays = true;
      scene->mUpdateLodLists = false;
      scene->mEnableForwardPlus = false;
      // A zero visibility mask excludes every scene movable regardless of the
      // render-queue window, so a retained world can never leak into a menu
      // frame; overlays are injected by the render-queue listener instead.
      scene->setVisibilityMask(0U);
      scene->setAllClearColours(
          Ogre::ColourValue(0.012F, 0.018F, 0.028F, 1.0F));
      // Colour only, per attachment. The clear-only bootstrap node likewise
      // clears just Colour0 on the borrowed window; the overlay panel's
      // macroblock disables depth check and depth write, so the window's depth
      // attachment is neither read nor written and must not be loaded (its
      // contents are undefined after a swap) or stored.
      scene->mLoadActionColour[0U] = Ogre::LoadAction::Clear;
      scene->mLoadActionDepth = Ogre::LoadAction::DontCare;
      scene->mLoadActionStencil = Ogre::LoadAction::DontCare;
      scene->mStoreActionDepth = Ogre::StoreAction::DontCare;
      scene->mStoreActionStencil = Ogre::StoreAction::DontCare;

      Ogre::CompositorWorkspaceDef *workspace_definition =
          compositors->addWorkspaceDefinition(kMenuPresentationWorkspaceName);
      menu_workspace_definition_created = true;
      workspace_definition->connectExternal(0U, node->getName(), 0U);
      const RenderOperationResult rebound =
          RebindMenuPresentationWorkspace(window_texture);
      if (!rebound) {
        const bool clean = DestroyMenuPresentationGraph();
        if (!clean) {
          faulted = true;
          return NativeTeardownFailure(
              "Ogre-Next GUI-only presentation bind rollback");
        }
        return rebound;
      }
      return RenderOperationResult::Success();
    }
    const Ogre::CompositorChannelVec &channels =
        menu_workspace->getExternalRenderTargets();
    if (channels.size() != 1U || channels[0U] != menu_window_texture) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "live GUI-only presentation graph changed topology");
    }
    if (menu_window_texture != window_texture) {
      const RenderOperationResult rebound =
          RebindMenuPresentationWorkspace(window_texture);
      if (!rebound) {
        const bool clean = DestroyMenuPresentationGraph();
        if (!clean) {
          faulted = true;
          return NativeTeardownFailure(
              "Ogre-Next GUI-only drawable rebind rollback");
        }
        return rebound;
      }
    }
    return RenderOperationResult::Success();
  }

  [[nodiscard]] RenderOperationResult RebindBootstrapPresentationWorkspace(
      Ogre::TextureGpu *window_texture) {
    if (window_texture == nullptr || root == nullptr || scene_manager == nullptr ||
        camera == nullptr || !bootstrap_workspace_definition_created ||
        !bootstrap_node_definition_created) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "bootstrap presentation rebind has an incomplete clear-only graph");
    }
    Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
    try {
      if (bootstrap_workspace != nullptr) {
        compositors->removeWorkspace(bootstrap_workspace);
        bootstrap_workspace = nullptr;
        bootstrap_window_texture = nullptr;
        ++presentation_audit.bootstrap_workspace_destroys;
      }
      Ogre::CompositorChannelVec channels;
      channels.reserve(1U);
      channels.push_back(window_texture);
      bootstrap_workspace = compositors->addWorkspace(
          scene_manager, channels, camera,
          kBootstrapPresentationWorkspaceName, true);
      ++presentation_audit.bootstrap_workspace_creates;
      const Ogre::CompositorChannelVec &observed =
          bootstrap_workspace->getExternalRenderTargets();
      if (!bootstrap_workspace->getEnabled() || observed.size() != 1U ||
          observed[0U] != window_texture) {
        return RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "bootstrap Compositor2 workspace lost its exact window-only channel");
      }
      bootstrap_window_texture = window_texture;
      return RenderOperationResult::Success();
    } catch (const Ogre::Exception &error) {
      return BackendFailure(error);
    } catch (const std::exception &error) {
      return RenderOperationResult::Failure(RenderOperationCode::BACKEND_FAILURE,
                                            error.what());
    }
  }

  [[nodiscard]] RenderOperationResult EnsureBootstrapPresentationGraph() {
    const OgreNextN1ParticleRuntimeAudit particles = particle_runtime.audit();
    if (!ProductionPresentationEnabled() || presentation_window == nullptr ||
        !presentation_resource_group_created || surface.suspended ||
        production_workspace != nullptr || production_source_target != nullptr ||
        presentation_audit.presented_frames != 0U ||
        submission_state.TrackedSnapshotIdentityCount() != 0U ||
        production_output_handles.live_count() != 0U ||
        registry != nullptr || particles.committed_source_sequence != 0U ||
        particles.create_commands != 0U || particles.update_commands != 0U ||
        particles.stop_commands != 0U || particles.destroy_commands != 0U) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "scene-free bootstrap presentation requested after portable renderer state became live");
    }

    try {
      Ogre::TextureGpu *window_texture = presentation_window->getTexture();
      if (window_texture == nullptr ||
          window_texture->getWidth() != surface.pixel_width ||
          window_texture->getHeight() != surface.pixel_height) {
        return RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "bootstrap presentation window lost its acknowledged pixel extent");
      }
      if (bootstrap_workspace == nullptr) {
        Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
        const Ogre::IdString node_name(kBootstrapPresentationNodeName);
        const Ogre::IdString workspace_name(
            kBootstrapPresentationWorkspaceName);
        if (bootstrap_node_definition_created ||
            bootstrap_workspace_definition_created ||
            compositors->hasNodeDefinition(node_name) ||
            compositors->hasWorkspaceDefinition(workspace_name)) {
          return RenderOperationResult::Failure(
              RenderOperationCode::BACKEND_FAILURE,
              "bootstrap presentation compositor identity is not empty");
        }

        Ogre::CompositorNodeDef *node =
            compositors->addNodeDefinition(kBootstrapPresentationNodeName);
        bootstrap_node_definition_created = true;
        ++presentation_audit.bootstrap_node_definition_creates;
        node->addTextureSourceName(
            "PresentationRT", 0U,
            Ogre::TextureDefinitionBase::TEXTURE_INPUT);
        node->setNumTargetPass(1U);
        Ogre::CompositorTargetDef *target =
            node->addTargetPass("PresentationRT");
        target->setNumPasses(1U);
        auto *clear = static_cast<Ogre::CompositorPassClearDef *>(
            target->addPass(Ogre::PASS_CLEAR));
        clear->setBuffersToClear(Ogre::RenderPassDescriptor::Colour0);
        clear->setAllClearColours(
            Ogre::ColourValue(0.012F, 0.018F, 0.028F, 1.0F));

        Ogre::CompositorWorkspaceDef *workspace_definition =
            compositors->addWorkspaceDefinition(
                kBootstrapPresentationWorkspaceName);
        bootstrap_workspace_definition_created = true;
        workspace_definition->connectExternal(0U, node->getName(), 0U);
        const RenderOperationResult rebound =
            RebindBootstrapPresentationWorkspace(window_texture);
        if (!rebound) {
          const bool clean = DestroyBootstrapPresentationGraph();
          return clean ? rebound
                       : NativeTeardownFailure(
                             "Ogre-Next bootstrap presentation bind rollback");
        }
      } else {
        const Ogre::CompositorChannelVec &channels =
            bootstrap_workspace->getExternalRenderTargets();
        if (!bootstrap_workspace->getEnabled() || channels.size() != 1U ||
            channels[0U] != bootstrap_window_texture) {
          return RenderOperationResult::Failure(
              RenderOperationCode::BACKEND_FAILURE,
              "live bootstrap presentation graph changed topology");
        }
        if (bootstrap_window_texture != window_texture) {
          const RenderOperationResult rebound =
              RebindBootstrapPresentationWorkspace(window_texture);
          if (!rebound) {
            const bool clean = DestroyBootstrapPresentationGraph();
            return clean
                       ? rebound
                       : NativeTeardownFailure(
                             "Ogre-Next bootstrap drawable rebind rollback");
          }
        }
      }

      if (!production_window_shown) {
        presentation_audit.workspace_ready_before_show = true;
        ++presentation_audit.show_callback_calls;
        FrontendSurfaceUpdate acknowledged_surface;
        if (!presentation_configuration.show_after_workspace_ready(
                presentation_configuration.show_callback_context,
                &acknowledged_surface)) {
          throw std::runtime_error(
              "native host did not acknowledge bootstrap show/configure after clear-only workspace readiness");
        }
        const ValidationResult observed_surface =
            ValidateFrontendSurfaceUpdate(acknowledged_surface, false);
        if (!observed_surface) {
          const RenderOperationResult failure =
              OgreNextN1OperationFromValidation(observed_surface);
          const bool clean = DestroyBootstrapPresentationGraph();
          return clean ? failure
                       : NativeTeardownFailure(
                             "Ogre-Next bootstrap show validation rollback");
        }
        if (!SameNativeWindow(acknowledged_surface.window,
                              presentation_configuration.exact_window)) {
          const bool clean = DestroyBootstrapPresentationGraph();
          return clean
                     ? RenderOperationResult::Failure(
                           RenderOperationCode::BACKEND_FAILURE,
                           "bootstrap show acknowledgement changed native window identity or generation")
                     : NativeTeardownFailure(
                           "Ogre-Next bootstrap show identity rollback");
        }
        const bool same_surface =
            acknowledged_surface.surface_revision == surface.surface_revision &&
            acknowledged_surface.pixel_width == surface.pixel_width &&
            acknowledged_surface.pixel_height == surface.pixel_height &&
            acknowledged_surface.content_scale == surface.content_scale &&
            acknowledged_surface.suspended == surface.suspended;
        if (!same_surface) {
          const ValidationResult transition = ValidateFrontendSurfaceTransition(
              surface, acknowledged_surface, false, true);
          if (!transition) {
            const RenderOperationResult failure =
                OgreNextN1OperationFromValidation(transition);
            const bool clean = DestroyBootstrapPresentationGraph();
            return clean ? failure
                         : NativeTeardownFailure(
                               "Ogre-Next bootstrap show transition rollback");
          }
        }
        const RenderOperationResult shown_extent =
            RefreshPresentationWindowExtent(
                acknowledged_surface.pixel_width,
                acknowledged_surface.pixel_height, true);
        if (!shown_extent) {
          const bool clean = DestroyBootstrapPresentationGraph();
          return clean ? shown_extent
                       : NativeTeardownFailure(
                             "Ogre-Next bootstrap shown-extent rollback");
        }
        surface = acknowledged_surface;
        production_window_shown = true;
        if (!same_surface) {
          const bool clean = DestroyBootstrapPresentationGraph();
          return clean
                     ? RenderOperationResult::Failure(
                           RenderOperationCode::RESOURCE_STALE,
                           "bootstrap presentation surface changed after native show ACK",
                           RenderOperationRecovery::
                               RETRY_AFTER_PRESENTATION_SURFACE_UPDATE)
                     : NativeTeardownFailure(
                           "Ogre-Next bootstrap stale-show rollback");
        }
        Ogre::TextureGpu *acknowledged_window_texture =
            presentation_window->getTexture();
        if (acknowledged_window_texture != bootstrap_window_texture) {
          const RenderOperationResult rebound =
              RebindBootstrapPresentationWorkspace(
                  acknowledged_window_texture);
          if (!rebound) {
            const bool clean = DestroyBootstrapPresentationGraph();
            return clean ? rebound
                         : NativeTeardownFailure(
                               "Ogre-Next bootstrap post-show rebind rollback");
          }
        }
      }

      const Ogre::CompositorChannelVec &channels =
          bootstrap_workspace->getExternalRenderTargets();
      if (!bootstrap_workspace->getEnabled() || channels.size() != 1U ||
          channels[0U] != bootstrap_window_texture ||
          bootstrap_window_texture == nullptr ||
          bootstrap_window_texture->getWidth() != surface.pixel_width ||
          bootstrap_window_texture->getHeight() != surface.pixel_height) {
        return RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "bootstrap presentation failed final window-only channel validation");
      }
      return RenderOperationResult::Success();
    } catch (const std::bad_alloc &) {
      const bool clean = DestroyBootstrapPresentationGraph();
      return clean
                 ? RenderOperationResult::Failure(
                       RenderOperationCode::OUT_OF_MEMORY,
                       "bootstrap presentation graph allocation ran out of memory")
                 : NativeTeardownFailure(
                       "Ogre-Next bootstrap allocation rollback");
    } catch (const Ogre::Exception &error) {
      const RenderOperationResult failure = BackendFailure(error);
      const bool clean = DestroyBootstrapPresentationGraph();
      return clean ? failure
                   : NativeTeardownFailure(
                         "Ogre-Next bootstrap Ogre rollback");
    } catch (const std::exception &error) {
      const RenderOperationResult failure = RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE, error.what());
      const bool clean = DestroyBootstrapPresentationGraph();
      return clean ? failure
                   : NativeTeardownFailure(
                         "Ogre-Next bootstrap graph rollback");
    }
  }

  /// Retires only the production run-loop graph. The borrowed native window,
  /// presentation material group, Root, scene manager, and one-frame gate
  /// remain under their existing lifetime contracts.
  [[nodiscard]] bool DestroyProductionPresentationGraph() noexcept {
    if (root == nullptr || renderer == nullptr) {
      return production_workspace == nullptr &&
             production_source_target == nullptr &&
             !production_workspace_definition_created &&
             !production_node_definition_created &&
             !production_shadow_node_definition_created;
    }
    Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
    if (compositors == nullptr) {
      return false;
    }
    if (production_workspace != nullptr) {
      try {
        compositors->removeWorkspace(production_workspace);
        production_workspace = nullptr;
        production_window_texture = nullptr;
        ++presentation_audit.compositor_workspace_destroys;
      } catch (...) {
        return false;
      }
    }
    if (production_workspace_definition_created) {
      try {
        compositors->removeWorkspaceDefinition(
            Ogre::IdString(kProductionPresentationWorkspaceName));
        production_workspace_definition_created = false;
      } catch (...) {
        return false;
      }
    }
    if (production_node_definition_created) {
      try {
        compositors->removeNodeDefinition(
            Ogre::IdString(kProductionPresentationNodeName));
        production_node_definition_created = false;
        ++presentation_audit.compositor_node_definition_destroys;
      } catch (...) {
        return false;
      }
    }
    try {
      const Ogre::IdString shadow_name(
          kProductionPresentationShadowNodeName);
      const bool shadow_exists =
          compositors->hasShadowNodeDefinition(shadow_name);
      if (production_shadow_node_definition_created && !shadow_exists) {
        return false;
      }
      if (shadow_exists) {
        // The helper publishes its native definition before every validation
        // below can complete. Account for and retire that partial ownership
        // even when construction threw before the durable flag was set.
        if (!production_shadow_node_definition_created) {
          production_shadow_node_definition_created = true;
          ++shadow_audit.shadow_node_creates;
        }
        compositors->removeShadowNodeDefinition(
            shadow_name);
        production_shadow_node_definition_created = false;
        ++shadow_audit.shadow_node_destroys;
      }
      if (compositors->hasShadowNodeDefinition(shadow_name)) {
        return false;
      }
    } catch (...) {
      return false;
    }
    if (production_source_target != nullptr) {
      if (hdr_enabled && production_source_target == hdr_output_target) {
        // Borrowed from the owning HDR compositor; only this graph's alias is
        // retired here.
        production_source_target = nullptr;
      } else {
        try {
          Ogre::TextureGpuManager *texture_manager =
              renderer->getTextureGpuManager();
          texture_manager->destroyTexture(production_source_target);
          texture_manager->waitForStreamingCompletion();
          production_source_target = nullptr;
          ++presentation_audit.source_target_destroys;
        } catch (...) {
          return false;
        }
      }
    }
    production_width = 0U;
    production_height = 0U;
    production_color_format = PixelFormat::INVALID;
    production_shadow_visibility_mask = 0U;
    return true;
  }

  [[nodiscard]] RenderOperationResult RebindProductionPresentationWorkspace(
      Ogre::TextureGpu *window_texture, bool enabled = true) {
    if (window_texture == nullptr || production_source_target == nullptr ||
        !production_workspace_definition_created ||
        !production_node_definition_created || root == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "production presentation rebind has an incomplete persistent graph");
    }
    Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
    try {
      if (production_workspace != nullptr) {
        compositors->removeWorkspace(production_workspace);
        production_workspace = nullptr;
        production_window_texture = nullptr;
        ++presentation_audit.compositor_workspace_destroys;
      }
      Ogre::CompositorChannelVec channels;
      channels.reserve(2U);
      channels.push_back(production_source_target);
      channels.push_back(window_texture);
      production_workspace = compositors->addWorkspace(
          scene_manager, channels, camera,
          kProductionPresentationWorkspaceName, enabled);
      ++presentation_audit.compositor_workspace_creates;
      const Ogre::CompositorChannelVec &observed =
          production_workspace->getExternalRenderTargets();
      if (observed.size() != 2U ||
          observed[0U] != production_source_target ||
          observed[1U] != window_texture ||
          production_workspace->getEnabled() != enabled) {
        return RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "production Compositor2 workspace lost its exact source/window channel order");
      }
      production_window_texture = window_texture;
      return RenderOperationResult::Success();
    } catch (const Ogre::Exception &error) {
      return BackendFailure(error);
    } catch (const std::exception &error) {
      return RenderOperationResult::Failure(RenderOperationCode::BACKEND_FAILURE,
                                            error.what());
    }
  }

  [[nodiscard]] RenderOperationResult EnsureProductionPresentationGraph(
      const RenderFrameRequest &request, const CameraViewRequest &view,
      std::uint32_t authored_view_visibility, bool pssm_enabled,
      bool deferred_v2 = false) {
    if (!ProductionPresentationEnabled() ||
        (request.present == deferred_v2) ||
        presentation_window == nullptr ||
        !presentation_resource_group_created || surface.suspended) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "production presentation graph requested outside its active native-window contract");
    }
    const bool copy_only_hdr = hdr_enabled;
    if (copy_only_hdr &&
        (hdr_workspace == nullptr || hdr_output_target == nullptr ||
         !hdr_linear_scene_target_verified ||
         !hdr_srgb_output_verified ||
         (pssm_enabled &&
          (!hdr_shadow_node_definition_created ||
           authored_view_visibility !=
               kOgreNextRt4AuthoredVisibilityMask)))) {
      return RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "HDR presentation requires its verified persistent graph and the full reviewed RT4 authored visibility mask for PSSM");
    }
    if (production_workspace != nullptr) {
      Ogre::TextureGpu *window_texture = presentation_window->getTexture();
      const bool exact = production_source_target != nullptr &&
                         production_workspace->getEnabled() == !deferred_v2 &&
                         bootstrap_workspace == nullptr &&
                         production_width == view.width &&
                         production_height == view.height &&
                         production_color_format == request.color_format &&
                         (copy_only_hdr
                              ? (!production_shadow_node_definition_created &&
                                 (!pssm_enabled ||
                                  hdr_shadow_node_definition_created))
                              : (production_shadow_node_definition_created ==
                                     pssm_enabled &&
                                 (!pssm_enabled ||
                                  production_shadow_visibility_mask ==
                                      authored_view_visibility))) &&
                         production_window_texture == window_texture &&
                         window_texture != nullptr &&
                         window_texture->getWidth() == view.width &&
                         window_texture->getHeight() == view.height;
      if (exact) {
        return RenderOperationResult::Success();
      }
      if (!DestroyProductionPresentationGraph()) {
        return NativeTeardownFailure(
            "Ogre-Next production presentation graph replacement");
      }
    }

    const bool replacing_bootstrap = bootstrap_workspace != nullptr;
    const bool rebuilding = presentation_audit.source_target_creates != 0U;
    const auto rollback_to_bootstrap = [&]() noexcept {
      bool clean = true;
      if (replacing_bootstrap && bootstrap_workspace != nullptr) {
        try {
          if (!bootstrap_workspace->getEnabled()) {
            bootstrap_workspace->setEnabled(true);
          }
          clean = bootstrap_workspace->getEnabled();
        } catch (...) {
          clean = false;
        }
      }
      clean = DestroyProductionPresentationGraph() && clean;
      if (!clean) {
        faulted = true;
      }
      return clean;
    };
    try {
      Ogre::TextureGpuManager *texture_manager =
          renderer->getTextureGpuManager();
      if (copy_only_hdr) {
        production_source_target = hdr_output_target;
      } else {
        production_source_target = texture_manager->createTexture(
            kProductionPresentationTargetName,
            Ogre::GpuPageOutStrategy::Discard,
            Ogre::TextureFlags::RenderToTexture,
            Ogre::TextureTypes::Type2D);
        ++presentation_audit.source_target_creates;
        production_source_target->setResolution(view.width, view.height);
        production_source_target->setPixelFormat(
            request.color_format == PixelFormat::RGBA16_FLOAT
                ? Ogre::PFG_RGBA16_FLOAT
                : Ogre::PFG_RGBA8_UNORM_SRGB);
        production_source_target->scheduleTransitionTo(
            Ogre::GpuResidency::Resident);
      }
      production_width = view.width;
      production_height = view.height;
      production_color_format = request.color_format;

      Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
      const Ogre::IdString node_name(kProductionPresentationNodeName);
      const Ogre::IdString workspace_name(
          kProductionPresentationWorkspaceName);
      if (compositors->hasNodeDefinition(node_name) ||
          compositors->hasWorkspaceDefinition(workspace_name) ||
          compositors->hasShadowNodeDefinition(Ogre::IdString(
              kProductionPresentationShadowNodeName))) {
        throw std::runtime_error(
            "production presentation compositor identity already exists");
      }
      Ogre::CompositorNodeDef *node =
          compositors->addNodeDefinition(kProductionPresentationNodeName);
      production_node_definition_created = true;
      ++presentation_audit.compositor_node_definition_creates;
      node->addTextureSourceName(
          "MainRT", 0U, Ogre::TextureDefinitionBase::TEXTURE_INPUT);
      node->addTextureSourceName(
          "PresentationRT", 1U,
          Ogre::TextureDefinitionBase::TEXTURE_INPUT);
      node->setNumTargetPass(copy_only_hdr ? 1U : 2U);
      if (!copy_only_hdr) {
        Ogre::CompositorTargetDef *main_target =
            node->addTargetPass("MainRT");
        main_target->setNumPasses(1U);
        auto *scene = static_cast<Ogre::CompositorPassSceneDef *>(
            main_target->addPass(Ogre::PASS_SCENE));
        scene->mFirstRQ = 0U;
        scene->mLastRQ = kOgreNextPccReservedRenderQueue;
        scene->mIncludeOverlays = false;
        scene->mEnableForwardPlus = true;
        scene->setVisibilityMask(authored_view_visibility);
        scene->setAllClearColours(
            Ogre::ColourValue(0.0F, 0.0F, 0.0F, 1.0F));
        scene->setAllLoadActions(Ogre::LoadAction::Clear);
        scene->mStoreActionDepth = Ogre::StoreAction::DontCare;
        scene->mStoreActionStencil = Ogre::StoreAction::DontCare;
      }
      if (pssm_enabled && !copy_only_hdr) {
        const Ogre::RenderSystemCapabilities *capabilities =
            renderer->getCapabilities();
        if (capabilities == nullptr) {
          throw std::runtime_error(
              "Ogre-Next lost device capabilities before persistent PSSM construction");
        }
        CreateAndVerifyPssmShadowNode(
            *compositors, *capabilities,
            kProductionPresentationShadowNodeName,
            authored_view_visibility);
        production_shadow_node_definition_created = true;
        production_shadow_visibility_mask = authored_view_visibility;
        ++shadow_audit.shadow_node_creates;
        BindAndVerifyPssmWorkspace(
            *compositors, kProductionPresentationNodeName,
            kProductionPresentationShadowNodeName);
      }
      Ogre::CompositorTargetDef *presentation_target =
          node->addTargetPass("PresentationRT");
      presentation_target->setNumPasses(1U);
      auto *copy = static_cast<Ogre::CompositorPassQuadDef *>(
          presentation_target->addPass(Ogre::PASS_QUAD));
      copy->mMaterialIsHlms = false;
      copy->mMaterialName = "Ogre/Copy/4xFP32";
      copy->mUseQuad = false;
      copy->addQuadTextureSource(0U, "MainRT");

      Ogre::CompositorWorkspaceDef *workspace_definition =
          compositors->addWorkspaceDefinition(
              kProductionPresentationWorkspaceName);
      production_workspace_definition_created = true;
      workspace_definition->connectExternal(0U, node->getName(), 0U);
      workspace_definition->connectExternal(1U, node->getName(), 1U);
      Ogre::TextureGpu *window_texture = presentation_window->getTexture();
      if (window_texture == nullptr ||
          window_texture->getWidth() != view.width ||
          window_texture->getHeight() != view.height) {
        throw std::runtime_error(
            "production presentation window texture lost the acknowledged pixel extent");
      }
      const RenderOperationResult bound =
          RebindProductionPresentationWorkspace(
              window_texture, !replacing_bootstrap);
      if (!bound) {
        const bool clean = DestroyProductionPresentationGraph();
        return clean ? bound
                     : NativeTeardownFailure(
                           "Ogre-Next production presentation bind rollback");
      }
      if (rebuilding) {
        ++presentation_audit.surface_graph_rebuilds;
      }

      if (!production_window_shown) {
        presentation_audit.workspace_ready_before_show = true;
        ++presentation_audit.show_callback_calls;
        FrontendSurfaceUpdate acknowledged_surface;
        if (!presentation_configuration.show_after_workspace_ready(
                presentation_configuration.show_callback_context,
                &acknowledged_surface)) {
          throw std::runtime_error(
              "native host did not acknowledge production show/configure after workspace readiness");
        }
        const ValidationResult observed_surface =
            ValidateFrontendSurfaceUpdate(acknowledged_surface, false);
        if (!observed_surface) {
          const RenderOperationResult failure =
              OgreNextN1OperationFromValidation(observed_surface);
          const bool clean = DestroyProductionPresentationGraph();
          return clean ? failure
                       : NativeTeardownFailure(
                             "Ogre-Next production show rollback");
        }
        if (!SameNativeWindow(acknowledged_surface.window,
                              presentation_configuration.exact_window)) {
          const bool clean = DestroyProductionPresentationGraph();
          return clean
                     ? RenderOperationResult::Failure(
                           RenderOperationCode::BACKEND_FAILURE,
                           "production show acknowledgement changed native window identity or generation")
                     : NativeTeardownFailure(
                           "Ogre-Next production show identity rollback");
        }
        if (acknowledged_surface.surface_revision == surface.surface_revision) {
          if (acknowledged_surface.pixel_width != surface.pixel_width ||
              acknowledged_surface.pixel_height != surface.pixel_height ||
              acknowledged_surface.content_scale != surface.content_scale ||
              acknowledged_surface.suspended != surface.suspended) {
            const bool clean = DestroyProductionPresentationGraph();
            return clean
                       ? RenderOperationResult::Failure(
                             RenderOperationCode::BACKEND_FAILURE,
                             "production show surface changed without advancing its revision")
                       : NativeTeardownFailure(
                             "Ogre-Next production show surface rollback");
          }
        } else {
          const ValidationResult transition = ValidateFrontendSurfaceTransition(
              surface, acknowledged_surface, false, true);
          if (!transition) {
            const RenderOperationResult failure =
                OgreNextN1OperationFromValidation(transition);
            const bool clean = DestroyProductionPresentationGraph();
            return clean ? failure
                         : NativeTeardownFailure(
                               "Ogre-Next production show transition rollback");
          }
        }
        const ValidationResult exact_presentation = deferred_v2
            ? ((acknowledged_surface.pixel_width == view.width &&
                acknowledged_surface.pixel_height == view.height &&
                !acknowledged_surface.suspended)
                   ? ValidationResult::Success()
                   : ValidationResult::Failure(
                         ValidationCode::REVISION_MISMATCH,
                         "presentation_surface_revision",
                         "deferred V2 preparation view does not match the acknowledged native surface"))
            : ValidateRenderFramePresentation(request,
                                              acknowledged_surface);
        const RenderOperationResult shown_extent =
            RefreshPresentationWindowExtent(
                acknowledged_surface.pixel_width,
                acknowledged_surface.pixel_height, true);
        if (!shown_extent) {
          const bool clean = DestroyProductionPresentationGraph();
          return clean ? shown_extent
                       : NativeTeardownFailure(
                             "Ogre-Next production shown-extent rollback");
        }
        surface = acknowledged_surface;
        production_window_shown = true;
        if (!exact_presentation) {
          std::ostringstream detail;
          detail << "production presentation surface out of date after native show ACK; resubmit revision "
                 << acknowledged_surface.surface_revision << " at "
                 << acknowledged_surface.pixel_width << 'x'
                 << acknowledged_surface.pixel_height;
          const bool clean = DestroyProductionPresentationGraph();
          return clean
                     ? RenderOperationResult::Failure(
                           RenderOperationCode::RESOURCE_STALE, detail.str(),
                           RenderOperationRecovery::
                               RETRY_AFTER_PRESENTATION_SURFACE_UPDATE)
                     : NativeTeardownFailure(
                           "Ogre-Next production stale-show rollback");
        }
        Ogre::TextureGpu *acknowledged_window_texture =
            presentation_window->getTexture();
        if (acknowledged_window_texture != production_window_texture) {
          const RenderOperationResult rebound =
              RebindProductionPresentationWorkspace(
                  acknowledged_window_texture);
          if (!rebound) {
            const bool clean = DestroyProductionPresentationGraph();
            return clean ? rebound
                         : NativeTeardownFailure(
                               "Ogre-Next production post-show rebind rollback");
          }
          ++presentation_audit.compositor_workspace_rebinds;
        }
      }

      const Ogre::CompositorChannelVec &channels =
          production_workspace->getExternalRenderTargets();
      if (channels.size() != 2U ||
          channels[0U] != production_source_target ||
          channels[1U] != production_window_texture ||
          production_window_texture == nullptr ||
          production_window_texture->getWidth() != surface.pixel_width ||
          production_window_texture->getHeight() != surface.pixel_height) {
        throw std::runtime_error(
            "production presentation graph failed final channel/extent validation");
      }
      const bool exact_shadow_runtime =
          !pssm_enabled ||
          (copy_only_hdr
               ? (hdr_workspace != nullptr &&
                  hdr_workspace->findShadowNode(
                      Ogre::IdString(kOgreNextHdrShadowNode)) != nullptr)
               : production_workspace->findShadowNode(
                     Ogre::IdString(kProductionPresentationShadowNodeName)) !=
                     nullptr);
      if (!exact_shadow_runtime) {
        throw std::runtime_error(
            "production presentation graph did not instantiate persistent PSSM");
      }
      if (replacing_bootstrap) {
        if (bootstrap_workspace == nullptr ||
            !bootstrap_workspace->getEnabled() ||
            production_workspace == nullptr ||
            production_workspace->getEnabled()) {
          throw std::runtime_error(
              "bootstrap-to-scene presentation replacement lost its exclusive workspace state");
        }
        production_workspace->setEnabled(true);
        if (!production_workspace->getEnabled()) {
          throw std::runtime_error(
              "bootstrap-to-scene presentation replacement did not activate the validated scene graph");
        }
        bootstrap_workspace->setEnabled(false);
        if (bootstrap_workspace->getEnabled()) {
          throw std::runtime_error(
              "bootstrap-to-scene presentation replacement did not retire the clear graph");
        }
        if (!DestroyBootstrapPresentationGraph()) {
          faulted = true;
          return NativeTeardownFailure(
              "Ogre-Next bootstrap-to-scene presentation replacement");
        }
      }
      if (deferred_v2) {
        production_workspace->setEnabled(false);
        if (production_workspace->getEnabled()) {
          throw std::runtime_error(
              "sun-visibility V2 preparation could not defer the window copy");
        }
      }
      return RenderOperationResult::Success();
    } catch (const std::bad_alloc &) {
      const bool clean = rollback_to_bootstrap();
      return clean
                 ? RenderOperationResult::Failure(
                       RenderOperationCode::OUT_OF_MEMORY,
                       "production presentation graph allocation ran out of memory")
                 : NativeTeardownFailure(
                       "Ogre-Next production allocation rollback");
    } catch (const Ogre::Exception &error) {
      const RenderOperationResult failure = BackendFailure(error);
      const bool clean = rollback_to_bootstrap();
      return clean ? failure
                   : NativeTeardownFailure(
                         "Ogre-Next production Ogre rollback");
    } catch (const std::exception &error) {
      const RenderOperationResult failure = RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE, error.what());
      const bool clean = rollback_to_bootstrap();
      return clean ? failure
                   : NativeTeardownFailure(
                         "Ogre-Next production graph rollback");
    }
  }

  NativeSunVisibilityV2Result ContinueFromLitHdr(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      std::uint64_t view_id,
      std::uintptr_t ogre_lit_hdr_texture) override {
    const auto failure = [&](NativeSunVisibilityV2Code code,
                             const char *detail) {
      return V2PresentationResult(code, frame_id, snapshot_id, detail);
    };
    if (!initialized || !SunVisibilityV2Enabled()) {
      return failure(NativeSunVisibilityV2Code::UNSUPPORTED,
                     "v2-continuation-disabled");
    }
    if (!OnOwnerThread()) {
      return failure(NativeSunVisibilityV2Code::BACKEND_FAILURE,
                     "v2-continuation-wrong-thread");
    }
    if (faulted) {
      return failure(NativeSunVisibilityV2Code::BACKEND_FAILURE,
                     "v2-continuation-frontend-faulted");
    }
    if (!sun_visibility_v2_frame_awaiting_continuation ||
        !sun_visibility_v2_hdr_commit_pending) {
      return failure(NativeSunVisibilityV2Code::RESOURCE_STALE,
                     "v2-continuation-not-pending");
    }
    if (frame_id != sun_visibility_v2_pending_frame_id ||
        snapshot_id != sun_visibility_v2_pending_snapshot_id ||
        view_id != sun_visibility_v2_pending_view_id) {
      return failure(NativeSunVisibilityV2Code::RESOURCE_STALE,
                     "v2-continuation-lineage-stale");
    }
    auto *const lit_hdr =
        reinterpret_cast<Ogre::TextureGpu *>(ogre_lit_hdr_texture);
    const bool exact_graph =
        root != nullptr && hdr_workspace != nullptr &&
        hdr_v2_continuation_workspace != nullptr &&
        production_workspace != nullptr && production_source_target != nullptr &&
        production_source_target == hdr_output_target &&
        production_window_texture != nullptr && hdr_lit_target != nullptr &&
        hdr_opaque_depth_target != nullptr &&
        lit_hdr == hdr_lit_target && hdr_lit_target_verified &&
        hdr_lit_target->isUav() &&
        hdr_lit_target->getPixelFormat() == Ogre::PFG_RGBA16_FLOAT &&
        hdr_lit_target->getWidth() == sun_visibility_v2_pending_width &&
        hdr_lit_target->getHeight() == sun_visibility_v2_pending_height &&
        hdr_opaque_depth_target->getPixelFormat() == Ogre::PFG_D32_FLOAT &&
        hdr_opaque_depth_target->getWidth() ==
            sun_visibility_v2_pending_width &&
        hdr_opaque_depth_target->getHeight() ==
            sun_visibility_v2_pending_height &&
        !surface.suspended &&
        surface.surface_revision ==
            sun_visibility_v2_pending_surface_revision &&
        surface.pixel_width == sun_visibility_v2_pending_width &&
        surface.pixel_height == sun_visibility_v2_pending_height &&
        production_window_texture->getWidth() ==
            sun_visibility_v2_pending_width &&
        production_window_texture->getHeight() ==
            sun_visibility_v2_pending_height &&
        hdr_workspace->getEnabled() &&
        hdr_workspace->getExecutionMask() ==
            kOgreNextHdrSplitExecutionMask &&
        !hdr_v2_continuation_workspace->getEnabled() &&
        hdr_v2_continuation_workspace->getExecutionMask() ==
            kOgreNextHdrPostExecutionMask &&
        !production_workspace->getEnabled();
    if (!exact_graph) {
      faulted = true;
      return failure(NativeSunVisibilityV2Code::RESOURCE_STALE,
                     "v2-continuation-graph-stale");
    }

    const ValidationResult prepared = hdr_temporal_state.PrepareGpuOnlyCommit(
        sun_visibility_v2_pending_hdr_plan);
    if (!prepared || !hdr_temporal_state.CanCommitPrepared()) {
      hdr_temporal_state.AbortPrepared();
      faulted = true;
      return failure(NativeSunVisibilityV2Code::BACKEND_FAILURE,
                     "v2-continuation-history-prepare-failed");
    }

    const auto restore_workspaces = [&]() noexcept {
      bool clean = true;
      try {
        if (production_workspace != nullptr &&
            production_workspace->getEnabled()) {
          production_workspace->setEnabled(false);
        }
        clean = production_workspace != nullptr &&
                !production_workspace->getEnabled() && clean;
      } catch (...) {
        clean = false;
      }
      try {
        if (hdr_v2_continuation_workspace != nullptr &&
            hdr_v2_continuation_workspace->getEnabled()) {
          hdr_v2_continuation_workspace->setEnabled(false);
        }
        clean = hdr_v2_continuation_workspace != nullptr &&
                !hdr_v2_continuation_workspace->getEnabled() && clean;
      } catch (...) {
        clean = false;
      }
      try {
        if (hdr_workspace != nullptr && !hdr_workspace->getEnabled()) {
          hdr_workspace->setEnabled(true);
        }
        clean = hdr_workspace != nullptr && hdr_workspace->getEnabled() &&
                hdr_workspace->getExecutionMask() ==
                    kOgreNextHdrSplitExecutionMask &&
                clean;
      } catch (...) {
        clean = false;
      }
      return clean;
    };

    bool rendered = false;
    try {
      hdr_workspace->setEnabled(false);
      hdr_v2_continuation_workspace->setEnabled(true);
      production_workspace->setEnabled(true);
      if (hdr_workspace->getEnabled() ||
          !hdr_v2_continuation_workspace->getEnabled() ||
          !production_workspace->getEnabled()) {
        throw std::runtime_error(
            "deferred V2 presentation workspaces did not become exclusive");
      }
      rendered = root->renderOneFrame();
    } catch (...) {
      rendered = false;
    }
    const bool restored = restore_workspaces();
    if (!rendered || !restored ||
        !hdr_temporal_state.CanCommitPrepared()) {
      hdr_temporal_state.AbortPrepared();
      faulted = true;
      return failure(NativeSunVisibilityV2Code::BACKEND_FAILURE,
                     rendered ? "v2-continuation-restore-failed"
                              : "v2-continuation-render-failed");
    }

    hdr_temporal_state.CommitPrepared();
    hdr_exact_current_to_old_copy_verified = false;
    hdr_history_comparison = hdr_temporal_state.last_history_comparison();
    hdr_native_history_validated = false;
    OgreNextNativeLightingPassAudit lighting =
        sun_visibility_v2_pending_lighting;
    lighting.gpu_hdr_history_sequenced =
        hdr_temporal_state.committed_frame_id() == frame_id;
    lighting.single_step_hdr_history =
        hdr_temporal_state.committed_frame_id() == frame_id;
    const bool rendered_thin_slab =
        lighting.last_transmission_items > 0U &&
        hdr_refraction_node_definition_created &&
        hdr_v2_continuation_workspace_definition_created;
    lighting.thin_parallel_slab_refraction = rendered_thin_slab;
    lighting.physical_snell_refraction = rendered_thin_slab;
    lighting.beer_lambert_attenuation = rendered_thin_slab;
    lighting.screen_space_radiance_lookup = rendered_thin_slab;
    lighting.refraction_scene_evaluations = rendered_thin_slab ? 1U : 0U;
    if (!lighting.gpu_hdr_history_sequenced ||
        !lighting.single_step_hdr_history ||
        !lighting.native_scene_lighting_pass ||
        !lighting.linear_rgba16_hdr_target ||
        !lighting.separate_base_hdr_target ||
        !lighting.separate_unoccluded_sun_full_hdr_target ||
        !lighting.separate_sun_direct_hdr_target ||
        !lighting.gpu_sun_direct_derivation ||
        !lighting.transactional_directional_sun_toggle ||
        lighting.raster_scene_evaluations != 3U ||
        !lighting.hdr_auto_exposure || !lighting.hdr_bloom ||
        !lighting.filmic_tone_map || !lighting.srgb_presentation ||
        (lighting.last_transmission_items > 0U &&
         (!lighting.thin_parallel_slab_refraction ||
          !lighting.physical_snell_refraction ||
          !lighting.beer_lambert_attenuation ||
          !lighting.screen_space_radiance_lookup ||
          lighting.refraction_scene_evaluations != 1U)) ||
        !lighting.production_gpu_only || !lighting.no_ogre14_lighting ||
        lighting.production_content_readbacks != 0U ||
        lighting.production_framebuffer_readbacks != 0U ||
        lighting.ogre14_lighting_passes != 0U) {
      faulted = true;
      return failure(NativeSunVisibilityV2Code::BACKEND_FAILURE,
                     "v2-continuation-audit-invalid");
    }
    ++lighting.completed_frames;
    lighting_audit = lighting;

    if (presentation_audit.presented_frames == 0U) {
      presentation_audit.first_presented_frame_id = frame_id;
    } else {
      presentation_audit.monotonic_presented_frame_ids =
          presentation_audit.monotonic_presented_frame_ids &&
          frame_id > presentation_audit.last_presented_frame_id;
    }
    presentation_audit.last_presented_frame_id = frame_id;
    presentation_audit.exact_two_external_channels = true;
    presentation_audit.ui_free_source = true;
    presentation_audit.gpu_quad_copy = true;
    presentation_audit.cpu_window_copy = false;
    presentation_audit.workspace_ready_before_show = true;
    presentation_audit.bounded_swap_completed = true;
    ++presentation_audit.source_scene_passes;
    ++presentation_audit.presentation_quad_passes;
    ++presentation_audit.render_one_frame_calls;
    ++presentation_audit.window_final_target_updates;
    ++presentation_audit.window_swap_completions;
    ++presentation_audit.presented_frames;
    ++presentation_audit.gpu_only_output_frames;
    presentation_audit.last_view_id = view_id;
    presentation_audit.last_surface_revision =
        sun_visibility_v2_pending_surface_revision;
    presentation_audit.last_width = sun_visibility_v2_pending_width;
    presentation_audit.last_height = sun_visibility_v2_pending_height;

    sun_visibility_v2_frame_awaiting_continuation = false;
    sun_visibility_v2_hdr_commit_pending = false;
    sun_visibility_v2_pending_frame_id = 0U;
    sun_visibility_v2_pending_snapshot_id = 0U;
    sun_visibility_v2_pending_view_id = 0U;
    sun_visibility_v2_pending_surface_revision = 0U;
    sun_visibility_v2_pending_width = 0U;
    sun_visibility_v2_pending_height = 0U;
    sun_visibility_v2_pending_lighting = {};
    sun_visibility_v2_pending_hdr_plan = {};
    return V2PresentationResult(NativeSunVisibilityV2Code::OK, frame_id,
                                snapshot_id, "ok");
  }

  [[nodiscard]] bool DestroyPresentationResources() noexcept {
    bool clean = true;
    if (presentation_window != nullptr) {
      try {
        renderer->destroyRenderWindow(presentation_window);
        presentation_window = nullptr;
      } catch (...) {
        clean = false;
      }
    }
    if (bootstrap_window != nullptr) {
      try {
        renderer->destroyRenderWindow(bootstrap_window);
        bootstrap_window = nullptr;
      } catch (...) {
        clean = false;
      }
    }
    if (presentation_resource_group_created) {
      try {
        Ogre::ResourceGroupManager::getSingleton().destroyResourceGroup(
            kOgreNextPresentationResourceGroup);
        presentation_resource_group_created = false;
      } catch (...) {
        clean = false;
      }
    }
    return clean;
  }

  [[nodiscard]] bool CleanupBackend() noexcept {
    if (native_interop) {
      native_interop->RevokeFrontend();
      native_interop.reset();
    }
    bool clean = production_output_handles.live_count() == 0U;
    // Full GPU drain before any native teardown. Metal's in-flight frame
    // semaphore is created at the dynamic buffer multiplier (3) and must be
    // back at that value before ~MetalRenderSystem releases it, or
    // libdispatch traps in _dispatch_semaphore_dispose. The documented full
    // stall commits the tail command buffer and blocks until the device has
    // retired every pending frame, which runs each frame's completion
    // handler that re-signals the semaphore.
    if (renderer != nullptr) {
      try {
        Ogre::VaoManager *const vao_manager = renderer->getVaoManager();
        if (vao_manager != nullptr) {
          vao_manager->waitForSpecificFrameToFinish(
              vao_manager->getFrameCount());
        }
      } catch (...) {
        clean = false;
      }
    }
    if (reflection_probe_runtime) {
      clean = reflection_probe_runtime->Shutdown() && clean;
      reflection_probe_runtime.reset();
    }
    clean = DestroyProductionPresentationGraph() && clean;
    clean = DestroyBootstrapPresentationGraph() && clean;
    clean = DestroyMenuPresentationGraph() && clean;
    // The GUI-only panel and its private image die before the catalog and the
    // scene manager, and before the shared overlay listener is unregistered.
    clean = DestroyMenuOverlayRuntime() && clean;
    clean = DestroyRetainedOutputTarget() && clean;
    clean = DestroyHdrCompositor() && clean;
    clean = DestroyOverlaySystem() && clean;
    // Retained Items and receiver clones must die before the datablocks,
    // textures, and meshes they link to.
    clean = DestroyRetainedScene() && clean;
    clean = DestroyFrameMeshes() && clean;
    clean = DestroyCatalog() && clean;
    if (root && scene_manager != nullptr) {
      try {
        root->destroySceneManager(scene_manager);
      } catch (...) {
        clean = false;
      }
    }
    scene_manager = nullptr;
    camera = nullptr;
    pbs = nullptr;
    unlit = nullptr;
    if (!DestroyPresentationResources()) {
      return false;
    }
    renderer = nullptr;
    root.reset();
    plugin.reset();
    if (owns_root_claim) {
      ReleaseOgreNextN1Root();
      owns_root_claim = false;
    }
    submission_state.Reset();
    particle_runtime.Reset();
    particle_native_batch_creates = 0U;
    particle_native_batch_destroys = 0U;
    particle_native_particles_submitted = 0U;
    particle_native_state_verifications = 0U;
    analytic_sky_audit = {};
    scene_generation = 1U;
    next_native_storage_generation = 1U;
    maximum_texture_dimension =
        kOgreNextN1ConservativeMaximumTextureDimension;
    maximum_anisotropy = 1.0F;
    initialized = false;
    faulted = false;
    production_window_shown = false;
    owner_thread = {};
    surface = {};
    return clean;
  }

  Ogre::AbiCookie abi_cookie = Ogre::generateAbiCookie();
  std::unique_ptr<N1RendererPlugin> plugin;
  std::unique_ptr<Ogre::Root> root;
  Ogre::RenderSystem *renderer = nullptr;
  Ogre::Window *bootstrap_window = nullptr;
  Ogre::Window *presentation_window = nullptr;
  Ogre::HlmsPbs *pbs = nullptr;
  Ogre::HlmsUnlit *unlit = nullptr;
  Ogre::SceneManager *scene_manager = nullptr;
  Ogre::Camera *camera = nullptr;
  FrontendSurfaceUpdate surface;
  std::unique_ptr<RenderAssetRegistry> registry;
  std::map<RenderAssetId, NativeMesh> meshes;
  std::map<RenderAssetId, NativeMaterial> materials;
  std::map<RenderAssetId, NativeTexture> textures;
  std::vector<NativeMesh> frame_meshes;
  /// Retained native lights in snapshot order (strictly increasing light_id).
  std::vector<RetainedLight> retained_lights;
  /// Retained native scene keyed by instance_id, so the per-present diff is a
  /// merge-join against the strictly increasing snapshot instance vector.
  std::map<std::uint64_t, RetainedInstance> retained_instances;
  /// Shadow-plan state the retained scene was built under. A flip forces a
  /// full update pass instead of trusting bit-equal descriptors.
  bool retained_scene_shadow_enabled = false;
  /// Uniquifies deformed-mesh names across deferred retirement so a live
  /// mesh name can never collide with one awaiting destruction.
  std::uint64_t deformed_mesh_sequence = 0U;
  // Live aggregate contributions of retained_instances, maintained
  // O(changed) and cross-checked against the snapshot-derived shadow plan
  // every present before they are published.
  std::uint32_t retained_pbs_items = 0U;
  std::uint32_t retained_transmission_items = 0U;
  std::uint32_t retained_normal_mapped_items = 0U;
  std::uint32_t retained_emissive_items = 0U;
  std::uint32_t retained_shadow_casters = 0U;
  std::uint32_t retained_shadow_receivers = 0U;
  /// N3/N4 retain their last HDR target until the native image publication
  /// is discarded, or until frontend shutdown first revokes every token.
  Ogre::TextureGpu *retained_output_target = nullptr;
  std::shared_ptr<OgreNextN1NativeInteropBridge> native_interop;
  std::unique_ptr<OgreNextReflectionProbeRuntime> reflection_probe_runtime;
  OgreNextN1ParticleRuntime particle_runtime;
  std::uint64_t particle_native_batch_creates = 0U;
  std::uint64_t particle_native_batch_destroys = 0U;
  std::uint64_t particle_native_particles_submitted = 0U;
  std::uint64_t particle_native_state_verifications = 0U;
  OgreNextAnalyticSkyRuntimeAudit analytic_sky_audit;
  OgreNextN1SubmissionState submission_state;
  OgreNextNativeFeatureTier native_feature_tier =
      OgreNextNativeFeatureTier::RASTER_N1;
  OgreNextNativeVertexLayout native_vertex_layout =
      OgreNextNativeVertexLayout::INVALID;
  OgreNextRasterFeatureTier raster_feature_tier =
      OgreNextRasterFeatureTier::STATIC_PBR_N1;
  OgreNextDirectionalShadowMode directional_shadow_mode =
      OgreNextDirectionalShadowMode::DISABLED;
  OgreNextHdrSceneTopology hdr_scene_topology =
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2;
  OgreNextPssmShadowRuntimeAudit shadow_audit;
  OgreNextNativeLightingPassAudit lighting_audit;
  OgreNextRetainedSceneAudit retained_audit;
  /// Named counters for the render-boundary severity invariant. Every gate
  /// that degrades instead of ending the session lands one of these.
  OgreNextN1RenderBoundaryDegradeAudit degrade_audit;
  std::thread::id owner_thread;
  std::string configured_shader_media_root;
  OgreNextN1PresentationConfiguration presentation_configuration;
  OgreNextN1PresentationAudit presentation_audit;
  ResourceHandlePool production_output_handles{ResourceKind::RENDER_TARGET};
  Ogre::TextureGpu *bootstrap_window_texture = nullptr;
  Ogre::CompositorWorkspace *bootstrap_workspace = nullptr;
  Ogre::TextureGpu *production_source_target = nullptr;
  Ogre::TextureGpu *production_window_texture = nullptr;
  Ogre::CompositorWorkspace *production_workspace = nullptr;
  std::uint32_t production_width = 0U;
  std::uint32_t production_height = 0U;
  std::uint32_t production_shadow_visibility_mask = 0U;
  PixelFormat production_color_format = PixelFormat::INVALID;
  std::string resolved_shader_media_root;
  OgreNextHdrTemporalConfiguration hdr_configuration;
  OgreNextHdrTemporalState hdr_temporal_state;
  HdrDirectionalSplitListener hdr_directional_split_listener;
  OgreNextHdrHistoryComparison hdr_history_comparison;
  Ogre::TextureGpu *hdr_output_target = nullptr;
  Ogre::CompositorWorkspace *hdr_workspace = nullptr;
  Ogre::CompositorWorkspace *hdr_v2_continuation_workspace = nullptr;
  Ogre::TextureGpu *hdr_base_hdr_target = nullptr;
  Ogre::TextureGpu *hdr_sun_direct_hdr_target = nullptr;
  Ogre::TextureGpu *hdr_visibility_target = nullptr;
  Ogre::TextureGpu *hdr_lit_target = nullptr;
  Ogre::TextureGpu *hdr_opaque_depth_target = nullptr;
  Ogre::TextureGpu *hdr_history_target = nullptr;
  std::uint32_t hdr_width = 0U;
  std::uint32_t hdr_height = 0U;
  std::uint64_t hdr_warmup_frames = 0U;
  bool initialized = false;
  bool faulted = false;
  bool owns_root_claim = false;
  bool hdr_enabled = false;
  /// Test-artifact-only synchronous VB/IB evidence. Production construction
  /// hard-wires this false because its public configuration has no such field.
  bool retain_analytic_sky_geometry_content_evidence = false;
  bool retain_native_lighting_content_evidence = false;
  bool retain_sun_visibility_v2_content_evidence = false;
  bool presentation_resource_group_created = false;
  bool bootstrap_node_definition_created = false;
  bool bootstrap_workspace_definition_created = false;
  bool production_node_definition_created = false;
  bool production_shadow_node_definition_created = false;
  bool production_workspace_definition_created = false;
  bool production_window_shown = false;
  bool hdr_resource_group_created = false;
  bool hdr_resources_initialized = false;
  bool hdr_workspace_definition_created = false;
  bool hdr_v2_continuation_workspace_definition_created = false;
  bool hdr_split_node_definition_created = false;
  bool hdr_refraction_node_definition_created = false;
  bool hdr_haze_node_definition_created = false;
  bool hdr_shadow_node_definition_created = false;
  bool hdr_pssm_finalization_prepared = false;
  bool hdr_pssm_finalized_with_populated_scene = false;
  std::uint64_t hdr_pssm_warmup_native_absence_checks = 0U;
  bool hdr_pssm_deferred_until_scene_population_verified = false;
  bool hdr_zero_light_pssm_warmup_avoided_verified = false;
  std::uint64_t hdr_pssm_finalization_attempts = 0U;
  std::uint64_t hdr_pssm_finalization_commits = 0U;
  std::uint64_t hdr_pssm_finalization_rollbacks = 0U;
  /// Frames that carried no shadow geometry yet and therefore
  /// deferred single-evaluation finalization instead of failing.
  std::uint64_t hdr_pssm_finalization_deferrals = 0U;
  /// True when the most recent finalization attempt deferred. Publication
  /// accepts a deferred frame as an intended topology, not a changed one.
  bool hdr_pssm_finalization_deferred = false;
  bool hdr_linear_scene_target_verified = false;
  bool hdr_base_hdr_target_verified = false;
  bool hdr_sun_full_hdr_target_verified = false;
  bool hdr_sun_direct_hdr_target_verified = false;
  bool hdr_visibility_target_verified = false;
  bool hdr_lit_target_verified = false;
  bool hdr_gpu_sun_direct_split_verified = false;
  /// The scene node's exported RoROpaqueDepth resolved as D32 at the reviewed
  /// extent. Vacuously true for topologies that export no depth.
  bool hdr_opaque_depth_export_verified = false;
  /// The haze node instance and its RGBA16F output were resolved and matched
  /// the reviewed extent and format.
  bool hdr_aerial_haze_workspace_verified = false;
  /// Every haze named constant survived its per-frame readback.
  bool hdr_aerial_haze_constants_bound = false;
  /// The most recent bind carried a live (non-zero-extinction) atmosphere.
  /// False means the pass ran as an exact pass-through, not that it was
  /// skipped: there is no present-without-haze fallback.
  bool hdr_aerial_haze_applied = false;
  /// Frames whose camera basis failed the rigid/orthonormal admission and
  /// were therefore presented with identity (exactly no) haze. A healthy
  /// session reports zero; a nonzero count is a real signal, not a fault.
  std::uint64_t hdr_aerial_haze_basis_rejections = 0U;
  /// Last bound atmosphere, for the presenter's runtime evidence log.
  float hdr_aerial_haze_extinction_per_meter = 0.0F;
  Float3 hdr_aerial_haze_inscatter{};
  bool hdr_auto_exposure_graph_verified = false;
  bool hdr_bloom_graph_verified = false;
  bool hdr_tone_map_graph_verified = false;
  bool hdr_srgb_output_verified = false;
  bool hdr_manual_delta_bound = false;
  bool hdr_native_history_validated = false;
  bool hdr_exact_current_to_old_copy_verified = false;
  bool hdr_hud_workspace_verified = false;
  bool sun_visibility_v2_frame_awaiting_continuation = false;
  bool sun_visibility_v2_hdr_commit_pending = false;
  std::uint64_t sun_visibility_v2_pending_frame_id = 0U;
  std::uint64_t sun_visibility_v2_pending_snapshot_id = 0U;
  std::uint64_t sun_visibility_v2_pending_view_id = 0U;
  std::uint64_t sun_visibility_v2_pending_surface_revision = 0U;
  std::uint32_t sun_visibility_v2_pending_width = 0U;
  std::uint32_t sun_visibility_v2_pending_height = 0U;
  OgreNextNativeLightingPassAudit sun_visibility_v2_pending_lighting;
  OgreNextHdrTemporalFramePlan sun_visibility_v2_pending_hdr_plan;
  std::uint64_t texture_allocation_creates = 0U;
  std::uint64_t texture_allocation_destroys = 0U;
  std::uint64_t texture_retired_name_lookups = 0U;
  std::uint64_t texture_retired_name_rejections = 0U;
  std::uint64_t scene_generation = 1U;
  std::uint64_t next_native_storage_generation = 1U;
  std::uint32_t maximum_texture_dimension =
      kOgreNextN1ConservativeMaximumTextureDimension;
  float maximum_anisotropy = 1.0F;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  OgreNextN1TextureUploadFailureStage texture_upload_failure_stage =
      OgreNextN1TextureUploadFailureStage::NONE;
  bool retain_reflection_capture_evidence = false;
  bool texture_upload_failure_pending =
      texture_upload_failure_stage != OgreNextN1TextureUploadFailureStage::NONE;
  std::uint64_t normal_uploads_verified = 0U;
  std::uint64_t normal_upload_mip_levels_verified = 0U;
  std::uint64_t normal_upload_rows_verified = 0U;
  std::uint64_t normal_upload_texels_verified = 0U;
  std::uint64_t normal_upload_rg_bytes_verified = 0U;
  std::uint64_t normal_upload_padded_source_rows_verified = 0U;
  OgreNextN1PssmFailureStage pssm_failure_stage =
      OgreNextN1PssmFailureStage::NONE;
  bool pssm_failure_pending =
      pssm_failure_stage != OgreNextN1PssmFailureStage::NONE;
  OgreNextN1HdrFailureStage hdr_failure_stage =
      OgreNextN1HdrFailureStage::NONE;
  bool hdr_failure_pending = hdr_failure_stage != OgreNextN1HdrFailureStage::NONE;
  OgreNextN1AnalyticSkyFailureStage analytic_sky_failure_stage =
      OgreNextN1AnalyticSkyFailureStage::NONE;
  bool analytic_sky_failure_pending =
      analytic_sky_failure_stage !=
      OgreNextN1AnalyticSkyFailureStage::NONE;
  bool hdr_ui_overlay_control = false;
  std::unique_ptr<Ogre::v1::OverlaySystem> hdr_overlay_system;
  Ogre::v1::Overlay *hdr_overlay = nullptr;
  Ogre::v1::OverlayContainer *hdr_overlay_panel = nullptr;
  bool hdr_overlay_listener_registered = false;
#endif
  // The scene manager's single overlay render-queue listener, shared by the
  // production HUD panel and the GUI-only menu panel. Frontend-lifetime: it is
  // created on first use and retired in CleanupBackend, never with either
  // panel runtime.
  std::unique_ptr<Ogre::v1::OverlaySystem> overlay_system;
  bool overlay_listener_registered = false;
  // Production transported menu/HUD overlay composited by the HdrRenderUi
  // node. Created with the persistent HDR compositor resources; shown only
  // while a validated snapshot enables its HUD reference.
  Ogre::v1::Overlay *hud_overlay = nullptr;
  Ogre::v1::OverlayContainer *hud_overlay_panel = nullptr;
  Ogre::HlmsUnlitDatablock *hud_overlay_datablock = nullptr;
  bool hud_overlay_datablock_created = false;
  // Exact texture reference (id + revision) currently bound to the panel
  // datablock. Rebinding happens only when this reference changes.
  RenderAssetReference hud_overlay_bound_texture;
  // Scene-free GUI-only presentation. The image is frontend-private: it holds
  // no RenderAssetId and never appears in the registry, so a GUI-only present
  // cannot advance or observe portable asset lineage.
  Ogre::v1::Overlay *menu_overlay = nullptr;
  Ogre::v1::OverlayContainer *menu_overlay_panel = nullptr;
  Ogre::HlmsUnlitDatablock *menu_overlay_datablock = nullptr;
  bool menu_overlay_datablock_created = false;
  Ogre::TextureGpu *menu_overlay_texture = nullptr;
  std::uint32_t menu_overlay_texture_width = 0U;
  std::uint32_t menu_overlay_texture_height = 0U;
  /// Content hash of the pixels currently resident in menu_overlay_texture.
  /// Zero means "allocated but never uploaded"; the request contract rejects
  /// zero, so an unchanged GUI performs no upload and no allocation.
  std::uint64_t menu_overlay_texture_content_hash = 0U;
  bool menu_overlay_texture_bound = false;
  Ogre::TextureGpu *menu_window_texture = nullptr;
  Ogre::CompositorWorkspace *menu_workspace = nullptr;
  bool menu_node_definition_created = false;
  bool menu_workspace_definition_created = false;
};

OgreNextN1Frontend::OgreNextN1Frontend(
    OgreNextN1Configuration configuration)
    : OgreNextN1Frontend(std::move(configuration),
                         OgreNextNativeFeatureTier::RASTER_N1) {}

OgreNextN1Frontend::OgreNextN1Frontend(
    OgreNextN1Configuration configuration,
    OgreNextNativeFeatureTier native_feature_tier)
    : impl_(std::make_unique<Impl>(std::move(configuration))) {
  impl_->native_feature_tier = native_feature_tier;
}

OgreNextN1Frontend::~OgreNextN1Frontend() {
  if (impl_) {
    static_cast<void>(impl_->CleanupBackend());
  }
}

FrontendCapabilityReport OgreNextN1Frontend::QueryCapabilities() const {
  return impl_->Capabilities();
}

OgreNextN1TextureAllocationAudit
OgreNextN1Frontend::QueryTextureAllocationAudit() const noexcept {
  return impl_->TextureAllocationAudit();
}

OgreNextN1PbsUv0AffineState
OgreNextN1Frontend::QueryPbsUv0AffineState(
    RenderAssetReference material) const noexcept {
  return impl_->PbsUv0AffineState(material);
}

OgreNextReflectionProbeAudit
OgreNextN1Frontend::QueryReflectionProbeAudit() const noexcept {
  return impl_->reflection_probe_runtime
             ? impl_->reflection_probe_runtime->QueryAudit()
             : OgreNextReflectionProbeAudit{};
}

OgreNextN1ParticleRuntimeAudit
OgreNextN1Frontend::QueryParticleRuntimeAudit() const noexcept {
  OgreNextN1ParticleRuntimeAudit audit = impl_->particle_runtime.audit();
  audit.native_batch_creates = impl_->particle_native_batch_creates;
  audit.native_batch_destroys = impl_->particle_native_batch_destroys;
  audit.native_particles_submitted =
      impl_->particle_native_particles_submitted;
  audit.native_state_readbacks = 0U;
  audit.native_state_verifications =
      impl_->particle_native_state_verifications;
  return audit;
}

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
OgreNextN1NormalUploadAudit
OgreNextN1Frontend::QueryNormalUploadAudit() const noexcept {
  return impl_->NormalUploadAudit();
}

OgreNextN1DisplayDomainUploadAudit
OgreNextN1Frontend::QueryDisplayDomainUploadAudit() const {
  return impl_->DisplayDomainUploadAudit();
}

OgreNextReflectionProbeCaptureEvidence
OgreNextN1Frontend::QueryReflectionProbeCaptureEvidence() const {
  return impl_->reflection_probe_runtime
             ? impl_->reflection_probe_runtime->QueryLastCaptureEvidence()
             : OgreNextReflectionProbeCaptureEvidence{};
}

OgreNextReflectionProbeNativeOwnershipEvidence
OgreNextN1Frontend::QueryReflectionProbeNativeOwnershipEvidence() const
    noexcept {
  return impl_->reflection_probe_runtime
             ? impl_->reflection_probe_runtime->QueryNativeOwnershipEvidence()
             : OgreNextReflectionProbeNativeOwnershipEvidence{};
}

OgreNextHdrLightingSplitContentEvidence
OgreNextN1Frontend::CaptureHdrLightingSplitContentEvidence() {
  return impl_->CaptureHdrLightingSplitContentEvidence();
}

OgreNextSunVisibilityV2ContentEvidence
OgreNextN1Frontend::CaptureSunVisibilityV2ContentEvidence() {
  return impl_->CaptureSunVisibilityV2ContentEvidence();
}
#endif

OgreNextPssmShadowRuntimeAudit
OgreNextN1Frontend::QueryDirectionalShadowAudit() const noexcept {
  return impl_->DirectionalShadowAudit();
}

OgreNextHdrCompositorAudit
OgreNextN1Frontend::QueryHdrCompositorAudit() const noexcept {
  return impl_->HdrCompositorAudit();
}

OgreNextNativeLightingPassAudit
OgreNextN1Frontend::QueryNativeLightingPassAudit() const noexcept {
  return impl_->NativeLightingPassAudit();
}

OgreNextN1PresentationAudit
OgreNextN1Frontend::QueryPresentationAudit() const noexcept {
  return impl_->PresentationAudit();
}

OgreNextRetainedSceneAudit
OgreNextN1Frontend::QueryRetainedSceneAudit() const noexcept {
  return impl_->retained_audit;
}

OgreNextN1RenderBoundaryDegradeAudit
OgreNextN1Frontend::QueryRenderBoundaryDegradeAudit() const noexcept {
  return impl_->degrade_audit;
}

OgreNextAnalyticSkyRuntimeAudit
OgreNextN1Frontend::QueryAnalyticSkyAudit() const noexcept {
  return impl_->analytic_sky_audit;
}

RenderOperationResult OgreNextN1Frontend::Initialize(
    const FrontendInitializationRequest &request) {
  if (impl_->initialized) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "Ogre-Next N1 is already initialized");
  }
  if (!IsKnownOgreNextRasterFeatureTier(impl_->raster_feature_tier)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "unknown Ogre-Next raster feature tier");
  }
  const ValidationResult shadow_configuration =
      ValidateOgreNextPssmInitialization(impl_->raster_feature_tier,
                                         impl_->directional_shadow_mode);
  if (!shadow_configuration) {
    return OgreNextN1OperationFromValidation(shadow_configuration);
  }
  if ((UsesMetalDirectionalHardShadow(impl_->native_feature_tier) ||
       UsesMetalSunVisibilityV2(impl_->native_feature_tier)) &&
      impl_->directional_shadow_mode !=
          OgreNextDirectionalShadowMode::DISABLED) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        UsesMetalSunVisibilityV2(impl_->native_feature_tier)
            ? "sun-visibility V2 requires unoccluded SunDirectHdr and rejects PSSM before frontend initialization"
            : "native N4 and PSSM directional shadows are mutually exclusive; select the validated fallback before frontend initialization");
  }
  if (!TryResolveNativeVertexLayout(
          impl_->raster_feature_tier, impl_->native_feature_tier,
          impl_->native_vertex_layout)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "Ogre-Next raster/native feature tiers have no reviewed exact vertex-layout contract");
  }
  if (impl_->hdr_enabled &&
      (impl_->raster_feature_tier !=
           OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1 ||
       (impl_->native_feature_tier !=
            OgreNextNativeFeatureTier::RASTER_N1 &&
        !UsesMetalSunVisibilityV2(impl_->native_feature_tier)))) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "the persistent HDR compositor requires RT4/V1 raster or the exact four-image sun-visibility V2 interop tier");
  }
  const bool single_scene_hdr_pssm = impl_->SingleSceneHdrPssmEnabled();
  if ((!impl_->hdr_enabled &&
       impl_->hdr_scene_topology !=
           OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2) ||
      (impl_->hdr_enabled &&
       impl_->directional_shadow_mode ==
           OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1 &&
       !single_scene_hdr_pssm) ||
      (single_scene_hdr_pssm &&
       (impl_->directional_shadow_mode !=
            OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1 ||
        UsesMetalSunVisibilityV2(impl_->native_feature_tier)))) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "HDR/PSSM requires the dedicated single-evaluation RT4 scene topology; the split and native-visibility topologies remain separate evidence paths");
  }
  const RenderOperationResult presentation_configuration =
      ValidatePresentationConfiguration(impl_->presentation_configuration,
                                        request);
  if (!presentation_configuration) {
    return presentation_configuration;
  }
  if (impl_->presentation_configuration.enabled &&
      ((impl_->native_feature_tier != OgreNextNativeFeatureTier::RASTER_N1 &&
        !UsesMetalSunVisibilityV2(impl_->native_feature_tier)) ||
       (impl_->hdr_enabled &&
        (impl_->presentation_configuration.mode !=
             OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP ||
         !impl_->presentation_configuration.gpu_only_output)))) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "HDR native presentation requires the raster or sun-visibility V2 production run loop with GPU-only output");
  }
  if (UsesMetalSunVisibilityV2(impl_->native_feature_tier) &&
      (!impl_->hdr_enabled || !impl_->presentation_configuration.enabled ||
       impl_->presentation_configuration.mode !=
           OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP ||
       !impl_->presentation_configuration.gpu_only_output)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "sun-visibility V2 requires persistent HDR and deferred GPU-only native presentation");
  }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  if (impl_->presentation_configuration.enabled &&
      impl_->presentation_configuration.mode ==
          OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP &&
      (impl_->retain_analytic_sky_geometry_content_evidence ||
       impl_->retain_native_lighting_content_evidence)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "production native presentation forbids all optional content readback evidence");
  }
  if (impl_->retain_sun_visibility_v2_content_evidence &&
      !UsesMetalSunVisibilityV2(impl_->native_feature_tier)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "sun-visibility V2 content evidence is available only to its isolated Metal V2 acceptance executable");
  }
#endif
#if !defined(ROR_OGRE_NEXT_N1_METAL)
  if (impl_->native_feature_tier != OgreNextNativeFeatureTier::RASTER_N1) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "Ogre-Next Metal native interop is available only in the macOS Metal target");
  }
#endif
  const ValidationResult validation =
      ValidateOgreNextN1Initialization(
          request, impl_->Capabilities(),
          impl_->presentation_configuration.enabled);
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }
  const RenderOperationResult media_validation = ResolveShaderMediaRoot(
      impl_->configured_shader_media_root, impl_->resolved_shader_media_root);
  if (!media_validation) {
    return media_validation;
  }
  const RenderOperationResult media_integrity =
      VerifyOgreNextN1ShaderMedia(impl_->resolved_shader_media_root);
  if (!media_integrity) {
    return media_integrity;
  }
  if (impl_->raster_feature_tier ==
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
    const RenderOperationResult hdr_media_integrity =
        VerifyOgreNextN1HdrMedia(impl_->resolved_shader_media_root);
    if (!hdr_media_integrity) {
      return hdr_media_integrity;
    }
    const RenderOperationResult reflection_media_integrity =
        VerifyOgreNextReflectionProbeMedia(
            impl_->resolved_shader_media_root, true);
    if (!reflection_media_integrity) {
      return reflection_media_integrity;
    }
  }
  if (impl_->presentation_configuration.enabled) {
    const RenderOperationResult presentation_media_integrity =
        VerifyOgreNextN1PresentationMedia(
            impl_->presentation_configuration.shader_media_root);
    if (!presentation_media_integrity) {
      return presentation_media_integrity;
    }
  }
  if (!TryClaimOgreNextN1Root()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "another Ogre-Next N1 frontend owns Ogre's process-global Root");
  }
  impl_->lighting_audit = {};
  impl_->owns_root_claim = true;
  const auto fail_after_cleanup = [&](RenderOperationResult failure) {
    if (!impl_->CleanupBackend()) {
      return NativeTeardownFailure("Ogre-Next N1 initialization rollback");
    }
    return failure;
  };

  if (impl_->hdr_enabled) {
    const ValidationResult hdr_configuration =
        impl_->hdr_temporal_state.Initialize(impl_->hdr_configuration);
    if (!hdr_configuration) {
      return fail_after_cleanup(OgreNextN1OperationFromValidation(
          hdr_configuration));
    }
  }

  try {
    impl_->plugin = std::make_unique<N1RendererPlugin>();
    impl_->root = std::make_unique<Ogre::Root>(
        &impl_->abi_cookie, "", "", "", "RoR Ogre-Next N1");
    impl_->root->installPlugin(impl_->plugin.get(), nullptr);
    impl_->renderer =
        impl_->root->getRenderSystemByName(ROR_OGRE_NEXT_N1_RENDERER_NAME);
    if (impl_->renderer == nullptr) {
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "reviewed Ogre-Next renderer did not register"));
    }
    impl_->root->setRenderSystem(impl_->renderer);
    const RenderOperationResult archive_validation =
        ValidateShaderArchives(impl_->resolved_shader_media_root);
    if (!archive_validation) {
      return fail_after_cleanup(archive_validation);
    }
    const Ogre::ConfigOptionMap options = impl_->renderer->getConfigOptions();
    if (options.find("sRGB Gamma Conversion") != options.end()) {
      impl_->renderer->setConfigOption("sRGB Gamma Conversion", "Yes");
    }
    if (impl_->presentation_configuration.enabled) {
      for (std::size_t index = 0U;
           index < impl_->presentation_configuration.renderer_option_count;
           ++index) {
        const OgreNextN1PresentationParameter &option =
            impl_->presentation_configuration.renderer_options[index];
        if (options.find(option.name) == options.end()) {
          return fail_after_cleanup(RenderOperationResult::Failure(
              RenderOperationCode::BACKEND_FAILURE,
              "reviewed presentation renderer option is unavailable: " +
                  option.name));
        }
        impl_->renderer->setConfigOption(option.name, option.value);
      }
    }
    impl_->root->initialise(false);
    if (!impl_->presentation_configuration.enabled) {
      Ogre::NameValuePairList window_parameters;
      window_parameters["hidden"] = "true";
      window_parameters["gamma"] = "true";
      window_parameters["FSAA"] = "1";
      impl_->bootstrap_window = impl_->root->createRenderWindow(
          "RoR Ogre-Next N1 bootstrap", 64U, 64U, false,
          &window_parameters);
    } else {
      if (impl_->presentation_configuration
              .bootstrap_window_parameter_count != 0U) {
        Ogre::NameValuePairList bootstrap_parameters = ToOgreParameters(
            impl_->presentation_configuration.bootstrap_window_parameters,
            impl_->presentation_configuration
                .bootstrap_window_parameter_count);
        impl_->bootstrap_window = impl_->root->createRenderWindow(
            "RoR Ogre-Next N1 null bootstrap", 64U, 64U, false,
            &bootstrap_parameters);
      }
      Ogre::NameValuePairList presentation_parameters = ToOgreParameters(
          impl_->presentation_configuration.presentation_window_parameters,
          impl_->presentation_configuration.presentation_window_parameter_count);
      impl_->presentation_window = impl_->root->createRenderWindow(
          "RoR Ogre-Next N1 native presentation", request.initial_width,
          request.initial_height, false, &presentation_parameters);
    }
    if ((!impl_->presentation_configuration.enabled &&
         impl_->bootstrap_window == nullptr) ||
        (impl_->presentation_configuration.enabled &&
         impl_->presentation_window == nullptr) ||
        impl_->root->getCompositorManager2() == nullptr) {
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "Ogre-Next did not initialize its hidden/null Compositor2 device"));
    }
    if (impl_->presentation_configuration.enabled) {
      const RenderOperationResult exact_extent =
          impl_->RefreshPresentationWindowExtent(
              request.initial_width, request.initial_height, false);
      if (!exact_extent) {
        return fail_after_cleanup(exact_extent);
      }
      const RenderOperationResult presentation_resources =
          impl_->CreatePresentationResourceGroup();
      if (!presentation_resources) {
        return fail_after_cleanup(presentation_resources);
      }
    }
    const Ogre::RenderSystemCapabilities *device_capabilities =
        impl_->renderer->getCapabilities();
    const std::uint32_t device_maximum_texture_dimension =
        device_capabilities != nullptr
            ? device_capabilities->getMaximumResolution2D()
            : 0U;
    if (device_maximum_texture_dimension == 0U) {
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "Ogre-Next did not report a usable 2D texture limit"));
    }
    if (request.initial_width > device_maximum_texture_dimension ||
        request.initial_height > device_maximum_texture_dimension) {
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "initial extent exceeds the initialized Ogre-Next device limit"));
    }
    impl_->maximum_texture_dimension = device_maximum_texture_dimension;
    impl_->maximum_anisotropy =
        std::max(1.0F, static_cast<float>(
                           device_capabilities->getMaxSupportedAnisotropy()));
    if (impl_->directional_shadow_mode ==
        OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1) {
      Ogre::TextureGpuManager *texture_manager =
          impl_->renderer->getTextureGpuManager();
      if (texture_manager == nullptr) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "Ogre-Next did not expose a texture manager for the PSSM capability gate"));
      }
      impl_->shadow_audit.capability_check_completed = true;
      impl_->shadow_audit.observed_maximum_texture_dimension =
          impl_->maximum_texture_dimension;
      impl_->shadow_audit.atlas_dimensions_supported =
          impl_->maximum_texture_dimension >= kOgreNextPssmAtlasWidth &&
          impl_->maximum_texture_dimension >= kOgreNextPssmAtlasHeight;
      impl_->shadow_audit.texture_gather_supported =
          device_capabilities->hasCapability(Ogre::RSC_TEXTURE_GATHER);
      if (!impl_->shadow_audit.atlas_dimensions_supported ||
          !impl_->shadow_audit.texture_gather_supported) {
        // These are the only reviewed unsupported classifications. Prove that
        // no capability-probe allocation exists even though the native D32
        // transaction was deliberately not attempted.
        if (texture_manager->findTextureNoThrow(
                Ogre::IdString("RoRPssmD32AtlasCapabilityProbe")) != nullptr) {
          return fail_after_cleanup(RenderOperationResult::Failure(
              RenderOperationCode::BACKEND_FAILURE,
              "Ogre-Next retained a PSSM D32 capability-probe allocation "
              "before admission"));
        }
        impl_->shadow_audit.d32_atlas_cleanup_verified = true;
        impl_->shadow_audit.d32_atlas_cleanup_absence_checks = 1U;
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            kOgreNextPssmCapabilityUnsupportedDetail));
      }
      const PssmD32AtlasProbeResult d32_probe = ProbePssmD32Atlas(
          *texture_manager
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
          ,
          impl_->pssm_failure_stage, impl_->pssm_failure_pending,
          impl_->retain_native_lighting_content_evidence,
          impl_->LightingContentReadbackCounter()
#endif
      );
      impl_->shadow_audit.d32_probe_attempted = d32_probe.attempted;
      impl_->shadow_audit.d32_render_target_supported = d32_probe.supported;
      impl_->shadow_audit.d32_atlas_allocation_verified =
          d32_probe.allocation_verified;
      impl_->shadow_audit.d32_atlas_readback_verified =
          d32_probe.readback_verified;
      impl_->shadow_audit.d32_atlas_cleanup_verified =
          d32_probe.cleanup_verified;
      impl_->shadow_audit.d32_atlas_cleanup_absence_checks =
          d32_probe.cleanup_absence_checks;
      if (!impl_->shadow_audit.d32_probe_attempted ||
          !impl_->shadow_audit.d32_render_target_supported) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "Ogre-Next PSSM D32 native capability transaction was incomplete"));
      }
    }
#if defined(ROR_OGRE_NEXT_N1_METAL)
    if (impl_->native_feature_tier !=
        OgreNextNativeFeatureTier::RASTER_N1) {
      const RenderOperationResult interop_result = CreateOgreNextMetalInterop(
          reinterpret_cast<std::uintptr_t>(impl_->renderer),
          impl_->native_feature_tier,
          impl_->native_interop);
      if (!interop_result) {
        return fail_after_cleanup(interop_result);
      }
      if (UsesMetalSunVisibilityV2(impl_->native_feature_tier) &&
          dynamic_cast<OgreNextSunVisibilityV2NativeInterop *>(
              impl_->native_interop.get()) == nullptr) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "Metal interop bridge did not expose the required sun-visibility V2 transaction"));
      }
    }
#endif
    impl_->pbs = RegisterPbs(*impl_->root, impl_->resolved_shader_media_root);
    if (impl_->directional_shadow_mode ==
        OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1) {
      impl_->pbs->setShadowSettings(Ogre::HlmsPbs::PCF_4x4);
      if (impl_->pbs->getShadowFilter() != Ogre::HlmsPbs::PCF_4x4) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "Ogre-Next substituted the reviewed PCF4 shadow filter"));
      }
    }
    impl_->unlit =
        RegisterUnlit(*impl_->root, impl_->resolved_shader_media_root);
    impl_->scene_manager = impl_->root->createSceneManager(
        Ogre::ST_GENERIC, 1U, "RoROgreNextN1Scene");
    impl_->camera =
        impl_->scene_manager->createCamera("RoROgreNextN1Camera");
    if (impl_->raster_feature_tier ==
        OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
      impl_->reflection_probe_runtime =
          std::make_unique<OgreNextReflectionProbeRuntime>();
      OgreNextReflectionProbeRuntimeConfiguration reflection_configuration;
      reflection_configuration.ogre_root =
          reinterpret_cast<std::uintptr_t>(impl_->root.get());
      reflection_configuration.ogre_scene_manager =
          reinterpret_cast<std::uintptr_t>(impl_->scene_manager);
      reflection_configuration.ogre_hlms_pbs =
          reinterpret_cast<std::uintptr_t>(impl_->pbs);
      reflection_configuration.shader_media_root =
          impl_->resolved_shader_media_root;
      reflection_configuration.maximum_blend_resolution = 2048U;
      // This frontend creates and exclusively owns a fresh HlmsPbs. It keeps
      // Ogre's automatic IBL-mipmap policy and admits no PBSM_REFLECTION or
      // environment texture, which makes resetIblSpecMipmap(0) the canonical
      // PCC-removal recomputation on every backend.
      reflection_configuration.owns_automatic_ibl_mipmap_policy = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      reflection_configuration.retain_capture_evidence =
          impl_->retain_reflection_capture_evidence;
#endif
      const RenderOperationResult reflection_initialization =
          impl_->reflection_probe_runtime->Initialize(
              std::move(reflection_configuration));
      if (!reflection_initialization) {
        return fail_after_cleanup(reflection_initialization);
      }
    }
    if (impl_->hdr_enabled) {
      const RenderOperationResult hdr_compositor = impl_->CreateHdrCompositor(
          request.initial_width, request.initial_height);
      if (!hdr_compositor) {
        return fail_after_cleanup(hdr_compositor);
      }
    }
    impl_->surface.version = request.version;
    impl_->surface.surface_revision = request.initial_surface_revision;
    impl_->surface.pixel_width = request.initial_width;
    impl_->surface.pixel_height = request.initial_height;
    impl_->surface.content_scale = request.initial_content_scale;
    impl_->surface.suspended = false;
    if (impl_->presentation_configuration.enabled) {
      impl_->surface.window = request.window;
      impl_->presentation_audit.enabled = true;
      impl_->presentation_audit.mode =
          impl_->presentation_configuration.mode;
      impl_->presentation_audit.exact_external_window_binding = true;
      impl_->presentation_audit.monotonic_presented_frame_ids = true;
    }
    impl_->owner_thread = std::this_thread::get_id();
    impl_->initialized = true;
    return RenderOperationResult::Success();
  } catch (const std::bad_alloc &) {
    return fail_after_cleanup(RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "Ogre-Next N1 initialization ran out of memory"));
  } catch (const Ogre::Exception &error) {
    return fail_after_cleanup(BackendFailure(error));
  } catch (const std::exception &error) {
    return fail_after_cleanup(RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE, error.what()));
  }
}

RenderOperationResult OgreNextN1Frontend::PresentBootstrapFrame() {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  if (!impl_->ProductionPresentationEnabled()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "scene-free startup presentation requires the production native-window mode");
  }
  if (impl_->surface.suspended) {
    return RenderOperationResult::Failure(
        RenderOperationCode::RESOURCE_STALE,
        "scene-free startup presentation is waiting for an active surface");
  }

  const RenderOperationResult graph =
      impl_->EnsureBootstrapPresentationGraph();
  if (!graph) {
    return graph;
  }
  try {
    if (!impl_->root->renderOneFrame()) {
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "Ogre-Next ended the clear-only bootstrap frame loop");
    }
    const OgreNextN1ParticleRuntimeAudit particles =
        impl_->particle_runtime.audit();
    if (impl_->registry != nullptr ||
        impl_->submission_state.TrackedSnapshotIdentityCount() != 0U ||
        impl_->production_output_handles.live_count() != 0U ||
        impl_->presentation_audit.presented_frames != 0U ||
        particles.committed_source_sequence != 0U ||
        particles.create_commands != 0U || particles.update_commands != 0U ||
        particles.stop_commands != 0U || particles.destroy_commands != 0U) {
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "clear-only bootstrap presentation changed portable renderer state");
    }
    impl_->presentation_audit.bootstrap_clear_only = true;
    impl_->presentation_audit.bootstrap_presented_before_scene = true;
    ++impl_->presentation_audit.bootstrap_clear_passes;
    ++impl_->presentation_audit.bootstrap_render_one_frame_calls;
    ++impl_->presentation_audit.bootstrap_window_swap_completions;
    return RenderOperationResult::Success();
  } catch (const Ogre::Exception &error) {
    impl_->faulted = true;
    return BackendFailure(error);
  } catch (const std::exception &error) {
    impl_->faulted = true;
    return RenderOperationResult::Failure(RenderOperationCode::BACKEND_FAILURE,
                                          error.what());
  }
}

RenderOperationResult OgreNextN1Frontend::PresentUiOverlayFrame(
    const UiOverlayFrameRequest &request) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  const ValidationResult shape = ValidateUiOverlayFrameRequest(request);
  if (!shape) {
    return OgreNextN1OperationFromValidation(shape);
  }
  if (!impl_->ProductionPresentationEnabled()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "scene-free GUI-only presentation requires the production native-window mode");
  }
  if (impl_->HudOverlayControlSelected()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "the isolated UI-overlay negative control owns the overlay runtime");
  }
  if (impl_->surface.suspended) {
    return RenderOperationResult::Failure(
        RenderOperationCode::RESOURCE_STALE,
        "scene-free GUI-only presentation is waiting for an active surface");
  }
  if (impl_->sun_visibility_v2_frame_awaiting_continuation) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUTSTANDING_LEASES,
        "sun-visibility V2 retains its HDR image set until the post-external continuation");
  }
  if (impl_->production_output_handles.live_count() != 0U) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUTSTANDING_LEASES,
        "scene-free GUI-only presentation requires released frame outputs");
  }
  // The image is composited 1:1 over the cleared window; a mismatched extent
  // would silently rescale the GUI, so it fails closed exactly like the
  // transported HUD's extent check does for a scene frame.
  if (request.width != impl_->surface.pixel_width ||
      request.height != impl_->surface.pixel_height) {
    return RenderOperationResult::Failure(
        RenderOperationCode::RESOURCE_STALE,
        "GUI-only overlay extent must equal the presented drawable extent",
        RenderOperationRecovery::RETRY_AFTER_PRESENTATION_SURFACE_UPDATE);
  }

  // Captured BEFORE any native work so the post-condition below proves the
  // GUI-only frame consumed no portable identity, exactly as the clear-only
  // bootstrap present does.
  const OgreNextN1ParticleRuntimeAudit particles_before =
      impl_->particle_runtime.audit();
  const std::uint64_t presented_frames_before =
      impl_->presentation_audit.presented_frames;
  const std::size_t tracked_snapshots_before =
      impl_->submission_state.TrackedSnapshotIdentityCount();
  const RenderAssetRegistry *const registry_before = impl_->registry.get();

  try {
    if (impl_->menu_overlay == nullptr) {
      const RenderOperationResult runtime = impl_->CreateMenuOverlayRuntime();
      if (!runtime) {
        const bool clean = impl_->DestroyMenuOverlayRuntime();
        if (!clean) {
          impl_->faulted = true;
          return NativeTeardownFailure(
              "Ogre-Next GUI-only overlay runtime rollback");
        }
        return runtime;
      }
    }
    const RenderOperationResult image = impl_->EnsureMenuOverlayImage(request);
    if (!image) {
      return image;
    }
    const RenderOperationResult graph = impl_->EnsureMenuPresentationGraph();
    if (!graph) {
      return graph;
    }

    // renderOneFrame() executes every ENABLED workspace. A GUI-only frame must
    // execute exactly one of them, so the scene graphs are suspended for the
    // duration and restored to the exact state they were found in - which is
    // what keeps EnsureProductionPresentationGraph's `exact` fast path valid
    // on the next scene frame instead of forcing a graph rebuild.
    const auto suspend = [](Ogre::CompositorWorkspace *workspace) {
      const bool was_enabled =
          workspace != nullptr && workspace->getEnabled();
      if (was_enabled) {
        workspace->setEnabled(false);
      }
      return was_enabled;
    };
    const bool bootstrap_enabled = suspend(impl_->bootstrap_workspace);
    const bool production_enabled = suspend(impl_->production_workspace);
    const bool hdr_enabled_workspace = suspend(impl_->hdr_workspace);
    const bool hdr_v2_enabled = suspend(impl_->hdr_v2_continuation_workspace);
    bool restored = false;
    const auto restore = [&]() noexcept {
      if (restored) {
        return true;
      }
      restored = true;
      bool clean = true;
      try {
        if (impl_->menu_overlay != nullptr) {
          impl_->menu_overlay->hide();
        }
        if (impl_->menu_workspace != nullptr) {
          impl_->menu_workspace->setEnabled(false);
          clean = !impl_->menu_workspace->getEnabled() && clean;
        }
        if (bootstrap_enabled && impl_->bootstrap_workspace != nullptr) {
          impl_->bootstrap_workspace->setEnabled(true);
          clean = impl_->bootstrap_workspace->getEnabled() && clean;
        }
        if (production_enabled && impl_->production_workspace != nullptr) {
          impl_->production_workspace->setEnabled(true);
          clean = impl_->production_workspace->getEnabled() && clean;
        }
        if (hdr_enabled_workspace && impl_->hdr_workspace != nullptr) {
          impl_->hdr_workspace->setEnabled(true);
          clean = impl_->hdr_workspace->getEnabled() && clean;
        }
        if (hdr_v2_enabled &&
            impl_->hdr_v2_continuation_workspace != nullptr) {
          impl_->hdr_v2_continuation_workspace->setEnabled(true);
          clean =
              impl_->hdr_v2_continuation_workspace->getEnabled() && clean;
        }
      } catch (...) {
        clean = false;
      }
      return clean;
    };

    bool rendered = false;
    try {
      impl_->menu_overlay->show();
      impl_->menu_workspace->setEnabled(true);
      if (!impl_->menu_workspace->getEnabled()) {
        throw std::runtime_error(
            "GUI-only presentation workspace refused activation");
      }
      ++impl_->presentation_audit.ui_overlay_render_one_frame_calls;
      rendered = impl_->root->renderOneFrame();
    } catch (...) {
      if (!restore()) {
        impl_->faulted = true;
      }
      throw;
    }
    if (!restore()) {
      impl_->faulted = true;
      return NativeTeardownFailure(
          "Ogre-Next GUI-only presentation graph restoration");
    }
    if (!rendered) {
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "Ogre-Next ended the GUI-only frame loop");
    }

    const OgreNextN1ParticleRuntimeAudit particles_after =
        impl_->particle_runtime.audit();
    if (impl_->registry.get() != registry_before ||
        impl_->submission_state.TrackedSnapshotIdentityCount() !=
            tracked_snapshots_before ||
        impl_->production_output_handles.live_count() != 0U ||
        impl_->presentation_audit.presented_frames !=
            presented_frames_before ||
        particles_after.committed_source_sequence !=
            particles_before.committed_source_sequence ||
        particles_after.create_commands != particles_before.create_commands ||
        particles_after.update_commands != particles_before.update_commands ||
        particles_after.stop_commands != particles_before.stop_commands ||
        particles_after.destroy_commands !=
            particles_before.destroy_commands) {
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "scene-free GUI-only presentation changed portable renderer state");
    }
    ++impl_->presentation_audit.ui_overlay_presented_frames;
    impl_->presentation_audit.ui_overlay_last_width = request.width;
    impl_->presentation_audit.ui_overlay_last_height = request.height;
    return RenderOperationResult::Success();
  } catch (const Ogre::Exception &error) {
    impl_->faulted = true;
    return BackendFailure(error);
  } catch (const std::bad_alloc &) {
    impl_->faulted = true;
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "scene-free GUI-only presentation ran out of memory");
  } catch (const std::exception &error) {
    impl_->faulted = true;
    return RenderOperationResult::Failure(RenderOperationCode::BACKEND_FAILURE,
                                          error.what());
  }
}

RenderOperationResult OgreNextN1Frontend::UpdateSurface(
    const FrontendSurfaceUpdate &update, bool headless,
    std::uint64_t /*timeout_nanoseconds*/) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  const bool presentation_enabled =
      impl_->presentation_configuration.enabled;
  const bool production_presentation =
      impl_->ProductionPresentationEnabled();
  if (headless == presentation_enabled) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "surface update mode differs from the initialized Ogre-Next presentation mode");
  }
  const ValidationResult validation = ValidateFrontendSurfaceTransition(
      impl_->surface, update, !presentation_enabled, true);
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }
  if (presentation_enabled &&
      !SameNativeWindow(update.window,
                        impl_->presentation_configuration.exact_window)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "Ogre-Next presentation cannot replace its borrowed native window");
  }
  if (update.pixel_width > impl_->maximum_texture_dimension ||
      update.pixel_height > impl_->maximum_texture_dimension) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "surface extent exceeds the initialized Ogre-Next device limit");
  }
  const bool surface_shape_changed =
      update.suspended != impl_->surface.suspended ||
      update.pixel_width != impl_->surface.pixel_width ||
      update.pixel_height != impl_->surface.pixel_height ||
      update.content_scale != impl_->surface.content_scale;
  if (impl_->sun_visibility_v2_frame_awaiting_continuation) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUTSTANDING_LEASES,
        "sun-visibility V2 retains its exact HDR image set and surface revision until the post-external continuation");
  }
  if (presentation_enabled) {
    try {
      const bool retire_production_graph =
          production_presentation && surface_shape_changed;
      if (retire_production_graph &&
          !impl_->DestroyProductionPresentationGraph()) {
        impl_->faulted = true;
        return NativeTeardownFailure(
            "Ogre-Next production presentation surface transition");
      }
      if (surface_shape_changed && impl_->bootstrap_workspace != nullptr &&
          !impl_->DestroyBootstrapPresentationGraph()) {
        impl_->faulted = true;
        return NativeTeardownFailure(
            "Ogre-Next bootstrap presentation surface transition");
      }
      // The GUI-only workspace holds the old window texture as its single
      // external channel, and its private image is allocated at the old
      // extent. Retire both here rather than letting the next GUI-only
      // present observe a superseded drawable.
      if (surface_shape_changed &&
          (impl_->menu_workspace != nullptr ||
           impl_->menu_overlay_texture != nullptr) &&
          !(impl_->DestroyMenuPresentationGraph() &&
            impl_->DestroyMenuOverlayImage())) {
        impl_->faulted = true;
        return NativeTeardownFailure(
            "Ogre-Next GUI-only presentation surface transition");
      }
      if (update.suspended) {
        if (impl_->presentation_window == nullptr) {
          impl_->faulted = true;
          return RenderOperationResult::Failure(
              RenderOperationCode::BACKEND_FAILURE,
              "presentation window disappeared before suspend acknowledgement");
        }
        impl_->presentation_window->windowMovedOrResized();
        ++impl_->presentation_audit.window_moved_or_resized_calls;
      } else {
        const RenderOperationResult extent =
            impl_->RefreshPresentationWindowExtent(
                update.pixel_width, update.pixel_height, true);
        if (!extent) {
          impl_->faulted = true;
          return extent;
        }
        if (production_presentation &&
            impl_->production_workspace != nullptr &&
            impl_->presentation_window->getTexture() !=
                impl_->production_window_texture &&
            !impl_->DestroyProductionPresentationGraph()) {
          impl_->faulted = true;
          return NativeTeardownFailure(
              "Ogre-Next production drawable-texture transition");
        }
        if (impl_->bootstrap_workspace != nullptr &&
            impl_->presentation_window->getTexture() !=
                impl_->bootstrap_window_texture &&
            !impl_->DestroyBootstrapPresentationGraph()) {
          impl_->faulted = true;
          return NativeTeardownFailure(
              "Ogre-Next bootstrap drawable-texture transition");
        }
      }
    } catch (const Ogre::Exception &error) {
      impl_->faulted = true;
      return BackendFailure(error);
    } catch (const std::exception &error) {
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE, error.what());
    }
  }
  const bool rebuild_hdr =
      impl_->hdr_enabled && !update.suspended &&
      (update.pixel_width != impl_->hdr_width ||
       update.pixel_height != impl_->hdr_height);
  if (rebuild_hdr) {
    // Keep the parsed HDR resources and immutable node/workspace definitions
    // live across a drawable-size change. Ogre's script managers are
    // process-global and do not support reparsing the same named definitions
    // into one Root. Only runtime textures/workspace are rebuilt.
    if (!impl_->DestroyHdrCompositor(false)) {
      impl_->faulted = true;
      return NativeTeardownFailure(
          "Ogre-Next HDR surface-transition teardown");
    }
    const RenderOperationResult rebuilt = impl_->CreateHdrCompositor(
        update.pixel_width, update.pixel_height);
    if (!rebuilt) {
      const bool clean = impl_->DestroyHdrCompositor();
      impl_->faulted = true;
      return clean ? rebuilt
                   : NativeTeardownFailure(
                         "Ogre-Next HDR surface-transition rollback");
    }
  }
  if (production_presentation) {
    if (update.suspended) {
      ++impl_->presentation_audit.suspended_surface_updates;
    } else if (impl_->surface.suspended) {
      ++impl_->presentation_audit.restored_surface_updates;
    }
  }
  impl_->surface = update;
  return RenderOperationResult::Success();
}

RenderOperationResult
OgreNextN1Frontend::SynchronizeAssets(const RenderAssetDelta &delta) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  if (impl_->sun_visibility_v2_frame_awaiting_continuation) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUTSTANDING_LEASES,
        "sun-visibility V2 cannot mutate its asset catalog before the post-external continuation");
  }
  struct PendingMeshAllocation final {
    Impl *owner = nullptr;
    Impl::NativeMesh native;
    bool owns = true;

    ~PendingMeshAllocation() {
      if (owns && !owner->DestroyMesh(native)) {
        owner->faulted = true;
      }
    }
  };
  struct PendingMaterialAllocation final {
    Impl *owner = nullptr;
    Impl::NativeMaterial native;
    bool owns = true;

    ~PendingMaterialAllocation() {
      if (owns && !owner->DestroyMaterial(native)) {
        owner->faulted = true;
      }
    }
  };
  struct PendingTextureAllocation final {
    Impl *owner = nullptr;
    Impl::NativeTexture native;
    bool owns = true;

    ~PendingTextureAllocation() {
      if (owns && !owner->DestroyTexture(native)) {
        owner->faulted = true;
      }
    }
  };
  try {
    std::unique_ptr<RenderAssetRegistry> candidate =
        impl_->registry
            ? std::make_unique<RenderAssetRegistry>(*impl_->registry)
            : std::make_unique<RenderAssetRegistry>(delta.registry_id);
    ValidationResult validation = candidate->Apply(delta);
    if (!validation) {
      return OgreNextN1OperationFromValidation(validation);
    }
    validation = ValidateOgreNextN1AssetCatalog(
        *candidate, impl_->Capabilities().supports_dynamic_mesh_updates,
        impl_->raster_feature_tier);
    if (!validation) {
      return OgreNextN1OperationFromValidation(validation);
    }
    const RenderOperationResult sampler_device_validation =
        impl_->ValidateSamplerDeviceLimits(*candidate);
    if (!sampler_device_validation) {
      return sampler_device_validation;
    }

    std::map<RenderAssetId, Impl::NativeMesh> candidate_meshes;
    std::map<RenderAssetId, Impl::NativeMaterial> candidate_materials;
    std::map<RenderAssetId, Impl::NativeTexture> candidate_textures;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    bool has_texture_role_transition = false;
#endif
    try {
      std::map<RenderAssetId, ReferencedTextureUsage> referenced_textures;
      ValidationResult visit = candidate->VisitRecords(
          [&](const RenderAssetRecord &record) {
            const auto *material =
                std::get_if<MaterialDescriptor>(record.payload.get());
            if (!record.live() || material == nullptr ||
                impl_->raster_feature_tier !=
                    OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
              return ValidationResult::Success();
            }
            struct BindingUsage final {
              const TextureBinding *binding;
              NativeTextureUsage usage;
            };
            const bool display_domain_base =
                material->base_color_transfer ==
                BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE;
            const BindingUsage bindings[] = {
                {&material->base_color_texture,
                 {!display_domain_base, display_domain_base, false, false,
                  false, false}},
                {&material->metallic_roughness_texture,
                 {false, false, false, true, true, false}},
                {&material->normal_texture,
                 {false, false, false, false, false, true}},
                {&material->emissive_texture,
                 {true, false, false, false, false, false}},
                {&material->specular_texture,
                 {false, false, true, false, false, false}},
                // Detail albedo composites with the base color, so it decodes
                // exactly like an ordinary sRGB sampled map. The weight mask
                // selects layers and must stay linear.
                {&material->detail_weight_texture,
                 {false, false, true, false, false, false}},
                {&material->detail_textures[0],
                 {true, false, false, false, false, false}},
                {&material->detail_textures[1],
                 {true, false, false, false, false, false}},
                {&material->detail_textures[2],
                 {true, false, false, false, false, false}},
                {&material->detail_textures[3],
                 {true, false, false, false, false, false}},
            };
            for (const BindingUsage &binding_usage : bindings) {
              const TextureBinding &binding = *binding_usage.binding;
              if (!binding.texture.valid()) {
                continue;
              }
              const auto inserted = referenced_textures.emplace(
                  binding.texture.id,
                  ReferencedTextureUsage{binding.texture,
                                         binding_usage.usage});
              if (!inserted.second) {
                ReferencedTextureUsage &existing = inserted.first->second;
                if (existing.asset != binding.texture) {
                  return ValidationResult::Failure(
                      ValidationCode::REVISION_MISMATCH,
                      "assets.material.texture_binding",
                      "RT4/V1 aliases one texture ID through different revisions");
                }
                existing.usage.sampled_rgba =
                    existing.usage.sampled_rgba ||
                    binding_usage.usage.sampled_rgba;
                existing.usage.display_domain_rgba =
                    existing.usage.display_domain_rgba ||
                    binding_usage.usage.display_domain_rgba;
                existing.usage.linear_rgba =
                    existing.usage.linear_rgba ||
                    binding_usage.usage.linear_rgba;
                existing.usage.roughness_g =
                    existing.usage.roughness_g ||
                    binding_usage.usage.roughness_g;
                existing.usage.metallic_b =
                    existing.usage.metallic_b ||
                    binding_usage.usage.metallic_b;
                existing.usage.normal_rg =
                    existing.usage.normal_rg ||
                    binding_usage.usage.normal_rg;
                if ((existing.usage.sampled_rgba &&
                     (existing.usage.display_domain_rgba ||
                      existing.usage.linear_rgba ||
                      existing.usage.roughness_g ||
                      existing.usage.metallic_b ||
                      existing.usage.normal_rg)) ||
                    (existing.usage.display_domain_rgba &&
                     (existing.usage.linear_rgba ||
                      existing.usage.roughness_g ||
                      existing.usage.metallic_b ||
                      existing.usage.normal_rg)) ||
                    (existing.usage.linear_rgba &&
                     (existing.usage.roughness_g ||
                      existing.usage.metallic_b ||
                      existing.usage.normal_rg)) ||
                    (existing.usage.normal_rg &&
                     (existing.usage.roughness_g ||
                      existing.usage.metallic_b))) {
                  return ValidationResult::Failure(
                      ValidationCode::UNSUPPORTED_FEATURE,
                      "assets.material.texture_binding",
                      "RT4/V1 rejects aliases among decode-before-filter sRGB, display-domain UNORM, authored linear RGBA, packed linear, and canonical normal texture roles");
                }
              }
            }
            return ValidationResult::Success();
          });
      if (!visit) {
        return OgreNextN1OperationFromValidation(visit);
      }

      visit = candidate->VisitRecords([&](const RenderAssetRecord &record) {
        const auto referenced = referenced_textures.find(record.asset.id);
        if (!record.live() || record.asset.kind != RenderAssetKind::TEXTURE ||
            referenced == referenced_textures.end() ||
            referenced->second.asset != record.asset) {
          return ValidationResult::Success();
        }
        const auto existing = impl_->textures.find(record.asset.id);
        if (existing != impl_->textures.end() &&
            existing->second.asset == record.asset &&
            existing->second.usage == referenced->second.usage) {
          candidate_textures.emplace(record.asset.id, existing->second);
        } else {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
          has_texture_role_transition =
              has_texture_role_transition ||
              (existing != impl_->textures.end() &&
               existing->second.asset == record.asset &&
               existing->second.usage != referenced->second.usage);
#endif
          PendingTextureAllocation created{
              impl_.get(),
              impl_->CreateTexture(
                  record.asset,
                  std::get<TextureResourceDescriptor>(*record.payload),
                  referenced->second.usage),
              true};
          candidate_textures.emplace(record.asset.id, created.native);
          created.owns = false;
        }
        return ValidationResult::Success();
      });
      if (!visit) {
        throw std::logic_error("RT4/V1 texture catalog visitation failed");
      }
      if (!candidate_textures.empty()) {
        impl_->renderer->getTextureGpuManager()->waitForStreamingCompletion();
      }
      for (const auto &entry : candidate_textures) {
        const TextureResourceDescriptor *descriptor =
            candidate->ResolveTexture(entry.second.asset);
        if (descriptor == nullptr) {
          throw std::logic_error(
              "RT4/V1 texture disappeared before upload verification");
        }
        const auto usage = referenced_textures.find(entry.first);
        if (usage == referenced_textures.end() ||
            usage->second.asset != entry.second.asset) {
          throw std::logic_error(
              "RT4/V1 texture usage disappeared before upload verification");
        }
        impl_->VerifyTexture(entry.second, *descriptor,
                             usage->second.usage);
      }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      if (has_texture_role_transition) {
        impl_->MaybeInjectTextureUploadFailure(
            OgreNextN1TextureUploadFailureStage::
                AFTER_ROLE_TRANSITION_CANDIDATE_TEXTURES);
      }
#endif

      visit = candidate->VisitRecords([&](const RenderAssetRecord &record) {
        if (!record.live() || record.asset.kind != RenderAssetKind::MESH) {
          return ValidationResult::Success();
        }
        const auto existing = impl_->meshes.find(record.asset.id);
        if (existing != impl_->meshes.end() &&
            existing->second.asset == record.asset) {
          candidate_meshes.emplace(record.asset.id, existing->second);
        } else {
          PendingMeshAllocation created{
              impl_.get(),
              impl_->CreateMesh(
                  record.asset,
                  std::get<MeshResourceDescriptor>(*record.payload)),
              true};
          candidate_meshes.emplace(record.asset.id, created.native);
          created.owns = false;
        }
        return ValidationResult::Success();
      });
      if (!visit) {
        throw std::logic_error("N1 mesh catalog visitation failed");
      }

      visit = candidate->VisitRecords([&](const RenderAssetRecord &record) {
        if (!record.live() || record.asset.kind != RenderAssetKind::MATERIAL) {
          return ValidationResult::Success();
        }
        const auto existing = impl_->materials.find(record.asset.id);
        if (existing != impl_->materials.end() &&
            existing->second.asset == record.asset) {
          candidate_materials.emplace(record.asset.id, existing->second);
        } else {
          PendingMaterialAllocation created{
              impl_.get(),
              impl_->CreateMaterial(
                  record.asset,
                  std::get<MaterialDescriptor>(*record.payload), *candidate,
                  candidate_textures),
              true};
          candidate_materials.emplace(record.asset.id, created.native);
          created.owns = false;
        }
        return ValidationResult::Success();
      });
      if (!visit) {
        throw std::logic_error("N1 material catalog visitation failed");
      }
    } catch (...) {
      if (!impl_->RollbackCandidateAllocations(candidate_meshes,
                                               candidate_materials,
                                               candidate_textures)) {
        impl_->faulted = true;
      }
      throw;
    }

    if (impl_->native_interop) {
      const RenderOperationResult discard =
          impl_->native_interop->DiscardPublishedFrame();
      if (!discard) {
        if (!impl_->RollbackCandidateAllocations(candidate_meshes,
                                                 candidate_materials,
                                                 candidate_textures)) {
          impl_->faulted = true;
          return NativeTeardownFailure("Ogre-Next N2 asset rollback");
        }
        return discard;
      }
      if (!impl_->DestroyFrameMeshes()) {
        static_cast<void>(impl_->RollbackCandidateAllocations(
            candidate_meshes, candidate_materials, candidate_textures));
        impl_->faulted = true;
        return NativeTeardownFailure("Ogre-Next N2 frame geometry retirement");
      }
    }

    if (!impl_->UnbindHudOverlayTextureBeforeAssetReplacement()) {
      static_cast<void>(impl_->RollbackCandidateAllocations(
          candidate_meshes, candidate_materials, candidate_textures));
      impl_->faulted = true;
      return NativeTeardownFailure("Ogre-Next HUD overlay texture unbind");
    }
    // Retained instances referencing an asset the retire loops below will
    // destroy or replace must die first: a datablock or mesh must never be
    // destroyed while a live Item still links to it. If a later rollback
    // keeps the old assets live, the destroyed instances stay destroyed and
    // the next present's diff recreates them against the old catalog.
    if (!impl_->DestroyRetainedInstancesForAssetReplacement(
            candidate_meshes, candidate_materials)) {
      static_cast<void>(impl_->RollbackCandidateAllocations(
          candidate_meshes, candidate_materials, candidate_textures));
      impl_->faulted = true;
      return NativeTeardownFailure("Ogre-Next retained scene asset unbind");
    }
    bool retired_cleanly = true;
    for (auto &entry : impl_->materials) {
      const auto replacement = candidate_materials.find(entry.first);
      if (replacement == candidate_materials.end() ||
          replacement->second.asset != entry.second.asset) {
        retired_cleanly =
            impl_->DestroyMaterial(entry.second) && retired_cleanly;
      }
    }
    if (!retired_cleanly) {
      static_cast<void>(impl_->RollbackCandidateAllocations(
          candidate_meshes, candidate_materials, candidate_textures));
      impl_->faulted = true;
      return NativeTeardownFailure("Ogre-Next N1 material replacement");
    }
    for (auto &entry : impl_->textures) {
      const auto replacement = candidate_textures.find(entry.first);
      if (replacement == candidate_textures.end() ||
          replacement->second.asset != entry.second.asset ||
          replacement->second.usage != entry.second.usage) {
        retired_cleanly =
            impl_->DestroyTexture(entry.second) && retired_cleanly;
      }
    }
    if (!retired_cleanly) {
      static_cast<void>(impl_->RollbackCandidateAllocations(
          candidate_meshes, candidate_materials, candidate_textures));
      impl_->faulted = true;
      return NativeTeardownFailure("Ogre-Next RT4/V1 texture replacement");
    }
    for (auto &entry : impl_->meshes) {
      const auto replacement = candidate_meshes.find(entry.first);
      if (replacement == candidate_meshes.end() ||
          replacement->second.asset != entry.second.asset) {
        retired_cleanly = impl_->DestroyMesh(entry.second) && retired_cleanly;
      }
    }
    if (!retired_cleanly) {
      static_cast<void>(impl_->RollbackCandidateAllocations(
          candidate_meshes, candidate_materials, candidate_textures));
      impl_->faulted = true;
      return NativeTeardownFailure("Ogre-Next N1 mesh replacement");
    }
    impl_->meshes.swap(candidate_meshes);
    impl_->materials.swap(candidate_materials);
    impl_->textures.swap(candidate_textures);
    impl_->registry.swap(candidate);
    // Every surviving retained instance must resolve in the swapped catalog;
    // anything else means the unbind step above missed a replacement.
    for (const auto &entry : impl_->retained_instances) {
      const MeshInstanceDescriptor &descriptor = entry.second.descriptor;
      const auto mesh = impl_->meshes.find(descriptor.mesh.id);
      const auto material = impl_->materials.find(descriptor.material.id);
      if (mesh == impl_->meshes.end() ||
          mesh->second.asset != descriptor.mesh ||
          material == impl_->materials.end() ||
          material->second.asset != descriptor.material) {
        if (!impl_->DestroyRetainedScene()) {
          impl_->faulted = true;
        }
        return NativeTeardownFailure(
            "Ogre-Next retained scene catalog consistency");
      }
    }
    return RenderOperationResult::Success();
  } catch (const std::bad_alloc &) {
    if (impl_->faulted) {
      return NativeTeardownFailure("Ogre-Next N1 asset rollback");
    }
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "Ogre-Next N1 asset synchronization ran out of memory");
  } catch (const Ogre::Exception &error) {
    if (impl_->faulted) {
      return NativeTeardownFailure("Ogre-Next N1 asset rollback");
    }
    return BackendFailure(error);
  } catch (const std::exception &error) {
    if (impl_->faulted) {
      return NativeTeardownFailure("Ogre-Next N1 asset rollback");
    }
    return RenderOperationResult::Failure(RenderOperationCode::BACKEND_FAILURE,
                                          error.what());
  }
}

RenderOperationResult
OgreNextN1Frontend::ResetSceneGeneration(std::uint64_t next_generation) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  if (impl_->sun_visibility_v2_frame_awaiting_continuation) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUTSTANDING_LEASES,
        "sun-visibility V2 cannot reset scene generation before the post-external continuation");
  }
  if (impl_->scene_generation ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      next_generation != impl_->scene_generation + 1U) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "scene generation must advance exactly once");
  }
  if (impl_->production_output_handles.live_count() != 0U) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUTSTANDING_LEASES,
        "scene generation reset requires released frame outputs");
  }
  if (impl_->reflection_probe_runtime) {
    // A generation whose final scene was retired never ran the render path's
    // probe Prepare/Finalize pair (12244 / 12853), so nothing erased the live
    // probe set or unbound HlmsPbs. Complete that lifecycle explicitly here;
    // it is a no-op when the final scene was rendered.
    //
    // Placing it in the reset chain rather than in RetireFrameState is what
    // makes it cover every retire trigger: RendererInProcessSession.cpp:507
    // also retires on a stale surface, a suspended surface and
    // shutdown_requested, and the dispatcher retires on a presentation-extent
    // mismatch (RendererFrontendDirectDispatcher.cpp:326-332). It is also why
    // the dispatcher's no-frontend-work fast path at :337 needs no change.
    const RenderOperationResult retired =
        impl_->reflection_probe_runtime->RetireProbesForSceneGeneration();
    if (!retired) {
      return retired;
    }
    const RenderOperationResult probes =
        impl_->reflection_probe_runtime->ResetSceneGeneration();
    if (!probes) {
      return probes;
    }
  }
  impl_->particle_runtime.Reset();
  if (impl_->hdr_enabled) {
    const ValidationResult temporal =
        impl_->hdr_temporal_state.ResetSceneGeneration();
    if (!temporal) {
      impl_->faulted = true;
      return OgreNextN1OperationFromValidation(temporal);
    }
    HdrR16Float initial_history;
    const ValidationResult quantized = QuantizeHdrR16Float(
        impl_->hdr_configuration.initial_inverse_luminance, initial_history);
    HdrR16Float observed_history;
    const RenderOperationResult uploaded =
        quantized
            ? impl_->InitializeExactHdrHistory(initial_history,
                                               observed_history)
            : OgreNextN1OperationFromValidation(quantized);
    if (!uploaded) {
      impl_->faulted = true;
      return uploaded;
    }
    impl_->hdr_history_comparison = OgreNextHdrHistoryComparison{};
    if (impl_->retain_native_lighting_content_evidence) {
      impl_->hdr_history_comparison.mode =
          OgreNextHdrHistoryValidationMode::
              NATIVE_AUTHORITATIVE_CONDITIONING_PLUS_ONE_R16_ULP;
      impl_->hdr_history_comparison.native_inverse_luminance_r16 =
          observed_history;
      impl_->hdr_history_comparison.reference_inverse_luminance_r16 =
          initial_history;
      impl_->hdr_history_comparison.accepted = true;
      impl_->hdr_native_history_validated = true;
    } else {
      impl_->hdr_native_history_validated = false;
    }
    impl_->hdr_exact_current_to_old_copy_verified = false;
  }
  // Retained INSTANCES are destroyed on BOTH paths by
  // DestroyRetainedInstancesForAssetReplacement during the emptying asset
  // synchronization (:9586 -> :4541), because every instance's mesh and
  // material are absent from the emptied candidate catalog. An instance
  // surviving to here really is an anomaly.
  //
  // Retained LIGHTS are not asset-backed: only the render path's light-set
  // diff (:10683-10690) destroys them, and a retired final scene never runs
  // that diff. Tearing lights down here is the expected completion of the
  // retire path, not a recovery, so count it separately -- otherwise every
  // terrain unload raises a corruption counter and the real signal is lost.
  if (!impl_->retained_instances.empty()) {
    ++impl_->retained_audit.recovery_teardowns;
    if (!impl_->DestroyRetainedScene()) {
      impl_->faulted = true;
      return NativeTeardownFailure(
          "Ogre-Next retained scene generation reset");
    }
    impl_->shadow_audit.last_native_bounds_observations.clear();
  } else if (!impl_->retained_lights.empty()) {
    ++impl_->retained_audit.retired_light_teardowns;
    if (!impl_->DestroyRetainedLights()) {
      impl_->faulted = true;
      return NativeTeardownFailure(
          "Ogre-Next retained light generation reset");
    }
    impl_->shadow_audit.last_native_bounds_observations.clear();
  }
  impl_->retained_audit.generation = next_generation;
  impl_->retained_audit.last_created = 0U;
  impl_->retained_audit.last_updated = 0U;
  impl_->retained_audit.last_destroyed = 0U;
  impl_->retained_audit.last_dynamic_updates = 0U;
  impl_->retained_audit.last_verified = 0U;
  impl_->retained_audit.verify_cursor = 0U;
  impl_->scene_generation = next_generation;
  return RenderOperationResult::Success();
}

RenderOperationResult
OgreNextN1Frontend::ReleaseResource(ResourceHandle resource) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  if (impl_->production_output_handles.IsLive(resource)) {
    return impl_->production_output_handles.Release(resource)
               ? RenderOperationResult::Success()
               : RenderOperationResult::Failure(
                     RenderOperationCode::RESOURCE_STALE,
                     "Ogre-Next production output lease became stale");
  }
  return RenderOperationResult::Failure(
      resource.valid() ? RenderOperationCode::RESOURCE_STALE
                       : RenderOperationCode::INVALID_ARGUMENT,
      "N1 transfers CPU readbacks only and owns no releasable output handles");
}

RenderOperationResult OgreNextN1Frontend::Render(
    const RenderFrameRequest &request, RenderFrameOutput &output) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  if (!impl_->registry) {
    return RenderOperationResult::Failure(
        RenderOperationCode::RESOURCE_STALE,
        "Ogre-Next N1 requires an asset snapshot before rendering");
  }
  const bool deferred_sun_visibility_v2 =
      UsesMetalSunVisibilityV2(impl_->native_feature_tier);
  const ValidationResult validation = ValidateOgreNextN1Frame(
      request, impl_->Capabilities(), *impl_->registry,
      impl_->raster_feature_tier, impl_->directional_shadow_mode,
      impl_->hdr_enabled,
      UsesMetalDirectionalHardShadow(impl_->native_feature_tier),
      impl_->presentation_configuration.enabled,
      deferred_sun_visibility_v2,
      impl_->hdr_scene_topology);
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }
  const bool production_presentation =
      impl_->ProductionPresentationEnabled();
  if (request.present || deferred_sun_visibility_v2) {
    if (!production_presentation &&
        impl_->presentation_audit.presented_frames != 0U) {
      return RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "the first native presentation gate admits exactly one presented frame per frontend lifetime");
    }
    if (impl_->presentation_window == nullptr ||
        !impl_->presentation_resource_group_created) {
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "native presentation resources disappeared before submission");
    }
  }
  const CameraViewRequest &validated_view = request.views.front();
  OgreNextPssmShadowFramePlan shadow_plan;
  if (!UsesMetalDirectionalHardShadow(impl_->native_feature_tier) &&
      !deferred_sun_visibility_v2) {
    const ValidationResult shadow_validation =
        TryBuildOgreNextPssmShadowFramePlan(
            *request.scene_snapshot, *impl_->registry, validated_view,
            impl_->raster_feature_tier, impl_->directional_shadow_mode,
            shadow_plan);
    if (!shadow_validation) {
      return OgreNextN1OperationFromValidation(shadow_validation);
    }
  }
  OgreNextHdrTemporalFramePlan hdr_plan;
  if (impl_->hdr_enabled) {
    if (validated_view.width != impl_->hdr_width ||
        validated_view.height != impl_->hdr_height ||
        impl_->hdr_workspace == nullptr ||
        impl_->hdr_output_target == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "the persistent HDR compositor requires its fixed initialized extent");
    }
    const ValidationResult planned = impl_->hdr_temporal_state.PrepareFrame(
        request, impl_->raster_feature_tier, hdr_plan,
        deferred_sun_visibility_v2);
    if (!planned) {
      return OgreNextN1OperationFromValidation(planned);
    }
    if (impl_->retain_native_lighting_content_evidence) {
      if (impl_->LightingContentReadbackCounter() >
          (std::numeric_limits<std::uint64_t>::max)() - 4U) {
        return RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "native HDR content-readback audit counter space is exhausted");
      }
      HdrR16Float native_previous;
      if (!impl_->ReadHdrHistory(native_previous) ||
          native_previous.bits !=
              hdr_plan.previous_inverse_luminance_r16.bits ||
          native_previous.decoded !=
              hdr_plan.previous_inverse_luminance_r16.decoded) {
        impl_->faulted = true;
        return HdrBackendFailure(
            "persistent R16 history diverged before frame submission");
      }
    }
    const RenderOperationResult configured = impl_->ConfigureHdrParameters(
        hdr_plan.ogre_exposure, hdr_plan.minimum_auto_exposure,
        hdr_plan.maximum_auto_exposure, hdr_plan.bloom_minimum_threshold,
        hdr_plan.bloom_inverse_transition_width, hdr_plan.delta_seconds);
    if (!configured) {
      impl_->faulted = true;
      return configured;
    }
    if (impl_->SingleSceneHdrPssmEnabled()) {
      const RenderOperationResult haze = impl_->ConfigureAerialHazeForFrame(
          request.scene_snapshot->environment(), validated_view);
      if (!haze) {
        impl_->faulted = true;
        return haze;
      }
    }
  }
  std::uint64_t readback_row_pitch = 0U;
  std::size_t readback_total_bytes = 0U;
  const bool gpu_only_output =
      production_presentation &&
      impl_->presentation_configuration.gpu_only_output;
  if (deferred_sun_visibility_v2 && !gpu_only_output) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "sun-visibility V2 forbids CPU output and requires the deferred GPU-only lease");
  }
  if (!gpu_only_output &&
      !TryComputeReadbackLayout(validated_view.width, validated_view.height,
                                request.color_format, readback_row_pitch,
                                readback_total_bytes)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "N1 readback extent cannot be represented by the host allocation model");
  }
  const std::uint64_t lighting_verification_budget =
      1U + static_cast<std::uint64_t>(
               request.scene_snapshot->lights().size()) +
      static_cast<std::uint64_t>(
          request.scene_snapshot->mesh_instances().size());
  if (impl_->lighting_audit.completed_frames ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      impl_->lighting_audit.native_state_verifications >
          (std::numeric_limits<std::uint64_t>::max)() -
              lighting_verification_budget ||
      (!gpu_only_output && production_presentation &&
       impl_->lighting_audit.production_framebuffer_readbacks ==
           (std::numeric_limits<std::uint64_t>::max)()) ||
      (!gpu_only_output && !production_presentation &&
       impl_->lighting_audit.test_artifact_framebuffer_readbacks ==
           (std::numeric_limits<std::uint64_t>::max)())) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "native lighting audit counter space is exhausted");
  }
  OgreNextNativeLightingPassAudit lighting_candidate =
      impl_->lighting_audit;
  lighting_candidate.last_frame_id = request.frame_id;
  lighting_candidate.last_snapshot_id =
      request.scene_snapshot->snapshot_id();
  lighting_candidate.last_material_descriptor_version = 0U;
  lighting_candidate.last_directional_lights = 0U;
  lighting_candidate.last_pbs_items = 0U;
  lighting_candidate.last_transmission_items = 0U;
  lighting_candidate.last_normal_mapped_items = 0U;
  lighting_candidate.last_emissive_items = 0U;
  lighting_candidate.last_shadow_casters = 0U;
  lighting_candidate.last_shadow_receivers = 0U;
  lighting_candidate.shadow_mode = impl_->directional_shadow_mode;
  lighting_candidate.hdr_scene_topology = impl_->hdr_scene_topology;
  lighting_candidate.pssm_finalized_with_populated_scene =
      impl_->hdr_pssm_finalized_with_populated_scene;
  lighting_candidate.native_scene_lighting_pass = false;
  lighting_candidate.linear_rgba16_hdr_target =
      impl_->hdr_enabled && impl_->hdr_linear_scene_target_verified;
  lighting_candidate.separate_base_hdr_target =
      impl_->hdr_enabled && impl_->hdr_base_hdr_target_verified;
  lighting_candidate.separate_unoccluded_sun_full_hdr_target =
      impl_->hdr_enabled && impl_->hdr_sun_full_hdr_target_verified;
  lighting_candidate.separate_sun_direct_hdr_target =
      impl_->hdr_enabled && impl_->hdr_sun_direct_hdr_target_verified;
  lighting_candidate.gpu_sun_direct_derivation =
      impl_->hdr_enabled && impl_->hdr_gpu_sun_direct_split_verified;
  lighting_candidate.transactional_directional_sun_toggle = false;
  lighting_candidate.raster_lit_hdr_target =
      impl_->hdr_enabled && impl_->hdr_linear_scene_target_verified;
  lighting_candidate.single_step_hdr_history = false;
  lighting_candidate.raster_scene_evaluations =
      impl_->hdr_enabled && !impl_->SingleSceneHdrPssmEnabled() ? 3U : 1U;
  lighting_candidate.calibrated_directional_lighting = false;
  lighting_candidate.ambient_environment_lighting = false;
  lighting_candidate.analytic_sky_contribution = false;
  lighting_candidate.aerial_haze_applied = false;
  lighting_candidate.emissive_material_response = false;
  lighting_candidate.pssm_shadow_response = false;
  lighting_candidate.hdr_auto_exposure =
      impl_->hdr_enabled && impl_->hdr_auto_exposure_graph_verified;
  lighting_candidate.hdr_bloom =
      impl_->hdr_enabled && impl_->hdr_bloom_graph_verified;
  lighting_candidate.filmic_tone_map =
      impl_->hdr_enabled && impl_->hdr_tone_map_graph_verified;
  lighting_candidate.srgb_presentation =
      impl_->hdr_enabled && impl_->hdr_srgb_output_verified;
  lighting_candidate.production_gpu_only = gpu_only_output;
  lighting_candidate.no_ogre14_lighting = true;
  const RenderOperationResult identity_validation =
      impl_->submission_state.Validate(request);
  if (!identity_validation) {
    return identity_validation;
  }
  if (impl_->native_interop) {
    if (impl_->sun_visibility_v2_frame_awaiting_continuation) {
      return RenderOperationResult::Failure(
          RenderOperationCode::OUTSTANDING_LEASES,
          "sun-visibility V2 cannot replace a frame before its post-external continuation");
    }
    const RenderOperationResult can_publish =
        impl_->native_interop->CanPublishFrame();
    if (!can_publish) {
      return can_publish;
    }
    const RenderOperationResult discarded =
        impl_->native_interop->DiscardPublishedFrame();
    if (!discarded) {
      return discarded;
    }
    if (impl_->retained_output_target != nullptr) {
      if (!impl_->DestroyRetainedOutputTarget()) {
        impl_->faulted = true;
        return NativeTeardownFailure(
            "Ogre-Next N3 prior output image retirement");
      }
    }
    if (!impl_->DestroyFrameMeshes()) {
      impl_->faulted = true;
      return NativeTeardownFailure(
          "Ogre-Next N2 prior frame geometry retirement");
    }
  }
  if (UsesMetalImageInterop(impl_->native_feature_tier) &&
      request.color_format != PixelFormat::RGBA16_FLOAT) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "Ogre-Next Metal N3/N4 requires a linear RGBA16_FLOAT colour target");
  }

  const bool persistent_hdr = impl_->hdr_enabled;
  Ogre::TextureGpu *target =
      persistent_hdr
          ? impl_->hdr_output_target
          : (production_presentation
                 ? impl_->production_source_target
                 : nullptr);
  Ogre::TextureGpu *retained_target = nullptr;
  Ogre::CompositorWorkspace *workspace =
      persistent_hdr
          ? impl_->hdr_workspace
          : (production_presentation
                 ? impl_->production_workspace
                 : nullptr);
  ResourceHandle production_output_resource;
  Ogre::IdString workspace_name;
  bool reflection_frame_prepared = false;
  bool submission_commit_prepared = false;
  bool interop_commit_prepared = false;
  bool workspace_name_prepared = false;
  std::string workspace_node_text;
  bool workspace_node_creation_counted = false;
  std::string shadow_node_text;
  bool shadow_node_creation_counted = false;
  std::string target_text;
  bool hdr_native_frame_executed = false;
  bool hdr_commit_prepared = false;
  bool particle_frame_prepared = false;
  std::vector<std::pair<Ogre::ManualObject *, Ogre::SceneNode *>>
      particle_batches;
  Impl::NativeAnalyticSkySection analytic_sky_background_section;
  Impl::NativeAnalyticSkySection analytic_sky_sun_section;
  Ogre::Item *analytic_sky_background_item = nullptr;
  Ogre::Item *analytic_sky_sun_item = nullptr;
  Ogre::IdType analytic_sky_background_item_id = 0U;
  Ogre::IdType analytic_sky_sun_item_id = 0U;
  bool analytic_sky_background_item_id_valid = false;
  bool analytic_sky_sun_item_id_valid = false;
  Ogre::SceneNode *analytic_sky_node = nullptr;
  Ogre::IdType analytic_sky_node_id = 0U;
  bool analytic_sky_node_id_valid = false;
  Ogre::HlmsUnlitDatablock *analytic_sky_background_datablock = nullptr;
  Ogre::HlmsUnlitDatablock *analytic_sky_sun_datablock = nullptr;
  bool analytic_sky_background_datablock_attempted = false;
  bool analytic_sky_sun_datablock_attempted = false;
  std::string analytic_sky_background_datablock_name;
  std::string analytic_sky_sun_datablock_name;
  bool analytic_sky_frame_completed = false;
  AnalyticSkyDescriptor analytic_sky_committed_descriptor;
  OgreNextAnalyticSkyNativeMesh analytic_sky_mesh;
  OgreNextAnalyticSkyRuntimeAudit analytic_sky_lifetime_before;
  std::uint64_t analytic_sky_cpu_geometry_fnv1a64 = 0U;
  float analytic_sky_native_radius = 0.0F;
  Ogre::HlmsUnlitDatablock *particle_datablock = nullptr;
  std::string particle_datablock_name;
  std::uint64_t particle_native_batch_creates = 0U;
  std::uint64_t particle_native_batch_destroys = 0U;
  std::uint64_t particle_native_particles_submitted = 0U;
  std::uint64_t particle_native_state_verifications = 0U;
  std::vector<OgreNextReflectionProbeItemBinding> reflection_items;
  // True once the retained native scene may have been mutated this present.
  // Failures before this point return without touching retained state; any
  // later failure tears the retained scene down to empty (§fail_after_cleanup)
  // so a half-applied diff is never observable.
  bool scene_mutation_started = false;
  std::vector<OgreNextPssmNativeBoundsObservation> native_bounds_observations;
  std::vector<OgreNextN2FrameGeometryBinding> interop_geometry;
  std::vector<OgreNextN3FrameImageBinding> interop_images;
  auto *sun_visibility_v2_interop =
      deferred_sun_visibility_v2
          ? dynamic_cast<OgreNextSunVisibilityV2NativeInterop *>(
                impl_->native_interop.get())
          : nullptr;
  bool sun_visibility_v2_commit_prepared = false;
  if (deferred_sun_visibility_v2 && sun_visibility_v2_interop == nullptr) {
    impl_->faulted = true;
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "sun-visibility V2 interop disappeared before frame preparation");
  }
  const auto cleanup_scene = [&]() noexcept {
    bool clean = true;
    if (impl_->hdr_directional_split_listener.active()) {
      clean = impl_->hdr_directional_split_listener.AbortFrame() && clean;
    }
    Ogre::CompositorManager2 *compositors =
        impl_->root->getCompositorManager2();
    if (!persistent_hdr && !production_presentation &&
        workspace != nullptr) {
      try {
        compositors->removeWorkspace(workspace);
      } catch (...) {
        clean = false;
      }
      workspace = nullptr;
    }
    if (workspace_name_prepared) {
      bool present = false;
      try {
        present = compositors->hasWorkspaceDefinition(workspace_name);
      } catch (...) {
        clean = false;
      }
      if (present) {
        try {
          compositors->removeWorkspaceDefinition(workspace_name);
        } catch (...) {
          clean = false;
        }
      }
      try {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        MaybeInjectPssmFailureStage(
            impl_->pssm_failure_stage, impl_->pssm_failure_pending,
            OgreNextN1PssmFailureStage::
                DURING_WORKSPACE_DEFINITION_CLEANUP_LOOKUP,
            "injected PSSM workspace-definition cleanup absence-lookup failure");
#endif
        if (compositors->hasWorkspaceDefinition(workspace_name)) {
          clean = false;
        } else if (shadow_plan.enabled) {
          ++impl_->shadow_audit.workspace_definition_cleanup_absence_checks;
        }
      } catch (...) {
        clean = false;
      }
      workspace_name_prepared = false;
    }
    if (!workspace_node_text.empty()) {
      const Ogre::IdString workspace_node_name(workspace_node_text);
      try {
        if (compositors->hasNodeDefinition(workspace_node_name)) {
          if (!workspace_node_creation_counted) {
            ++impl_->shadow_audit.workspace_node_definition_creates;
            workspace_node_creation_counted = true;
          }
          compositors->removeNodeDefinition(workspace_node_name);
          ++impl_->shadow_audit.workspace_node_definition_destroys;
        }
      } catch (...) {
        clean = false;
      }
      try {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        MaybeInjectPssmFailureStage(
            impl_->pssm_failure_stage, impl_->pssm_failure_pending,
            OgreNextN1PssmFailureStage::DURING_WORKSPACE_NODE_CLEANUP_LOOKUP,
            "injected PSSM workspace-node cleanup absence-lookup failure");
#endif
        if (compositors->hasNodeDefinition(workspace_node_name)) {
          clean = false;
        } else if (shadow_plan.enabled) {
          ++impl_->shadow_audit.workspace_node_cleanup_absence_checks;
        }
      } catch (...) {
        clean = false;
      }
      workspace_node_text.clear();
    }
    if (!persistent_hdr && !production_presentation &&
        !shadow_node_text.empty()) {
      const Ogre::IdString shadow_node_name(shadow_node_text);
      try {
        if (compositors->hasShadowNodeDefinition(shadow_node_name)) {
          if (!shadow_node_creation_counted) {
            ++impl_->shadow_audit.shadow_node_creates;
            shadow_node_creation_counted = true;
          }
          compositors->removeShadowNodeDefinition(shadow_node_name);
          ++impl_->shadow_audit.shadow_node_destroys;
        }
      } catch (...) {
        clean = false;
      }
      try {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        MaybeInjectPssmFailureStage(
            impl_->pssm_failure_stage, impl_->pssm_failure_pending,
            OgreNextN1PssmFailureStage::DURING_SHADOW_NODE_CLEANUP_LOOKUP,
            "injected PSSM shadow-node cleanup absence-lookup failure");
#endif
        if (compositors->hasShadowNodeDefinition(shadow_node_name)) {
          clean = false;
        } else {
          ++impl_->shadow_audit.shadow_node_cleanup_absence_checks;
        }
      } catch (...) {
        clean = false;
      }
      shadow_node_text.clear();
    }
    if (!persistent_hdr && !production_presentation &&
        (target != nullptr || !target_text.empty())) {
      Ogre::TextureGpuManager *texture_manager =
          impl_->renderer->getTextureGpuManager();
      Ogre::TextureGpu *registered_target = target;
      if (registered_target == nullptr) {
        try {
          registered_target =
              texture_manager->findTextureNoThrow(Ogre::IdString(target_text));
        } catch (...) {
          clean = false;
        }
      }
      bool destroy_returned = registered_target == nullptr;
      if (registered_target != nullptr) {
        try {
          texture_manager->destroyTexture(registered_target);
          texture_manager->waitForStreamingCompletion();
          destroy_returned = true;
        } catch (...) {
          clean = false;
          destroy_returned = false;
        }
      }
      target = nullptr;
      try {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        MaybeInjectPssmFailureStage(
            impl_->pssm_failure_stage, impl_->pssm_failure_pending,
            OgreNextN1PssmFailureStage::
                DURING_TARGET_TEXTURE_CLEANUP_LOOKUP,
            "injected PSSM target-texture cleanup absence-lookup failure");
#endif
        if (!destroy_returned || texture_manager->findTextureNoThrow(
                                     Ogre::IdString(target_text)) != nullptr) {
          clean = false;
        } else if (shadow_plan.enabled) {
          ++impl_->shadow_audit.target_texture_cleanup_absence_checks;
        }
      } catch (...) {
        clean = false;
      }
      target_text.clear();
    }
    for (auto iterator = particle_batches.rbegin();
         iterator != particle_batches.rend(); ++iterator) {
      if (iterator->second != nullptr && iterator->first != nullptr) {
        try {
          iterator->second->detachObject(iterator->first);
        } catch (...) {
          clean = false;
        }
      }
      if (iterator->first != nullptr) {
        try {
          impl_->scene_manager->destroyManualObject(iterator->first);
          ++particle_native_batch_destroys;
        } catch (...) {
          clean = false;
        }
      }
      if (iterator->second != nullptr) {
        try {
          impl_->scene_manager->destroySceneNode(iterator->second);
        } catch (...) {
          clean = false;
        }
      }
    }
    particle_batches.clear();
    const auto destroy_sky_item =
        [&](Ogre::Item *&item, Ogre::IdType id,
            bool &id_valid) noexcept {
          if (item == nullptr && !id_valid) {
            return true;
          }
          bool local_clean = true;
          if (item != nullptr && item->isAttached()) {
            try {
              if (analytic_sky_node == nullptr ||
                  item->getParentSceneNode() != analytic_sky_node) {
                local_clean = false;
              } else {
                analytic_sky_node->detachObject(item);
              }
            } catch (...) {
              local_clean = false;
            }
          }
          if (item != nullptr) {
            try {
              impl_->scene_manager->destroyItem(item);
              ++impl_->analytic_sky_audit.native_item_destroys;
            } catch (...) {
              local_clean = false;
            }
            item = nullptr;
          }
          if (id_valid) {
            try {
              if (!impl_->AnalyticSkyItemIsAbsent(id)) {
                local_clean = false;
              } else {
                ++impl_->analytic_sky_audit.native_item_absence_checks;
              }
            } catch (...) {
              local_clean = false;
            }
          }
          id_valid = false;
          return local_clean;
        };
    clean = destroy_sky_item(analytic_sky_sun_item,
                             analytic_sky_sun_item_id,
                             analytic_sky_sun_item_id_valid) &&
            clean;
    clean = destroy_sky_item(analytic_sky_background_item,
                             analytic_sky_background_item_id,
                             analytic_sky_background_item_id_valid) &&
            clean;
    if (analytic_sky_node != nullptr) {
      try {
        impl_->scene_manager->destroySceneNode(analytic_sky_node);
        ++impl_->analytic_sky_audit.native_scene_node_destroys;
      } catch (...) {
        clean = false;
      }
      analytic_sky_node = nullptr;
    }
    if (analytic_sky_node_id_valid) {
      try {
        if (impl_->scene_manager->getSceneNode(analytic_sky_node_id) !=
            nullptr) {
          clean = false;
        } else {
          ++impl_->analytic_sky_audit.native_scene_node_absence_checks;
        }
      } catch (...) {
        clean = false;
      }
      analytic_sky_node_id_valid = false;
    }
    clean = impl_->DestroyAnalyticSkySection(analytic_sky_sun_section) &&
            clean;
    clean =
        impl_->DestroyAnalyticSkySection(analytic_sky_background_section) &&
        clean;
    const auto destroy_sky_datablock =
        [&](Ogre::HlmsUnlitDatablock *&datablock,
            std::string &name, bool &attempted) noexcept {
          if (!attempted || name.empty()) {
            datablock = nullptr;
            name.clear();
            attempted = false;
            return true;
          }
          bool local_clean = true;
          try {
            Ogre::HlmsDatablock *registered =
                impl_->unlit->getDatablock(Ogre::IdString(name));
            if (registered != nullptr) {
              if (datablock == nullptr) {
                ++impl_->analytic_sky_audit.native_datablock_creates;
              }
              impl_->unlit->destroyDatablock(registered->getName());
              ++impl_->analytic_sky_audit.native_datablock_destroys;
            }
            if (impl_->unlit->getDatablock(Ogre::IdString(name)) != nullptr) {
              local_clean = false;
            } else {
              ++impl_->analytic_sky_audit.native_datablock_absence_checks;
            }
          } catch (...) {
            local_clean = false;
          }
          datablock = nullptr;
          name.clear();
          attempted = false;
          return local_clean;
        };
    clean = destroy_sky_datablock(analytic_sky_sun_datablock,
                                  analytic_sky_sun_datablock_name,
                                  analytic_sky_sun_datablock_attempted) &&
            clean;
    clean = destroy_sky_datablock(analytic_sky_background_datablock,
                                  analytic_sky_background_datablock_name,
                                  analytic_sky_background_datablock_attempted) &&
            clean;
    if (particle_datablock != nullptr) {
      try {
        impl_->unlit->destroyDatablock(
            Ogre::IdString(particle_datablock_name));
        if (impl_->unlit->getDatablock(
                Ogre::IdString(particle_datablock_name)) != nullptr) {
          clean = false;
        }
      } catch (...) {
        clean = false;
      }
      particle_datablock = nullptr;
      particle_datablock_name.clear();
    }
    return clean;
  };
  const auto destroy_retained_target = [&]() noexcept {
    if (retained_target == nullptr) {
      return true;
    }
    try {
      impl_->renderer->getTextureGpuManager()->destroyTexture(retained_target);
      retained_target = nullptr;
      return true;
    } catch (...) {
      return false;
    }
  };
  const auto abort_reflection_frame = [&]() noexcept {
    if (!reflection_frame_prepared || !impl_->reflection_probe_runtime) {
      return true;
    }
    const bool clean =
        impl_->reflection_probe_runtime->AbortFrame(request.frame_id);
    reflection_frame_prepared = false;
    return clean;
  };
  const auto abort_particle_frame = [&]() noexcept {
    if (particle_frame_prepared) {
      impl_->particle_runtime.Abort(request.frame_id);
      particle_frame_prepared = false;
    }
    return true;
  };
  const auto abort_submission_commit = [&]() noexcept {
    if (submission_commit_prepared) {
      impl_->submission_state.AbortPrepared();
      submission_commit_prepared = false;
    }
    return true;
  };
  const auto abort_interop_commit = [&]() noexcept {
    if (sun_visibility_v2_commit_prepared && sun_visibility_v2_interop) {
      sun_visibility_v2_interop
          ->AbortPreparedSunVisibilityV2ImageSet();
      sun_visibility_v2_commit_prepared = false;
    }
    if (interop_commit_prepared && impl_->native_interop) {
      impl_->native_interop->AbortPreparedFrame();
      interop_commit_prepared = false;
    }
    return true;
  };
  // Returns a VERIFIED rollback rather than an unconditional true: after
  // AbortPrepared the state must hold no committable candidate. `terminal` is
  // reserved for a rollback that demonstrably failed, so this verdict has to
  // be derived rather than assumed.
  const auto abort_hdr_commit = [&]() noexcept {
    if (!hdr_commit_prepared) {
      return true;
    }
    impl_->hdr_temporal_state.AbortPrepared();
    hdr_commit_prepared = false;
    return !impl_->hdr_temporal_state.CanCommitPrepared();
  };
  const auto abort_hdr_pssm_finalization = [&]() noexcept {
    if (!impl_->hdr_pssm_finalization_prepared) {
      return true;
    }
    return impl_->RollbackSingleSceneHdrPssm();
  };
  const auto abort_production_output = [&]() noexcept {
    if (!production_output_resource.valid()) {
      return true;
    }
    const bool released =
        impl_->production_output_handles.Release(production_output_resource);
    production_output_resource = {};
    return released;
  };
  const auto fail_after_cleanup = [&](RenderOperationResult failure) {
    bool clean = abort_particle_frame();
    clean = abort_reflection_frame() && clean;
    clean = abort_submission_commit() && clean;
    clean = abort_interop_commit() && clean;
    const bool hdr_prepared_rollback_verified = abort_hdr_commit();
    clean = hdr_prepared_rollback_verified && clean;
    clean = abort_hdr_pssm_finalization() && clean;
    clean = abort_production_output() && clean;
    clean = cleanup_scene() && clean;
    if (scene_mutation_started) {
      // A failed present must never leave a half-mutated retained scene:
      // tear down to empty and let the next present rebuild from scratch.
      // The retained PSSM evidence dies with the scene it observed.
      ++impl_->retained_audit.recovery_teardowns;
      clean = impl_->DestroyRetainedScene() && clean;
      impl_->shadow_audit.last_native_bounds_observations.clear();
    }
    clean = destroy_retained_target() && clean;
    // F3. This latch used to read `if (hdr_native_frame_executed)`, and the
    // flag itself used to be raised BEFORE renderOneFrame ran. Every failure
    // in the back half of Render therefore became a permanent frontend fault
    // -- including a Metal drawable timeout thrown out of renderOneFrame,
    // where the GPU advanced no HDR history at all. A frontend that faults
    // permanently ends the session, which the render-boundary invariant
    // reserves for state proven unrecoverable.
    //
    // Terminal now requires HDR history that is genuinely half-written:
    //   * the native frame must actually have COMPLETED (the flag is raised
    //     after renderOneFrame returns true, so a frame that threw or refused
    //     to run advances nothing and latches nothing), and
    //   * the CPU must keep a mirror of that native history
    //     (`retain_native_lighting_content_evidence`). On the zero-readback
    //     path the compositor owns oldLumRt across frames and the CPU value
    //     is only a sequencing token -- "neither compared with nor presented
    //     as the live GPU history value" (OgreNextHdrTemporalContract.cpp,
    //     PrepareGpuOnlyCommit) -- so discarding it leaves nothing divergent.
    //     Frame identity also stays contiguous, because the dispatcher does
    //     not advance frontend frame identity on a failed frame.
    //   * or the prepared CPU transaction must have failed to roll back,
    //     which is the general "rollback demonstrably failed" case.
    if (hdr_native_frame_executed &&
        (impl_->retain_native_lighting_content_evidence ||
         !hdr_prepared_rollback_verified)) {
      impl_->faulted = true;
    } else if (hdr_native_frame_executed) {
      // A post-submit failure this frontend can survive. Counted, never
      // silent: a degrade nobody can see is not a fix.
      ++impl_->degrade_audit.post_submit_recoverable_failures;
    }
    if (!clean) {
      impl_->faulted = true;
      return FrameCleanupFailure();
    }
    // F2. The frontend has always computed the true recoverability verdict
    // here and then thrown it away: `clean` is the conjunction of eight
    // reverse aborts, and `!impl_->faulted` says the frame left no
    // half-written HDR history. Publish that verdict on the result instead of
    // discarding it, so ~165 frontend Failure( sites stop being indistinguish-
    // able from unrecoverable ones.
    //
    // This is deliberately NOT a new judgement: both terms are already
    // decided above, and both must hold. The dispatcher currently only counts
    // this recovery -- see RenderOperationRecovery::RETRY_NEXT_FRAME -- and
    // still poisons, because a misclassified partial commit would become
    // silent corruption, which is worse than the crash it replaces.
    if (failure.recovery == RenderOperationRecovery::NONE &&
        !impl_->faulted) {
      failure.recovery = RenderOperationRecovery::RETRY_NEXT_FRAME;
    }
    return failure;
  };

  try {
    const auto cpu_start = std::chrono::steady_clock::now();
    const SceneSnapshot &snapshot = *request.scene_snapshot;
    const CameraViewRequest &view = request.views.front();
    const AnalyticSkyDescriptor &portable_sky =
        snapshot.environment().analytic_sky;
    if (portable_sky.enabled) {
      const auto sun = std::lower_bound(
          snapshot.lights().begin(), snapshot.lights().end(),
          portable_sky.sun_light_id,
          [](const LightDescriptor &light, std::uint64_t identity) {
            return light.light_id < identity;
          });
      if (sun == snapshot.lights().end() ||
          sun->light_id != portable_sky.sun_light_id) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "validated analytic-sky sun identity disappeared before native staging"));
      }
      const double native_radius_double = std::sqrt(
          static_cast<double>(view.near_plane) *
          static_cast<double>(view.far_plane));
      const float native_radius = static_cast<float>(native_radius_double);
      analytic_sky_native_radius = native_radius;
      const ValidationResult sky_mesh_validation =
          BuildOgreNextAnalyticSkyNativeMesh(
              snapshot.environment(), *sun, native_radius,
              analytic_sky_mesh);
      if (!sky_mesh_validation) {
        return fail_after_cleanup(
            OgreNextN1OperationFromValidation(sky_mesh_validation));
      }
      analytic_sky_cpu_geometry_fnv1a64 =
          AnalyticSkyCpuGeometryFnv1a64(analytic_sky_mesh);
      const auto has_headroom = [](std::uint64_t value,
                                   std::uint64_t amount) {
        return value <=
               (std::numeric_limits<std::uint64_t>::max)() - amount;
      };
      const OgreNextAnalyticSkyRuntimeAudit &audit =
          impl_->analytic_sky_audit;
      if (!has_headroom(audit.completed_frames, 1U) ||
          !has_headroom(audit.native_mesh_creates, 2U) ||
          !has_headroom(audit.native_mesh_destroys, 2U) ||
          !has_headroom(audit.native_vertex_buffer_creates, 2U) ||
          !has_headroom(audit.native_vertex_buffer_destroys, 2U) ||
          !has_headroom(audit.native_index_buffer_creates, 2U) ||
          !has_headroom(audit.native_index_buffer_destroys, 2U) ||
          !has_headroom(audit.native_vao_creates, 2U) ||
          !has_headroom(audit.native_vao_destroys, 2U) ||
          !has_headroom(audit.native_item_creates, 2U) ||
          !has_headroom(audit.native_item_destroys, 2U) ||
          !has_headroom(audit.native_scene_node_creates, 1U) ||
          !has_headroom(audit.native_scene_node_destroys, 1U) ||
          !has_headroom(audit.native_datablock_creates, 2U) ||
          !has_headroom(audit.native_datablock_destroys, 2U) ||
          !has_headroom(audit.native_mesh_absence_checks, 2U) ||
          !has_headroom(audit.native_item_absence_checks, 2U) ||
          !has_headroom(audit.native_scene_node_absence_checks, 1U) ||
          !has_headroom(audit.native_datablock_absence_checks, 2U) ||
          (impl_->retain_analytic_sky_geometry_content_evidence &&
           (!has_headroom(audit.native_gpu_content_readbacks, 4U) ||
            !has_headroom(impl_->LightingContentReadbackCounter(), 4U))) ||
          !has_headroom(audit.native_state_verifications, 1U)) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 analytic-sky lifetime telemetry exhausted"));
      }
      analytic_sky_lifetime_before = audit;
      analytic_sky_committed_descriptor = portable_sky;
    }
    ValidationResult particle_validation = impl_->particle_runtime.Prepare(
        request.frame_id, request.continuous_particles, *impl_->registry,
        snapshot.simulation_tick(), snapshot.absolute_world_origin_meters());
    if (!particle_validation) {
      return fail_after_cleanup(
          OgreNextN1OperationFromValidation(particle_validation));
    }
    particle_frame_prepared = true;
    const std::uint32_t native_authored_visibility_mask =
        impl_->raster_feature_tier ==
                OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1
            ? kOgreNextRt4AuthoredVisibilityMask
            : kOgreNextN1AuthoredVisibilityMask;
    if (shadow_plan.enabled) {
      impl_->shadow_audit.native_projection_extents_verified = false;
      impl_->shadow_audit.native_readback_verified = false;
      impl_->shadow_audit.native_bounds_readback_verified = false;
      // The committed observation set is not cleared here: it stays the
      // evidence of the last completed shadow present until this present
      // commits its own retained set, or a mutation failure tears the
      // retained scene (and its evidence) down with it.
      impl_->scene_manager->setShadowFarDistance(kOgreNextPssmFarMeters);
      impl_->scene_manager->setShadowDirectionalLightExtrusionDistance(
          kOgreNextPssmFarMeters);
    }
    const Ogre::ColourValue expected_ambient(
        snapshot.environment().ambient_radiance.x *
            snapshot.environment().environment_intensity,
        snapshot.environment().ambient_radiance.y *
            snapshot.environment().environment_intensity,
        snapshot.environment().ambient_radiance.z *
            snapshot.environment().environment_intensity);
    impl_->scene_manager->setAmbientLight(expected_ambient, expected_ambient,
                                          Ogre::Vector3::UNIT_Y);
    if (!NearlyEqual(
            impl_->scene_manager->getAmbientLightUpperHemisphere(),
            expected_ambient) ||
        !NearlyEqual(
            impl_->scene_manager->getAmbientLightLowerHemisphere(),
            expected_ambient) ||
        !NearlyEqual(
            impl_->scene_manager->getAmbientLightHemisphereDir(),
            Ogre::Vector3::UNIT_Y)) {
      throw std::logic_error(
          "validated RT4 ambient environment failed native readback");
    }
    ++lighting_candidate.native_state_verifications;
    lighting_candidate.ambient_environment_lighting =
        expected_ambient.r > 0.0F || expected_ambient.g > 0.0F ||
        expected_ambient.b > 0.0F;
    const std::uint32_t authored_view_visibility =
        view.visibility_mask & native_authored_visibility_mask;
    if (shadow_plan.enabled &&
        shadow_plan.native_visibility_mask != authored_view_visibility) {
      throw std::logic_error(
          "PSSM plan visibility differs from the RT4 authored layer mask");
    }
    impl_->scene_manager->setVisibilityMask(authored_view_visibility);

    {
      const RenderOperationResult hud_commit =
          impl_->CommitHudOverlay(snapshot, view);
      if (!hud_commit) {
        return fail_after_cleanup(hud_commit);
      }
    }

    // The retained light set is keyed by light_id; set changes (generation
    // boundaries) destroy and recreate. Every present re-applies the full
    // setter set and reads it back: the HDR directional split listener
    // rewrites directional power inside the compositor each frame, so the
    // retained light has the highest drift exposure in the scene.
    const auto light_phase_start = std::chrono::steady_clock::now();
    scene_mutation_started = true;
    bool retained_light_set_matches =
        impl_->retained_lights.size() == snapshot.lights().size();
    for (std::size_t index = 0U;
         retained_light_set_matches && index < snapshot.lights().size();
         ++index) {
      retained_light_set_matches =
          impl_->retained_lights[index].descriptor.light_id ==
          snapshot.lights()[index].light_id;
    }
    if (!retained_light_set_matches) {
      if (!impl_->DestroyRetainedLights()) {
        impl_->faulted = true;
        return fail_after_cleanup(
            NativeTeardownFailure("Ogre-Next retained light replacement"));
      }
      impl_->retained_lights.reserve(snapshot.lights().size());
      for (const LightDescriptor &descriptor : snapshot.lights()) {
        // Establish the ownership record before native allocation so a
        // partial light set has exactly one teardown target.
        impl_->retained_lights.emplace_back();
        Impl::RetainedLight &record = impl_->retained_lights.back();
        record.descriptor = descriptor;
        record.light = impl_->scene_manager->createLight();
        record.node =
            impl_->scene_manager->getRootSceneNode()->createChildSceneNode();
        record.node->attachObject(record.light);
      }
      impl_->retained_audit.retained_lights =
          static_cast<std::uint64_t>(impl_->retained_lights.size());
    }
    bool positive_calibrated_directional_light = false;
    for (std::size_t light_index = 0U;
         light_index < snapshot.lights().size(); ++light_index) {
      const LightDescriptor &descriptor = snapshot.lights()[light_index];
      Impl::RetainedLight &retained_light =
          impl_->retained_lights[light_index];
      retained_light.descriptor = descriptor;
      Ogre::Light *light = retained_light.light;
      light->setType(Ogre::Light::LT_DIRECTIONAL);
      if (persistent_hdr) {
        light->setVisibilityFlags(kOgreNextRt4AuthoredVisibilityMask);
      }
      light->setDiffuseColour(descriptor.color_linear.x,
                              descriptor.color_linear.y,
                              descriptor.color_linear.z);
      light->setSpecularColour(descriptor.color_linear.x,
                               descriptor.color_linear.y,
                               descriptor.color_linear.z);
      light->setPowerScale(
          descriptor.intensity * kOgreNextRt4LuxToNativePowerScale);
      light->setDirection(Ogre::Vector3(descriptor.direction.x,
                                        descriptor.direction.y,
                                        descriptor.direction.z));
      light->setCastShadows(shadow_plan.enabled);
      if (shadow_plan.enabled) {
        light->setShadowFarDistance(kOgreNextPssmFarMeters);
      }
      const Ogre::ColourValue expected_color(descriptor.color_linear.x,
                                             descriptor.color_linear.y,
                                             descriptor.color_linear.z);
      const Ogre::Vector3 expected_direction(descriptor.direction.x,
                                             descriptor.direction.y,
                                             descriptor.direction.z);
      const float expected_power =
          descriptor.intensity * kOgreNextRt4LuxToNativePowerScale;
      if (light->getType() != Ogre::Light::LT_DIRECTIONAL ||
          !NearlyEqual(light->getDiffuseColour(), expected_color) ||
          !NearlyEqual(light->getSpecularColour(), expected_color) ||
          !NearlyEqual(light->getPowerScale(), expected_power) ||
          !NearlyEqual(light->getDirection(), expected_direction) ||
          (persistent_hdr &&
           light->getVisibilityFlags() !=
               kOgreNextRt4AuthoredVisibilityMask) ||
          light->getCastShadows() != shadow_plan.enabled ||
          (shadow_plan.enabled &&
           (!NearlyEqual(light->getShadowFarDistance(),
                         kOgreNextPssmFarMeters) ||
            descriptor.light_id != shadow_plan.shadow_light_id))) {
        throw std::logic_error(
            "validated RT4/V1 directional/PSSM light failed native readback");
      }
      ++lighting_candidate.last_directional_lights;
      ++lighting_candidate.native_state_verifications;
      positive_calibrated_directional_light =
          positive_calibrated_directional_light || expected_power > 0.0F;
    }
    lighting_candidate.calibrated_directional_lighting =
        positive_calibrated_directional_light;
    impl_->retained_audit.last_light_phase_microseconds =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - light_phase_start)
                .count());

    const auto instance_phase_start = std::chrono::steady_clock::now();
    reflection_items.reserve(snapshot.mesh_instances().size());
    if (impl_->native_interop) {
      interop_geometry.reserve(snapshot.mesh_instances().size());
    }
    std::uint64_t diff_created = 0U;
    std::uint64_t diff_updated = 0U;
    std::uint64_t diff_destroyed = 0U;
    std::uint64_t diff_dynamic_updates = 0U;
    std::uint64_t diff_verified = 0U;

    const auto resolve_retained_render_mesh =
        [&](const Impl::RetainedInstance &record)
        -> const Impl::NativeMesh * {
      if (record.descriptor.deformation_revision > 1U) {
        return record.deformed_mesh.mesh ? &record.deformed_mesh : nullptr;
      }
      const auto mesh = impl_->meshes.find(record.descriptor.mesh.id);
      return mesh != impl_->meshes.end() &&
                     mesh->second.asset == record.descriptor.mesh
                 ? &mesh->second
                 : nullptr;
    };

    // Refreshes the retained PSSM bounds evidence for one instance from a
    // fresh native readback of its node transform and Mesh/Item AABBs.
    // Throws on any mismatch against the admitted descriptor.
    const auto observe_instance_bounds =
        [&](Impl::RetainedInstance &record,
            const Impl::NativeMesh &render_mesh,
            const Ogre::Matrix4 &reconstructed, bool casts_shadow,
            bool receives_shadow) {
      OgreNextN1NativeMeshBounds expected_bounds;
      if (!TryBuildOgreNextN1NativeMeshBounds(
              record.descriptor.local_bounds, expected_bounds)) {
        throw std::logic_error(
            "validated PSSM bounds became non-finite before native readback");
      }
      const Ogre::Aabb expected_local(
          Ogre::Vector3(expected_bounds.center.x, expected_bounds.center.y,
                        expected_bounds.center.z),
          Ogre::Vector3(expected_bounds.half_size.x,
                        expected_bounds.half_size.y,
                        expected_bounds.half_size.z));
      const Ogre::Matrix4 native_world_transform =
          record.node->_getFullTransformUpdated();
      const bool native_transform_matches =
          NearlyEqualNativeTransform(reconstructed, native_world_transform);
      Ogre::Aabb expected_world = expected_local;
      expected_world.transformAffine(native_world_transform);
      const Ogre::Aabb mesh_local = render_mesh.mesh->getAabb();
      const Ogre::Aabb item_local = record.item->getLocalAabb();
      const Ogre::Aabb item_world = record.item->getWorldAabbUpdated();
      const bool item_world_matches =
          NearlyEqualNativeTransformedAabb(expected_world, item_world);
      if (!native_transform_matches ||
          !NearlyEqual(mesh_local, expected_local) ||
          !NearlyEqual(item_local, expected_local) ||
          !item_world_matches) {
        std::ostringstream detail;
        detail << "Ogre-Next PSSM AABB failed native readback for instance "
               << record.descriptor.instance_id << " (node-world-transform="
               << native_transform_matches << ", mesh-local="
               << NearlyEqual(mesh_local, expected_local)
               << ", item-local="
               << NearlyEqual(item_local, expected_local)
               << ", item-world="
               << item_world_matches
               << ", expected-world-center=" << expected_world.mCenter.x
               << ',' << expected_world.mCenter.y << ','
               << expected_world.mCenter.z << ", observed-world-center="
               << item_world.mCenter.x << ',' << item_world.mCenter.y << ','
               << item_world.mCenter.z << ", expected-world-half="
               << expected_world.mHalfSize.x << ','
               << expected_world.mHalfSize.y << ','
               << expected_world.mHalfSize.z << ", observed-world-half="
               << item_world.mHalfSize.x << ',' << item_world.mHalfSize.y
               << ',' << item_world.mHalfSize.z << ')';
        throw std::runtime_error(detail.str());
      }
      OgreNextPssmNativeBoundsObservation observation;
      observation.instance_id = record.descriptor.instance_id;
      observation.casts_shadow = casts_shadow;
      observation.receives_shadow = receives_shadow;
      observation.expected_local = ObserveNativeAabb(expected_local);
      observation.ogre_mesh_local = ObserveNativeAabb(mesh_local);
      observation.ogre_item_local = ObserveNativeAabb(item_local);
      observation.expected_world = ObserveNativeAabb(expected_world);
      observation.ogre_item_world = ObserveNativeAabb(item_world);
      record.bounds = observation;
      if (!record.bounds_valid) {
        record.bounds_valid = true;
        ++impl_->retained_audit.bounds_entries;
      }
      ++impl_->shadow_audit.bounds_observations_refreshed;
    };

    // Cloned non-receiver datablocks must stay inside the same reviewed
    // RoR PBS shader domain as their source. The custom UV0 affine piece
    // is deliberately selected by this reserved prefix in both the
    // normal and shadow-caster hashes; dropping it here would change the
    // authored texture coordinates on every non-receiver. The clone lives
    // for the instance lifetime, so its name carries the instance id and
    // no frame id.
    const auto create_receiver_clone =
        [&](Impl::RetainedInstance &record,
            Ogre::HlmsPbsDatablock *pbs_datablock) {
      const std::string receiver_name =
          std::string(kOgreNextUvAffinePbsDatablockPrefix) +
          "PssmNonReceiver_i" +
          std::to_string(record.descriptor.instance_id);
      Ogre::HlmsDatablock *cloned = nullptr;
      bool creation_counted = false;
      bool tracked = false;
      try {
        cloned = pbs_datablock->clone(receiver_name);
        ++impl_->shadow_audit.receiver_datablock_creates;
        creation_counted = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        if (impl_->pssm_failure_pending &&
            impl_->pssm_failure_stage ==
                OgreNextN1PssmFailureStage::
                    AFTER_RECEIVER_DATABLOCK_CLONE) {
          impl_->pssm_failure_pending = false;
          throw std::runtime_error(
              "injected PSSM receiver datablock rollback failure");
        }
#endif
        Ogre::HlmsPbsDatablock *receiver_datablock =
            dynamic_cast<Ogre::HlmsPbsDatablock *>(cloned);
        if (receiver_datablock == nullptr) {
          throw std::runtime_error(
              "Ogre-Next PSSM receiver clone changed HLMS type");
        }
        record.receiver_clone = receiver_datablock;
        record.receiver_clone_name = receiver_name;
        tracked = true;
      } catch (...) {
        const std::exception_ptr failure = std::current_exception();
        try {
          Ogre::HlmsDatablock *orphan =
              cloned != nullptr
                  ? cloned
                  : impl_->pbs->getDatablock(Ogre::IdString(receiver_name));
          if (!tracked && orphan != nullptr) {
            if (!creation_counted) {
              ++impl_->shadow_audit.receiver_datablock_creates;
            }
            const Ogre::IdString orphan_name = orphan->getName();
            impl_->pbs->destroyDatablock(orphan_name);
            ++impl_->shadow_audit.receiver_datablock_destroys;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
            MaybeInjectPssmFailureStage(
                impl_->pssm_failure_stage, impl_->pssm_failure_pending,
                OgreNextN1PssmFailureStage::
                    DURING_RECEIVER_DATABLOCK_CLEANUP_LOOKUP,
                "injected PSSM receiver rollback absence-lookup failure");
#endif
            if (impl_->pbs->getDatablock(orphan_name) != nullptr) {
              throw std::runtime_error(
                  "Ogre-Next PSSM receiver rollback retained its datablock");
            }
            ++impl_->shadow_audit.receiver_datablock_cleanup_absence_checks;
          }
        } catch (...) {
          throw std::runtime_error(
              "Ogre-Next PSSM receiver datablock transactional rollback failed");
        }
        std::rethrow_exception(failure);
      }
      record.receiver_clone->setReceiveShadows(false);
    };

    // Builds the complete native state for one admitted instance. The record
    // exists in retained_instances before the first native allocation so a
    // mid-create failure has exactly one teardown target; readback
    // verification is centralized in the per-present verification pass.
    const auto create_retained_instance =
        [&](const MeshInstanceDescriptor &instance) -> RenderOperationResult {
      const auto mesh = impl_->meshes.find(instance.mesh.id);
      const auto material = impl_->materials.find(instance.material.id);
      const MeshResourceDescriptor *base_mesh =
          impl_->registry->ResolveMesh(instance.mesh);
      const MaterialDescriptor *portable_material =
          impl_->registry->ResolveMaterial(instance.material);
      if (mesh == impl_->meshes.end() || material == impl_->materials.end() ||
          base_mesh == nullptr || portable_material == nullptr) {
        return RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "N1 native asset allocation is missing for a validated scene");
      }
      const auto inserted = impl_->retained_instances.emplace(
          instance.instance_id, Impl::RetainedInstance{});
      if (!inserted.second) {
        throw std::logic_error(
            "retained-scene diff scheduled a duplicate instance creation");
      }
      Impl::RetainedInstance &record = inserted.first->second;
      record.descriptor = instance;
      record.dynamic_mesh = base_mesh->dynamic;
      const Impl::NativeMesh *render_mesh = &mesh->second;
      if (instance.deformation_revision > 1U) {
        const auto update = std::find_if(
            snapshot.dynamic_mesh_updates().begin(),
            snapshot.dynamic_mesh_updates().end(),
            [&instance](const DynamicMeshUpdateDescriptor &candidate) {
              return candidate.instance_id == instance.instance_id;
            });
        if (update == snapshot.dynamic_mesh_updates().end() ||
            update->instance_id != instance.instance_id) {
          return RenderOperationResult::Failure(
              RenderOperationCode::RESOURCE_STALE,
              "N2 could not resolve the validated full deformation update");
        }
        MeshResourceDescriptor deformed = *base_mesh;
        deformed.positions = update->positions;
        deformed.normals = update->normals;
        deformed.tangents = update->tangents;
        deformed.velocities = update->velocities;
        deformed.local_bounds = update->updated_local_bounds;
        const std::string suffix =
            "_i" + std::to_string(instance.instance_id) + "_d" +
            std::to_string(instance.deformation_revision) + "_q" +
            std::to_string(++impl_->deformed_mesh_sequence);
        record.deformed_mesh =
            impl_->CreateMesh(instance.mesh, deformed, suffix);
        render_mesh = &record.deformed_mesh;
      }
      Ogre::Vector3 position;
      Ogre::Vector3 scale;
      Ogre::Quaternion orientation;
      Ogre::Matrix4 reconstructed;
      if (!DecomposeTrs(instance.render_from_object, position, scale,
                        orientation, reconstructed)) {
        return RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 transform did not survive exact Ogre TRS decomposition");
      }
      if (!CanRepresentOgreNextN1WorldBounds(instance.local_bounds,
                                             FromOgreMatrix(reconstructed))) {
        return RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 reconstructed Ogre TRS can overflow native world bounds");
      }
      record.item = impl_->scene_manager->createItem(render_mesh->mesh,
                                                     Ogre::SCENE_DYNAMIC);
      const std::uint32_t authored_instance_visibility =
          instance.visibility_mask & native_authored_visibility_mask;
      const bool pbs_material =
          material->second.kind == Impl::NativeMaterial::Kind::PBS;
      Ogre::HlmsPbsDatablock *pbs_datablock =
          material->second.pbs_datablock;
      Ogre::HlmsUnlitDatablock *unlit_datablock =
          material->second.display_domain_unlit_datablock;
      if ((pbs_material &&
           (pbs_datablock == nullptr || unlit_datablock != nullptr)) ||
          (!pbs_material &&
           (pbs_datablock != nullptr || unlit_datablock == nullptr))) {
        throw std::logic_error(
            "Ogre-Next native material lifetime lost its exact HLMS type");
      }
      const bool receives_shadow =
          (instance.flags & MESH_INSTANCE_RECEIVES_SHADOW) != 0U;
      const bool authored_casts_shadow =
          (instance.flags & MESH_INSTANCE_CASTS_SHADOW) != 0U;
      if (!pbs_material && (receives_shadow || authored_casts_shadow)) {
        throw std::logic_error(
            "Ogre-Next display-domain Unlit reached submission with shadow flags");
      }
      Ogre::HlmsDatablock *instance_datablock =
          material->second.Datablock();
      const bool thin_slab_transmission =
          portable_material->transmission_mode ==
          MaterialTransmissionMode::THIN_PARALLEL_SLAB;
      if (pbs_material && shadow_plan.enabled && !receives_shadow) {
        create_receiver_clone(record, pbs_datablock);
        instance_datablock = record.receiver_clone;
      }
      record.item->setDatablock(instance_datablock);
      record.base_render_queue = record.item->getRenderQueueGroup();
      if (thin_slab_transmission) {
        record.item->setRenderQueueGroup(kOgreNextThinSlabRenderQueue);
      }
      record.item->setVisibilityFlags(authored_instance_visibility);
      const bool casts_shadow =
          pbs_material && shadow_plan.enabled &&
          MeshInstanceCastsShadowForLight(snapshot.lights().front(),
                                          instance, *base_mesh);
      record.item->setCastShadows(casts_shadow);
      record.node = impl_->scene_manager
                        ->getRootSceneNode(Ogre::SCENE_DYNAMIC)
                        ->createChildSceneNode(Ogre::SCENE_DYNAMIC);
      record.node->setPosition(position);
      record.node->setScale(scale);
      record.node->setOrientation(orientation);
      record.node->attachObject(record.item);
      ++diff_created;
      return RenderOperationResult::Success();
    };

    // Re-applies material, flag, visibility, and transform state to a
    // retained instance whose descriptor changed without changing its mesh
    // identity, topology, or deformation revision.
    const auto update_retained_instance =
        [&](Impl::RetainedInstance &record,
            const MeshInstanceDescriptor &instance) -> RenderOperationResult {
      const auto material = impl_->materials.find(instance.material.id);
      const MeshResourceDescriptor *base_mesh =
          impl_->registry->ResolveMesh(instance.mesh);
      const MaterialDescriptor *portable_material =
          impl_->registry->ResolveMaterial(instance.material);
      if (material == impl_->materials.end() || base_mesh == nullptr ||
          portable_material == nullptr) {
        return RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "N1 native asset allocation is missing for a validated scene");
      }
      // A clone is a copy of its source material's contents: when the
      // instance moves to a different material reference, the retained clone
      // must be recreated from the new source, not merely rebound.
      const bool material_reference_changed =
          record.descriptor.material != instance.material;
      record.descriptor = instance;
      record.dynamic_mesh = base_mesh->dynamic;
      const bool pbs_material =
          material->second.kind == Impl::NativeMaterial::Kind::PBS;
      Ogre::HlmsPbsDatablock *pbs_datablock =
          material->second.pbs_datablock;
      Ogre::HlmsUnlitDatablock *unlit_datablock =
          material->second.display_domain_unlit_datablock;
      if ((pbs_material &&
           (pbs_datablock == nullptr || unlit_datablock != nullptr)) ||
          (!pbs_material &&
           (pbs_datablock != nullptr || unlit_datablock == nullptr))) {
        throw std::logic_error(
            "Ogre-Next native material lifetime lost its exact HLMS type");
      }
      const bool receives_shadow =
          (instance.flags & MESH_INSTANCE_RECEIVES_SHADOW) != 0U;
      const bool authored_casts_shadow =
          (instance.flags & MESH_INSTANCE_CASTS_SHADOW) != 0U;
      if (!pbs_material && (receives_shadow || authored_casts_shadow)) {
        throw std::logic_error(
            "Ogre-Next display-domain Unlit reached submission with shadow flags");
      }
      const bool thin_slab_transmission =
          portable_material->transmission_mode ==
          MaterialTransmissionMode::THIN_PARALLEL_SLAB;
      const bool needs_receiver_clone =
          pbs_material && shadow_plan.enabled && !receives_shadow;
      if ((!needs_receiver_clone || material_reference_changed) &&
          record.receiver_clone != nullptr &&
          !impl_->DestroyRetainedReceiverClone(record)) {
        throw std::runtime_error(
            "Ogre-Next PSSM receiver clone retirement failed its native absence check");
      }
      if (needs_receiver_clone && record.receiver_clone == nullptr) {
        create_receiver_clone(record, pbs_datablock);
      }
      Ogre::HlmsDatablock *instance_datablock =
          record.receiver_clone != nullptr
              ? static_cast<Ogre::HlmsDatablock *>(record.receiver_clone)
              : material->second.Datablock();
      if (record.receiver_clone != nullptr) {
        record.receiver_clone->setReceiveShadows(false);
      }
      record.item->setDatablock(instance_datablock);
      record.item->setRenderQueueGroup(
          thin_slab_transmission ? kOgreNextThinSlabRenderQueue
                                 : record.base_render_queue);
      record.item->setVisibilityFlags(instance.visibility_mask &
                                      native_authored_visibility_mask);
      const bool casts_shadow =
          pbs_material && shadow_plan.enabled &&
          MeshInstanceCastsShadowForLight(snapshot.lights().front(),
                                          instance, *base_mesh);
      record.item->setCastShadows(casts_shadow);
      Ogre::Vector3 position;
      Ogre::Vector3 scale;
      Ogre::Quaternion orientation;
      Ogre::Matrix4 reconstructed;
      if (!DecomposeTrs(instance.render_from_object, position, scale,
                        orientation, reconstructed)) {
        return RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 transform did not survive exact Ogre TRS decomposition");
      }
      if (!CanRepresentOgreNextN1WorldBounds(instance.local_bounds,
                                             FromOgreMatrix(reconstructed))) {
        return RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 reconstructed Ogre TRS can overflow native world bounds");
      }
      record.node->setPosition(position);
      record.node->setScale(scale);
      record.node->setOrientation(orientation);
      return RenderOperationResult::Success();
    };

    // Deformable content changed: Items cannot rebind meshes, so the item is
    // recreated against a freshly uploaded deformed mesh while the retained
    // SceneNode, receiver clone, and record identity survive. The prior
    // deformed mesh retires through the interop frame-mesh list (destroyed
    // after the next published-frame discard); on the immediate path Ogre's
    // VaoManager already defers the actual GPU free by frame count.
    const auto rebuild_deformed_instance =
        [&](Impl::RetainedInstance &record,
            const MeshInstanceDescriptor &instance) -> RenderOperationResult {
      const MeshResourceDescriptor *base_mesh =
          impl_->registry->ResolveMesh(instance.mesh);
      if (base_mesh == nullptr) {
        return RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "N1 native asset allocation is missing for a validated scene");
      }
      if (record.item != nullptr) {
        record.node->detachObject(record.item);
        impl_->scene_manager->destroyItem(record.item);
        record.item = nullptr;
      }
      if (record.deformed_mesh.mesh) {
        if (impl_->native_interop) {
          impl_->frame_meshes.push_back(std::move(record.deformed_mesh));
        } else if (!impl_->DestroyMesh(record.deformed_mesh)) {
          throw std::runtime_error(
              "Ogre-Next retained deformed-mesh retirement failed");
        }
        record.deformed_mesh = Impl::NativeMesh{};
      }
      // Snapshots may back frames in any order, so a revision transition
      // back to one legitimately restores the undeformed catalog mesh.
      const Impl::NativeMesh *render_mesh = nullptr;
      if (instance.deformation_revision > 1U) {
        const auto update = std::find_if(
            snapshot.dynamic_mesh_updates().begin(),
            snapshot.dynamic_mesh_updates().end(),
            [&instance](const DynamicMeshUpdateDescriptor &candidate) {
              return candidate.instance_id == instance.instance_id;
            });
        if (update == snapshot.dynamic_mesh_updates().end() ||
            update->instance_id != instance.instance_id) {
          return RenderOperationResult::Failure(
              RenderOperationCode::RESOURCE_STALE,
              "N2 could not resolve the validated full deformation update");
        }
        MeshResourceDescriptor deformed = *base_mesh;
        deformed.positions = update->positions;
        deformed.normals = update->normals;
        deformed.tangents = update->tangents;
        deformed.velocities = update->velocities;
        deformed.local_bounds = update->updated_local_bounds;
        const std::string suffix =
            "_i" + std::to_string(instance.instance_id) + "_d" +
            std::to_string(instance.deformation_revision) + "_q" +
            std::to_string(++impl_->deformed_mesh_sequence);
        record.deformed_mesh =
            impl_->CreateMesh(instance.mesh, deformed, suffix);
        render_mesh = &record.deformed_mesh;
      } else {
        const auto mesh = impl_->meshes.find(instance.mesh.id);
        if (mesh == impl_->meshes.end() ||
            mesh->second.asset != instance.mesh) {
          return RenderOperationResult::Failure(
              RenderOperationCode::RESOURCE_STALE,
              "N1 native asset allocation is missing for a validated scene");
        }
        render_mesh = &mesh->second;
      }
      record.item = impl_->scene_manager->createItem(render_mesh->mesh,
                                                     Ogre::SCENE_DYNAMIC);
      record.node->attachObject(record.item);
      ++diff_dynamic_updates;
      return RenderOperationResult::Success();
    };

    // Full native verification of one retained instance: exactly the
    // creation readbacks. Runs for every created and updated instance in
    // the present that changed it, and for untouched instances on the
    // rotating window schedule. Any mismatch fails the present closed.
    const auto verify_retained_instance =
        [&](Impl::RetainedInstance &record) -> RenderOperationResult {
      const MeshInstanceDescriptor &instance = record.descriptor;
      const auto material = impl_->materials.find(instance.material.id);
      const MeshResourceDescriptor *base_mesh =
          impl_->registry->ResolveMesh(instance.mesh);
      const MaterialDescriptor *portable_material =
          impl_->registry->ResolveMaterial(instance.material);
      const Impl::NativeMesh *render_mesh =
          resolve_retained_render_mesh(record);
      if (material == impl_->materials.end() ||
          material->second.asset != instance.material ||
          base_mesh == nullptr || portable_material == nullptr ||
          render_mesh == nullptr) {
        return RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "N1 retained instance lost its synchronized native assets");
      }
      const bool pbs_material =
          material->second.kind == Impl::NativeMaterial::Kind::PBS;
      Ogre::HlmsPbsDatablock *pbs_datablock =
          material->second.pbs_datablock;
      Ogre::HlmsUnlitDatablock *unlit_datablock =
          material->second.display_domain_unlit_datablock;
      if ((pbs_material &&
           (pbs_datablock == nullptr || unlit_datablock != nullptr)) ||
          (!pbs_material &&
           (pbs_datablock != nullptr || unlit_datablock == nullptr))) {
        throw std::logic_error(
            "Ogre-Next native material lifetime lost its exact HLMS type");
      }
      const bool receives_shadow =
          (instance.flags & MESH_INSTANCE_RECEIVES_SHADOW) != 0U;
      const bool authored_casts_shadow =
          (instance.flags & MESH_INSTANCE_CASTS_SHADOW) != 0U;
      if (!pbs_material && (receives_shadow || authored_casts_shadow)) {
        throw std::logic_error(
            "Ogre-Next display-domain Unlit reached submission with shadow flags");
      }
      const bool thin_slab_transmission =
          portable_material->transmission_mode ==
          MaterialTransmissionMode::THIN_PARALLEL_SLAB;
      Ogre::HlmsDatablock *instance_datablock =
          record.receiver_clone != nullptr
              ? static_cast<Ogre::HlmsDatablock *>(record.receiver_clone)
              : material->second.Datablock();
      Ogre::HlmsPbsDatablock *instance_pbs_datablock =
          record.receiver_clone != nullptr ? record.receiver_clone
                                           : pbs_datablock;
      if (pbs_material && shadow_plan.enabled &&
          (instance_pbs_datablock->getReceiveShadows() != receives_shadow ||
           pbs_datablock->mShadowConstantBias !=
               kOgreNextPssmMaterialConstantBias)) {
        throw std::runtime_error(
            "Ogre-Next PSSM receiver or material bias failed native readback");
      }
      if (pbs_material) {
        VerifyPbsMapping(*instance_pbs_datablock, *portable_material);
      }
      const std::uint32_t authored_instance_visibility =
          instance.visibility_mask & native_authored_visibility_mask;
      const bool casts_shadow =
          pbs_material && shadow_plan.enabled &&
          MeshInstanceCastsShadowForLight(snapshot.lights().front(),
                                          instance, *base_mesh);
      if (record.item == nullptr || record.node == nullptr ||
          record.item->getCastShadows() != casts_shadow ||
          record.item->getRenderQueueGroup() !=
              (thin_slab_transmission ? kOgreNextThinSlabRenderQueue
                                      : record.base_render_queue) ||
          (!thin_slab_transmission &&
           record.base_render_queue >= kOgreNextThinSlabRenderQueue) ||
          record.item->getVisibilityFlags() != authored_instance_visibility ||
          record.item->getNumSubItems() != 1U ||
          record.item->getSubItem(0U)->getDatablock() != instance_datablock) {
        throw std::runtime_error(
            "Ogre-Next PBS datablock, caster, or visibility state failed native readback");
      }
      record.pbs = pbs_material;
      record.transmission = pbs_material && thin_slab_transmission;
      record.normal_mapped =
          pbs_material && portable_material->normal_texture.texture.valid();
      const Float3 &emissive = portable_material->emissive_factor;
      record.emissive = pbs_material &&
                        portable_material->emissive_strength > 0.0F &&
                        (emissive.x > 0.0F || emissive.y > 0.0F ||
                         emissive.z > 0.0F);
      record.casts_shadow = casts_shadow;
      record.receives_shadow =
          pbs_material && shadow_plan.enabled && receives_shadow;
      record.material_descriptor_version =
          pbs_material ? portable_material->version : 0U;
      if (pbs_material) {
        ++lighting_candidate.native_state_verifications;
      }
      Ogre::Vector3 position;
      Ogre::Vector3 scale;
      Ogre::Quaternion orientation;
      Ogre::Matrix4 reconstructed;
      if (!DecomposeTrs(instance.render_from_object, position, scale,
                        orientation, reconstructed)) {
        return RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 transform did not survive exact Ogre TRS decomposition");
      }
      if (shadow_plan.enabled) {
        observe_instance_bounds(record, *render_mesh, reconstructed,
                                casts_shadow, receives_shadow);
      }
      record.verified_frame_id = request.frame_id;
      ++diff_verified;
      return RenderOperationResult::Success();
    };

    // The thin-slab parameter block stores the projection's vertical scale,
    // which is frame state shared per material rather than per instance:
    // refresh every thin-slab PBS datablock once per present, before the
    // diff so a receiver clone created this present inherits the fresh
    // value. Retained clones are refreshed in the post-diff walk below.
    for (const auto &material_entry : impl_->materials) {
      if (material_entry.second.kind != Impl::NativeMaterial::Kind::PBS) {
        continue;
      }
      const MaterialDescriptor *portable_material =
          impl_->registry->ResolveMaterial(material_entry.second.asset);
      if (portable_material == nullptr ||
          portable_material->transmission_mode !=
              MaterialTransmissionMode::THIN_PARALLEL_SLAB) {
        continue;
      }
      Ogre::Vector4 parameters =
          material_entry.second.pbs_datablock->getUserValue(2U);
      parameters.w = std::fabs(validated_view.clip_from_view.elements[5U]);
      material_entry.second.pbs_datablock->setUserValue(2U, parameters);
    }

    // Merge-join of the sorted snapshot instance vector against the retained
    // map. A bit-identical descriptor — including a parked deformable whose
    // deformation_revision did not advance — costs no native work.
    const bool retained_shadow_state_changed =
        impl_->retained_scene_shadow_enabled != shadow_plan.enabled;
    std::vector<std::uint64_t> stale_instances;
    std::vector<const MeshInstanceDescriptor *> incoming_instances;
    std::vector<const MeshInstanceDescriptor *> deforming_instances;
    std::vector<const MeshInstanceDescriptor *> changed_instances;
    std::size_t skipped_non_drawable_instances = 0U;
    {
      auto retained = impl_->retained_instances.begin();
      for (const MeshInstanceDescriptor &instance :
           snapshot.mesh_instances()) {
        while (retained != impl_->retained_instances.end() &&
               retained->first < instance.instance_id) {
          stale_instances.push_back(retained->first);
          ++retained;
        }
        // F7. The pinned PBS vertex path multiplies authored normals AND
        // tangents by worldViewMat with no inverse-transpose equivalent, so a
        // non-uniformly scaled instance cannot carry a correct tangent frame.
        // Refusing to draw it is right; refusing to draw anything ever again
        // is not. Drop the object, keep the frame, count the drop. The
        // producer filters these out upstream; this is the backstop for an
        // instance that reaches the presenter anyway.
        //
        // Scoped to the RT4/V1 tier exactly as the retired scene-level policy
        // check was: no other tier ever refused a non-uniform scale, and this
        // must not start dropping instances those tiers render correctly.
        if (impl_->raster_feature_tier ==
                OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1 &&
            !HasEffectivelyUniformLinearScale(instance.render_from_object)) {
          ++impl_->degrade_audit.non_uniform_scale_instance_rejections;
          ++skipped_non_drawable_instances;
          // Retire it if an earlier frame retained it, so it leaves the
          // picture rather than freezing at its last drawable transform.
          if (retained != impl_->retained_instances.end() &&
              retained->first == instance.instance_id) {
            stale_instances.push_back(retained->first);
            ++retained;
          }
          continue;
        }
        if (retained == impl_->retained_instances.end() ||
            retained->first > instance.instance_id) {
          incoming_instances.push_back(&instance);
          continue;
        }
        const MeshInstanceDescriptor &previous = retained->second.descriptor;
        if (previous.mesh != instance.mesh ||
            previous.topology_revision != instance.topology_revision) {
          // An Item cannot rebind its mesh: mesh identity and topology
          // changes go through destroy + create.
          stale_instances.push_back(retained->first);
          incoming_instances.push_back(&instance);
        } else if (previous.deformation_revision !=
                   instance.deformation_revision) {
          deforming_instances.push_back(&instance);
        } else if (retained_shadow_state_changed ||
                   !SameMeshInstanceDescriptor(previous, instance)) {
          changed_instances.push_back(&instance);
        }
        ++retained;
      }
      for (; retained != impl_->retained_instances.end(); ++retained) {
        stale_instances.push_back(retained->first);
      }
    }
    // Destroys run first: they free receiver-clone names and interop slots
    // that the updates and creates below may reclaim.
    for (const std::uint64_t stale_instance : stale_instances) {
      const auto record = impl_->retained_instances.find(stale_instance);
      if (record == impl_->retained_instances.end()) {
        throw std::logic_error(
            "retained-scene diff lost a scheduled instance retirement");
      }
      if (!impl_->DestroyRetainedInstanceNative(record->second)) {
        throw std::runtime_error(
            "Ogre-Next retained instance retirement failed its native absence check");
      }
      impl_->SubtractRetainedContribution(record->second);
      impl_->retained_instances.erase(record);
      ++diff_destroyed;
    }
    for (const MeshInstanceDescriptor *instance : deforming_instances) {
      const auto record =
          impl_->retained_instances.find(instance->instance_id);
      if (record == impl_->retained_instances.end()) {
        throw std::logic_error(
            "retained-scene diff lost a scheduled deformation update");
      }
      impl_->SubtractRetainedContribution(record->second);
      const RenderOperationResult rebuilt =
          rebuild_deformed_instance(record->second, *instance);
      if (!rebuilt) {
        return fail_after_cleanup(rebuilt);
      }
      const RenderOperationResult updated =
          update_retained_instance(record->second, *instance);
      if (!updated) {
        return fail_after_cleanup(updated);
      }
      const RenderOperationResult verified =
          verify_retained_instance(record->second);
      if (!verified) {
        return fail_after_cleanup(verified);
      }
      impl_->AddRetainedContribution(record->second);
    }
    for (const MeshInstanceDescriptor *instance : changed_instances) {
      const auto record =
          impl_->retained_instances.find(instance->instance_id);
      if (record == impl_->retained_instances.end()) {
        throw std::logic_error(
            "retained-scene diff lost a scheduled instance update");
      }
      impl_->SubtractRetainedContribution(record->second);
      const RenderOperationResult updated =
          update_retained_instance(record->second, *instance);
      if (!updated) {
        return fail_after_cleanup(updated);
      }
      const RenderOperationResult verified =
          verify_retained_instance(record->second);
      if (!verified) {
        return fail_after_cleanup(verified);
      }
      impl_->AddRetainedContribution(record->second);
      ++diff_updated;
    }
    for (const MeshInstanceDescriptor *instance : incoming_instances) {
      const RenderOperationResult created =
          create_retained_instance(*instance);
      if (!created) {
        return fail_after_cleanup(created);
      }
      const auto record =
          impl_->retained_instances.find(instance->instance_id);
      if (record == impl_->retained_instances.end()) {
        throw std::logic_error(
            "retained-scene diff lost a just-created instance");
      }
      const RenderOperationResult verified =
          verify_retained_instance(record->second);
      if (!verified) {
        return fail_after_cleanup(verified);
      }
      impl_->AddRetainedContribution(record->second);
    }
    impl_->retained_scene_shadow_enabled = shadow_plan.enabled;
    // Instances skipped for a non-drawable transform are deliberately absent
    // from the retained scene, so they are subtracted from the expected total
    // rather than allowed to trip this invariant.
    if (impl_->retained_instances.size() +
            skipped_non_drawable_instances !=
        snapshot.mesh_instances().size()) {
      throw std::logic_error(
          "retained native scene diverged from the admitted snapshot after its diff");
    }
    // Rotating fail-closed re-verification: retained native state is not
    // provably immutable between presents (the HDR split listener rewrites
    // directional power, reflection capture rewrites visibility flags, and
    // Ogre itself is a large mutable engine), so up to
    // kOgreNextRetainedVerifyWindow untouched instances are re-read from
    // Ogre each present. Every retained instance is therefore re-verified
    // at least once every ceil(N / window) presents.
    if (!impl_->retained_instances.empty()) {
      const std::size_t retained_total = impl_->retained_instances.size();
      std::uint64_t window_budget = kOgreNextRetainedVerifyWindow;
      auto window_cursor = impl_->retained_instances.upper_bound(
          impl_->retained_audit.verify_cursor);
      std::size_t window_visited = 0U;
      while (window_budget != 0U && window_visited < retained_total) {
        if (window_cursor == impl_->retained_instances.end()) {
          window_cursor = impl_->retained_instances.begin();
        }
        Impl::RetainedInstance &record = window_cursor->second;
        const std::uint64_t cursor_id = window_cursor->first;
        ++window_visited;
        ++window_cursor;
        impl_->retained_audit.verify_cursor = cursor_id;
        if (record.verified_frame_id == request.frame_id) {
          // Created or updated this present: already verified above.
          continue;
        }
        const RenderOperationResult verified =
            verify_retained_instance(record);
        if (!verified) {
          return fail_after_cleanup(verified);
        }
        --window_budget;
      }
    }
    // Rebuild the per-present views and publish the retained aggregates.
    // This walk is CPU-only POD work: no native scene calls.
    std::uint32_t retained_material_descriptor_version = 0U;
    for (auto &entry : impl_->retained_instances) {
      Impl::RetainedInstance &record = entry.second;
      retained_material_descriptor_version =
          std::max(retained_material_descriptor_version,
                   record.material_descriptor_version);
      if (record.receiver_clone != nullptr && record.transmission) {
        Ogre::Vector4 clone_parameters =
            record.receiver_clone->getUserValue(2U);
        clone_parameters.w =
            std::fabs(validated_view.clip_from_view.elements[5U]);
        record.receiver_clone->setUserValue(2U, clone_parameters);
      }
      reflection_items.push_back(OgreNextReflectionProbeItemBinding{
          reinterpret_cast<std::uintptr_t>(record.item),
          record.descriptor.visibility_mask &
              native_authored_visibility_mask,
          record.descriptor.flags, record.dynamic_mesh});
      if (impl_->native_interop) {
        const Impl::NativeMesh *render_mesh =
            resolve_retained_render_mesh(record);
        if (render_mesh == nullptr) {
          return fail_after_cleanup(RenderOperationResult::Failure(
              RenderOperationCode::RESOURCE_STALE,
              "N1 retained instance lost its synchronized native mesh"));
        }
        OgreNextN2FrameGeometryBinding binding;
        binding.frame_id = request.frame_id;
        binding.snapshot_id = snapshot.snapshot_id();
        binding.instance_id = record.descriptor.instance_id;
        binding.mesh = record.descriptor.mesh;
        binding.topology_revision = record.descriptor.topology_revision;
        binding.deformation_revision =
            record.descriptor.deformation_revision;
        binding.native_storage_generation =
            render_mesh->native_storage_generation;
        binding.topology = MeshPrimitiveTopology::TRIANGLE_LIST;
        binding.ogre_vertex_buffer = reinterpret_cast<std::uintptr_t>(
            render_mesh->vertex_buffer);
        binding.ogre_index_buffer = reinterpret_cast<std::uintptr_t>(
            render_mesh->index_buffer);
        binding.vertex_layout = render_mesh->vertex_layout;
        binding.position_offset_bytes = 0U;
        binding.vertex_stride_bytes = render_mesh->vertex_stride_bytes;
        binding.vertex_count = static_cast<std::uint32_t>(
            render_mesh->vertex_buffer->getNumElements());
        binding.index_count = static_cast<std::uint32_t>(
            render_mesh->index_buffer->getNumElements());
        binding.index_format =
            render_mesh->index_buffer->getIndexType() ==
                    Ogre::IndexBufferPacked::IT_16BIT
                ? NativeIndexFormat::UINT16
                : NativeIndexFormat::UINT32;
        interop_geometry.push_back(binding);
      }
    }
    lighting_candidate.last_material_descriptor_version =
        retained_material_descriptor_version;
    lighting_candidate.last_pbs_items = impl_->retained_pbs_items;
    lighting_candidate.last_transmission_items =
        impl_->retained_transmission_items;
    lighting_candidate.last_normal_mapped_items =
        impl_->retained_normal_mapped_items;
    lighting_candidate.last_emissive_items = impl_->retained_emissive_items;
    lighting_candidate.last_shadow_casters = impl_->retained_shadow_casters;
    lighting_candidate.last_shadow_receivers =
        impl_->retained_shadow_receivers;
    // The retained aggregates must agree with the plan the policy recomputed
    // from this snapshot alone. Non-PBS instances with shadow flags are
    // rejected above, so the plan's counts and the PBS-only aggregates
    // describe the same population.
    if (shadow_plan.enabled &&
        (impl_->retained_shadow_receivers != shadow_plan.receiver_count ||
         impl_->retained_shadow_casters !=
             shadow_plan.static_caster_count +
                 shadow_plan.dynamic_caster_count)) {
      throw std::runtime_error(
          "retained shadow aggregates diverged from the snapshot-derived PSSM plan");
    }
    if (!shadow_plan.enabled &&
        (impl_->retained_shadow_receivers != 0U ||
         impl_->retained_shadow_casters != 0U)) {
      throw std::runtime_error(
          "retained shadow aggregates survived a disabled shadow plan");
    }
    // The committed bounds evidence stays a complete per-instance set: it is
    // materialized from the retained records (sorted by instance_id, like
    // the snapshot) into a frame-local vector here, and moved into the
    // shadow audit only after every publication stage has prepared.
    if (shadow_plan.enabled) {
      if (impl_->retained_audit.bounds_entries !=
          static_cast<std::uint64_t>(impl_->retained_instances.size())) {
        throw std::runtime_error(
            "Ogre-Next PSSM native bounds observation set is incomplete");
      }
      native_bounds_observations.reserve(impl_->retained_instances.size());
      for (const auto &entry : impl_->retained_instances) {
        native_bounds_observations.push_back(entry.second.bounds);
      }
    }
    impl_->retained_audit.last_instance_phase_microseconds =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - instance_phase_start)
                .count());

    impl_->camera->setNearClipDistance(view.near_plane);
    impl_->camera->setFarClipDistance(view.far_plane);
    impl_->camera->setAspectRatio(static_cast<float>(view.width) /
                                  static_cast<float>(view.height));
    const Ogre::Matrix4 native_view = ToOgreMatrix(view.view_from_render);
    if (shadow_plan.enabled) {
      // Ogre's focused/concentric shadow-camera setups consult the Camera's
      // derived pose in addition to its view matrix. Keep those two sources
      // exactly coherent; a custom view matrix alone leaves the derived pose
      // at the origin and can silently produce an empty PSSM atlas.
      Ogre::Vector3 camera_position;
      Ogre::Vector3 camera_scale;
      Ogre::Quaternion camera_orientation;
      const Ogre::Matrix4 pssm_render_from_view = native_view.inverseAffine();
      pssm_render_from_view.decomposition(camera_position, camera_scale,
                                          camera_orientation);
      // F5. This site had two defects, and they compounded.
      //
      // It bounded `native_view.inverseAffine().decomposition(...)` with the
      // generic 1.0e-6 NearlyEqual -- the same datum whose noise floor is
      // documented at kCameraBasisOrthonormalTolerance, which exists precisely
      // because inverseAffine inverts by cofactors and returns an orthonormal
      // basis only to a few float32 ulps. And on the very frame where
      // ConfigureAerialHazeForFrame correctly DEGRADES on that basis, this
      // check then ended the session anyway, defeating the degrade path added
      // for the bug it exists for.
      //
      // Reuse the calibrated bound (never a new constant) and, on rejection,
      // renormalize the pose to the nearest rigid frame instead of failing.
      // The camera's derived pose only feeds Ogre's focused shadow-camera
      // setup; setCustomViewMatrix below is applied unconditionally and is
      // what actually transforms the scene, so a renormalized pose cannot move
      // the rendered image -- it only keeps the shadow-camera setup coherent.
      const Ogre::Vector3 pssm_camera_right(pssm_render_from_view[0U][0U],
                                            pssm_render_from_view[1U][0U],
                                            pssm_render_from_view[2U][0U]);
      const Ogre::Vector3 pssm_camera_up(pssm_render_from_view[0U][1U],
                                         pssm_render_from_view[1U][1U],
                                         pssm_render_from_view[2U][1U]);
      const Ogre::Vector3 pssm_camera_forward(pssm_render_from_view[0U][2U],
                                              pssm_render_from_view[1U][2U],
                                              pssm_render_from_view[2U][2U]);
      if (!IsRigidOrthonormalCameraBasis(pssm_camera_right, pssm_camera_up,
                                         pssm_camera_forward)) {
        ++impl_->degrade_audit.pssm_pose_renormalizations;
      }
      // A no-op for a basis already rigid within the calibrated bound, and the
      // nearest rigid frame otherwise. Ogre's shadow-camera setup requires a
      // unit orientation either way.
      camera_orientation.normalise();
      impl_->camera->setPosition(camera_position);
      impl_->camera->setOrientation(camera_orientation);
      if (!NearlyEqual(impl_->camera->getDerivedPosition(), camera_position)) {
        throw std::runtime_error(
            "Ogre-Next PSSM camera derived position differs from the reviewed view matrix");
      }
    }
    impl_->camera->setCustomViewMatrix(true, native_view);
    // Convert the renderer-boundary [0,1] depth explicitly. The pinned Ogre
    // alternateDepthRange path applies the inverse transform here, so N1
    // supplies canonical [-1,1] clip depth and lets the active RenderSystem
    // perform exactly one native Metal/D3D11/Vulkan conversion.
    Matrix4x4 converted_projection;
    if (!TryConvertPortableProjectionToOgreClip(view.clip_from_view,
                                                 converted_projection)) {
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "N1 projection did not survive Ogre clip-depth conversion"));
    }
    const Ogre::Matrix4 native_projection =
        ToOgreMatrix(converted_projection);
    if (shadow_plan.enabled) {
      ConfigureAndVerifyPssmProjection(
          *impl_->camera, shadow_plan.projection_extents, native_projection);
      impl_->shadow_audit.native_projection_extents_verified = true;
    } else {
      impl_->camera->setCustomProjectionMatrix(true, native_projection, false);
    }

    if (portable_sky.enabled) {
      Ogre::HlmsMacroblock sky_macroblock;
      sky_macroblock.mDepthCheck = false;
      sky_macroblock.mDepthWrite = false;
      sky_macroblock.mDepthFunc = Ogre::CMPF_ALWAYS_PASS;
      sky_macroblock.mCullMode = Ogre::CULL_NONE;
      Ogre::HlmsBlendblock background_blendblock;
      background_blendblock.setBlendType(Ogre::SBT_REPLACE);
      Ogre::HlmsBlendblock sun_blendblock;
      sun_blendblock.setBlendType(Ogre::SBT_ADD, Ogre::SBT_REPLACE);

      analytic_sky_background_datablock_name =
          "RoRAnalyticSkyGradient_f" + std::to_string(request.frame_id);
      analytic_sky_sun_datablock_name =
          "RoRAnalyticSkySun_f" + std::to_string(request.frame_id);
      if (impl_->unlit->getDatablock(
              Ogre::IdString(analytic_sky_background_datablock_name)) !=
              nullptr ||
          impl_->unlit->getDatablock(
              Ogre::IdString(analytic_sky_sun_datablock_name)) != nullptr) {
        throw std::logic_error(
            "N1 native analytic-sky datablock name was still live before allocation");
      }
      analytic_sky_background_datablock =
          dynamic_cast<Ogre::HlmsUnlitDatablock *>(
              (analytic_sky_background_datablock_attempted = true,
               impl_->unlit->createDatablock(
                  analytic_sky_background_datablock_name,
                  analytic_sky_background_datablock_name, sky_macroblock,
                  background_blendblock, Ogre::HlmsParamVec())));
      if (analytic_sky_background_datablock == nullptr) {
        throw std::logic_error(
            "N1 native analytic-sky background datablock creation returned "
            "a non-Unlit value");
      }
      ++impl_->analytic_sky_audit.native_datablock_creates;
      analytic_sky_background_datablock->setUseColour(true);
      analytic_sky_background_datablock->setColour(
          Ogre::ColourValue::White);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      impl_->MaybeInjectAnalyticSkyFailure(
          OgreNextN1AnalyticSkyFailureStage::AFTER_BACKGROUND_DATABLOCK,
          "injected analytic-sky background-datablock rollback failure");
#endif
      analytic_sky_sun_datablock =
          dynamic_cast<Ogre::HlmsUnlitDatablock *>(
              (analytic_sky_sun_datablock_attempted = true,
               impl_->unlit->createDatablock(
                  analytic_sky_sun_datablock_name,
                  analytic_sky_sun_datablock_name, sky_macroblock,
                  sun_blendblock, Ogre::HlmsParamVec())));
      if (analytic_sky_sun_datablock == nullptr) {
        throw std::logic_error(
            "N1 native analytic-sky sun datablock creation returned a "
            "non-Unlit value");
      }
      ++impl_->analytic_sky_audit.native_datablock_creates;
      analytic_sky_sun_datablock->setUseColour(true);
      analytic_sky_sun_datablock->setColour(Ogre::ColourValue::White);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      impl_->MaybeInjectAnalyticSkyFailure(
          OgreNextN1AnalyticSkyFailureStage::AFTER_SUN_DATABLOCK,
          "injected analytic-sky sun-datablock rollback failure");
#endif

      analytic_sky_background_section = impl_->CreateAnalyticSkySection(
          "RoRAnalyticSkyBackgroundMesh_f" +
              std::to_string(request.frame_id),
          analytic_sky_mesh.background_vertices,
          analytic_sky_mesh.background_indices, analytic_sky_native_radius,
          true);
      analytic_sky_sun_section = impl_->CreateAnalyticSkySection(
          "RoRAnalyticSkySunMesh_f" + std::to_string(request.frame_id),
          analytic_sky_mesh.sun_vertices, analytic_sky_mesh.sun_indices,
          analytic_sky_native_radius, false);

      analytic_sky_background_item = impl_->scene_manager->createItem(
          analytic_sky_background_section.mesh, Ogre::SCENE_DYNAMIC);
      if (analytic_sky_background_item == nullptr) {
        throw std::logic_error(
            "N1 native analytic-sky background Item creation returned null");
      }
      ++impl_->analytic_sky_audit.native_item_creates;
      analytic_sky_background_item_id =
          analytic_sky_background_item->getId();
      analytic_sky_background_item_id_valid = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      impl_->MaybeInjectAnalyticSkyFailure(
          OgreNextN1AnalyticSkyFailureStage::AFTER_BACKGROUND_ITEM,
          "injected analytic-sky background-Item rollback failure");
#endif
      analytic_sky_sun_item = impl_->scene_manager->createItem(
          analytic_sky_sun_section.mesh, Ogre::SCENE_DYNAMIC);
      if (analytic_sky_sun_item == nullptr) {
        throw std::logic_error(
            "N1 native analytic-sky sun Item creation returned null");
      }
      ++impl_->analytic_sky_audit.native_item_creates;
      analytic_sky_sun_item_id = analytic_sky_sun_item->getId();
      analytic_sky_sun_item_id_valid = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      impl_->MaybeInjectAnalyticSkyFailure(
          OgreNextN1AnalyticSkyFailureStage::AFTER_SUN_ITEM,
          "injected analytic-sky sun-Item rollback failure");
#endif

      analytic_sky_node = impl_->scene_manager->getRootSceneNode(
          Ogre::SCENE_DYNAMIC)->createChildSceneNode(Ogre::SCENE_DYNAMIC);
      if (analytic_sky_node == nullptr) {
        throw std::logic_error(
            "N1 native analytic-sky scene-node creation returned null");
      }
      ++impl_->analytic_sky_audit.native_scene_node_creates;
      analytic_sky_node_id = analytic_sky_node->getId();
      analytic_sky_node_id_valid = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      impl_->MaybeInjectAnalyticSkyFailure(
          OgreNextN1AnalyticSkyFailureStage::AFTER_SCENE_NODE,
          "injected analytic-sky scene-node rollback failure");
#endif
      const Ogre::Vector3 camera_position =
          native_view.inverseAffine().getTrans();
      analytic_sky_node->setPosition(camera_position);
      analytic_sky_background_item->setDatablock(
          analytic_sky_background_datablock);
      analytic_sky_sun_item->setDatablock(analytic_sky_sun_datablock);
      analytic_sky_background_item->setRenderQueueGroup(0U);
      analytic_sky_sun_item->setRenderQueueGroup(0U);
      analytic_sky_background_item->setVisibilityFlags(
          authored_view_visibility);
      analytic_sky_sun_item->setVisibilityFlags(authored_view_visibility);
      analytic_sky_background_item->setCastShadows(false);
      analytic_sky_sun_item->setCastShadows(false);
      if (analytic_sky_background_item->getNumSubItems() != 1U ||
          analytic_sky_sun_item->getNumSubItems() != 1U) {
        throw std::logic_error(
            "N1 native analytic-sky Item did not retain one exact section");
      }
      Ogre::SubItem *background_subitem =
          analytic_sky_background_item->getSubItem(0U);
      Ogre::SubItem *sun_subitem = analytic_sky_sun_item->getSubItem(0U);
      background_subitem->setRenderQueueSubGroup(0U);
      sun_subitem->setRenderQueueSubGroup(1U);
      analytic_sky_node->attachObject(analytic_sky_background_item);
      analytic_sky_node->attachObject(analytic_sky_sun_item);

      const auto exact_sky_section =
          [&](const Impl::NativeAnalyticSkySection &section,
              const std::vector<OgreNextAnalyticSkyNativeVertex> &vertices,
              const std::vector<std::uint32_t> &indices, Ogre::Item *item,
              Ogre::SubItem *subitem, Ogre::HlmsDatablock *datablock,
              Ogre::uint8 subgroup) {
        if (!section.mesh || section.mesh->getNumSubMeshes() != 1U ||
            item == nullptr || subitem == nullptr ||
            item->getMesh() != section.mesh || item->getNumSubItems() != 1U ||
            item->getSubItem(0U) != subitem ||
            subitem->getSubMesh() != section.mesh->getSubMesh(0U) ||
            subitem->getDatablock() != datablock ||
            subitem->getRenderQueueSubGroup() != subgroup) {
          return false;
        }
        const Ogre::VertexArrayObjectArray &vaos =
            section.mesh->getSubMesh(0U)->mVao[Ogre::VpNormal];
        if (vaos.size() != 1U || vaos.front() != section.vao ||
            section.vao == nullptr ||
            vaos.front()->getVertexBuffers().size() != 1U ||
            vaos.front()->getVertexBuffers().front() !=
                section.vertex_buffer ||
            vaos.front()->getIndexBuffer() != section.index_buffer ||
            vaos.front()->getOperationType() != Ogre::OT_TRIANGLE_LIST ||
            vaos.front()->getPrimitiveStart() != 0U ||
            vaos.front()->getPrimitiveCount() != indices.size() ||
            section.vertex_buffer == nullptr ||
            section.vertex_buffer->getNumElements() != vertices.size() ||
            section.vertex_buffer->getBytesPerElement() !=
                sizeof(OgreNextAnalyticSkyNativeVertex) ||
            section.index_buffer == nullptr ||
            section.index_buffer->getNumElements() != indices.size() ||
            section.index_buffer->getBytesPerElement() !=
                sizeof(std::uint32_t) ||
            section.index_buffer->getIndexType() !=
                Ogre::IndexBufferPacked::IT_32BIT) {
          return false;
        }
        const Ogre::VertexElement2Vec &elements =
            section.vertex_buffer->getVertexElements();
        const bool exact_native_metadata =
            elements.size() == 2U &&
            elements[0U].mSemantic == Ogre::VES_POSITION &&
            elements[0U].mType == Ogre::VET_FLOAT3 &&
            elements[1U].mSemantic == Ogre::VES_DIFFUSE &&
            elements[1U].mType == Ogre::VET_FLOAT4;
        if (!exact_native_metadata ||
            !impl_->retain_analytic_sky_geometry_content_evidence) {
          return exact_native_metadata;
        }
        return impl_->ExactAnalyticSkyBufferContents(
                   section.vertex_buffer, vertices.data(), vertices.size(),
                   sizeof(OgreNextAnalyticSkyNativeVertex)) &&
               impl_->ExactAnalyticSkyBufferContents(
                   section.index_buffer, indices.data(), indices.size(),
                   sizeof(std::uint32_t));
      };
      const Ogre::HlmsMacroblock *background_macroblock =
          analytic_sky_background_datablock->getMacroblock();
      const Ogre::HlmsMacroblock *sun_macroblock =
          analytic_sky_sun_datablock->getMacroblock();
      const Ogre::HlmsBlendblock *background_blend =
          analytic_sky_background_datablock->getBlendblock();
      const Ogre::HlmsBlendblock *sun_blend =
          analytic_sky_sun_datablock->getBlendblock();
      if (!exact_sky_section(
              analytic_sky_background_section,
              analytic_sky_mesh.background_vertices,
              analytic_sky_mesh.background_indices,
              analytic_sky_background_item, background_subitem,
              analytic_sky_background_datablock, 0U) ||
          !exact_sky_section(analytic_sky_sun_section,
                             analytic_sky_mesh.sun_vertices,
                             analytic_sky_mesh.sun_indices,
                             analytic_sky_sun_item, sun_subitem,
                             analytic_sky_sun_datablock, 1U) ||
          background_macroblock == nullptr || sun_macroblock == nullptr ||
          background_blend == nullptr || sun_blend == nullptr ||
          background_macroblock->mDepthCheck ||
          background_macroblock->mDepthWrite ||
          background_macroblock->mDepthFunc != Ogre::CMPF_ALWAYS_PASS ||
          background_macroblock->mCullMode != Ogre::CULL_NONE ||
          *sun_macroblock != *background_macroblock ||
          background_blend->mSeparateBlend ||
          background_blend->mSourceBlendFactor != Ogre::SBF_ONE ||
          background_blend->mDestBlendFactor != Ogre::SBF_ZERO ||
          background_blend->mBlendOperation != Ogre::SBO_ADD ||
          !sun_blend->mSeparateBlend ||
          sun_blend->mSourceBlendFactor != Ogre::SBF_ONE ||
          sun_blend->mDestBlendFactor != Ogre::SBF_ONE ||
          sun_blend->mSourceBlendFactorAlpha != Ogre::SBF_ONE ||
          sun_blend->mDestBlendFactorAlpha != Ogre::SBF_ZERO ||
          sun_blend->mBlendOperation != Ogre::SBO_ADD ||
          sun_blend->mBlendOperationAlpha != Ogre::SBO_ADD ||
          !analytic_sky_background_datablock->hasColour() ||
          !analytic_sky_sun_datablock->hasColour() ||
          analytic_sky_background_datablock->getColour() !=
              Ogre::ColourValue::White ||
          analytic_sky_sun_datablock->getColour() !=
              Ogre::ColourValue::White ||
          analytic_sky_background_item->getRenderQueueGroup() != 0U ||
          analytic_sky_sun_item->getRenderQueueGroup() != 0U ||
          analytic_sky_background_item->getVisibilityFlags() !=
              authored_view_visibility ||
          analytic_sky_sun_item->getVisibilityFlags() !=
              authored_view_visibility ||
          analytic_sky_background_item->getCastShadows() ||
          analytic_sky_sun_item->getCastShadows() ||
          !analytic_sky_background_item->isAttached() ||
          !analytic_sky_sun_item->isAttached() ||
          analytic_sky_background_item->getParentSceneNode() !=
              analytic_sky_node ||
          analytic_sky_sun_item->getParentSceneNode() != analytic_sky_node ||
          analytic_sky_node->getPosition() != camera_position) {
        throw std::logic_error(
            "N1 native analytic sky failed exact v2 Item/VAO metadata, optional test-artifact content, camera-centred, render-first, no-depth, or separate-alpha verification");
      }
      ++impl_->analytic_sky_audit.native_state_verifications;
      analytic_sky_frame_completed = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      impl_->MaybeInjectAnalyticSkyFailure(
          OgreNextN1AnalyticSkyFailureStage::
              AFTER_ATTACHED_STATE_VERIFICATION,
          "injected analytic-sky attached-state rollback failure");
#endif
    }

    const auto &continuous_systems =
        impl_->particle_runtime.prepared_systems();
    std::size_t visible_particle_count = 0U;
    RenderAssetReference particle_material_asset;
    RenderAssetReference particle_texture_asset;
    RenderAssetReference particle_sampler_asset;
    const Impl::NativeTexture *particle_texture = nullptr;
    const SamplerResourceDescriptor *particle_sampler = nullptr;
    for (const auto &system : continuous_systems) {
      if (system == nullptr || !system->effective_visible) {
        continue;
      }
      if (system->billboard_mode !=
              Ogre14ParticleBillboardMode::CAMERA_FACING_POINT ||
          system->billboard_rotation_mode !=
              Ogre14ParticleBillboardRotationMode::TEXTURE_COORDINATES) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 native Dust batch requires common-facing point quads with "
            "pinned BBR_TEXCOORD rotation"));
      }
      const MaterialDescriptor *const material = impl_->registry->ResolveMaterial(
          system->material_closure.material);
      const auto native_texture = impl_->textures.find(
          system->material_closure.source_texture.id);
      const SamplerResourceDescriptor *const sampler =
          impl_->registry->ResolveSampler(system->material_closure.sampler);
      if (material == nullptr || native_texture == impl_->textures.end() ||
          native_texture->second.asset !=
              system->material_closure.source_texture ||
          native_texture->second.sampled == nullptr ||
          !native_texture->second.usage.sampled_rgba ||
          native_texture->second.usage.display_domain_rgba ||
          native_texture->second.usage.roughness_g ||
          native_texture->second.usage.metallic_b ||
          native_texture->second.usage.normal_rg || sampler == nullptr ||
          material->base_color_texture.texture !=
              system->material_closure.source_texture ||
          material->base_color_texture.sampler !=
              system->material_closure.sampler) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "N1 particle source texture, sampler, or material closure is missing from the exact live catalog"));
      }
      if (particle_texture != nullptr &&
          (particle_material_asset != system->material_closure.material ||
           particle_texture_asset != system->material_closure.source_texture ||
           particle_sampler_asset != system->material_closure.sampler)) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 particle v1 admits one exact tracks/SmokeMat batch"));
      }
      particle_material_asset = system->material_closure.material;
      particle_texture_asset = system->material_closure.source_texture;
      particle_sampler_asset = system->material_closure.sampler;
      particle_texture = &native_texture->second;
      particle_sampler = sampler;
      if (visible_particle_count >
          (std::numeric_limits<std::size_t>::max)() -
              system->particles.size()) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::OUT_OF_MEMORY,
            "N1 particle vertex count overflowed"));
      }
      visible_particle_count += system->particles.size();
    }
    // F6. Billboard quads are built directly from the camera basis, so a basis
    // that is not rigid-orthonormal within the calibrated inverse-affine
    // rounding bound cannot produce correct particles. The tolerance here is
    // already the calibrated one; only the response was wrong. Failing the
    // frame ended the session -- during heavy driving, which is exactly when
    // dust is on screen.
    //
    // The comment this replaces claimed particles "have no defined no-op state
    // the way haze does". They do, and it is the state every frame with no
    // visible system already takes: emit no batch. particle_batches stays
    // empty, particle_native_particles_submitted stays where it was, one frame
    // ships without dust, and the degrade is counted rather than silent.
    const Ogre::Matrix4 particle_render_from_view =
        native_view.inverseAffine();
    const Ogre::Vector3 particle_camera_right(particle_render_from_view[0U][0U],
                                              particle_render_from_view[1U][0U],
                                              particle_render_from_view[2U][0U]);
    const Ogre::Vector3 particle_camera_up(particle_render_from_view[0U][1U],
                                           particle_render_from_view[1U][1U],
                                           particle_render_from_view[2U][1U]);
    const bool particle_camera_basis_rigid = IsRigidOrthonormalCameraBasis(
        particle_camera_right, particle_camera_up,
        particle_camera_right.crossProduct(particle_camera_up));
    if (particle_texture != nullptr && visible_particle_count != 0U &&
        !particle_camera_basis_rigid) {
      ++impl_->degrade_audit.particle_basis_rejections;
    }
    if (particle_texture != nullptr && visible_particle_count != 0U &&
        particle_camera_basis_rigid) {
      if (impl_->particle_native_batch_creates ==
              (std::numeric_limits<std::uint64_t>::max)() ||
          impl_->particle_native_batch_destroys ==
              (std::numeric_limits<std::uint64_t>::max)() ||
          impl_->particle_native_state_verifications ==
              (std::numeric_limits<std::uint64_t>::max)() ||
          visible_particle_count >
              (std::numeric_limits<std::uint64_t>::max)() -
                  impl_->particle_native_particles_submitted) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 continuous-particle lifetime telemetry exhausted"));
      }
      Ogre::HlmsMacroblock particle_macroblock;
      particle_macroblock.mDepthCheck = true;
      particle_macroblock.mDepthWrite = false;
      particle_macroblock.mDepthFunc = Ogre::CMPF_LESS_EQUAL;
      particle_macroblock.mCullMode = Ogre::CULL_CLOCKWISE;
      Ogre::HlmsBlendblock particle_blendblock;
      // Exact legacy `scene_blend alpha_blend`: RGB and alpha both use
      // SRC_ALPHA, ONE_MINUS_SRC_ALPHA. This is not Porter-Duff source-over.
      particle_blendblock.setBlendType(Ogre::SBT_TRANSPARENT_ALPHA);
      const Ogre::HlmsSamplerblock native_particle_sampler =
          ToOgreSampler(*particle_sampler);
      particle_datablock_name =
          "RoRContinuousDust_f" + std::to_string(request.frame_id);
      particle_datablock = static_cast<Ogre::HlmsUnlitDatablock *>(
          impl_->unlit->createDatablock(
              particle_datablock_name, particle_datablock_name,
              particle_macroblock, particle_blendblock,
              Ogre::HlmsParamVec()));
      particle_datablock->setUseColour(true);
      particle_datablock->setColour(Ogre::ColourValue::White);
      particle_datablock->setTexture(
          0U, particle_texture->sampled, &native_particle_sampler);
      particle_datablock->setTextureUvSource(0U, 0U);
      // Pinned HLMS discards when threshold OP sampled alpha. Portable
      // GREATER therefore lowers to the inverse GREATER_EQUAL discard test.
      particle_datablock->setAlphaTest(Ogre::CMPF_GREATER_EQUAL, false, true);
      particle_datablock->setAlphaTestThreshold(2.0F / 255.0F);

      const Ogre::HlmsMacroblock *const macroblock =
          particle_datablock->getMacroblock();
      const Ogre::HlmsBlendblock *const blendblock =
          particle_datablock->getBlendblock();
      const Ogre::HlmsSamplerblock *const sampler_readback =
          particle_datablock->getSamplerblock(0U);
      bool exact_unlit_layers = true;
      for (Ogre::uint8 slot = 0U; slot < Ogre::NUM_UNLIT_TEXTURE_TYPES;
           ++slot) {
        exact_unlit_layers =
            exact_unlit_layers &&
            (slot == 0U || particle_datablock->getTexture(slot) == nullptr) &&
            particle_datablock->getTextureUvSource(slot) == 0U &&
            particle_datablock->getBlendMode(slot) ==
                Ogre::UNLIT_BLEND_NORMAL_NON_PREMUL &&
            !particle_datablock->getEnableAnimationMatrix(slot) &&
            !particle_datablock->getEnablePlanarReflection(slot);
      }
      const Ogre::String *const particle_datablock_name_readback =
          particle_datablock->getNameStr();
      if (particle_datablock->getCreator() != impl_->unlit ||
          particle_datablock_name_readback == nullptr ||
          *particle_datablock_name_readback != particle_datablock_name ||
          !particle_datablock->hasColour() ||
          particle_datablock->getColour() != Ogre::ColourValue::White ||
          particle_datablock->getTexture(0U) != particle_texture->sampled ||
          particle_datablock->getTextureUvSource(0U) != 0U ||
          sampler_readback == nullptr ||
          *sampler_readback != native_particle_sampler ||
          macroblock == nullptr || blendblock == nullptr ||
          !macroblock->mDepthCheck || macroblock->mDepthWrite ||
          macroblock->mDepthFunc != Ogre::CMPF_LESS_EQUAL ||
          macroblock->mCullMode != Ogre::CULL_CLOCKWISE ||
          blendblock->mSourceBlendFactor != Ogre::SBF_SOURCE_ALPHA ||
          blendblock->mDestBlendFactor !=
              Ogre::SBF_ONE_MINUS_SOURCE_ALPHA ||
          blendblock->mSourceBlendFactorAlpha != Ogre::SBF_SOURCE_ALPHA ||
          blendblock->mDestBlendFactorAlpha !=
              Ogre::SBF_ONE_MINUS_SOURCE_ALPHA ||
          blendblock->mBlendOperation != Ogre::SBO_ADD ||
          blendblock->mBlendOperationAlpha != Ogre::SBO_ADD ||
          particle_datablock->getAlphaTest() != Ogre::CMPF_GREATER_EQUAL ||
          particle_datablock->getAlphaTestShadowCasterOnly() ||
          !exact_unlit_layers ||
          !NearlyEqual(particle_datablock->getAlphaTestThreshold(),
                       2.0F / 255.0F)) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "N1 native Dust Unlit readback differs from legacy straight-alpha, portable GREATER 2/255, depth-write-off, vertex-colour source policy"));
      }
      ++particle_native_state_verifications;

      // Verified rigid-orthonormal above; this block is not entered otherwise.
      const Ogre::Vector3 &camera_right = particle_camera_right;
      const Ogre::Vector3 &camera_up = particle_camera_up;

      // Establish the rollback record before allocating any native object.
      // If vector growth fails there is nothing native to release; every later
      // throw is covered by fail_after_cleanup's reverse ownership walk.
      particle_batches.emplace_back(nullptr, nullptr);
      Ogre::ManualObject *batch =
          impl_->scene_manager->createManualObject(Ogre::SCENE_DYNAMIC);
      if (batch == nullptr) {
        throw std::logic_error("N1 native particle batch creation returned null");
      }
      particle_batches.back().first = batch;
      ++particle_native_batch_creates;
      Ogre::SceneNode *node =
          impl_->scene_manager->getRootSceneNode()->createChildSceneNode();
      particle_batches.back().second = node;
      batch->estimateVertexCount(visible_particle_count * 4U);
      batch->estimateIndexCount(visible_particle_count * 6U);
      batch->begin(particle_datablock_name, Ogre::OT_TRIANGLE_LIST);
      std::uint32_t vertex_base = 0U;
      for (const auto &system : continuous_systems) {
        if (system == nullptr || !system->effective_visible) {
          continue;
        }
        for (const Ogre14ParticleState &particle : system->particles) {
          const Ogre::Vector3 right =
              camera_right * (0.5F * particle.size_meters.x);
          const Ogre::Vector3 up =
              camera_up * (0.5F * particle.size_meters.y);
          const Ogre::Vector3 center(particle.position.x,
                                     particle.position.y,
                                     particle.position.z);
          const Ogre::ColourValue colour(
              particle.color_linear.x, particle.color_linear.y,
              particle.color_linear.z, particle.color_linear.w);
          const Ogre::Vector3 vertices[4U] = {
              center - right + up, center - right - up,
              center + right - up, center + right + up};
          const std::array<Float2, 4U> portable_uvs =
              BuildOgre14ParticleTextureCoordinateQuad(
                  particle.rotation_radians);
          for (std::size_t corner = 0U; corner < 4U; ++corner) {
            batch->position(vertices[corner]);
            batch->textureCoord(portable_uvs[corner].x,
                                portable_uvs[corner].y);
            batch->colour(colour);
          }
          batch->quad(vertex_base, vertex_base + 1U, vertex_base + 2U,
                      vertex_base + 3U);
          vertex_base += 4U;
        }
      }
      Ogre::ManualObject::ManualObjectSection *const section = batch->end();
      bool exact_vertex_colour_layout = false;
      if (section != nullptr) {
        const Ogre::VertexArrayObjectArray &vaos =
            section->getVaos(Ogre::VpNormal);
        if (vaos.size() == 1U && vaos.front() != nullptr &&
            vaos.front()->getVertexBuffers().size() == 1U &&
            vaos.front()->getVertexBuffers().front() != nullptr) {
          const Ogre::VertexElement2Vec &elements =
              vaos.front()->getVertexBuffers().front()->getVertexElements();
          exact_vertex_colour_layout =
              elements.size() == 3U &&
              elements[0U].mSemantic == Ogre::VES_POSITION &&
              elements[1U].mSemantic == Ogre::VES_TEXTURE_COORDINATES &&
              elements[2U].mSemantic == Ogre::VES_DIFFUSE;
        }
      }
      if (section == nullptr || batch->getNumSections() != 1U ||
          section->getDatablock() != particle_datablock ||
          !exact_vertex_colour_layout ||
          vertex_base != visible_particle_count * 4U) {
        throw std::logic_error(
            "N1 native particle batch ownership/readback was incomplete");
      }
      batch->setVisibilityFlags(authored_view_visibility);
      batch->setCastShadows(false);
      node->attachObject(batch);
      if (!batch->isAttached() || batch->getParentSceneNode() != node) {
        throw std::logic_error(
            "N1 native particle batch did not retain its exact scene owner");
      }
      if (batch->getCastShadows()) {
        throw std::logic_error(
            "N1 native particle batch unexpectedly casts shadows");
      }
      particle_native_particles_submitted += visible_particle_count;
    }

    if (impl_->reflection_probe_runtime) {
      const RenderOperationResult reflection_capture =
          impl_->reflection_probe_runtime->PrepareFrame(
              request.frame_id, snapshot.simulation_tick(),
              snapshot.absolute_world_origin_meters(),
              snapshot.reflection_probes(), reflection_items,
              reinterpret_cast<std::uintptr_t>(impl_->camera));
      if (!reflection_capture) {
        return fail_after_cleanup(reflection_capture);
      }
      reflection_frame_prepared = true;
    }

    if (persistent_hdr && impl_->SingleSceneHdrPssmEnabled() &&
        shadow_plan.enabled) {
      const RenderOperationResult finalized =
          impl_->FinalizeSingleSceneHdrPssm(
              lighting_candidate.last_directional_lights,
              lighting_candidate.last_shadow_casters,
              lighting_candidate.last_shadow_receivers,
              authored_view_visibility);
      if (!finalized) {
        return fail_after_cleanup(finalized);
      }
      lighting_candidate.pssm_finalized_with_populated_scene =
          impl_->hdr_pssm_finalization_prepared ||
          impl_->hdr_pssm_finalized_with_populated_scene;
    }

    // Shadows being requested is not the same as the shadow node existing.
    // Single-evaluation PSSM defers finalization until the scene has geometry
    // to shadow, so a paged terrain renders its first frames before the node
    // is created. Presenting those frames with PSSM selected would demand a
    // graph that does not exist yet; present them unshadowed instead, and let
    // PSSM engage on the frame finalization completes.
    const bool pssm_ready_this_frame =
        shadow_plan.enabled &&
        (!persistent_hdr || impl_->hdr_shadow_node_definition_created);
    if (production_presentation) {
      const RenderOperationResult production_graph =
          impl_->EnsureProductionPresentationGraph(
              request, view, authored_view_visibility,
              pssm_ready_this_frame, deferred_sun_visibility_v2);
      if (!production_graph) {
        return fail_after_cleanup(production_graph);
      }
      target = impl_->production_source_target;
      workspace = persistent_hdr ? impl_->hdr_workspace
                                 : impl_->production_workspace;
      if (pssm_ready_this_frame) {
        shadow_node_text = persistent_hdr
                               ? kOgreNextHdrShadowNode
                               : kProductionPresentationShadowNodeName;
      }
    } else if (persistent_hdr) {
      if (pssm_ready_this_frame) {
        shadow_node_text = kOgreNextHdrShadowNode;
      }
    } else {
      target_text = "RoRN1Target_" + std::to_string(request.frame_id);
    std::uint32_t target_flags = Ogre::TextureFlags::RenderToTexture;
    if (UsesMetalImageInterop(impl_->native_feature_tier)) {
      target_flags |= Ogre::TextureFlags::Uav;
    }
    target = impl_->renderer->getTextureGpuManager()->createTexture(
        target_text, Ogre::GpuPageOutStrategy::Discard, target_flags,
        Ogre::TextureTypes::Type2D);
    target->setResolution(view.width, view.height);
    target->setPixelFormat(request.color_format == PixelFormat::RGBA16_FLOAT
                               ? Ogre::PFG_RGBA16_FLOAT
                               : Ogre::PFG_RGBA8_UNORM_SRGB);
    target->scheduleTransitionTo(Ogre::GpuResidency::Resident);

    const std::string workspace_text =
        "RoRN1Workspace_" + std::to_string(request.frame_id);
    const std::string node_text = workspace_text + "_MainNode";
    workspace_name = Ogre::IdString(workspace_text);
    workspace_name_prepared = true;
    workspace_node_text = node_text;
    Ogre::CompositorManager2 *compositors =
        impl_->root->getCompositorManager2();
    if (compositors->hasNodeDefinition(Ogre::IdString(workspace_node_text)) ||
        compositors->hasWorkspaceDefinition(workspace_name)) {
      throw std::runtime_error(
          "N1 main compositor identity already exists");
    }
    Ogre::CompositorNodeDef *node =
        compositors->addNodeDefinition(node_text);
    ++impl_->shadow_audit.workspace_node_definition_creates;
    workspace_node_creation_counted = true;
    node->addTextureSourceName(
        "MainRT", 0U, Ogre::TextureDefinitionBase::TEXTURE_INPUT);
    if (request.present) {
      node->addTextureSourceName(
          "PresentationRT", 1U,
          Ogre::TextureDefinitionBase::TEXTURE_INPUT);
    }
    node->setNumTargetPass(request.present ? 2U : 1U);
    Ogre::CompositorTargetDef *main_target =
        node->addTargetPass("MainRT");
    main_target->setNumPasses(1U);
    auto *scene = static_cast<Ogre::CompositorPassSceneDef *>(
        main_target->addPass(Ogre::PASS_SCENE));
    scene->mFirstRQ = 0U;
    scene->mLastRQ = kOgreNextPccReservedRenderQueue;
    scene->mIncludeOverlays = false;
    scene->mEnableForwardPlus = true;
    scene->setVisibilityMask(authored_view_visibility);
    scene->setAllClearColours(Ogre::ColourValue(0.0F, 0.0F, 0.0F, 1.0F));
    scene->setAllLoadActions(Ogre::LoadAction::Clear);
    scene->mStoreActionDepth = Ogre::StoreAction::DontCare;
    scene->mStoreActionStencil = Ogre::StoreAction::DontCare;
    if (request.present) {
      Ogre::CompositorTargetDef *presentation_target =
          node->addTargetPass("PresentationRT");
      presentation_target->setNumPasses(1U);
      auto *copy = static_cast<Ogre::CompositorPassQuadDef *>(
          presentation_target->addPass(Ogre::PASS_QUAD));
      copy->mMaterialIsHlms = false;
      copy->mMaterialName = "Ogre/Copy/4xFP32";
      copy->mUseQuad = false;
      copy->addQuadTextureSource(0U, "MainRT");
    }
    Ogre::CompositorWorkspaceDef *workspace_definition =
        compositors->addWorkspaceDefinition(workspace_text);
    if (!compositors->hasNodeDefinition(
            Ogre::IdString(workspace_node_text))) {
      throw std::runtime_error(
          "Ogre-Next workspace did not retain its reviewed main node definition");
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    if (shadow_plan.enabled && impl_->pssm_failure_pending &&
        impl_->pssm_failure_stage ==
            OgreNextN1PssmFailureStage::AFTER_WORKSPACE_NODE_DEFINITION) {
      impl_->pssm_failure_pending = false;
      throw std::runtime_error(
          "injected PSSM workspace node transactional rollback failure");
    }
#endif
    if (shadow_plan.enabled) {
      shadow_node_text =
          "RoRPssmShadowNode_" + std::to_string(request.frame_id);
      const Ogre::RenderSystemCapabilities *capabilities =
          impl_->renderer->getCapabilities();
      if (capabilities == nullptr) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "Ogre-Next lost device capabilities before PSSM construction"));
      }
      CreateAndVerifyPssmShadowNode(*compositors, *capabilities,
                                    shadow_node_text,
                                    authored_view_visibility);
      ++impl_->shadow_audit.shadow_node_creates;
      shadow_node_creation_counted = true;
      BindAndVerifyPssmWorkspace(*compositors, node_text,
                                 shadow_node_text);
    }
    workspace_definition->connectExternal(0U, node->getName(), 0U);
    if (request.present) {
      workspace_definition->connectExternal(1U, node->getName(), 1U);
      Ogre::TextureGpu *window_texture =
          impl_->presentation_window->getTexture();
      if (window_texture == nullptr ||
          window_texture->getWidth() != view.width ||
          window_texture->getHeight() != view.height) {
        throw std::runtime_error(
            "native presentation window texture lost the acknowledged pixel extent");
      }
      Ogre::CompositorChannelVec external_channels;
      external_channels.reserve(2U);
      external_channels.push_back(target);
      external_channels.push_back(window_texture);
      workspace = compositors->addWorkspace(
          impl_->scene_manager, external_channels, impl_->camera,
          workspace_text, true);
      const Ogre::CompositorChannelVec &observed_channels =
          workspace->getExternalRenderTargets();
      if (observed_channels.size() != 2U ||
          observed_channels[0U] != target ||
          observed_channels[1U] != window_texture) {
        throw std::runtime_error(
            "Compositor2 did not retain the exact ordered source/window channel pair");
      }
      FrontendSurfaceUpdate acknowledged_surface;
      ++impl_->presentation_audit.show_callback_calls;
      if (!impl_->presentation_configuration.show_after_workspace_ready(
              impl_->presentation_configuration.show_callback_context,
              &acknowledged_surface)) {
        throw std::runtime_error(
            "native host did not acknowledge show/configure after workspace readiness");
      }
      const ValidationResult observed_surface =
          ValidateFrontendSurfaceUpdate(acknowledged_surface, false);
      if (!observed_surface) {
        return fail_after_cleanup(
            OgreNextN1OperationFromValidation(observed_surface));
      }
      if (!SameNativeWindow(
              acknowledged_surface.window,
              impl_->presentation_configuration.exact_window)) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "post-show acknowledgement changed native window identity or generation"));
      }
      if (acknowledged_surface.surface_revision ==
          impl_->surface.surface_revision) {
        if (acknowledged_surface.pixel_width != impl_->surface.pixel_width ||
            acknowledged_surface.pixel_height != impl_->surface.pixel_height ||
            acknowledged_surface.content_scale != impl_->surface.content_scale ||
            acknowledged_surface.suspended != impl_->surface.suspended) {
          return fail_after_cleanup(RenderOperationResult::Failure(
              RenderOperationCode::BACKEND_FAILURE,
              "post-show surface changed without advancing its revision"));
        }
      } else {
        const ValidationResult transition =
            ValidateFrontendSurfaceTransition(
                impl_->surface, acknowledged_surface, false, true);
        if (!transition) {
          return fail_after_cleanup(
              OgreNextN1OperationFromValidation(transition));
        }
      }
      const ValidationResult exact_presentation =
          ValidateRenderFramePresentation(request, acknowledged_surface);
      const RenderOperationResult shown_extent =
          impl_->RefreshPresentationWindowExtent(
              acknowledged_surface.pixel_width,
              acknowledged_surface.pixel_height, true);
      if (!shown_extent) {
        return fail_after_cleanup(shown_extent);
      }
      impl_->surface = acknowledged_surface;
      if (!exact_presentation) {
        std::ostringstream detail;
        detail << "presentation surface out of date after native show ACK; "
               << "resubmit revision "
               << acknowledged_surface.surface_revision << " at "
               << acknowledged_surface.pixel_width << 'x'
               << acknowledged_surface.pixel_height;
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE, detail.str(),
            RenderOperationRecovery::
                RETRY_AFTER_PRESENTATION_SURFACE_UPDATE));
      }

      Ogre::TextureGpu *acknowledged_window_texture =
          impl_->presentation_window->getTexture();
      if (acknowledged_window_texture == nullptr) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "post-show acknowledgement lost the Ogre window texture"));
      }
      if (acknowledged_window_texture != window_texture) {
        compositors->removeWorkspace(workspace);
        workspace = nullptr;
        Ogre::CompositorChannelVec rebound_channels;
        rebound_channels.reserve(2U);
        rebound_channels.push_back(target);
        rebound_channels.push_back(acknowledged_window_texture);
        workspace = compositors->addWorkspace(
            impl_->scene_manager, rebound_channels, impl_->camera,
            workspace_text, true);
      }
      const Ogre::CompositorChannelVec &post_show_channels =
          workspace->getExternalRenderTargets();
      if (post_show_channels.size() != 2U ||
          post_show_channels[0U] != target ||
          post_show_channels[1U] != acknowledged_window_texture ||
          acknowledged_window_texture->getWidth() !=
              acknowledged_surface.pixel_width ||
          acknowledged_window_texture->getHeight() !=
              acknowledged_surface.pixel_height) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "post-show Compositor2 rebind did not retain the exact acknowledged window texture"));
      }
    } else {
      workspace = compositors->addWorkspace(impl_->scene_manager, target,
                                            impl_->camera, workspace_text,
                                            true);
    }
    // A frame that deferred single-evaluation finalization deliberately has no
    // shadow node; only require one when this frame actually selected PSSM.
    if (pssm_ready_this_frame &&
        workspace->findShadowNode(Ogre::IdString(shadow_node_text)) ==
            nullptr) {
      throw std::runtime_error(
          "Ogre-Next did not instantiate the reviewed PSSM shadow node");
    }
    }
    const std::size_t render_iterations =
        request.present || persistent_hdr ? 1U : 3U;
    if (persistent_hdr && !impl_->SingleSceneHdrPssmEnabled()) {
      std::vector<std::pair<Ogre::Light *, Ogre::SceneNode *>>
          retained_light_pairs;
      retained_light_pairs.reserve(impl_->retained_lights.size());
      for (const Impl::RetainedLight &record : impl_->retained_lights) {
        retained_light_pairs.emplace_back(record.light, record.node);
      }
      impl_->hdr_directional_split_listener.BeginFrame(retained_light_pairs);
    }
    for (std::size_t warmup = 0U; warmup < render_iterations; ++warmup) {
      // Raised only after the native frame COMPLETED. A frame that threw a
      // recoverable backend exception on the way in, or that Ogre refused to
      // run, advanced no HDR history: nothing is half-written, so nothing may
      // latch a permanent frontend fault (see fail_after_cleanup).
      if (!impl_->root->renderOneFrame()) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "Ogre-Next ended the N1 frame loop before readback"));
      }
      if (persistent_hdr) {
        hdr_native_frame_executed = true;
      }
    }
    if (persistent_hdr && !impl_->SingleSceneHdrPssmEnabled() &&
        !impl_->hdr_directional_split_listener.EndFrame()) {
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "Ogre-Next HDR directional split did not execute and restore exactly once"));
    }
    lighting_candidate.transactional_directional_sun_toggle =
        persistent_hdr && !impl_->SingleSceneHdrPssmEnabled();
    NativePssmReadback observed_shadow_state;
    // Only read back cascade state from a shadow node that exists. On a frame
    // that deferred finalization there is no node, and verifying its splits
    // reports every field as zero.
    if (pssm_ready_this_frame) {
      observed_shadow_state =
          ReadAndVerifyNativePssmState(*workspace, shadow_node_text);
      lighting_candidate.pssm_shadow_response =
          lighting_candidate.calibrated_directional_lighting &&
          lighting_candidate.last_shadow_casters > 0U &&
          lighting_candidate.last_shadow_receivers > 0U;
    }

    FrameAttachment attachment;
    attachment.view_id = view.view_id;
    attachment.output = FrameOutputMask::COLOR;
    attachment.format = request.color_format;
    attachment.width = view.width;
    attachment.height = view.height;
    if (gpu_only_output) {
      production_output_resource =
          impl_->production_output_handles.Allocate();
      if (!production_output_resource.valid()) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::OUT_OF_MEMORY,
            "Ogre-Next could not allocate a production GPU output lease"));
      }
      attachment.gpu_resource = production_output_resource;
    } else {
      Ogre::Image2 image;
      std::uint64_t &framebuffer_readbacks =
          production_presentation
              ? impl_->lighting_audit.production_framebuffer_readbacks
              : impl_->lighting_audit.test_artifact_framebuffer_readbacks;
      ++framebuffer_readbacks;
      image.convertFromTexture(target, 0U, 0U);
      const Ogre::TextureBox pixels = image.getData(0U);
      attachment.row_pitch_bytes = readback_row_pitch;
      attachment.bytes.resize(readback_total_bytes);
      for (std::uint32_t row = 0U; row < view.height; ++row) {
        const void *source = pixels.at(0U, row, 0U);
        void *destination = attachment.bytes.data() +
                            static_cast<std::size_t>(row) *
                                attachment.row_pitch_bytes;
        std::memcpy(destination, source,
                    static_cast<std::size_t>(attachment.row_pitch_bytes));
      }
    }

    if (UsesMetalImageInterop(impl_->native_feature_tier)) {
      OgreNextN3FrameImageBinding binding;
      binding.frame_id = request.frame_id;
      binding.snapshot_id = snapshot.snapshot_id();
      binding.view_id = view.view_id;
      binding.scene_snapshot = request.scene_snapshot;
      binding.view = view;
      binding.output = FrameOutputMask::COLOR;
      binding.format = request.color_format;
      binding.ogre_texture = reinterpret_cast<std::uintptr_t>(target);
      binding.width = view.width;
      binding.height = view.height;
      interop_images.push_back(binding);
      retained_target = target;
      target = nullptr;
      target_text.clear();
    }

    OgreNextSunVisibilityV2FrameImageBinding sun_visibility_v2_binding;
    if (deferred_sun_visibility_v2) {
      if (!persistent_hdr || impl_->hdr_base_hdr_target == nullptr ||
          impl_->hdr_sun_direct_hdr_target == nullptr ||
          impl_->hdr_visibility_target == nullptr ||
          impl_->hdr_lit_target == nullptr ||
          !impl_->hdr_base_hdr_target_verified ||
          !impl_->hdr_sun_direct_hdr_target_verified ||
          !impl_->hdr_visibility_target_verified ||
          !impl_->hdr_lit_target_verified ||
          impl_->hdr_v2_continuation_workspace == nullptr ||
          impl_->hdr_v2_continuation_workspace->getEnabled()) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "sun-visibility V2 four-image graph changed before publication"));
      }
      sun_visibility_v2_binding.frame_id = request.frame_id;
      sun_visibility_v2_binding.snapshot_id = snapshot.snapshot_id();
      sun_visibility_v2_binding.view_id = view.view_id;
      sun_visibility_v2_binding.scene_snapshot = request.scene_snapshot;
      sun_visibility_v2_binding.view = view;
      sun_visibility_v2_binding.width = view.width;
      sun_visibility_v2_binding.height = view.height;
      sun_visibility_v2_binding.ogre_base_hdr_texture =
          reinterpret_cast<std::uintptr_t>(impl_->hdr_base_hdr_target);
      sun_visibility_v2_binding.ogre_sun_direct_hdr_texture =
          reinterpret_cast<std::uintptr_t>(
              impl_->hdr_sun_direct_hdr_target);
      sun_visibility_v2_binding.ogre_visibility_texture =
          reinterpret_cast<std::uintptr_t>(impl_->hdr_visibility_target);
      sun_visibility_v2_binding.ogre_lit_hdr_texture =
          reinterpret_cast<std::uintptr_t>(impl_->hdr_lit_target);
      sun_visibility_v2_binding.presentation_continuation = impl_.get();
    }

    RenderFrameOutput candidate;
    candidate.frame_id = request.frame_id;
    candidate.snapshot_id = snapshot.snapshot_id();
    candidate.status = RenderFrameStatus::RENDERED;
    candidate.presented = request.present;
    candidate.presented_view_id =
        request.present ? request.presentation_view_id : 0U;
    candidate.cpu_submit_milliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - cpu_start)
            .count();
    candidate.attachments.push_back(std::move(attachment));
    const ValidationResult output_validation =
        ValidateRenderFrameOutput(request, candidate);
    if (!output_validation) {
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 generated an invalid frame output: " + output_validation.field +
              ": " + output_validation.detail));
    }
    if (persistent_hdr) {
      if (!deferred_sun_visibility_v2) {
        const RenderOperationResult hdr_preparation =
            impl_->retain_native_lighting_content_evidence
                ? impl_->VerifyAndPrepareHdrFrame(hdr_plan)
                : OgreNextN1OperationFromValidation(
                      impl_->hdr_temporal_state.PrepareGpuOnlyCommit(hdr_plan));
        if (!hdr_preparation) {
          return fail_after_cleanup(hdr_preparation);
        }
        hdr_commit_prepared = true;
      }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      if (!deferred_sun_visibility_v2) {
        const RenderOperationResult injected = impl_->MaybeInjectHdrFailure(
            OgreNextN1HdrFailureStage::AFTER_FRAME_COMMIT_PREPARE);
        if (!injected) {
          return fail_after_cleanup(injected);
        }
      }
#endif
    }
    const RenderOperationResult commit_preparation =
        impl_->submission_state.PrepareCommit(request);
    if (!commit_preparation) {
      return fail_after_cleanup(commit_preparation);
    }
    submission_commit_prepared = true;
    if (impl_->native_interop) {
      const RenderOperationResult publication_preparation =
          impl_->native_interop->PreparePublishFrame(
              request.frame_id, snapshot.snapshot_id(), interop_geometry,
              interop_images);
      if (!publication_preparation) {
        return fail_after_cleanup(publication_preparation);
      }
      interop_commit_prepared = true;
      if (deferred_sun_visibility_v2) {
        const NativeSunVisibilityV2Result image_preparation =
            sun_visibility_v2_interop
                ->PreparePublishSunVisibilityV2ImageSet(
                    sun_visibility_v2_binding);
        if (image_preparation.code != NativeSunVisibilityV2Code::OK ||
            image_preparation.stage !=
                NativeSunVisibilityV2Stage::IMAGE_EXPORT ||
            image_preparation.frame_id != request.frame_id ||
            image_preparation.snapshot_id != snapshot.snapshot_id() ||
            !ValidateNativeSunVisibilityV2Result(image_preparation)) {
          return fail_after_cleanup(RenderOperationResult::Failure(
              image_preparation.code ==
                      NativeSunVisibilityV2Code::INVALID_ARGUMENT
                  ? RenderOperationCode::INVALID_ARGUMENT
                  : RenderOperationCode::BACKEND_FAILURE,
              "sun-visibility V2 image-set preparation failed: " +
                  image_preparation.detail));
        }
        sun_visibility_v2_commit_prepared = true;
      }
    }
    // Ordinary end-of-present cleanup covers only per-present state: the
    // analytic sky, the particle batch, and the non-persistent test-path
    // graph. The retained instance/light scene survives into the next
    // present. Every fallible publication stage is now prepared, so a
    // teardown failure can abort all pending transactions through one path.
    const auto cleanup_phase_start = std::chrono::steady_clock::now();
    if (!cleanup_scene()) {
      impl_->faulted = true;
      return fail_after_cleanup(FrameCleanupFailure());
    }
    impl_->retained_audit.last_cleanup_phase_microseconds =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - cleanup_phase_start)
                .count());
    if (analytic_sky_frame_completed) {
      const OgreNextAnalyticSkyRuntimeAudit &after =
          impl_->analytic_sky_audit;
      const OgreNextAnalyticSkyRuntimeAudit &before =
          analytic_sky_lifetime_before;
      if (after.native_mesh_creates != before.native_mesh_creates + 2U ||
          after.native_mesh_destroys != before.native_mesh_destroys + 2U ||
          after.native_vertex_buffer_creates !=
              before.native_vertex_buffer_creates + 2U ||
          after.native_vertex_buffer_destroys !=
              before.native_vertex_buffer_destroys + 2U ||
          after.native_index_buffer_creates !=
              before.native_index_buffer_creates + 2U ||
          after.native_index_buffer_destroys !=
              before.native_index_buffer_destroys + 2U ||
          after.native_vao_creates != before.native_vao_creates + 2U ||
          after.native_vao_destroys != before.native_vao_destroys + 2U ||
          after.native_item_creates != before.native_item_creates + 2U ||
          after.native_item_destroys != before.native_item_destroys + 2U ||
          after.native_scene_node_creates !=
              before.native_scene_node_creates + 1U ||
          after.native_scene_node_destroys !=
              before.native_scene_node_destroys + 1U ||
          after.native_datablock_creates !=
              before.native_datablock_creates + 2U ||
          after.native_datablock_destroys !=
              before.native_datablock_destroys + 2U ||
          after.native_mesh_absence_checks !=
              before.native_mesh_absence_checks + 2U ||
          after.native_item_absence_checks !=
              before.native_item_absence_checks + 2U ||
          after.native_scene_node_absence_checks !=
              before.native_scene_node_absence_checks + 1U ||
          after.native_datablock_absence_checks !=
              before.native_datablock_absence_checks + 2U ||
          after.native_gpu_content_readbacks !=
              before.native_gpu_content_readbacks +
                  (impl_->retain_analytic_sky_geometry_content_evidence
                       ? 4U
                       : 0U) ||
          after.native_state_verifications !=
              before.native_state_verifications + 1U) {
        impl_->faulted = true;
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "N1 analytic-sky frame did not close its exact v2 resource ownership, metadata, and optional test-artifact content transaction"));
      }
    }
    if (persistent_hdr && !deferred_sun_visibility_v2 &&
        (!hdr_commit_prepared ||
         !impl_->hdr_temporal_state.CanCommitPrepared())) {
      impl_->faulted = true;
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 prepared HDR history changed before publication"));
    }
    if (persistent_hdr && impl_->SingleSceneHdrPssmEnabled() &&
        !impl_->CanCommitPreparedSingleSceneHdrPssm()) {
      impl_->faulted = true;
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 prepared single-evaluation HDR/PSSM topology changed before publication"));
    }
    if (!impl_->submission_state.CanCommitPrepared(request)) {
      impl_->faulted = true;
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 prepared submission identity changed before publication"));
    }
    if (!impl_->particle_runtime.CanCommit(request.frame_id)) {
      impl_->faulted = true;
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 prepared particle state changed before publication"));
    }
    if (impl_->native_interop &&
        !impl_->native_interop->CanCommitPreparedFrame(
            request.frame_id, snapshot.snapshot_id())) {
      impl_->faulted = true;
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 prepared native interop frame changed before publication"));
    }
    if (deferred_sun_visibility_v2 &&
        (!sun_visibility_v2_commit_prepared ||
         !sun_visibility_v2_interop
              ->CanCommitPreparedSunVisibilityV2ImageSet(
                  request.frame_id, snapshot.snapshot_id()))) {
      impl_->faulted = true;
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 prepared sun-visibility V2 image set changed before atomic publication"));
    }
    if (impl_->reflection_probe_runtime) {
      const RenderOperationResult reflection_finalization =
          impl_->reflection_probe_runtime->FinalizeFrame(request.frame_id);
      if (!reflection_finalization) {
        return fail_after_cleanup(reflection_finalization);
      }
      reflection_frame_prepared = false;
    }
    if (impl_->native_interop) {
      impl_->native_interop->CommitPreparedFrame();
      interop_commit_prepared = false;
      if (deferred_sun_visibility_v2) {
        sun_visibility_v2_interop
            ->CommitPreparedSunVisibilityV2ImageSet();
        sun_visibility_v2_commit_prepared = false;
      }
      impl_->retained_output_target = retained_target;
      retained_target = nullptr;
    }
    if (persistent_hdr && !deferred_sun_visibility_v2) {
      impl_->hdr_temporal_state.CommitPrepared();
      hdr_commit_prepared = false;
      impl_->hdr_exact_current_to_old_copy_verified =
          impl_->retain_native_lighting_content_evidence;
      impl_->hdr_history_comparison =
          impl_->hdr_temporal_state.last_history_comparison();
      impl_->hdr_native_history_validated =
          impl_->hdr_history_comparison.accepted;
    }
    if (shadow_plan.enabled) {
      impl_->shadow_audit.last_frame = shadow_plan;
      impl_->shadow_audit.last_native_splits = observed_shadow_state.splits;
      impl_->shadow_audit.last_native_normal_offset_bias =
          observed_shadow_state.normal_offset_bias;
      impl_->shadow_audit.native_readback_verified = true;
      impl_->shadow_audit.native_bounds_readback_verified = true;
      impl_->shadow_audit.last_native_bounds_observations =
          std::move(native_bounds_observations);
      ++impl_->shadow_audit.shadow_frames_completed;
    }
    impl_->submission_state.CommitPrepared(request);
    submission_commit_prepared = false;
    if (!impl_->particle_runtime.Commit(request.frame_id)) {
      impl_->faulted = true;
      return fail_after_cleanup(FrameCleanupFailure());
    }
    particle_frame_prepared = false;
    impl_->particle_native_batch_creates += particle_native_batch_creates;
    impl_->particle_native_batch_destroys += particle_native_batch_destroys;
    impl_->particle_native_particles_submitted +=
        particle_native_particles_submitted;
    impl_->particle_native_state_verifications +=
        particle_native_state_verifications;
    if (analytic_sky_frame_completed) {
      ++impl_->analytic_sky_audit.completed_frames;
      impl_->analytic_sky_audit.last_sun_light_id =
          analytic_sky_committed_descriptor.sun_light_id;
      impl_->analytic_sky_audit.last_background_vertex_count =
          static_cast<std::uint32_t>(
              analytic_sky_mesh.background_vertices.size());
      impl_->analytic_sky_audit.last_background_index_count =
          static_cast<std::uint32_t>(
              analytic_sky_mesh.background_indices.size());
      impl_->analytic_sky_audit.last_sun_vertex_count =
          static_cast<std::uint32_t>(analytic_sky_mesh.sun_vertices.size());
      impl_->analytic_sky_audit.last_sun_index_count =
          static_cast<std::uint32_t>(analytic_sky_mesh.sun_indices.size());
      impl_->analytic_sky_audit.last_native_content_bytes =
          analytic_sky_mesh.background_vertices.size() *
              sizeof(OgreNextAnalyticSkyNativeVertex) +
          analytic_sky_mesh.background_indices.size() *
              sizeof(std::uint32_t) +
          analytic_sky_mesh.sun_vertices.size() *
              sizeof(OgreNextAnalyticSkyNativeVertex) +
          analytic_sky_mesh.sun_indices.size() * sizeof(std::uint32_t);
      impl_->analytic_sky_audit.last_cpu_geometry_fnv1a64 =
          analytic_sky_cpu_geometry_fnv1a64;
      impl_->analytic_sky_audit.last_descriptor =
          analytic_sky_committed_descriptor;
      impl_->analytic_sky_audit.camera_centered = true;
      impl_->analytic_sky_audit.rendered_first = true;
      impl_->analytic_sky_audit.depth_check_disabled = true;
      impl_->analytic_sky_audit.depth_write_disabled = true;
      impl_->analytic_sky_audit.additive_sun_disk = true;
      impl_->analytic_sky_audit.separate_sun_alpha_replace = true;
      impl_->analytic_sky_audit.native_geometry_metadata_verified = true;
      impl_->analytic_sky_audit.exact_native_geometry_readback =
          impl_->retain_analytic_sky_geometry_content_evidence;
      impl_->analytic_sky_audit.casts_shadows = false;
      impl_->analytic_sky_audit.portable_scene_identity_absent = true;
    }
    lighting_candidate.analytic_sky_contribution =
        analytic_sky_frame_completed;
    // Only claimed when the pass carried a live atmosphere AND every haze
    // constant survived its readback this frame.
    lighting_candidate.aerial_haze_applied =
        impl_->hdr_aerial_haze_applied &&
        impl_->hdr_aerial_haze_constants_bound &&
        impl_->hdr_aerial_haze_workspace_verified;
    lighting_candidate.emissive_material_response =
        lighting_candidate.last_emissive_items > 0U;
    lighting_candidate.native_scene_lighting_pass =
        lighting_candidate.last_pbs_items > 0U &&
        (lighting_candidate.calibrated_directional_lighting ||
         lighting_candidate.ambient_environment_lighting ||
         lighting_candidate.analytic_sky_contribution ||
         lighting_candidate.emissive_material_response);
    lighting_candidate.production_content_readbacks =
        impl_->lighting_audit.production_content_readbacks;
    lighting_candidate.production_framebuffer_readbacks =
        impl_->lighting_audit.production_framebuffer_readbacks;
    lighting_candidate.test_artifact_content_readbacks =
        impl_->lighting_audit.test_artifact_content_readbacks;
    lighting_candidate.test_artifact_framebuffer_readbacks =
        impl_->lighting_audit.test_artifact_framebuffer_readbacks;
    lighting_candidate.gpu_hdr_history_sequenced =
        persistent_hdr && !impl_->retain_native_lighting_content_evidence &&
        impl_->hdr_temporal_state.committed_frame_id() == request.frame_id;
    lighting_candidate.single_step_hdr_history =
        persistent_hdr &&
        impl_->hdr_temporal_state.committed_frame_id() == request.frame_id;
    if (production_presentation &&
        (lighting_candidate.production_content_readbacks != 0U ||
         lighting_candidate.production_framebuffer_readbacks != 0U ||
         !lighting_candidate.production_gpu_only)) {
      impl_->faulted = true;
      return fail_after_cleanup(FrameCleanupFailure());
    }
    if (deferred_sun_visibility_v2) {
      if (impl_->sun_visibility_v2_frame_awaiting_continuation ||
          impl_->sun_visibility_v2_hdr_commit_pending) {
        impl_->faulted = true;
        return fail_after_cleanup(FrameCleanupFailure());
      }
      lighting_candidate.gpu_hdr_history_sequenced = false;
      lighting_candidate.single_step_hdr_history = false;
      impl_->sun_visibility_v2_pending_lighting = lighting_candidate;
      impl_->sun_visibility_v2_pending_hdr_plan = hdr_plan;
      impl_->sun_visibility_v2_pending_frame_id = request.frame_id;
      impl_->sun_visibility_v2_pending_snapshot_id = snapshot.snapshot_id();
      impl_->sun_visibility_v2_pending_view_id = view.view_id;
      impl_->sun_visibility_v2_pending_surface_revision =
          impl_->surface.surface_revision;
      impl_->sun_visibility_v2_pending_width = view.width;
      impl_->sun_visibility_v2_pending_height = view.height;
      impl_->sun_visibility_v2_hdr_commit_pending = true;
      impl_->sun_visibility_v2_frame_awaiting_continuation = true;
    } else {
      if (impl_->hdr_pssm_finalization_prepared) {
        impl_->CommitPreparedSingleSceneHdrPssm();
      }
      ++lighting_candidate.completed_frames;
      impl_->lighting_audit = lighting_candidate;
    }
    if (request.present) {
      if (impl_->presentation_audit.presented_frames == 0U) {
        impl_->presentation_audit.first_presented_frame_id = request.frame_id;
      } else {
        impl_->presentation_audit.monotonic_presented_frame_ids =
            impl_->presentation_audit.monotonic_presented_frame_ids &&
            request.frame_id >
                impl_->presentation_audit.last_presented_frame_id;
      }
      impl_->presentation_audit.last_presented_frame_id = request.frame_id;
      impl_->presentation_audit.exact_two_external_channels = true;
      impl_->presentation_audit.ui_free_source = true;
      impl_->presentation_audit.gpu_quad_copy = true;
      impl_->presentation_audit.cpu_window_copy = false;
      impl_->presentation_audit.workspace_ready_before_show = true;
      impl_->presentation_audit.bounded_swap_completed = true;
      ++impl_->presentation_audit.source_scene_passes;
      ++impl_->presentation_audit.presentation_quad_passes;
      ++impl_->presentation_audit.render_one_frame_calls;
      ++impl_->presentation_audit.window_final_target_updates;
      ++impl_->presentation_audit.window_swap_completions;
      ++impl_->presentation_audit.presented_frames;
      if (gpu_only_output) {
        ++impl_->presentation_audit.gpu_only_output_frames;
      } else {
        ++impl_->presentation_audit.source_readbacks;
      }
      impl_->presentation_audit.last_view_id =
          request.presentation_view_id;
      impl_->presentation_audit.last_surface_revision =
          request.presentation_surface_revision;
      impl_->presentation_audit.last_width = validated_view.width;
      impl_->presentation_audit.last_height = validated_view.height;
    }
    // Commit this present's retained-scene lifecycle evidence. Failed
    // presents never reach this point, so last_* always describes a
    // completed diff against a coherent retained scene.
    {
      OgreNextRetainedSceneAudit &retained = impl_->retained_audit;
      retained.generation = impl_->scene_generation;
      ++retained.frames_diffed;
      retained.last_created = diff_created;
      retained.last_updated = diff_updated;
      retained.last_destroyed = diff_destroyed;
      retained.last_dynamic_updates = diff_dynamic_updates;
      retained.last_verified = diff_verified;
      retained.created += diff_created;
      retained.updated += diff_updated;
      retained.destroyed += diff_destroyed;
      retained.dynamic_updates += diff_dynamic_updates;
      retained.verified += diff_verified;
      retained.retained_instances =
          static_cast<std::uint64_t>(impl_->retained_instances.size());
      retained.retained_lights =
          static_cast<std::uint64_t>(impl_->retained_lights.size());
    }
    output = std::move(candidate);
    // Ownership of this live token moved to the caller's attachment. The
    // frontend retires it only through ReleaseResource().
    production_output_resource = {};
    return RenderOperationResult::Success();
  } catch (const std::bad_alloc &) {
    return fail_after_cleanup(RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "Ogre-Next N1 frame allocation ran out of memory"));
  } catch (const Ogre::Exception &error) {
    return fail_after_cleanup(BackendFailure(error));
  } catch (const std::exception &error) {
    return fail_after_cleanup(RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE, error.what()));
  }
}

// State-only frame retirement performs no native scene work: the retained
// scene may therefore be older than the last retired snapshot. Correctness
// holds because the present-path diff always compares against retained
// native state, never against snapshot lineage.
RenderOperationResult
OgreNextN1Frontend::RetireFrameState(const RenderFrameRequest &request) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  if (impl_->sun_visibility_v2_frame_awaiting_continuation) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUTSTANDING_LEASES,
        "sun-visibility V2 cannot retire another frame before the post-external continuation");
  }
  if (!impl_->registry) {
    return RenderOperationResult::Failure(
        RenderOperationCode::RESOURCE_STALE,
        "Ogre-Next N1 requires an asset snapshot before retiring frame state");
  }
  if (request.continuous_particles == nullptr) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "state-only retirement requires a continuous-particle frame");
  }
  ValidationResult validation =
      ValidateRenderFrameRequestAgainstCapabilities(request,
                                                    impl_->Capabilities());
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }
  if (request.scene_snapshot->asset_registry_id() !=
          impl_->registry->registry_id() ||
      request.scene_snapshot->asset_sequence() != impl_->registry->sequence()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::RESOURCE_STALE,
        "retired frame requires a different synchronized asset catalog");
  }
  validation =
      ValidateSceneSnapshotAssets(*request.scene_snapshot, *impl_->registry);
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }

  bool particle_prepared = false;
  bool submission_prepared = false;
  const auto abort = [&]() noexcept {
    if (submission_prepared) {
      impl_->submission_state.AbortPrepared();
      submission_prepared = false;
    }
    if (particle_prepared) {
      impl_->particle_runtime.Abort(request.frame_id);
      particle_prepared = false;
    }
  };
  try {
    validation = impl_->particle_runtime.Prepare(
        request.frame_id, request.continuous_particles, *impl_->registry,
        request.scene_snapshot->simulation_tick(),
        request.scene_snapshot->absolute_world_origin_meters());
    if (!validation) {
      return OgreNextN1OperationFromValidation(validation);
    }
    particle_prepared = true;
    const RenderOperationResult submission =
        impl_->submission_state.PrepareCommit(request);
    if (!submission) {
      abort();
      return submission;
    }
    submission_prepared = true;
    if (!impl_->particle_runtime.CanCommit(request.frame_id) ||
        !impl_->submission_state.CanCommitPrepared(request) ||
        (impl_->hdr_enabled &&
         !impl_->hdr_temporal_state.CanAccountRetiredFrame(
             request.frame_id,
             request.scene_snapshot->simulation_time_seconds()))) {
      abort();
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "prepared N1 retired-frame state changed before publication, or the "
          "retired frame cannot be accounted in the HDR temporal lineage");
    }
    // Both publications are allocation-free. Particle commit is checked first
    // so a failed particle transaction can never consume frame identity.
    if (!impl_->particle_runtime.Commit(request.frame_id)) {
      particle_prepared = false;
      impl_->submission_state.AbortPrepared();
      submission_prepared = false;
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "prepared N1 retired particle state failed to commit");
    }
    particle_prepared = false;
    impl_->submission_state.CommitPrepared(request);
    // Frontend frame identity has now advanced for a frame that rendered
    // nothing. HDR must observe the same identity or its contiguity check
    // rejects every later rendered frame, including the next generation's
    // first. CanAccountRetiredFrame was checked above and nothing between
    // there and here touches HDR state, so a false here means an invariant
    // broke: fault-latch rather than desync silently.
    if (impl_->hdr_enabled &&
        !impl_->hdr_temporal_state.AccountRetiredFrame(
            request.frame_id,
            request.scene_snapshot->simulation_time_seconds())) {
      impl_->faulted = true;
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "retired frame could not be accounted in the HDR temporal lineage "
          "after frame identity advanced");
    }
    submission_prepared = false;
    return RenderOperationResult::Success();
  } catch (const std::bad_alloc &) {
    abort();
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "N1 retired-frame state allocation ran out of memory");
  } catch (const std::exception &error) {
    abort();
    return RenderOperationResult::Failure(RenderOperationCode::BACKEND_FAILURE,
                                          error.what());
  } catch (...) {
    abort();
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "N1 retired-frame state failed with an unknown exception");
  }
}

bool OgreNextN1Frontend::IsFrameComplete(
    std::uint64_t frame_id) const noexcept {
  return impl_->OnOwnerThread() && !impl_->faulted &&
         !(impl_->sun_visibility_v2_frame_awaiting_continuation &&
           impl_->sun_visibility_v2_pending_frame_id == frame_id) &&
         impl_->submission_state.IsFrameComplete(frame_id);
}

RenderOperationResult OgreNextN1Frontend::WaitForFrame(
    std::uint64_t frame_id, std::uint64_t /*timeout_nanoseconds*/) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (impl_->faulted) {
    return FaultedFrontend();
  }
  if (impl_->sun_visibility_v2_frame_awaiting_continuation &&
      impl_->sun_visibility_v2_pending_frame_id == frame_id) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUTSTANDING_LEASES,
        "sun-visibility V2 frame is not complete before its post-external continuation");
  }
  if (!impl_->submission_state.IsFrameComplete(frame_id)) {
    return RenderOperationResult::Failure(RenderOperationCode::INVALID_ARGUMENT,
                                          "unknown N1 frame ID");
  }
  return RenderOperationResult::Success();
}

NativeRenderInterop *OgreNextN1Frontend::GetNativeInterop() noexcept {
  return impl_->initialized ? impl_->native_interop.get() : nullptr;
}

RenderOperationResult
OgreNextN1Frontend::Shutdown(std::uint64_t timeout_nanoseconds) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  RenderOperationResult interop_shutdown = RenderOperationResult::Success();
  if (impl_->native_interop) {
    interop_shutdown =
        impl_->native_interop->PrepareFrontendShutdown(timeout_nanoseconds);
    if (!interop_shutdown &&
        interop_shutdown.code != RenderOperationCode::DEVICE_LOST) {
      return interop_shutdown;
    }
  }
  if (!impl_->CleanupBackend()) {
    return NativeTeardownFailure("Ogre-Next N1 shutdown");
  }
  return interop_shutdown;
}

} // namespace RoR::Render
