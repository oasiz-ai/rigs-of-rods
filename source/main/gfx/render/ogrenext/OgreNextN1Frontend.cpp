/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Frontend.h"

#include "OgreNextN1MediaIntegrity.h"
#include "OgreNextN1NativeInterop.h"
#include "OgreNextN1Policy.h"
#include "OgreNextReflectionProbeRuntime.h"
#include "ror_ogre_next_n1_config.h"

#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorNodeDef.h"
#include "Compositor/OgreCompositorShadowNode.h"
#include "Compositor/OgreCompositorShadowNodeDef.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "Compositor/OgreCompositorWorkspaceDef.h"
#include "Compositor/OgreTextureDefinition.h"
#include "Compositor/Pass/OgreCompositorPassDef.h"
#include "Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h"
#include "OgreAbiUtils.h"
#include "OgreArchiveManager.h"
#include "OgreCamera.h"
#include "OgreColourValue.h"
#include "OgreException.h"
#include "OgreHlmsSamplerblock.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsDatablock.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreImage2.h"
#include "OgreItem.h"
#include "OgreLight.h"
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
#include "OgreSubItem.h"
#include "OgreSubMesh2.h"
#include "OgreTextureBox.h"
#include "OgreTextureGpu.h"
#include "OgreTextureGpuManager.h"
#include "OgreWindow.h"
#include "Vao/OgreVaoManager.h"
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

bool TryClaimOgreNextN1Root() noexcept {
  bool expected = false;
  return g_ogre_next_n1_root_claimed.compare_exchange_strong(
      expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

void ReleaseOgreNextN1Root() noexcept {
  g_ogre_next_n1_root_claimed.store(false, std::memory_order_release);
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
    }
    return false;
  case OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1:
    switch (native_feature_tier) {
    case OgreNextNativeFeatureTier::RASTER_N1:
    case OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3:
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

enum class UploadedTextureChannel : std::uint8_t {
  RGBA,
  GREEN,
  BLUE,
  NORMAL_RG,
};

struct NativeTextureUsage final {
  bool sampled_rgba = false;
  bool roughness_g = false;
  bool metallic_b = false;
  bool normal_rg = false;

  [[nodiscard]] bool empty() const noexcept {
    return !sampled_rgba && !roughness_g && !metallic_b && !normal_rg;
  }

  friend bool operator==(const NativeTextureUsage &lhs,
                         const NativeTextureUsage &rhs) noexcept {
    return lhs.sampled_rgba == rhs.sampled_rgba &&
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

bool NearlyEqual(const Ogre::Aabb &lhs, const Ogre::Aabb &rhs) noexcept {
  return NearlyEqual(lhs.mCenter, rhs.mCenter) &&
         NearlyEqual(lhs.mHalfSize, rhs.mHalfSize);
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
      ToOgreFilter(descriptor.mip_filter, descriptor.anisotropy_enabled);
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

void VerifyPbsMapping(const Ogre::HlmsPbsDatablock &datablock,
                      const MaterialDescriptor &descriptor) {
  const Ogre::Vector3 expected_base_color(
      descriptor.base_color_factor.x, descriptor.base_color_factor.y,
      descriptor.base_color_factor.z);
  const Ogre::Vector3 expected_emissive(
      descriptor.emissive_factor.x * descriptor.emissive_strength,
      descriptor.emissive_factor.y * descriptor.emissive_strength,
      descriptor.emissive_factor.z * descriptor.emissive_strength);
  if (datablock.getBrdf() != Ogre::PbsBrdf::Default ||
      datablock.getWorkflow() != Ogre::HlmsPbsDatablock::MetallicWorkflow ||
      datablock.getTwoSidedLighting() != descriptor.double_sided ||
      !NearlyEqual(datablock.getDiffuse(), expected_base_color) ||
      !NearlyEqual(datablock.getMetalness(), descriptor.metallic_factor) ||
      !NearlyEqual(datablock.getRoughness(), descriptor.roughness_factor) ||
      !NearlyEqual(datablock.getEmissive(), expected_emissive) ||
      datablock.getNormalMapWeight() != 1.0F) {
    throw std::runtime_error(
        "Ogre-Next N1 live PBS datablock differs from the reviewed height-correlated metallic mapping");
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
  Ogre::String data_path;
  Ogre::StringVector library_paths;
  Ogre::HlmsPbs::getDefaultPaths(data_path, library_paths);
  library_paths.push_back(data_path);
  for (const Ogre::String &relative : library_paths) {
    std::error_code error;
    const std::filesystem::path archive =
        std::filesystem::u8path(resolved_media_root) / relative;
    if (!std::filesystem::is_directory(archive, error) || error) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "Ogre-Next N1 shader media root lacks required HLMS archives");
    }
  }
  return RenderOperationResult::Success();
}

Ogre::HlmsPbs *RegisterPbs(Ogre::Root &root,
                           const std::string &resolved_media_root) {
  Ogre::String data_path;
  Ogre::StringVector library_paths;
  Ogre::HlmsPbs::getDefaultPaths(data_path, library_paths);
  const Ogre::String media_root = Ogre::String(resolved_media_root) + "/";
  Ogre::ArchiveManager &archives = Ogre::ArchiveManager::getSingleton();
  Ogre::Archive *data =
      archives.load(media_root + data_path, "FileSystem", true);
  Ogre::ArchiveVec libraries;
  for (const Ogre::String &library_path : library_paths) {
    libraries.push_back(
        archives.load(media_root + library_path, "FileSystem", true));
  }
  Ogre::HlmsPbs *pbs = OGRE_NEW Ogre::HlmsPbs(data, &libraries);
  root.getHlmsManager()->registerHlms(pbs);
  return pbs;
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
    const std::string &shadow_node_name) {
  Ogre::CompositorNodeDef *node =
      compositors.getNodeDefinitionNonConst(
          Ogre::IdString(main_node_name));
  if (node == nullptr || node->getNumTargetPasses() != 1U) {
    throw std::runtime_error(
        "Ogre-Next main workspace topology changed before PSSM binding");
  }
  const Ogre::CompositorPassDefVec &passes =
      node->getTargetPass(0U)->getCompositorPasses();
  auto *scene_pass =
      passes.size() == 1U
          ? dynamic_cast<Ogre::CompositorPassSceneDef *>(passes.front())
          : nullptr;
  if (scene_pass == nullptr) {
    throw std::runtime_error(
        "Ogre-Next main workspace has no single UI-free scene pass");
  }
  scene_pass->mIncludeOverlays = false;
  scene_pass->mShadowNode = Ogre::IdString(shadow_node_name);
  scene_pass->mShadowNodeRecalculation = Ogre::SHADOW_NODE_FIRST_ONLY;
  if (scene_pass->mIncludeOverlays ||
      scene_pass->mShadowNode != Ogre::IdString(shadow_node_name) ||
      scene_pass->mShadowNodeRecalculation !=
          Ogre::SHADOW_NODE_FIRST_ONLY) {
    throw std::runtime_error(
        "Ogre-Next substituted the programmatic PSSM workspace binding");
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
    throw std::runtime_error(
        "Ogre-Next PSSM runtime did not expose the reviewed cascade split state");
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
                  bool &failure_pending
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

    // A metadata-only check is insufficient on Metal and D3D11 because their
    // pinned TextureGpuManager::checkSupport implementations are deliberately
    // broad. Force a real backend allocation and staging-ticket download of
    // the complete exact atlas. Pixel values are intentionally ignored: this
    // preflight verifies device use/readback, while the shadow smoke verifies
    // rendered depth semantics through the receiver colour evidence.
    {
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
    }
    result.readback_verified = true;
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
  result.supported = result.allocation_verified && result.readback_verified &&
                     result.cleanup_verified;
  return result;
}

} // namespace

class OgreNextN1Frontend::Impl final {
public:
  explicit Impl(OgreNextN1Configuration configuration)
      : raster_feature_tier(configuration.raster_feature_tier),
        directional_shadow_mode(configuration.directional_shadow_mode),
        configured_shader_media_root(
            std::move(configuration.shader_media_root))
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        , texture_upload_failure_stage(
              configuration.texture_upload_failure_stage),
        retain_reflection_capture_evidence(
              configuration.retain_reflection_capture_evidence),
        pssm_failure_stage(configuration.pssm_failure_stage)
#endif
  {}

  struct NativeMesh {
    RenderAssetReference asset;
    Ogre::MeshPtr mesh;
    Ogre::VertexBufferPacked *vertex_buffer = nullptr;
    Ogre::IndexBufferPacked *index_buffer = nullptr;
    OgreNextNativeVertexLayout vertex_layout =
        OgreNextNativeVertexLayout::INVALID;
    std::uint32_t vertex_stride_bytes = 0U;
    std::string name;
  };

  struct NativeMaterial {
    RenderAssetReference asset;
    Ogre::HlmsPbsDatablock *datablock = nullptr;
    std::string name;
  };

  struct NativeTexture {
    RenderAssetReference asset;
    NativeTextureUsage usage;
    Ogre::TextureGpu *sampled = nullptr;
    Ogre::TextureGpu *roughness = nullptr;
    Ogre::TextureGpu *metallic = nullptr;
    Ogre::TextureGpu *normal = nullptr;
    std::string sampled_name;
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
      audit.roughness_r8_allocations +=
          texture.roughness != nullptr ? 1U : 0U;
      audit.metallic_r8_allocations += texture.metallic != nullptr ? 1U : 0U;
      audit.normal_rg8_allocations += texture.normal != nullptr ? 1U : 0U;
      audit.exact_usage =
          audit.exact_usage && !texture.usage.empty() &&
          (texture.sampled != nullptr) == texture.usage.sampled_rgba &&
          (texture.roughness != nullptr) == texture.usage.roughness_g &&
          (texture.metallic != nullptr) == texture.usage.metallic_b &&
          (texture.normal != nullptr) == texture.usage.normal_rg;
    }
    audit.live_native_allocations =
        static_cast<std::uint64_t>(audit.sampled_rgba_allocations) +
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
#endif

  OgreNextPssmShadowRuntimeAudit DirectionalShadowAudit() const noexcept {
    OgreNextPssmShadowRuntimeAudit audit = shadow_audit;
    audit.configured_mode = directional_shadow_mode;
    return audit;
  }

  bool OnOwnerThread() const noexcept {
    return initialized && std::this_thread::get_id() == owner_thread;
  }

  RenderOperationResult ValidateSamplerDeviceLimits(
      const RenderAssetRegistry &candidate_registry) const {
    if (raster_feature_tier !=
        OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
      return RenderOperationResult::Success();
    }
    const ValidationResult visit = candidate_registry.VisitRecords(
        [&](const RenderAssetRecord &record) {
          const auto *material =
              std::get_if<MaterialDescriptor>(record.payload.get());
          if (!record.live() || material == nullptr) {
            return ValidationResult::Success();
          }
          const TextureBinding *bindings[] = {
              &material->base_color_texture,
              &material->metallic_roughness_texture,
              &material->normal_texture,
              &material->emissive_texture,
          };
          for (const TextureBinding *binding : bindings) {
            if (!binding->texture.valid()) {
              continue;
            }
            const SamplerResourceDescriptor *sampler =
                candidate_registry.ResolveSampler(binding->sampler);
            if (sampler == nullptr) {
              return ValidationResult::Failure(
                  ValidationCode::MISSING_REFERENCE,
                  "assets.material.texture_binding",
                  "RT4/V1 sampler disappeared before device validation");
            }
            if (sampler->anisotropy_enabled &&
                sampler->maximum_anisotropy > maximum_anisotropy) {
              return ValidationResult::Failure(
                  ValidationCode::UNSUPPORTED_FEATURE,
                  "assets.sampler.maximum_anisotropy",
                  "RT4/V1 rejects anisotropy above the active device limit instead of permitting backend clamping");
            }
          }
          return ValidationResult::Success();
        });
    return visit ? RenderOperationResult::Success()
                 : OgreNextN1OperationFromValidation(visit);
  }

  NativeMesh CreateMesh(const RenderAssetReference &asset,
                        const MeshResourceDescriptor &descriptor,
                        const std::string &name_suffix = {}) {
    NativeMesh native;
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
    const bool rgba = channel == UploadedTextureChannel::RGBA;
    const bool normal_rg = channel == UploadedTextureChannel::NORMAL_RG;
    const Ogre::PixelFormatGpu pixel_format =
        rgba ? (descriptor.color_space == TextureColorSpace::SRGB
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
    native.sampled_name = AssetName("RoRRT4Texture", asset);
    native.roughness_name = native.sampled_name + "_roughness_g";
    native.metallic_name = native.sampled_name + "_metallic_b";
    native.normal_name = native.sampled_name + "_normal_rg";
    const bool aliases_normal_role =
        usage.normal_rg &&
        (usage.sampled_rgba || usage.roughness_g || usage.metallic_b);
    if (usage.empty() ||
        (usage.sampled_rgba && (usage.roughness_g || usage.metallic_b)) ||
        aliases_normal_role ||
        (usage.sampled_rgba &&
         descriptor.color_space != TextureColorSpace::SRGB) ||
        ((usage.roughness_g || usage.metallic_b || usage.normal_rg) &&
         descriptor.color_space != TextureColorSpace::LINEAR)) {
      throw std::logic_error(
          "RT4/V1 texture alias or usage is incompatible with its sampled color-space role");
    }
    try {
      if (usage.sampled_rgba) {
        native.sampled = CreateUploadedTexture(
            descriptor, native.sampled_name, UploadedTextureChannel::RGBA);
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
    } else if (native.sampled != nullptr) {
      throw std::runtime_error(
          "Ogre-Next RT4/V1 allocated an unused sampled RGBA texture");
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
    native.name = AssetName("RoRN1Material", asset);
    Ogre::HlmsMacroblock macroblock;
    if (descriptor.double_sided) {
      macroblock.mCullMode = Ogre::CULL_NONE;
    }
    try {
      native.datablock = static_cast<Ogre::HlmsPbsDatablock *>(
          pbs->createDatablock(native.name, native.name, macroblock,
                               Ogre::HlmsBlendblock(), Ogre::HlmsParamVec()));
      native.datablock->setBrdf(Ogre::PbsBrdf::Default);
      native.datablock->setWorkflow(
          Ogre::HlmsPbsDatablock::MetallicWorkflow);
      native.datablock->setDiffuse(
          Ogre::Vector3(descriptor.base_color_factor.x,
                        descriptor.base_color_factor.y,
                        descriptor.base_color_factor.z));
      native.datablock->setSpecular(Ogre::Vector3::UNIT_SCALE);
      native.datablock->setMetalness(descriptor.metallic_factor);
      native.datablock->setRoughness(descriptor.roughness_factor);
      native.datablock->setEmissive(
          Ogre::Vector3(descriptor.emissive_factor.x,
                        descriptor.emissive_factor.y,
                        descriptor.emissive_factor.z) *
          descriptor.emissive_strength);
      // Pinned Ogre applies this value as a lerp from (0, 0, 1), not as the
      // glTF x/y normal scale. The admission policy therefore requires the
      // exact identity value and we write/read it explicitly here.
      native.datablock->setNormalMapWeight(1.0F);
      native.datablock->setTwoSidedLighting(descriptor.double_sided, false);
      if (directional_shadow_mode ==
          OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1) {
        // Do not inherit an upstream default implicitly: this bias is part of
        // the reviewed checkpoint and is read back again before submission.
        native.datablock->mShadowConstantBias =
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
            native.datablock->setTexture(slot_index, texture, &sampler);
            native.datablock->setTextureUvSource(slot, 0U);
            const Ogre::HlmsSamplerblock *actual_sampler =
                native.datablock->getSamplerblock(slot_index);
            if (native.datablock->getTexture(slot_index) != texture ||
                native.datablock->getTextureUvSource(slot) != 0U ||
                actual_sampler == nullptr) {
              throw std::runtime_error(
                  "Ogre-Next RT4/V1 live PBS texture binding differs from the reviewed mapping");
            }
            VerifySamplerMapping(*actual_sampler, sampler);
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
      }
      VerifyPbsMapping(*native.datablock, descriptor);
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
    destroy_one(native.sampled, native.sampled_name);
    return clean;
  }

  [[nodiscard]] bool DestroyMaterial(NativeMaterial &native) noexcept {
    if (native.datablock == nullptr) {
      return true;
    }
    bool clean = pbs != nullptr;
    if (pbs != nullptr) {
      try {
        pbs->destroyDatablock(Ogre::IdString(native.name));
      } catch (...) {
        clean = false;
      }
    }
    native.datablock = nullptr;
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
          existing->second.asset != entry.second.asset) {
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

  [[nodiscard]] bool CleanupBackend() noexcept {
    if (native_interop) {
      native_interop->RevokeFrontend();
      native_interop.reset();
    }
    bool clean = true;
    if (reflection_probe_runtime) {
      clean = reflection_probe_runtime->Shutdown() && clean;
      reflection_probe_runtime.reset();
    }
    clean = DestroyRetainedOutputTarget() && clean;
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
    renderer = nullptr;
    bootstrap_window = nullptr;
    root.reset();
    plugin.reset();
    if (owns_root_claim) {
      ReleaseOgreNextN1Root();
      owns_root_claim = false;
    }
    submission_state.Reset();
    maximum_texture_dimension =
        kOgreNextN1ConservativeMaximumTextureDimension;
    maximum_anisotropy = 1.0F;
    initialized = false;
    faulted = false;
    owner_thread = {};
    surface = {};
    return clean;
  }

  Ogre::AbiCookie abi_cookie = Ogre::generateAbiCookie();
  std::unique_ptr<N1RendererPlugin> plugin;
  std::unique_ptr<Ogre::Root> root;
  Ogre::RenderSystem *renderer = nullptr;
  Ogre::Window *bootstrap_window = nullptr;
  Ogre::HlmsPbs *pbs = nullptr;
  Ogre::SceneManager *scene_manager = nullptr;
  Ogre::Camera *camera = nullptr;
  FrontendSurfaceUpdate surface;
  std::unique_ptr<RenderAssetRegistry> registry;
  std::map<RenderAssetId, NativeMesh> meshes;
  std::map<RenderAssetId, NativeMaterial> materials;
  std::map<RenderAssetId, NativeTexture> textures;
  std::vector<NativeMesh> frame_meshes;
  /// N3 alone retains its last HDR target until the native image publication
  /// is discarded, or until frontend shutdown first revokes every token.
  Ogre::TextureGpu *retained_output_target = nullptr;
  std::shared_ptr<OgreNextN1NativeInteropBridge> native_interop;
  std::unique_ptr<OgreNextReflectionProbeRuntime> reflection_probe_runtime;
  OgreNextN1SubmissionState submission_state;
  OgreNextNativeFeatureTier native_feature_tier =
      OgreNextNativeFeatureTier::RASTER_N1;
  OgreNextNativeVertexLayout native_vertex_layout =
      OgreNextNativeVertexLayout::INVALID;
  OgreNextRasterFeatureTier raster_feature_tier =
      OgreNextRasterFeatureTier::STATIC_PBR_N1;
  OgreNextDirectionalShadowMode directional_shadow_mode =
      OgreNextDirectionalShadowMode::DISABLED;
  OgreNextPssmShadowRuntimeAudit shadow_audit;
  std::thread::id owner_thread;
  std::string configured_shader_media_root;
  std::string resolved_shader_media_root;
  bool initialized = false;
  bool faulted = false;
  bool owns_root_claim = false;
  std::uint64_t texture_allocation_creates = 0U;
  std::uint64_t texture_allocation_destroys = 0U;
  std::uint64_t texture_retired_name_lookups = 0U;
  std::uint64_t texture_retired_name_rejections = 0U;
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
#endif
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

OgreNextReflectionProbeAudit
OgreNextN1Frontend::QueryReflectionProbeAudit() const noexcept {
  return impl_->reflection_probe_runtime
             ? impl_->reflection_probe_runtime->QueryAudit()
             : OgreNextReflectionProbeAudit{};
}

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
OgreNextN1NormalUploadAudit
OgreNextN1Frontend::QueryNormalUploadAudit() const noexcept {
  return impl_->NormalUploadAudit();
}

OgreNextReflectionProbeCaptureEvidence
OgreNextN1Frontend::QueryReflectionProbeCaptureEvidence() const {
  return impl_->reflection_probe_runtime
             ? impl_->reflection_probe_runtime->QueryLastCaptureEvidence()
             : OgreNextReflectionProbeCaptureEvidence{};
}
#endif

OgreNextPssmShadowRuntimeAudit
OgreNextN1Frontend::QueryDirectionalShadowAudit() const noexcept {
  return impl_->DirectionalShadowAudit();
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
  if (!TryResolveNativeVertexLayout(
          impl_->raster_feature_tier, impl_->native_feature_tier,
          impl_->native_vertex_layout)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "Ogre-Next raster/native feature tiers have no reviewed exact vertex-layout contract");
  }
#if !defined(ROR_OGRE_NEXT_N1_METAL)
  if (impl_->native_feature_tier != OgreNextNativeFeatureTier::RASTER_N1) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "Ogre-Next Metal native interop is available only in the macOS Metal target");
  }
#endif
  const ValidationResult validation =
      ValidateOgreNextN1Initialization(request, impl_->Capabilities());
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
    const RenderOperationResult reflection_media_integrity =
        VerifyOgreNextReflectionProbeMedia(
            impl_->resolved_shader_media_root);
    if (!reflection_media_integrity) {
      return reflection_media_integrity;
    }
  }
  if (!TryClaimOgreNextN1Root()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "another Ogre-Next N1 frontend owns Ogre's process-global Root");
  }
  impl_->owns_root_claim = true;
  const auto fail_after_cleanup = [&](RenderOperationResult failure) {
    if (!impl_->CleanupBackend()) {
      return NativeTeardownFailure("Ogre-Next N1 initialization rollback");
    }
    return failure;
  };

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
    impl_->root->initialise(false);
    Ogre::NameValuePairList window_parameters;
    window_parameters["hidden"] = "true";
    window_parameters["gamma"] = "true";
    window_parameters["FSAA"] = "1";
    impl_->bootstrap_window = impl_->root->createRenderWindow(
        "RoR Ogre-Next N1 bootstrap", 64U, 64U, false,
        &window_parameters);
    if (impl_->bootstrap_window == nullptr ||
        impl_->root->getCompositorManager2() == nullptr) {
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "Ogre-Next did not initialize its hidden/null Compositor2 device"));
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
          impl_->pssm_failure_stage, impl_->pssm_failure_pending
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
          impl_->native_feature_tier ==
              OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3,
          impl_->native_interop);
      if (!interop_result) {
        return fail_after_cleanup(interop_result);
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
    impl_->surface.version = request.version;
    impl_->surface.surface_revision = request.initial_surface_revision;
    impl_->surface.pixel_width = request.initial_width;
    impl_->surface.pixel_height = request.initial_height;
    impl_->surface.content_scale = request.initial_content_scale;
    impl_->surface.suspended = false;
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
  const ValidationResult validation = ValidateFrontendSurfaceTransition(
      impl_->surface, update, headless, true);
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }
  if (!headless) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "Ogre-Next N1 cannot adopt a presentation surface");
  }
  if (update.pixel_width > impl_->maximum_texture_dimension ||
      update.pixel_height > impl_->maximum_texture_dimension) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "surface extent exceeds the initialized Ogre-Next device limit");
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
            const BindingUsage bindings[] = {
                {&material->base_color_texture, {true, false, false, false}},
                {&material->metallic_roughness_texture,
                 {false, true, true, false}},
                {&material->normal_texture, {false, false, false, true}},
                {&material->emissive_texture, {true, false, false, false}},
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
                     (existing.usage.roughness_g ||
                      existing.usage.metallic_b)) ||
                    (existing.usage.normal_rg &&
                     (existing.usage.sampled_rgba ||
                      existing.usage.roughness_g ||
                      existing.usage.metallic_b))) {
                  return ValidationResult::Failure(
                      ValidationCode::UNSUPPORTED_FEATURE,
                      "assets.material.texture_binding",
                      "RT4/V1 rejects aliases between sampled sRGB and packed linear texture roles, and aliases canonical normal textures with either role");
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
          replacement->second.asset != entry.second.asset) {
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
  const ValidationResult validation = ValidateOgreNextN1Frame(
      request, impl_->Capabilities(), *impl_->registry,
      impl_->raster_feature_tier, impl_->directional_shadow_mode);
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }
  const CameraViewRequest &validated_view = request.views.front();
  OgreNextPssmShadowFramePlan shadow_plan;
  const ValidationResult shadow_validation =
      TryBuildOgreNextPssmShadowFramePlan(
          *request.scene_snapshot, *impl_->registry, validated_view,
          impl_->raster_feature_tier, impl_->directional_shadow_mode,
          shadow_plan);
  if (!shadow_validation) {
    return OgreNextN1OperationFromValidation(shadow_validation);
  }
  std::uint64_t readback_row_pitch = 0U;
  std::size_t readback_total_bytes = 0U;
  if (!TryComputeReadbackLayout(validated_view.width,
                                validated_view.height,
                                request.color_format,
                                readback_row_pitch,
                                readback_total_bytes)) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "N1 readback extent cannot be represented by the host allocation model");
  }
  const RenderOperationResult identity_validation =
      impl_->submission_state.Validate(request);
  if (!identity_validation) {
    return identity_validation;
  }
  if (impl_->native_interop) {
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
  if (impl_->native_feature_tier ==
          OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3 &&
      request.color_format != PixelFormat::RGBA16_FLOAT) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "Ogre-Next Metal N3 requires a linear RGBA16_FLOAT colour target");
  }

  Ogre::TextureGpu *target = nullptr;
  Ogre::TextureGpu *retained_target = nullptr;
  Ogre::CompositorWorkspace *workspace = nullptr;
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
  std::vector<std::pair<Ogre::Item *, Ogre::SceneNode *>> items;
  std::vector<OgreNextReflectionProbeItemBinding> reflection_items;
  std::vector<std::pair<Ogre::Light *, Ogre::SceneNode *>> lights;
  struct ReceiverDatablock final {
    Ogre::IdString name;
  };
  std::vector<ReceiverDatablock> receiver_datablocks;
  std::vector<OgreNextPssmNativeBoundsObservation> native_bounds_observations;
  std::vector<Impl::NativeMesh> submitted_frame_meshes;
  std::vector<OgreNextN2FrameGeometryBinding> interop_geometry;
  std::vector<OgreNextN3FrameImageBinding> interop_images;
  const auto destroy_submitted_frame_meshes = [&]() noexcept {
    bool clean = true;
    for (Impl::NativeMesh &native : submitted_frame_meshes) {
      clean = impl_->DestroyMesh(native) && clean;
    }
    submitted_frame_meshes.clear();
    return clean;
  };
  const auto cleanup_scene = [&](bool destroy_frame_geometry) noexcept {
    bool clean = true;
    Ogre::CompositorManager2 *compositors =
        impl_->root->getCompositorManager2();
    if (workspace != nullptr) {
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
    if (!shadow_node_text.empty()) {
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
    if (target != nullptr || !target_text.empty()) {
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
    for (auto iterator = items.rbegin(); iterator != items.rend(); ++iterator) {
      if (iterator->second != nullptr) {
        try {
          iterator->second->detachObject(iterator->first);
        } catch (...) {
          clean = false;
        }
      }
      if (iterator->first != nullptr) {
        try {
          impl_->scene_manager->destroyItem(iterator->first);
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
    items.clear();
    for (auto iterator = receiver_datablocks.rbegin();
         iterator != receiver_datablocks.rend(); ++iterator) {
      bool destroy_returned = false;
      try {
        impl_->pbs->destroyDatablock(iterator->name);
        ++impl_->shadow_audit.receiver_datablock_destroys;
        destroy_returned = true;
      } catch (...) {
        clean = false;
      }
      try {
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        MaybeInjectPssmFailureStage(
            impl_->pssm_failure_stage, impl_->pssm_failure_pending,
            OgreNextN1PssmFailureStage::
                DURING_RECEIVER_DATABLOCK_CLEANUP_LOOKUP,
            "injected PSSM receiver-datablock cleanup absence-lookup failure");
#endif
        if (!destroy_returned ||
            impl_->pbs->getDatablock(iterator->name) != nullptr) {
          clean = false;
        } else {
          ++impl_->shadow_audit.receiver_datablock_cleanup_absence_checks;
        }
      } catch (...) {
        clean = false;
      }
    }
    receiver_datablocks.clear();
    for (auto iterator = lights.rbegin(); iterator != lights.rend();
         ++iterator) {
      if (iterator->second != nullptr && iterator->first != nullptr) {
        try {
          iterator->second->detachObject(iterator->first);
        } catch (...) {
          clean = false;
        }
      }
      if (iterator->first != nullptr) {
        try {
          impl_->scene_manager->destroyLight(iterator->first);
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
    lights.clear();
    if (destroy_frame_geometry) {
      clean = destroy_submitted_frame_meshes() && clean;
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
  const auto abort_submission_commit = [&]() noexcept {
    if (submission_commit_prepared) {
      impl_->submission_state.AbortPrepared();
      submission_commit_prepared = false;
    }
    return true;
  };
  const auto abort_interop_commit = [&]() noexcept {
    if (interop_commit_prepared && impl_->native_interop) {
      impl_->native_interop->AbortPreparedFrame();
      interop_commit_prepared = false;
    }
    return true;
  };
  const auto fail_after_cleanup = [&](RenderOperationResult failure) {
    bool clean = abort_reflection_frame();
    clean = abort_submission_commit() && clean;
    clean = abort_interop_commit() && clean;
    clean = cleanup_scene(false) && clean;
    clean = destroy_retained_target() && clean;
    clean = destroy_submitted_frame_meshes() && clean;
    if (!clean) {
      impl_->faulted = true;
      return FrameCleanupFailure();
    }
    return failure;
  };

  try {
    const auto cpu_start = std::chrono::steady_clock::now();
    const SceneSnapshot &snapshot = *request.scene_snapshot;
    const CameraViewRequest &view = request.views.front();
    const std::uint32_t native_authored_visibility_mask =
        impl_->raster_feature_tier ==
                OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1
            ? kOgreNextRt4AuthoredVisibilityMask
            : kOgreNextN1AuthoredVisibilityMask;
    if (shadow_plan.enabled) {
      impl_->shadow_audit.native_projection_extents_verified = false;
      impl_->shadow_audit.native_readback_verified = false;
      impl_->shadow_audit.native_bounds_readback_verified = false;
      impl_->shadow_audit.last_native_bounds_observations.clear();
      impl_->scene_manager->setShadowFarDistance(kOgreNextPssmFarMeters);
      impl_->scene_manager->setShadowDirectionalLightExtrusionDistance(
          kOgreNextPssmFarMeters);
    }
    impl_->scene_manager->setAmbientLight(
        Ogre::ColourValue(snapshot.environment().ambient_radiance.x,
                          snapshot.environment().ambient_radiance.y,
                          snapshot.environment().ambient_radiance.z) *
            snapshot.environment().environment_intensity,
        Ogre::ColourValue(snapshot.environment().ambient_radiance.x,
                          snapshot.environment().ambient_radiance.y,
                          snapshot.environment().ambient_radiance.z) *
            snapshot.environment().environment_intensity,
        Ogre::Vector3::UNIT_Y);
    const std::uint32_t authored_view_visibility =
        view.visibility_mask & native_authored_visibility_mask;
    impl_->scene_manager->setVisibilityMask(authored_view_visibility);

    lights.reserve(snapshot.lights().size());
    for (const LightDescriptor &descriptor : snapshot.lights()) {
      Ogre::Light *light = impl_->scene_manager->createLight();
      lights.emplace_back(light, nullptr);
      Ogre::SceneNode *node =
          impl_->scene_manager->getRootSceneNode()->createChildSceneNode();
      lights.back().second = node;
      light->setType(Ogre::Light::LT_DIRECTIONAL);
      light->setDiffuseColour(descriptor.color_linear.x,
                              descriptor.color_linear.y,
                              descriptor.color_linear.z);
      light->setSpecularColour(descriptor.color_linear.x,
                               descriptor.color_linear.y,
                               descriptor.color_linear.z);
      light->setPowerScale(
          descriptor.intensity * kOgreNextRt4LuxToNativePowerScale);
      node->attachObject(light);
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
          light->getCastShadows() != shadow_plan.enabled ||
          (shadow_plan.enabled &&
           (!NearlyEqual(light->getShadowFarDistance(),
                         kOgreNextPssmFarMeters) ||
            descriptor.light_id != shadow_plan.shadow_light_id))) {
        throw std::logic_error(
            "validated RT4/V1 directional/PSSM light failed native readback");
      }
    }

    items.reserve(snapshot.mesh_instances().size());
    reflection_items.reserve(snapshot.mesh_instances().size());
    if (shadow_plan.enabled) {
      // Allocate tracking storage before any Ogre datablock is registered.
      receiver_datablocks.reserve(snapshot.mesh_instances().size());
      native_bounds_observations.reserve(snapshot.mesh_instances().size());
    }
    submitted_frame_meshes.reserve(snapshot.dynamic_mesh_updates().size());
    if (impl_->native_interop) {
      interop_geometry.reserve(snapshot.mesh_instances().size());
    }
    for (const MeshInstanceDescriptor &instance : snapshot.mesh_instances()) {
      const auto mesh = impl_->meshes.find(instance.mesh.id);
      const auto material = impl_->materials.find(instance.material.id);
      const MeshResourceDescriptor *base_mesh =
          impl_->registry->ResolveMesh(instance.mesh);
      if (mesh == impl_->meshes.end() || material == impl_->materials.end() ||
          base_mesh == nullptr) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "N1 native asset allocation is missing for a validated scene"));
      }
      const Impl::NativeMesh *render_mesh = &mesh->second;
      if (instance.deformation_revision > 1U) {
        const auto update = std::find_if(
            snapshot.dynamic_mesh_updates().begin(),
            snapshot.dynamic_mesh_updates().end(),
            [&instance](const DynamicMeshUpdateDescriptor &candidate) {
              return candidate.instance_id == instance.instance_id;
            });
        if (base_mesh == nullptr ||
            update == snapshot.dynamic_mesh_updates().end() ||
            update->instance_id != instance.instance_id) {
          return fail_after_cleanup(RenderOperationResult::Failure(
              RenderOperationCode::RESOURCE_STALE,
              "N2 could not resolve the validated full deformation update"));
        }
        MeshResourceDescriptor deformed = *base_mesh;
        deformed.positions = update->positions;
        deformed.normals = update->normals;
        deformed.tangents = update->tangents;
        deformed.velocities = update->velocities;
        deformed.local_bounds = update->updated_local_bounds;
        const std::string suffix =
            "_f" + std::to_string(request.frame_id) + "_i" +
            std::to_string(instance.instance_id) + "_d" +
            std::to_string(instance.deformation_revision);
        submitted_frame_meshes.push_back(
            impl_->CreateMesh(instance.mesh, deformed, suffix));
        render_mesh = &submitted_frame_meshes.back();
      }
      Ogre::Vector3 position;
      Ogre::Vector3 scale;
      Ogre::Quaternion orientation;
      Ogre::Matrix4 reconstructed;
      if (!DecomposeTrs(instance.render_from_object, position, scale,
                        orientation, reconstructed)) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 transform did not survive exact Ogre TRS decomposition"));
      }
      if (!CanRepresentOgreNextN1WorldBounds(instance.local_bounds,
                                              FromOgreMatrix(reconstructed))) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 reconstructed Ogre TRS can overflow native world bounds"));
      }
      Ogre::Item *item = impl_->scene_manager->createItem(
          render_mesh->mesh, Ogre::SCENE_DYNAMIC);
      items.emplace_back(item, nullptr);
      const std::uint32_t authored_instance_visibility =
          instance.visibility_mask & native_authored_visibility_mask;
      Ogre::HlmsPbsDatablock *instance_datablock =
          material->second.datablock;
      const bool receives_shadow =
          (instance.flags & MESH_INSTANCE_RECEIVES_SHADOW) != 0U;
      if (shadow_plan.enabled && !receives_shadow) {
        const std::string receiver_name =
            "RoRPssmReceiver_f" + std::to_string(request.frame_id) + "_i" +
            std::to_string(instance.instance_id);
        Ogre::HlmsDatablock *cloned = nullptr;
        bool creation_counted = false;
        bool tracked = false;
        try {
          cloned = material->second.datablock->clone(receiver_name);
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
          instance_datablock =
              dynamic_cast<Ogre::HlmsPbsDatablock *>(cloned);
          if (instance_datablock == nullptr) {
            throw std::runtime_error(
                "Ogre-Next PSSM receiver clone changed HLMS type");
          }
          receiver_datablocks.push_back(
              ReceiverDatablock{instance_datablock->getName()});
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
        instance_datablock->setReceiveShadows(false);
      }
      if (shadow_plan.enabled &&
          (instance_datablock->getReceiveShadows() != receives_shadow ||
           material->second.datablock->mShadowConstantBias !=
               kOgreNextPssmMaterialConstantBias)) {
        throw std::runtime_error(
            "Ogre-Next PSSM receiver or material bias failed native readback");
      }
      item->setDatablock(instance_datablock);
      item->setVisibilityFlags(authored_instance_visibility);
      const bool casts_shadow =
          shadow_plan.enabled && MeshInstanceCastsShadowForLight(
                                     snapshot.lights().front(), instance,
                                     *base_mesh);
      item->setCastShadows(casts_shadow);
      if (item->getCastShadows() != casts_shadow ||
          item->getVisibilityFlags() != authored_instance_visibility) {
        throw std::runtime_error(
            "Ogre-Next PSSM caster or visibility mask failed native readback");
      }
      Ogre::SceneNode *node = impl_->scene_manager
                                  ->getRootSceneNode(Ogre::SCENE_DYNAMIC)
                                  ->createChildSceneNode(Ogre::SCENE_DYNAMIC);
      items.back().second = node;
      node->setPosition(position);
      node->setScale(scale);
      node->setOrientation(orientation);
      node->attachObject(item);
      reflection_items.push_back(OgreNextReflectionProbeItemBinding{
          reinterpret_cast<std::uintptr_t>(item),
          authored_instance_visibility,
          instance.flags, base_mesh->dynamic});

      if (shadow_plan.enabled) {
        OgreNextN1NativeMeshBounds expected_bounds;
        if (!TryBuildOgreNextN1NativeMeshBounds(instance.local_bounds,
                                                expected_bounds)) {
          throw std::logic_error(
              "validated PSSM bounds became non-finite before native readback");
        }
        const Ogre::Aabb expected_local(
            Ogre::Vector3(expected_bounds.center.x, expected_bounds.center.y,
                          expected_bounds.center.z),
            Ogre::Vector3(expected_bounds.half_size.x,
                          expected_bounds.half_size.y,
                          expected_bounds.half_size.z));
        Ogre::Aabb expected_world = expected_local;
        expected_world.transformAffine(reconstructed);
        const Ogre::Aabb mesh_local = render_mesh->mesh->getAabb();
        const Ogre::Aabb item_local = item->getLocalAabb();
        const Ogre::Aabb item_world = item->getWorldAabbUpdated();
        if (!NearlyEqual(mesh_local, expected_local) ||
            !NearlyEqual(item_local, expected_local) ||
            !NearlyEqual(item_world, expected_world)) {
          throw std::runtime_error("Ogre-Next PSSM Mesh/Item local or world "
                                   "AABB failed native readback");
        }
        OgreNextPssmNativeBoundsObservation observation;
        observation.instance_id = instance.instance_id;
        observation.casts_shadow = casts_shadow;
        observation.receives_shadow = receives_shadow;
        observation.expected_local = ObserveNativeAabb(expected_local);
        observation.ogre_mesh_local = ObserveNativeAabb(mesh_local);
        observation.ogre_item_local = ObserveNativeAabb(item_local);
        observation.expected_world = ObserveNativeAabb(expected_world);
        observation.ogre_item_world = ObserveNativeAabb(item_world);
        native_bounds_observations.push_back(observation);
      }

      if (impl_->native_interop) {
        OgreNextN2FrameGeometryBinding binding;
        binding.frame_id = request.frame_id;
        binding.snapshot_id = snapshot.snapshot_id();
        binding.instance_id = instance.instance_id;
        binding.mesh = instance.mesh;
        binding.topology_revision = instance.topology_revision;
        binding.deformation_revision = instance.deformation_revision;
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

    if (shadow_plan.enabled && native_bounds_observations.size() !=
                                   snapshot.mesh_instances().size()) {
      throw std::runtime_error(
          "Ogre-Next PSSM native bounds observation set is incomplete");
    }

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
      native_view.inverseAffine().decomposition(
          camera_position, camera_scale, camera_orientation);
      if (!NearlyEqual(camera_scale, Ogre::Vector3::UNIT_SCALE)) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "PSSM_3_CASCADE_V1 requires an exactly rigid native camera pose"));
      }
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

    target_text = "RoRN1Target_" + std::to_string(request.frame_id);
    std::uint32_t target_flags = Ogre::TextureFlags::RenderToTexture;
    if (impl_->native_feature_tier ==
        OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3) {
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
    node->setNumTargetPass(1U);
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
    workspace = compositors->addWorkspace(impl_->scene_manager, target,
                                          impl_->camera, workspace_text, true);
    if (shadow_plan.enabled &&
        workspace->findShadowNode(Ogre::IdString(shadow_node_text)) ==
            nullptr) {
      throw std::runtime_error(
          "Ogre-Next did not instantiate the reviewed PSSM shadow node");
    }
    for (std::size_t warmup = 0U; warmup < 3U; ++warmup) {
      if (!impl_->root->renderOneFrame()) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "Ogre-Next ended the N1 frame loop before readback"));
      }
    }
    NativePssmReadback observed_shadow_state;
    if (shadow_plan.enabled) {
      observed_shadow_state =
          ReadAndVerifyNativePssmState(*workspace, shadow_node_text);
    }

    Ogre::Image2 image;
    image.convertFromTexture(target, 0U, 0U);
    const Ogre::TextureBox pixels = image.getData(0U);
    FrameAttachment attachment;
    attachment.view_id = view.view_id;
    attachment.output = FrameOutputMask::COLOR;
    attachment.format = request.color_format;
    attachment.width = view.width;
    attachment.height = view.height;
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

    if (impl_->native_feature_tier ==
        OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3) {
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

    // Keep only the N3 target alive across ordinary scene cleanup. The
    // dedicated failure helper still destroys it on every exit.
    if (!cleanup_scene(false)) {
      impl_->faulted = true;
      static_cast<void>(destroy_retained_target());
      static_cast<void>(destroy_submitted_frame_meshes());
      return FrameCleanupFailure();
    }
    RenderFrameOutput candidate;
    candidate.frame_id = request.frame_id;
    candidate.snapshot_id = snapshot.snapshot_id();
    candidate.status = RenderFrameStatus::RENDERED;
    candidate.presented = false;
    candidate.presented_view_id = 0U;
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
    } else if (!destroy_submitted_frame_meshes()) {
      impl_->faulted = true;
      static_cast<void>(abort_reflection_frame());
      static_cast<void>(abort_submission_commit());
      return FrameCleanupFailure();
    }
    if (!impl_->submission_state.CanCommitPrepared(request)) {
      impl_->faulted = true;
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 prepared submission identity changed before publication"));
    }
    if (impl_->native_interop &&
        !impl_->native_interop->CanCommitPreparedFrame(
            request.frame_id, snapshot.snapshot_id())) {
      impl_->faulted = true;
      return fail_after_cleanup(RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 prepared native interop frame changed before publication"));
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
      impl_->retained_output_target = retained_target;
      retained_target = nullptr;
      impl_->frame_meshes.swap(submitted_frame_meshes);
    }
    impl_->submission_state.CommitPrepared(request);
    submission_commit_prepared = false;
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
    output = std::move(candidate);
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

bool OgreNextN1Frontend::IsFrameComplete(
    std::uint64_t frame_id) const noexcept {
  return impl_->OnOwnerThread() && !impl_->faulted &&
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
