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
#include "ror_ogre_next_n1_config.h"

#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "OgreAbiUtils.h"
#include "OgreArchiveManager.h"
#include "OgreCamera.h"
#include "OgreColourValue.h"
#include "OgreException.h"
#include "OgreHlmsSamplerblock.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreImage2.h"
#include "OgreItem.h"
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
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>
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

enum class UploadedTextureChannel : std::uint8_t {
  RGBA,
  GREEN,
  BLUE,
};

struct NativeTextureUsage final {
  bool sampled_rgba = false;
  bool roughness_g = false;
  bool metallic_b = false;

  [[nodiscard]] bool empty() const noexcept {
    return !sampled_rgba && !roughness_g && !metallic_b;
  }

  friend bool operator==(const NativeTextureUsage &lhs,
                         const NativeTextureUsage &rhs) noexcept {
    return lhs.sampled_rgba == rhs.sampled_rgba &&
           lhs.roughness_g == rhs.roughness_g &&
           lhs.metallic_b == rhs.metallic_b;
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
      !NearlyEqual(datablock.getEmissive(), expected_emissive)) {
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

} // namespace

class OgreNextN1Frontend::Impl final {
public:
  explicit Impl(OgreNextN1Configuration configuration)
      : raster_feature_tier(configuration.raster_feature_tier),
        configured_shader_media_root(
            std::move(configuration.shader_media_root)) {}

  struct NativeMesh {
    RenderAssetReference asset;
    Ogre::MeshPtr mesh;
    Ogre::VertexBufferPacked *vertex_buffer = nullptr;
    Ogre::IndexBufferPacked *index_buffer = nullptr;
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
    std::string sampled_name;
    std::string roughness_name;
    std::string metallic_name;
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
      audit.exact_usage =
          audit.exact_usage && !texture.usage.empty() &&
          (texture.sampled != nullptr) == texture.usage.sampled_rgba &&
          (texture.roughness != nullptr) == texture.usage.roughness_g &&
          (texture.metallic != nullptr) == texture.usage.metallic_b;
    }
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
      if (raster_feature_tier ==
          OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
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
      } else {
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
      }
      vertex_buffer = vao_manager->createVertexBuffer(
          elements, descriptor.positions.size(), Ogre::BT_IMMUTABLE, vertices,
          true);
      vertices = nullptr;

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

  Ogre::TextureGpu *CreateUploadedTexture(
      const TextureResourceDescriptor &descriptor, const std::string &name,
      UploadedTextureChannel channel) {
    const bool rgba = channel == UploadedTextureChannel::RGBA;
    const Ogre::PixelFormatGpu pixel_format =
        rgba ? (descriptor.color_space == TextureColorSpace::SRGB
                    ? Ogre::PFG_RGBA8_UNORM_SRGB
                    : Ogre::PFG_RGBA8_UNORM)
             : Ogre::PFG_R8_UNORM;
    auto *image = new Ogre::Image2();
    Ogre::TextureGpu *texture = nullptr;
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
        for (std::uint32_t row = 0U; row < source.height; ++row) {
          const auto *source_row = source.bytes.data() +
                                   static_cast<std::size_t>(row) *
                                       source.row_pitch_bytes;
          auto *destination_row = static_cast<std::uint8_t *>(
              destination.at(0U, row, 0U));
          if (rgba) {
            std::memcpy(destination_row, source_row,
                        static_cast<std::size_t>(source.width) * 4U);
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
      }

      Ogre::TextureGpuManager *texture_manager =
          renderer->getTextureGpuManager();
      texture = texture_manager->createTexture(
          name, Ogre::GpuPageOutStrategy::Discard, 0U,
          Ogre::TextureTypes::Type2D);
      texture->setResolution(descriptor.width, descriptor.height);
      texture->setNumMipmaps(
          static_cast<Ogre::uint8>(descriptor.mip_levels.size()));
      texture->setPixelFormat(pixel_format);
      texture->scheduleTransitionTo(Ogre::GpuResidency::Resident, image, true);
      image = nullptr;
      return texture;
    } catch (...) {
      delete image;
      if (texture != nullptr) {
        try {
          renderer->getTextureGpuManager()->destroyTexture(texture);
        } catch (...) {
          faulted = true;
        }
      }
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
    if (usage.empty() ||
        (usage.sampled_rgba && (usage.roughness_g || usage.metallic_b)) ||
        (usage.sampled_rgba &&
         descriptor.color_space != TextureColorSpace::SRGB) ||
        ((usage.roughness_g || usage.metallic_b) &&
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
      native.datablock->setTwoSidedLighting(descriptor.double_sided, false);
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
    const auto destroy_one = [&](Ogre::TextureGpu *&texture) {
      if (texture == nullptr) {
        return;
      }
      if (renderer != nullptr) {
        try {
          renderer->getTextureGpuManager()->destroyTexture(texture);
        } catch (...) {
          clean = false;
        }
      } else {
        clean = false;
      }
      texture = nullptr;
    };
    destroy_one(native.metallic);
    destroy_one(native.roughness);
    destroy_one(native.sampled);
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
    bool clean = DestroyRetainedOutputTarget();
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
  OgreNextN1SubmissionState submission_state;
  OgreNextNativeFeatureTier native_feature_tier =
      OgreNextNativeFeatureTier::RASTER_N1;
  OgreNextRasterFeatureTier raster_feature_tier =
      OgreNextRasterFeatureTier::STATIC_PBR_N1;
  std::thread::id owner_thread;
  std::string configured_shader_media_root;
  std::string resolved_shader_media_root;
  bool initialized = false;
  bool faulted = false;
  bool owns_root_claim = false;
  std::uint32_t maximum_texture_dimension =
      kOgreNextN1ConservativeMaximumTextureDimension;
  float maximum_anisotropy = 1.0F;
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
  if (impl_->raster_feature_tier ==
          OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1 &&
      impl_->native_feature_tier != OgreNextNativeFeatureTier::RASTER_N1) {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "RT4/V1 is an isolated raster milestone and cannot be combined with an Ogre-Next native interop tier");
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
    impl_->scene_manager = impl_->root->createSceneManager(
        Ogre::ST_GENERIC, 1U, "RoROgreNextN1Scene");
    impl_->camera =
        impl_->scene_manager->createCamera("RoROgreNextN1Camera");
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
                {&material->base_color_texture, {true, false, false}},
                {&material->metallic_roughness_texture,
                 {false, true, true}},
                {&material->emissive_texture, {true, false, false}},
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
                if (existing.usage.sampled_rgba &&
                    (existing.usage.roughness_g ||
                     existing.usage.metallic_b)) {
                  return ValidationResult::Failure(
                      ValidationCode::UNSUPPORTED_FEATURE,
                      "assets.material.texture_binding",
                      "RT4/V1 rejects aliases between sampled sRGB and packed linear texture roles");
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
      impl_->raster_feature_tier);
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }
  const CameraViewRequest &validated_view = request.views.front();
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
    if (impl_->retained_output_target != nullptr) {
      const RenderOperationResult discarded =
          impl_->native_interop->DiscardPublishedFrame();
      if (!discarded) {
        return discarded;
      }
      if (!impl_->DestroyRetainedOutputTarget()) {
        impl_->faulted = true;
        return NativeTeardownFailure(
            "Ogre-Next N3 prior output image retirement");
      }
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
  bool workspace_definition_created = false;
  std::vector<std::pair<Ogre::Item *, Ogre::SceneNode *>> items;
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
    if (workspace_definition_created) {
      try {
        compositors->removeWorkspaceDefinition(workspace_name);
      } catch (...) {
        clean = false;
      }
      workspace_definition_created = false;
    }
    if (target != nullptr) {
      try {
        impl_->renderer->getTextureGpuManager()->destroyTexture(target);
      } catch (...) {
        clean = false;
      }
      target = nullptr;
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
  const auto fail_after_cleanup = [&](RenderOperationResult failure) {
    bool clean = cleanup_scene(true);
    clean = destroy_retained_target() && clean;
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
    impl_->scene_manager->setVisibilityMask(view.visibility_mask);

    items.reserve(snapshot.mesh_instances().size());
    submitted_frame_meshes.reserve(snapshot.dynamic_mesh_updates().size());
    if (impl_->native_interop) {
      interop_geometry.reserve(snapshot.mesh_instances().size());
    }
    for (const MeshInstanceDescriptor &instance : snapshot.mesh_instances()) {
      const auto mesh = impl_->meshes.find(instance.mesh.id);
      const auto material = impl_->materials.find(instance.material.id);
      if (mesh == impl_->meshes.end() || material == impl_->materials.end()) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "N1 native asset allocation is missing for a validated scene"));
      }
      const Impl::NativeMesh *render_mesh = &mesh->second;
      if (instance.deformation_revision > 1U) {
        const MeshResourceDescriptor *base_mesh =
            impl_->registry->ResolveMesh(instance.mesh);
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
      item->setDatablock(material->second.datablock);
      item->setVisibilityFlags(instance.visibility_mask);
      item->setCastShadows(false);
      Ogre::SceneNode *node = impl_->scene_manager
                                  ->getRootSceneNode(Ogre::SCENE_DYNAMIC)
                                  ->createChildSceneNode(Ogre::SCENE_DYNAMIC);
      items.back().second = node;
      node->setPosition(position);
      node->setScale(scale);
      node->setOrientation(orientation);
      node->attachObject(item);

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
        binding.position_offset_bytes = 0U;
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

    impl_->camera->setNearClipDistance(view.near_plane);
    impl_->camera->setFarClipDistance(view.far_plane);
    impl_->camera->setAspectRatio(static_cast<float>(view.width) /
                                  static_cast<float>(view.height));
    impl_->camera->setCustomViewMatrix(true, ToOgreMatrix(view.view_from_render));
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
    impl_->camera->setCustomProjectionMatrix(
        true, ToOgreMatrix(converted_projection), false);

    const std::string target_name =
        "RoRN1Target_" + std::to_string(request.frame_id);
    std::uint32_t target_flags = Ogre::TextureFlags::RenderToTexture;
    if (impl_->native_feature_tier ==
        OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3) {
      target_flags |= Ogre::TextureFlags::Uav;
    }
    target = impl_->renderer->getTextureGpuManager()->createTexture(
        target_name, Ogre::GpuPageOutStrategy::Discard,
        target_flags, Ogre::TextureTypes::Type2D);
    target->setResolution(view.width, view.height);
    target->setPixelFormat(request.color_format == PixelFormat::RGBA16_FLOAT
                               ? Ogre::PFG_RGBA16_FLOAT
                               : Ogre::PFG_RGBA8_UNORM_SRGB);
    target->scheduleTransitionTo(Ogre::GpuResidency::Resident);

    const std::string workspace_text =
        "RoRN1Workspace_" + std::to_string(request.frame_id);
    workspace_name = Ogre::IdString(workspace_text);
    Ogre::CompositorManager2 *compositors =
        impl_->root->getCompositorManager2();
    compositors->createBasicWorkspaceDef(
        workspace_text, Ogre::ColourValue(0.0F, 0.0F, 0.0F, 1.0F),
        Ogre::IdString());
    workspace_definition_created = true;
    workspace = compositors->addWorkspace(impl_->scene_manager, target,
                                          impl_->camera, workspace_text, true);
    for (std::size_t warmup = 0U; warmup < 3U; ++warmup) {
      if (!impl_->root->renderOneFrame()) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "Ogre-Next ended the N1 frame loop before readback"));
      }
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
      if (!destroy_retained_target()) {
        impl_->faulted = true;
        static_cast<void>(destroy_submitted_frame_meshes());
        return FrameCleanupFailure();
      }
      if (!destroy_submitted_frame_meshes()) {
        impl_->faulted = true;
        return FrameCleanupFailure();
      }
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 generated an invalid frame output: " + output_validation.field +
              ": " + output_validation.detail);
    }
    if (impl_->native_interop) {
      const RenderOperationResult publication =
          impl_->native_interop->PublishFrame(
              request.frame_id, snapshot.snapshot_id(), interop_geometry,
              interop_images);
      if (!publication) {
        bool clean = destroy_retained_target();
        clean = destroy_submitted_frame_meshes() && clean;
        if (!clean) {
          impl_->faulted = true;
          return FrameCleanupFailure();
        }
        return publication;
      }
      impl_->retained_output_target = retained_target;
      retained_target = nullptr;
      std::vector<Impl::NativeMesh> retired_frame_meshes;
      retired_frame_meshes.swap(impl_->frame_meshes);
      impl_->frame_meshes = std::move(submitted_frame_meshes);
      bool retired_cleanly = true;
      for (Impl::NativeMesh &native : retired_frame_meshes) {
        retired_cleanly = impl_->DestroyMesh(native) && retired_cleanly;
      }
      if (!retired_cleanly) {
        impl_->faulted = true;
        return NativeTeardownFailure(
            "Ogre-Next N2 prior frame geometry retirement");
      }
    } else if (!destroy_submitted_frame_meshes()) {
      impl_->faulted = true;
      return FrameCleanupFailure();
    }
    impl_->submission_state.Commit(request);
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
