/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Frontend.h"

#include "OgreNextN1Policy.h"
#include "ror_ogre_next_n1_config.h"

#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "OgreAbiUtils.h"
#include "OgreArchiveManager.h"
#include "OgreCamera.h"
#include "OgreColourValue.h"
#include "OgreException.h"
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
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <map>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace RoR::Render {
namespace {

struct N1Vertex {
  float position[3];
  float normal[3];
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
                  Ogre::Vector3 &scale, Ogre::Quaternion &orientation) {
  const Ogre::Matrix4 matrix = ToOgreMatrix(source);
  matrix.decomposition(position, scale, orientation);
  Ogre::Matrix4 reconstructed;
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

} // namespace

class OgreNextN1Frontend::Impl final {
public:
  explicit Impl(OgreNextN1Configuration configuration)
      : configured_shader_media_root(
            std::move(configuration.shader_media_root)) {}

  struct NativeMesh {
    RenderAssetReference asset;
    Ogre::MeshPtr mesh;
    std::string name;
  };

  struct NativeMaterial {
    RenderAssetReference asset;
    Ogre::HlmsPbsDatablock *datablock = nullptr;
    std::string name;
  };

  FrontendCapabilityReport Capabilities() const {
    FrontendCapabilityReport report = BuildOgreNextN1CapabilityReport(
        CompiledRasterApi(), ROR_OGRE_NEXT_N1_VERSION);
    report.maximum_texture_dimension_2d = maximum_texture_dimension;
    return report;
  }

  bool OnOwnerThread() const noexcept {
    return initialized && std::this_thread::get_id() == owner_thread;
  }

  NativeMesh CreateMesh(const RenderAssetReference &asset,
                        const MeshResourceDescriptor &descriptor) {
    NativeMesh native;
    native.asset = asset;
    native.name = AssetName("RoRN1Mesh", asset);
    native.mesh = Ogre::MeshManager::getSingleton().createManual(
        native.name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    Ogre::VaoManager *vao_manager = renderer->getVaoManager();
    Ogre::VertexBufferPacked *vertex_buffer = nullptr;
    Ogre::IndexBufferPacked *index_buffer = nullptr;
    Ogre::VertexArrayObject *vao = nullptr;
    bool attached = false;
    N1Vertex *vertices = nullptr;
    void *indices = nullptr;
    try {
      const std::size_t vertex_bytes =
          sizeof(N1Vertex) * descriptor.positions.size();
      vertices = static_cast<N1Vertex *>(OGRE_MALLOC_SIMD(
          vertex_bytes, Ogre::MEMCATEGORY_GEOMETRY));
      for (std::size_t index = 0U; index < descriptor.positions.size();
           ++index) {
        const Float3 &position = descriptor.positions[index];
        const Float3 &normal = descriptor.normals[index];
        vertices[index] = {{position.x, position.y, position.z},
                           {normal.x, normal.y, normal.z}};
      }
      Ogre::VertexElement2Vec elements;
      elements.push_back(
          Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_POSITION));
      elements.push_back(
          Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_NORMAL));
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

  NativeMaterial CreateMaterial(const RenderAssetReference &asset,
                                const MaterialDescriptor &descriptor) {
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
    for (auto &entry : meshes) {
      clean = DestroyMesh(entry.second) && clean;
    }
    meshes.clear();
    for (auto &entry : materials) {
      clean = DestroyMaterial(entry.second) && clean;
    }
    materials.clear();
    registry.reset();
    return clean;
  }

  [[nodiscard]] bool RollbackCandidateAllocations(
      std::map<RenderAssetId, NativeMesh> &candidate_meshes,
      std::map<RenderAssetId, NativeMaterial> &candidate_materials) noexcept {
    bool clean = true;
    for (auto &entry : candidate_meshes) {
      const auto existing = meshes.find(entry.first);
      if (existing == meshes.end() ||
          existing->second.asset != entry.second.asset) {
        clean = DestroyMesh(entry.second) && clean;
      }
    }
    candidate_meshes.clear();
    for (auto &entry : candidate_materials) {
      const auto existing = materials.find(entry.first);
      if (existing == materials.end() ||
          existing->second.asset != entry.second.asset) {
        clean = DestroyMaterial(entry.second) && clean;
      }
    }
    candidate_materials.clear();
    return clean;
  }

  [[nodiscard]] bool CleanupBackend() noexcept {
    bool clean = DestroyCatalog();
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
    submission_state.Reset();
    maximum_texture_dimension =
        kOgreNextN1ConservativeMaximumTextureDimension;
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
  OgreNextN1SubmissionState submission_state;
  std::thread::id owner_thread;
  std::string configured_shader_media_root;
  std::string resolved_shader_media_root;
  bool initialized = false;
  bool faulted = false;
  std::uint32_t maximum_texture_dimension =
      kOgreNextN1ConservativeMaximumTextureDimension;
};

OgreNextN1Frontend::OgreNextN1Frontend(
    OgreNextN1Configuration configuration)
    : impl_(std::make_unique<Impl>(std::move(configuration))) {}

OgreNextN1Frontend::~OgreNextN1Frontend() {
  if (impl_) {
    static_cast<void>(impl_->CleanupBackend());
  }
}

FrontendCapabilityReport OgreNextN1Frontend::QueryCapabilities() const {
  return impl_->Capabilities();
}

RenderOperationResult OgreNextN1Frontend::Initialize(
    const FrontendInitializationRequest &request) {
  if (impl_->initialized) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "Ogre-Next N1 is already initialized");
  }
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
  try {
    std::unique_ptr<RenderAssetRegistry> candidate =
        impl_->registry
            ? std::make_unique<RenderAssetRegistry>(*impl_->registry)
            : std::make_unique<RenderAssetRegistry>(delta.registry_id);
    ValidationResult validation = candidate->Apply(delta);
    if (!validation) {
      return OgreNextN1OperationFromValidation(validation);
    }
    validation = ValidateOgreNextN1AssetCatalog(*candidate);
    if (!validation) {
      return OgreNextN1OperationFromValidation(validation);
    }

    std::map<RenderAssetId, Impl::NativeMesh> candidate_meshes;
    std::map<RenderAssetId, Impl::NativeMaterial> candidate_materials;
    try {
      const ValidationResult visit = candidate->VisitRecords(
          [&](const RenderAssetRecord &record) {
        if (!record.live()) {
          return ValidationResult::Success();
        }
        if (record.asset.kind == RenderAssetKind::MESH) {
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
        } else if (record.asset.kind == RenderAssetKind::MATERIAL) {
          const auto existing = impl_->materials.find(record.asset.id);
          if (existing != impl_->materials.end() &&
              existing->second.asset == record.asset) {
            candidate_materials.emplace(record.asset.id, existing->second);
          } else {
            PendingMaterialAllocation created{
                impl_.get(),
                impl_->CreateMaterial(
                    record.asset,
                    std::get<MaterialDescriptor>(*record.payload)),
                true};
            candidate_materials.emplace(record.asset.id, created.native);
            created.owns = false;
          }
        }
        return ValidationResult::Success();
      });
      if (!visit) {
        throw std::logic_error("N1 zero-copy catalog visitation failed");
      }
    } catch (...) {
      if (!impl_->RollbackCandidateAllocations(candidate_meshes,
                                               candidate_materials)) {
        impl_->faulted = true;
      }
      throw;
    }

    bool retired_cleanly = true;
    for (auto &entry : impl_->meshes) {
      const auto replacement = candidate_meshes.find(entry.first);
      if (replacement == candidate_meshes.end() ||
          replacement->second.asset != entry.second.asset) {
        retired_cleanly = impl_->DestroyMesh(entry.second) && retired_cleanly;
      }
    }
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
          candidate_meshes, candidate_materials));
      impl_->faulted = true;
      return NativeTeardownFailure("Ogre-Next N1 asset replacement");
    }
    impl_->meshes.swap(candidate_meshes);
    impl_->materials.swap(candidate_materials);
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
      request, impl_->Capabilities(), *impl_->registry);
  if (!validation) {
    return OgreNextN1OperationFromValidation(validation);
  }
  const RenderOperationResult identity_validation =
      impl_->submission_state.Validate(request);
  if (!identity_validation) {
    return identity_validation;
  }

  Ogre::TextureGpu *target = nullptr;
  Ogre::CompositorWorkspace *workspace = nullptr;
  Ogre::IdString workspace_name;
  bool workspace_definition_created = false;
  std::vector<std::pair<Ogre::Item *, Ogre::SceneNode *>> items;
  const auto cleanup_scene = [&]() noexcept {
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
    return clean;
  };
  const auto fail_after_cleanup = [&](RenderOperationResult failure) {
    if (!cleanup_scene()) {
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
    for (const MeshInstanceDescriptor &instance : snapshot.mesh_instances()) {
      const auto mesh = impl_->meshes.find(instance.mesh.id);
      const auto material = impl_->materials.find(instance.material.id);
      if (mesh == impl_->meshes.end() || material == impl_->materials.end()) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "N1 native asset allocation is missing for a validated scene"));
      }
      Ogre::Vector3 position;
      Ogre::Vector3 scale;
      Ogre::Quaternion orientation;
      if (!DecomposeTrs(instance.render_from_object, position, scale,
                        orientation)) {
        return fail_after_cleanup(RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "N1 transform did not survive exact Ogre TRS decomposition"));
      }
      Ogre::Item *item = impl_->scene_manager->createItem(
          mesh->second.mesh, Ogre::SCENE_DYNAMIC);
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
    impl_->camera->setCustomProjectionMatrix(
        true,
        ToOgreMatrix(ConvertPortableProjectionToOgreClip(view.clip_from_view)),
        false);

    const std::string target_name =
        "RoRN1Target_" + std::to_string(request.frame_id);
    target = impl_->renderer->getTextureGpuManager()->createTexture(
        target_name, Ogre::GpuPageOutStrategy::Discard,
        Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D);
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
    const std::uint64_t bytes_per_pixel =
        request.color_format == PixelFormat::RGBA16_FLOAT ? 8U : 4U;
    FrameAttachment attachment;
    attachment.view_id = view.view_id;
    attachment.output = FrameOutputMask::COLOR;
    attachment.format = request.color_format;
    attachment.width = view.width;
    attachment.height = view.height;
    attachment.row_pitch_bytes =
        static_cast<std::uint64_t>(view.width) * bytes_per_pixel;
    attachment.bytes.resize(static_cast<std::size_t>(
        attachment.row_pitch_bytes * view.height));
    for (std::uint32_t row = 0U; row < view.height; ++row) {
      const void *source = pixels.at(0U, row, 0U);
      void *destination = attachment.bytes.data() +
                          static_cast<std::size_t>(row) *
                              attachment.row_pitch_bytes;
      std::memcpy(destination, source,
                  static_cast<std::size_t>(attachment.row_pitch_bytes));
    }

    if (!cleanup_scene()) {
      impl_->faulted = true;
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
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "N1 generated an invalid frame output: " + output_validation.field +
              ": " + output_validation.detail);
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
  return nullptr;
}

RenderOperationResult
OgreNextN1Frontend::Shutdown(std::uint64_t /*timeout_nanoseconds*/) {
  if (!impl_->initialized) {
    return NotInitialized();
  }
  if (!impl_->OnOwnerThread()) {
    return WrongThread();
  }
  if (!impl_->CleanupBackend()) {
    return NativeTeardownFailure("Ogre-Next N1 shutdown");
  }
  return RenderOperationResult::Success();
}

} // namespace RoR::Render
