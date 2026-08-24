/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Native Ogre-Next parallax-corrected reflection-probe adapter.

#include "OgreNextReflectionProbeRuntime.h"

#include "OgreNextN1Policy.h"

#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorNodeDef.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "Compositor/OgreCompositorWorkspaceDef.h"
#include "Compositor/Pass/OgreCompositorPassDef.h"
#include "Compositor/Pass/PassIblSpecular/OgreCompositorPassIblSpecularDef.h"
#include "Compositor/Pass/PassQuad/OgreCompositorPassQuadDef.h"
#include "Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h"
#include "Cubemaps/OgreCubemapProbe.h"
#include "Cubemaps/OgreParallaxCorrectedCubemap.h"
#include "Math/Simple/OgreAabb.h"
#include "OgreBitwise.h"
#include "OgreAsyncTextureTicket.h"
#include "OgreCamera.h"
#include "OgreDepthBuffer.h"
#include "OgreHlmsCompute.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsUnlitDatablock.h"
#include "OgreId.h"
#include "OgreImage2.h"
#include "OgreItem.h"
#include "OgreMaterialManager.h"
#include "OgrePixelFormatGpuUtils.h"
#include "OgreRenderSystem.h"
#include "OgreRenderSystemCapabilities.h"
#include "OgreResourceGroupManager.h"
#include "OgreRoot.h"
#include "OgreSceneManager.h"
#include "OgreTextureBox.h"
#include "OgreTextureGpu.h"
#include "OgreTextureGpuManager.h"
#include "OgreVisibilityFlags.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace RoR::Render {
namespace {

static_assert(
    std::is_nothrow_destructible<Ogre::ParallaxCorrectedCubemap>::value,
    "PCC teardown is a no-throw transaction boundary");

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
static_assert(
    std::is_nothrow_move_assignable<
        OgreNextReflectionProbeCaptureEvidence>::value,
    "reflection evidence publication requires no-throw ownership transfer");
#endif

constexpr char kProbeNodeName[] = "RoROgreNextPccProbeNodeV1";
constexpr char kProbeWorkspaceName[] = "RoROgreNextPccProbeWorkspaceV1";
constexpr char kRawTextureName[] = "RoRPccRaw";
constexpr char kFilteredTextureName[] = "RoRPccFiltered";
constexpr char kDepthTextureName[] = "RoRPccDepth";
constexpr char kWithDepthRtvName[] = "RoRPccWithDepth";
constexpr char kWithoutDepthRtvName[] = "RoRPccWithoutDepth";
constexpr std::uint32_t kCandidateProbeMask = 1U << 0U;
constexpr std::uint32_t kCommittedProbeMask = 1U << 1U;
constexpr std::uint32_t kRgba16FloatBytesPerPixel = 8U;
constexpr float kOgrePccPadding = 1.005F;
constexpr std::uint64_t kFnv1a64OffsetBasis =
    UINT64_C(14695981039346656037);

#if (defined(ROR_OGRE_NEXT_N1_METAL) +                                    \
     defined(ROR_OGRE_NEXT_N1_D3D11) +                                   \
     defined(ROR_OGRE_NEXT_N1_VULKAN)) != 1
#error "Exactly one Ogre-Next N1 backend compile definition is required"
#endif

RenderOperationResult Failure(RenderOperationCode code,
                              const std::string &detail) {
  return RenderOperationResult::Failure(code, "Ogre-Next reflection probes: " +
                                                  detail);
}

ReflectionProbeCaptureBackend CompiledBackend() noexcept {
#if defined(ROR_OGRE_NEXT_N1_METAL)
  return ReflectionProbeCaptureBackend::OGRE_NEXT_METAL;
#elif defined(ROR_OGRE_NEXT_N1_D3D11)
  return ReflectionProbeCaptureBackend::OGRE_NEXT_D3D11;
#elif defined(ROR_OGRE_NEXT_N1_VULKAN)
  return ReflectionProbeCaptureBackend::OGRE_NEXT_VULKAN;
#endif
}

const char *BackendShaderDirectory() noexcept {
#if defined(ROR_OGRE_NEXT_N1_METAL)
  return "Metal";
#elif defined(ROR_OGRE_NEXT_N1_D3D11)
  return "HLSL";
#elif defined(ROR_OGRE_NEXT_N1_VULKAN)
  return "GLSL";
#endif
}

class StableHasher final {
public:
  void AddByte(std::uint8_t value) noexcept {
    value_ ^= value;
    value_ *= UINT64_C(1099511628211);
  }

  void AddU16(std::uint16_t value) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
      AddByte(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
  }

  void AddU32(std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
      AddByte(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
  }

  void AddU64(std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
      AddByte(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
  }

  void AddBytes(const void *bytes, std::size_t count) noexcept {
    const auto *cursor = static_cast<const std::uint8_t *>(bytes);
    for (std::size_t index = 0U; index < count; ++index) {
      AddByte(cursor[index]);
    }
  }

  void AddString(const char *value) noexcept {
    AddBytes(value, std::strlen(value));
    AddByte(0U);
  }

  [[nodiscard]] std::uint64_t Value() const noexcept {
    return value_ == 0U ? UINT64_C(0xcbf29ce484222325) : value_;
  }

private:
  std::uint64_t value_ = kFnv1a64OffsetBasis;
};

Ogre::Matrix3 ToOgreOrientation(const Matrix4x4 &matrix) {
  Ogre::Matrix3 result;
  for (std::size_t column = 0U; column < 3U; ++column) {
    for (std::size_t row = 0U; row < 3U; ++row) {
      result[row][column] = matrix.elements[column * 4U + row];
    }
  }
  return result;
}

Ogre::Vector3 ToOgreVector(const Float3 &value) {
  return Ogre::Vector3(value.x, value.y, value.z);
}

Ogre::Vector3 TransformPoint(const Matrix4x4 &matrix, const Float3 &point) {
  const Ogre::Matrix3 orientation = ToOgreOrientation(matrix);
  return orientation * ToOgreVector(point) +
         Ogre::Vector3(matrix.elements[12U], matrix.elements[13U],
                       matrix.elements[14U]);
}

bool NearlyEqual(float lhs, float rhs) noexcept {
  constexpr float kTolerance = 2.0e-5F;
  return std::isfinite(lhs) && std::isfinite(rhs) &&
         std::fabs(lhs - rhs) <=
             kTolerance *
                 (std::max)(1.0F, (std::max)(std::fabs(lhs), std::fabs(rhs)));
}

bool NearlyEqual(const Ogre::Vector3 &lhs, const Ogre::Vector3 &rhs) noexcept {
  return NearlyEqual(lhs.x, rhs.x) && NearlyEqual(lhs.y, rhs.y) &&
         NearlyEqual(lhs.z, rhs.z);
}

bool NearlyEqual(const Ogre::Matrix3 &lhs, const Ogre::Matrix3 &rhs) noexcept {
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      if (!NearlyEqual(lhs[row][column], rhs[row][column])) {
        return false;
      }
    }
  }
  return true;
}

void DefineProbeCompositor(Ogre::CompositorManager2 &compositors,
                           bool &owns_node_definition,
                           bool &owns_workspace_definition) {
  if (compositors.hasNodeDefinition(kProbeNodeName) ||
      compositors.hasWorkspaceDefinition(kProbeWorkspaceName)) {
    throw std::runtime_error(
        "programmatic PCC compositor identity already exists");
  }

  Ogre::CompositorNodeDef *node = compositors.addNodeDefinition(kProbeNodeName);
  owns_node_definition = true;
  node->addTextureSourceName(kRawTextureName, 0U,
                             Ogre::TextureDefinitionBase::TEXTURE_INPUT);
  node->addTextureSourceName(kFilteredTextureName, 1U,
                             Ogre::TextureDefinitionBase::TEXTURE_INPUT);

  node->setNumLocalTextureDefinitions(1U);
  Ogre::TextureDefinitionBase::TextureDefinition *depth =
      node->addTextureDefinition(kDepthTextureName);
  depth->textureType = Ogre::TextureTypes::Type2D;
  depth->width = 0U;
  depth->height = 0U;
  depth->depthOrSlices = 1U;
  depth->numMipmaps = 1U;
  depth->format = Ogre::PFG_D32_FLOAT;
  depth->textureFlags = Ogre::TextureFlags::RenderToTexture;
  depth->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;

  Ogre::RenderTargetViewDef *with_depth =
      node->addRenderTextureView(kWithDepthRtvName);
  Ogre::RenderTargetViewEntry raw_attachment;
  raw_attachment.textureName = kRawTextureName;
  with_depth->colourAttachments.push_back(raw_attachment);
  with_depth->depthAttachment.textureName = kDepthTextureName;
  with_depth->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;

  Ogre::RenderTargetViewDef *without_depth =
      node->addRenderTextureView(kWithoutDepthRtvName);
  without_depth->colourAttachments.push_back(raw_attachment);
  without_depth->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;

  node->setNumTargetPass(12U);
  for (std::uint32_t face = 0U; face < kReflectionProbeCubemapFaceCount;
       ++face) {
    Ogre::CompositorTargetDef *scene_target =
        node->addTargetPass(kWithDepthRtvName, face);
    scene_target->setNumPasses(1U);
    auto *scene = static_cast<Ogre::CompositorPassSceneDef *>(
        scene_target->addPass(Ogre::PASS_SCENE));
    scene->mIdentifier = 100U + face;
    scene->mCameraCubemapReorient = true;
    scene->mFirstRQ = 0U;
    scene->mLastRQ = kOgreNextPccReservedRenderQueue;
    scene->mEnableForwardPlus = true;
    scene->mIncludeOverlays = false;
    scene->setVisibilityMask(kOgreNextPccCaptureVisibilityBit);
    scene->mLoadActionColour[0] = Ogre::LoadAction::Clear;
    scene->mClearColour[0] = Ogre::ColourValue::Black;
    scene->mLoadActionDepth = Ogre::LoadAction::Clear;
    scene->mClearDepth = 1.0F;
    scene->mStoreActionColour[0] = Ogre::StoreAction::Store;
    scene->mStoreActionDepth = Ogre::StoreAction::Store;
    scene->mStoreActionStencil = Ogre::StoreAction::DontCare;

    Ogre::CompositorTargetDef *compress_target =
        node->addTargetPass(kWithoutDepthRtvName, face);
    compress_target->setNumPasses(
        face + 1U == kReflectionProbeCubemapFaceCount ? 2U : 1U);
    auto *compress = static_cast<Ogre::CompositorPassQuadDef *>(
        compress_target->addPass(Ogre::PASS_QUAD));
    compress->mIdentifier = 200U + face;
    compress->mMaterialName = "PCC/DepthCompressor";
    compress->mFrustumCorners = Ogre::CompositorPassQuadDef::VIEW_SPACE_CORNERS;
    compress->mCameraCubemapReorient = true;
    compress->mAnalyzeAllTextureLayouts = true;
    compress->addQuadTextureSource(0U, kDepthTextureName);
    compress->mLoadActionColour[0] = Ogre::LoadAction::Load;
    compress->mStoreActionColour[0] = Ogre::StoreAction::Store;
    compress->mStoreActionDepth = Ogre::StoreAction::DontCare;
    compress->mStoreActionStencil = Ogre::StoreAction::DontCare;

    if (face + 1U == kReflectionProbeCubemapFaceCount) {
      auto *ibl = static_cast<Ogre::CompositorPassIblSpecularDef *>(
          compress_target->addPass(Ogre::PASS_IBL_SPECULAR));
      ibl->setCubemapInput(kRawTextureName);
      ibl->setCubemapOutput(kFilteredTextureName);
      ibl->mSamplesPerIteration = 128.0F;
      ibl->mSamplesSingleIterationFallback = 128.0F;
      ibl->mForceMipmapFallback = false;
      ibl->mNumInitialPasses = 1U;
      ibl->mProfilingId = "RoR PCC filtered IBL";
    }
  }

  Ogre::CompositorWorkspaceDef *workspace =
      compositors.addWorkspaceDefinition(kProbeWorkspaceName);
  owns_workspace_definition = true;
  workspace->connectExternal(0U, node->getName(), 0U);
  workspace->connectExternal(1U, node->getName(), 1U);
}

constexpr std::size_t kReflectionResourceLocationCount = 8U;

void AddReflectionResources(
    const std::string &media_root,
    std::array<Ogre::String, kReflectionResourceLocationCount> &locations,
    std::size_t &added_location_count) {
  Ogre::ResourceGroupManager &resources =
      Ogre::ResourceGroupManager::getSingleton();
  const std::filesystem::path root = std::filesystem::u8path(media_root);
  const std::string backend = BackendShaderDirectory();
  const std::array<std::filesystem::path, kReflectionResourceLocationCount>
      paths{{
      root / "2.0/scripts/materials/Common",
      root / "2.0/scripts/materials/Common/Any",
      root / "2.0/scripts/materials/Common" / backend,
      root / "2.0/scripts/materials/LocalCubemaps",
      root / "2.0/scripts/materials/LocalCubemaps" / backend,
      root / "Compute/Algorithms/IBL",
      root / "Compute/Tools/Any",
      root / "Hlms/Common" / backend,
  }};
  for (std::size_t index = 0U; index < paths.size(); ++index) {
    locations[index] = paths[index].generic_u8string();
  }
  added_location_count = 0U;
  for (const Ogre::String &native : locations) {
    if (resources.resourceLocationExists(
            native, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)) {
      throw std::runtime_error(
          "reflection resource location was registered more than once");
    }
    resources.addResourceLocation(
        native, "FileSystem",
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, false, true);
    ++added_location_count;
  }
  resources.initialiseResourceGroup(
      Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, true);

  const Ogre::MaterialPtr compressor =
      Ogre::MaterialManager::getSingleton().getByName(
          "PCC/DepthCompressor",
          Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
  if (!compressor) {
    throw std::runtime_error("PCC depth-compressor material was not parsed");
  }
  Ogre::HlmsCompute *compute =
      Ogre::Root::getSingleton().getHlmsManager()->getComputeHlms();
  if (compute == nullptr ||
      compute->findComputeJobNoThrow("IblSpecular/Integrate") == nullptr) {
    throw std::runtime_error("exact IblSpecular/Integrate job was not parsed");
  }
}

bool TextureShapeMatches(const Ogre::TextureGpu &texture,
                         std::uint32_t resolution,
                         std::uint16_t mip_count) noexcept {
  return texture.getTextureType() == Ogre::TextureTypes::TypeCube &&
         texture.getPixelFormat() == Ogre::PFG_RGBA16_FLOAT &&
         texture.getWidth() == resolution &&
         texture.getHeight() == resolution &&
         texture.getNumSlices() == kReflectionProbeCubemapFaceCount &&
         texture.getNumMipmaps() == mip_count;
}

struct FilteredReadback final {
  Ogre::Image2 image;
  std::vector<ReflectionProbeCapturedSubresourceView> views;

  FilteredReadback() = default;
  FilteredReadback(const FilteredReadback &) = delete;
  FilteredReadback &operator=(const FilteredReadback &) = delete;
  FilteredReadback(FilteredReadback &&) = delete;
  FilteredReadback &operator=(FilteredReadback &&) = delete;
};

/// Production readback owns one ticket per filtered mip plus one raw-mip
/// ticket. Tickets are issued without accurate tracking so the render thread
/// can continue presenting the last committed probe generation while the GPU
/// copies into staging memory.
struct DeferredCaptureReadback final {
  Ogre::TextureGpuManager *manager = nullptr;
  Ogre::AsyncTextureTicket *raw_mip_zero = nullptr;
  std::vector<Ogre::AsyncTextureTicket *> filtered_mips;
  ReflectionProbeCaptureMipMetadata metadata;
  std::uint64_t issue_frame_id = 0U;
  std::uint64_t earliest_poll_frame_id = 0U;
};

bool DestroyDeferredCaptureReadback(DeferredCaptureReadback &readback) noexcept {
  bool clean = true;
  if (readback.manager == nullptr &&
      (readback.raw_mip_zero != nullptr || !readback.filtered_mips.empty())) {
    clean = false;
  }
  if (readback.manager != nullptr) {
    for (Ogre::AsyncTextureTicket *ticket : readback.filtered_mips) {
      if (ticket == nullptr) {
        clean = false;
        continue;
      }
      try {
        readback.manager->destroyAsyncTextureTicket(ticket);
      } catch (...) {
        clean = false;
      }
    }
    if (readback.raw_mip_zero != nullptr) {
      try {
        readback.manager->destroyAsyncTextureTicket(
            readback.raw_mip_zero);
      } catch (...) {
        clean = false;
      }
    }
  }
  readback = {};
  return clean;
}

bool DeferredCaptureReadbackReady(DeferredCaptureReadback &readback,
                                  std::uint64_t render_frame_id) {
  if (!IsOgreNextPccReadbackPollEligible(readback.issue_frame_id,
                                         render_frame_id) ||
      readback.earliest_poll_frame_id !=
          ComputeOgreNextPccEarliestReadbackFrame(readback.issue_frame_id) ||
      readback.manager == nullptr || readback.raw_mip_zero == nullptr ||
      readback.filtered_mips.size() != readback.metadata.mip_count) {
    return false;
  }
  if (!readback.raw_mip_zero->queryIsTransferDone()) {
    return false;
  }
  return std::all_of(
      readback.filtered_mips.begin(), readback.filtered_mips.end(),
      [](Ogre::AsyncTextureTicket *ticket) {
        return ticket != nullptr && ticket->queryIsTransferDone();
      });
}

void CopyAsyncTicketCanonical(Ogre::AsyncTextureTicket &ticket,
                              std::uint32_t width, std::uint32_t height,
                              std::uint32_t face_count,
                              std::vector<std::uint8_t> &bytes) {
  if (ticket.getWidth() != width || ticket.getHeight() != height ||
      ticket.getNumSlices() != face_count) {
    throw std::runtime_error(
        "deferred PCC readback ticket has an unexpected shape");
  }
  const std::size_t row_bytes =
      static_cast<std::size_t>(width) * kRgba16FloatBytesPerPixel;
  if (height != 0U && row_bytes >
                          (std::numeric_limits<std::size_t>::max)() / height) {
    throw std::bad_alloc();
  }
  const std::size_t face_bytes = row_bytes * height;
  if (face_count != 0U && face_bytes >
                              (std::numeric_limits<std::size_t>::max)() /
                                  face_count) {
    throw std::bad_alloc();
  }
  bytes.assign(face_bytes * face_count, 0U);
  const auto copy_box = [&](const Ogre::TextureBox &box,
                            std::uint32_t first_face) {
    if (box.width != width || box.height != height ||
        box.bytesPerPixel != kRgba16FloatBytesPerPixel ||
        first_face > face_count || box.numSlices > face_count - first_face) {
      throw std::runtime_error(
          "mapped PCC readback differs from its ticket layout");
    }
    for (std::uint32_t slice = 0U; slice < box.numSlices; ++slice) {
      for (std::uint32_t row = 0U; row < height; ++row) {
        const auto *source = static_cast<const std::uint8_t *>(
            box.at(0U, row, slice));
        std::uint8_t *destination =
            bytes.data() +
            static_cast<std::size_t>(first_face + slice) * face_bytes +
            static_cast<std::size_t>(row) * row_bytes;
        std::memcpy(destination, source, row_bytes);
      }
    }
  };

  if (ticket.canMapMoreThanOneSlice()) {
    const Ogre::TextureBox box = ticket.map(0U);
    try {
      if (box.numSlices != face_count) {
        throw std::runtime_error(
            "deferred PCC readback did not map every cubemap face");
      }
      copy_box(box, 0U);
    } catch (...) {
      ticket.unmap();
      throw;
    }
    ticket.unmap();
  } else {
    for (std::uint32_t face = 0U; face < face_count; ++face) {
      const Ogre::TextureBox box = ticket.map(face);
      try {
        if (box.numSlices != 1U) {
          throw std::runtime_error(
              "deferred PCC face readback mapped multiple slices");
        }
        copy_box(box, face);
      } catch (...) {
        ticket.unmap();
        throw;
      }
      ticket.unmap();
    }
  }
}

void BuildCapturedViews(
    const ReflectionProbeCaptureMipMetadata &metadata,
    const std::vector<std::vector<std::uint8_t>> &storage,
    std::vector<ReflectionProbeCapturedSubresourceView> &views) {
  if (storage.size() != metadata.mip_count) {
    throw std::runtime_error(
        "deferred PCC filtered storage does not match its mip contract");
  }
  views.clear();
  views.reserve(static_cast<std::size_t>(metadata.face_count) *
                metadata.mip_count);
  for (std::uint16_t mip = 0U; mip < metadata.mip_count; ++mip) {
    const std::size_t row_bytes =
        static_cast<std::size_t>(metadata.widths[mip]) *
        kRgba16FloatBytesPerPixel;
    const std::size_t face_bytes =
        row_bytes * static_cast<std::size_t>(metadata.heights[mip]);
    if (storage[mip].size() != face_bytes * metadata.face_count) {
      throw std::runtime_error(
          "deferred PCC filtered bytes differ from their canonical shape");
    }
    for (std::uint32_t face = 0U; face < metadata.face_count; ++face) {
      ReflectionProbeCapturedSubresourceView view;
      view.face_index = face;
      view.mip_level = mip;
      view.width = metadata.widths[mip];
      view.height = metadata.heights[mip];
      view.row_pitch_bytes = row_bytes;
      view.bytes = storage[mip].data() + face_bytes * face;
      view.byte_count = face_bytes;
      views.push_back(view);
    }
  }
}

void ReadFilteredTexture(Ogre::TextureGpu &texture,
                         const ReflectionProbeCaptureMipMetadata &metadata,
                         FilteredReadback &result) {
  if (metadata.mip_count == 0U ||
      metadata.mip_count >
          (std::numeric_limits<std::uint8_t>::max)()) {
    throw std::runtime_error(
        "filtered PCC mip count cannot be represented by Ogre Image2");
  }
  if (!TextureShapeMatches(texture, metadata.widths[0U], metadata.mip_count)) {
    throw std::runtime_error(
        "filtered PCC texture differs from its receipt layout");
  }
  result.image.convertFromTexture(
      &texture, 0U, static_cast<std::uint8_t>(metadata.mip_count - 1U));
  result.views.clear();
  result.views.reserve(static_cast<std::size_t>(metadata.face_count) *
                       metadata.mip_count);
  for (std::uint16_t mip = 0U; mip < metadata.mip_count; ++mip) {
    const std::uint8_t native_mip = static_cast<std::uint8_t>(mip);
    const Ogre::TextureBox box = result.image.getData(native_mip);
    if (box.width != metadata.widths[mip] ||
        box.height != metadata.heights[mip] ||
        box.numSlices != metadata.face_count ||
        box.bytesPerPixel != kRgba16FloatBytesPerPixel) {
      throw std::runtime_error(
          "filtered PCC readback returned a different native layout");
    }
    for (std::uint32_t face = 0U; face < metadata.face_count; ++face) {
      ReflectionProbeCapturedSubresourceView view;
      view.face_index = face;
      view.mip_level = mip;
      view.width = box.width;
      view.height = box.height;
      view.row_pitch_bytes = box.bytesPerRow;
      view.bytes = static_cast<const std::uint8_t *>(box.at(0U, 0U, face));
      view.byte_count = box.bytesPerImage;
      result.views.push_back(view);
    }
  }
}

struct FilteredContentStats final {
  std::uint64_t finite_component_count = 0U;
  std::uint64_t nonzero_rgb_component_count = 0U;
  float max_absolute_rgb = 0.0F;
  /// Per-channel arithmetic means over every filtered mip pixel, derived from
  /// the same readback bytes the measurement already walks - no additional
  /// GPU synchronization is introduced for them.
  double mean_r = 0.0;
  double mean_g = 0.0;
  double mean_b = 0.0;
};

FilteredContentStats MeasureFilteredContent(
    const std::vector<ReflectionProbeCapturedSubresourceView> &views) {
  FilteredContentStats result;
  for (const ReflectionProbeCapturedSubresourceView &view : views) {
    const std::size_t active_row_bytes =
        static_cast<std::size_t>(view.width) * kRgba16FloatBytesPerPixel;
    for (std::uint32_t row = 0U; row < view.height; ++row) {
      const std::uint8_t *cursor =
          view.bytes + static_cast<std::size_t>(row) * view.row_pitch_bytes;
      for (std::size_t offset = 0U; offset < active_row_bytes;
           offset += sizeof(std::uint16_t)) {
        std::uint16_t half = 0U;
        std::memcpy(&half, cursor + offset, sizeof(half));
        const float component = Ogre::Bitwise::halfToFloat(half);
        if (!std::isfinite(component)) {
          throw std::runtime_error(
              "filtered PCC readback contains a non-finite component");
        }
        ++result.finite_component_count;
        const std::size_t channel =
            (offset / sizeof(std::uint16_t)) % 4U;
        if (channel < 3U) {
          const float magnitude = std::fabs(component);
          if (magnitude > 0.0F) {
            ++result.nonzero_rgb_component_count;
          }
          result.max_absolute_rgb =
              (std::max)(result.max_absolute_rgb, magnitude);
          if (channel == 0U) {
            result.mean_r += static_cast<double>(component);
          } else if (channel == 1U) {
            result.mean_g += static_cast<double>(component);
          } else {
            result.mean_b += static_cast<double>(component);
          }
        }
      }
    }
  }
  if (result.finite_component_count != 0U) {
    const double pixel_count =
        static_cast<double>(result.finite_component_count) / 4.0;
    if (pixel_count > 0.0) {
      result.mean_r /= pixel_count;
      result.mean_g /= pixel_count;
      result.mean_b /= pixel_count;
    }
  }
  return result;
}

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
void CopyCanonicalSubresources(
    const std::vector<ReflectionProbeCapturedSubresourceView> &views,
    std::vector<std::uint8_t> &bytes) {
  std::size_t total_bytes = 0U;
  for (const ReflectionProbeCapturedSubresourceView &view : views) {
    const std::size_t row_bytes =
        static_cast<std::size_t>(view.width) * kRgba16FloatBytesPerPixel;
    if (view.height > 0U &&
        row_bytes > (std::numeric_limits<std::size_t>::max)() / view.height) {
      throw std::bad_alloc();
    }
    const std::size_t subresource_bytes = row_bytes * view.height;
    if (subresource_bytes >
        (std::numeric_limits<std::size_t>::max)() - total_bytes) {
      throw std::bad_alloc();
    }
    total_bytes += subresource_bytes;
  }
  bytes.clear();
  bytes.reserve(total_bytes);
  for (const ReflectionProbeCapturedSubresourceView &view : views) {
    const std::size_t row_bytes =
        static_cast<std::size_t>(view.width) * kRgba16FloatBytesPerPixel;
    for (std::uint32_t row = 0U; row < view.height; ++row) {
      const std::uint8_t *source =
          view.bytes + static_cast<std::size_t>(row) * view.row_pitch_bytes;
      bytes.insert(bytes.end(), source, source + row_bytes);
    }
  }
}
#endif

std::uint64_t ComputeNativeExecutionEvidence(
    Ogre::TextureGpu &raw_texture, const ReflectionProbeUpdateRequest &request,
    const ReflectionProbeCaptureMeasurementResult &measurement,
    std::vector<std::uint8_t> *canonical_raw_bytes) {
  if (raw_texture.getTextureType() != Ogre::TextureTypes::TypeCube ||
      raw_texture.getPixelFormat() != Ogre::PFG_RGBA16_FLOAT ||
      raw_texture.getWidth() != request.resolution ||
      raw_texture.getHeight() != request.resolution ||
      raw_texture.getNumSlices() != kReflectionProbeCubemapFaceCount) {
    throw std::runtime_error("raw PCC render target has an unexpected shape");
  }
  Ogre::Image2 raw_image;
  raw_image.convertFromTexture(&raw_texture, 0U, 0U);
  const Ogre::TextureBox box = raw_image.getData(0U);
  if (box.width != request.resolution || box.height != request.resolution ||
      box.numSlices != kReflectionProbeCubemapFaceCount ||
      box.bytesPerPixel != kRgba16FloatBytesPerPixel) {
    throw std::runtime_error("raw PCC readback has an unexpected layout");
  }

  StableHasher hasher;
  hasher.AddString("ror.ogre_next.native_pcc_execution.v1");
  hasher.AddByte(static_cast<std::uint8_t>(CompiledBackend()));
  hasher.AddU64(request.probe_id);
  hasher.AddU64(request.candidate_generation);
  hasher.AddU64(request.deterministic_seed);
  hasher.AddU64(measurement.canonical_capture_digest);
  hasher.AddU32(box.width);
  hasher.AddU32(box.height);
  const std::size_t active_row_bytes =
      static_cast<std::size_t>(box.width) * kRgba16FloatBytesPerPixel;
  if (canonical_raw_bytes != nullptr) {
    canonical_raw_bytes->clear();
    canonical_raw_bytes->reserve(active_row_bytes * box.height *
                                 kReflectionProbeCubemapFaceCount);
  }
  for (std::uint32_t face = 0U; face < kReflectionProbeCubemapFaceCount;
       ++face) {
    hasher.AddU32(face);
    for (std::uint32_t row = 0U; row < box.height; ++row) {
      const auto *source = static_cast<const std::uint8_t *>(
          box.at(0U, row, face));
      hasher.AddBytes(source, active_row_bytes);
      if (canonical_raw_bytes != nullptr) {
        canonical_raw_bytes->insert(canonical_raw_bytes->end(), source,
                                    source + active_row_bytes);
      }
    }
  }
  return hasher.Value();
}

std::uint64_t ComputeDeferredNativeExecutionEvidence(
    const std::vector<std::uint8_t> &canonical_raw_bytes,
    const ReflectionProbeUpdateRequest &request,
    const ReflectionProbeCaptureMeasurementResult &measurement) {
  const std::size_t row_bytes =
      static_cast<std::size_t>(request.resolution) *
      kRgba16FloatBytesPerPixel;
  const std::size_t expected_bytes =
      row_bytes * static_cast<std::size_t>(request.resolution) *
      kReflectionProbeCubemapFaceCount;
  if (canonical_raw_bytes.size() != expected_bytes) {
    throw std::runtime_error(
        "deferred raw PCC bytes differ from their receipt layout");
  }

  StableHasher hasher;
  hasher.AddString("ror.ogre_next.native_pcc_execution.v1");
  hasher.AddByte(static_cast<std::uint8_t>(CompiledBackend()));
  hasher.AddU64(request.probe_id);
  hasher.AddU64(request.candidate_generation);
  hasher.AddU64(request.deterministic_seed);
  hasher.AddU64(measurement.canonical_capture_digest);
  hasher.AddU32(request.resolution);
  hasher.AddU32(request.resolution);
  const std::size_t face_bytes =
      row_bytes * static_cast<std::size_t>(request.resolution);
  for (std::uint32_t face = 0U;
       face < kReflectionProbeCubemapFaceCount; ++face) {
    hasher.AddU32(face);
    hasher.AddBytes(canonical_raw_bytes.data() + face_bytes * face,
                    face_bytes);
  }
  return hasher.Value();
}

bool AreReflectionProbeDescriptorSetsEquivalent(
    const std::vector<ReflectionProbeRuntimeDescriptor> &lhs,
    const std::vector<ReflectionProbeRuntimeDescriptor> &rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.size(); ++index) {
    if (!AreReflectionProbeRuntimeDescriptorsEquivalent(lhs[index],
                                                        rhs[index])) {
      return false;
    }
  }
  return true;
}

} // namespace

class OgreNextReflectionProbeRuntime::Impl final {
public:
  struct ProbeState final {
    Ogre::CubemapProbe *committed = nullptr;
    ReflectionProbeRuntimeDescriptor descriptor;
  };

  struct PendingFrame final {
    std::uint64_t render_frame_id = 0U;
    std::uint64_t capture_frame_id = 0U;
    std::uint64_t plan_id = 0U;
    std::map<std::uint64_t, ProbeState> candidate_states;
    std::vector<Ogre::CubemapProbe *> retired_probes;
    std::vector<ReflectionProbeCaptureReceipt> receipts;
    Ogre::CubemapProbe *candidate = nullptr;
    ReflectionProbeUpdateRequest captured_request;
    ReflectionProbeCaptureMeasurementResult measurement;
    FilteredContentStats filtered_stats;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    OgreNextReflectionProbeCaptureEvidence capture_evidence;
#endif
    std::uint64_t native_execution_evidence = 0U;
    std::uint32_t prospective_live_probe_count = 0U;
    bool prior_pbs_bound = false;
    bool pbs_binding_changed = false;
    bool pcc_created = false;
    bool captured = false;
    bool pass_through = false;
  };

  struct InFlightCapture final {
    std::unique_ptr<PendingFrame> staged;
    std::vector<ReflectionProbeRuntimeDescriptor> descriptors;
    DeferredCaptureReadback readback;
  };

  explicit Impl()
      : scheduler(ReflectionProbeSchedulerConfiguration{1U, 256U, 1U}) {}

  ~Impl() { static_cast<void>(Shutdown()); }

  [[nodiscard]] RenderOperationResult
  Initialize(OgreNextReflectionProbeRuntimeConfiguration value) {
    if (audit.initialized) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "runtime is already initialized");
    }
    if (value.ogre_root == 0U || value.ogre_scene_manager == 0U ||
        value.ogre_hlms_pbs == 0U || value.shader_media_root.empty()) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "native objects and shader media root are required");
    }
    if (!value.owns_automatic_ibl_mipmap_policy) {
      return Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "RoR-owned automatic IBL-mipmap policy is required for PCC rollback");
    }
    if (value.maximum_blend_resolution < 32U ||
        value.maximum_blend_resolution > 2048U ||
        (value.maximum_blend_resolution &
         (value.maximum_blend_resolution - 1U)) != 0U) {
      return Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "blend resolution must be a reviewed power of two from 32 to 2048");
    }

    configuration = std::move(value);
    root = reinterpret_cast<Ogre::Root *>(configuration.ogre_root);
    scene_manager = reinterpret_cast<Ogre::SceneManager *>(
        configuration.ogre_scene_manager);
    pbs = reinterpret_cast<Ogre::HlmsPbs *>(configuration.ogre_hlms_pbs);
    try {
      if (Ogre::Root::getSingletonPtr() != root ||
          root->getRenderSystem() == nullptr ||
          root->getCompositorManager2() == nullptr ||
          root->getHlmsManager() == nullptr ||
          root->getHlmsManager()->getHlms(Ogre::HLMS_PBS) != pbs ||
          scene_manager->getDestinationRenderSystem() !=
              root->getRenderSystem()) {
        return Failure(
            RenderOperationCode::INVALID_ARGUMENT,
            "Root, SceneManager, renderer, compositor, and HLMS PBS must "
            "belong to one live Ogre instance");
      }
      if (Ogre::VisibilityFlags::RESERVED_VISIBILITY_FLAGS !=
          (kOgreNextRt4AuthoredVisibilityMask |
           kOgreNextPccCaptureVisibilityBit |
           kOgreNextPccProxyVisibilityBit)) {
        return Failure(
            RenderOperationCode::UNSUPPORTED,
            "Ogre visibility-layer layout differs from the reviewed PCC contract");
      }
      const Ogre::RenderSystemCapabilities *capabilities =
          root->getRenderSystem()->getCapabilities();
      if (capabilities == nullptr ||
          !capabilities->hasCapability(Ogre::RSC_COMPUTE_PROGRAM) ||
          !capabilities->hasCapability(Ogre::RSC_UAV) ||
          !capabilities->hasCapability(Ogre::RSC_TYPED_UAV_LOADS)) {
        return Failure(RenderOperationCode::UNSUPPORTED,
                       "device lacks compute, UAV, or typed-UAV-load support");
      }
      if (pbs->getParallaxCorrectedCubemap() != nullptr) {
        return Failure(
            RenderOperationCode::UNSUPPORTED,
            "HLMS PBS is already owned by another reflection-probe runtime");
      }
      AddReflectionResources(configuration.shader_media_root,
                             resource_locations,
                             added_resource_location_count);
      audit.exact_resources_loaded = true;
      DefineProbeCompositor(*root->getCompositorManager2(),
                            owns_probe_node_definition,
                            owns_probe_workspace_definition);
      audit.compositor_defined_in_code = true;
      audit.initialized = true;
      audit.blend_resolution = configuration.maximum_blend_resolution;
      audit.ui_free_capture = true;
      audit.reserved_render_queue_excluded = true;
      owner_thread = std::this_thread::get_id();
      return RenderOperationResult::Success();
    } catch (const Ogre::Exception &error) {
      static_cast<void>(Shutdown());
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     error.getFullDescription());
    } catch (const std::exception &error) {
      static_cast<void>(Shutdown());
      return Failure(RenderOperationCode::BACKEND_FAILURE, error.what());
    }
  }

  [[nodiscard]] bool ResetAutomaticIblMipmapPolicy() noexcept {
    if (pbs == nullptr || !configuration.owns_automatic_ibl_mipmap_policy) {
      return false;
    }
    try {
      if (pbs->getParallaxCorrectedCubemap() != nullptr) {
        return false;
      }
      // PCC setEnabled() raises HlmsPbs' automatic IBL mip high-water mark.
      // The N1 contract owns a fresh, automatic-policy PBS and admits no
      // PBSM_REFLECTION/environment textures, so Ogre's public zero reset is
      // the canonical recomputation after the PCC has been fully removed.
      pbs->resetIblSpecMipmap(0U);
      return pbs->getParallaxCorrectedCubemap() == nullptr;
    } catch (...) {
      return false;
    }
  }

  void EnsurePcc() {
    if (pcc != nullptr) {
      return;
    }
    Ogre::CompositorWorkspaceDef *workspace =
        root->getCompositorManager2()->getWorkspaceDefinition(
            kProbeWorkspaceName);
    Ogre::ParallaxCorrectedCubemap *created =
        new Ogre::ParallaxCorrectedCubemap(
        Ogre::Id::generateNewId<Ogre::ParallaxCorrectedCubemap>(), root,
        scene_manager, workspace, kOgreNextPccReservedRenderQueue,
        kOgreNextPccProxyVisibilityBit, 0U);
    try {
      created->mMask = kCommittedProbeMask;
      created->mPaused = false;
      created->setEnabled(true, configuration.maximum_blend_resolution,
                          configuration.maximum_blend_resolution,
                          Ogre::PFG_RGBA16_FLOAT);
      if (!created->getEnabled()) {
        throw std::runtime_error("PCC blend runtime did not enable");
      }
      Ogre::TextureGpu *bind_texture = created->getBindTexture();
      if (created->getSceneManager() != scene_manager ||
          bind_texture == nullptr ||
          !TextureShapeMatches(*bind_texture,
                               configuration.maximum_blend_resolution,
                               bind_texture->getNumMipmaps())) {
        throw std::runtime_error(
            "PCC blend runtime did not retain the configured native owner");
      }
    } catch (...) {
      bool destroyed = false;
      try {
        delete created;
        destroyed = true;
      } catch (...) {
        faulted = true;
      }
      if (destroyed && !ResetAutomaticIblMipmapPolicy()) {
        faulted = true;
      }
      throw;
    }
    pcc = created;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    ++native_pcc_create_count;
#endif
  }

  void ApplyDescriptor(Ogre::CubemapProbe &probe,
                       const ReflectionProbeRuntimeDescriptor &descriptor,
                       const Double3 &origin, bool dirty) {
    Matrix4x4 render_from_probe = descriptor.world_from_probe_orientation;
    const std::array<double, 3U> relative{{
        descriptor.absolute_world_position_meters.x - origin.x,
        descriptor.absolute_world_position_meters.y - origin.y,
        descriptor.absolute_world_position_meters.z - origin.z,
    }};
    for (std::size_t axis = 0U; axis < relative.size(); ++axis) {
      if (!std::isfinite(relative[axis]) ||
          std::fabs(relative[axis]) >
              static_cast<double>((std::numeric_limits<float>::max)())) {
        throw std::runtime_error(
            "render-relative probe position exceeds Ogre float coordinates");
      }
      render_from_probe.elements[12U + axis] =
          static_cast<float>(relative[axis]);
    }
    const Ogre::Matrix3 orientation = ToOgreOrientation(render_from_probe);
    const Ogre::Vector3 camera =
        TransformPoint(render_from_probe, descriptor.capture_position_local);
    const Ogre::Vector3 area_center =
        TransformPoint(render_from_probe, descriptor.influence_center_local);
    const Ogre::Vector3 shape_center = TransformPoint(
        render_from_probe, descriptor.correction_shape_center_local);
    const Ogre::Vector3 area_half =
        ToOgreVector(descriptor.influence_half_size);
    const Ogre::Vector3 shape_half =
        ToOgreVector(descriptor.correction_shape_half_size);
    const Ogre::Vector3 inner =
        ToOgreVector(descriptor.influence_inner_fraction);
    const Ogre::Aabb padded_area(area_center, area_half * kOgrePccPadding);
    const Ogre::Aabb padded_shape(shape_center, shape_half * kOgrePccPadding);
    if (!padded_shape.contains(padded_area)) {
      throw std::runtime_error(
          "pinned Ogre-Next cannot represent this rotated or off-center "
          "probe without clamping its influence box");
    }
    probe.set(camera, Ogre::Aabb(area_center, area_half), inner, orientation,
              Ogre::Aabb(shape_center, shape_half));
    probe.mNumIterations = 1U;
    probe.mEnabled = true;
    probe.mDirty = dirty;

    if (!NearlyEqual(probe.getProbeCameraPos(), camera) ||
        !NearlyEqual(probe.getArea().mCenter, area_center) ||
        !NearlyEqual(probe.getArea().mHalfSize, area_half * kOgrePccPadding) ||
        !NearlyEqual(probe.getAreaInnerRegion(), inner) ||
        !NearlyEqual(probe.getOrientation(), orientation) ||
        !NearlyEqual(probe.getProbeShape().mCenter, shape_center) ||
        !NearlyEqual(probe.getProbeShape().mHalfSize,
                     shape_half * kOgrePccPadding) ||
        !probe.getStatic()) {
      throw std::runtime_error("native PCC probe readback differs from its "
                               "exact descriptor mapping");
    }
  }

  Ogre::CubemapProbe *
  CreateCandidate(const ReflectionProbeUpdateRequest &request) {
    Ogre::CubemapProbe *candidate = pcc->createProbe();
    try {
      candidate->mEnabled = false;
      candidate->mMask = kCandidateProbeMask;
      candidate->mNumIterations = 1U;
      candidate->setTextureParams(request.resolution, request.resolution, true,
                                  Ogre::PFG_RGBA16_FLOAT, true);
      candidate->initWorkspace(request.descriptor.capture_near_meters,
                               request.descriptor.capture_far_meters);
      if (!candidate->isInitialized() || candidate->getWorkspace() == nullptr ||
          candidate->getInternalTexture() == nullptr) {
        throw std::runtime_error("candidate PCC probe did not initialize");
      }
      ApplyDescriptor(*candidate, request.descriptor,
                      request.absolute_world_origin_meters, true);
      candidate->mMask = kCandidateProbeMask;
      if (!candidate->mEnabled || !candidate->mDirty ||
          candidate->mMask != kCandidateProbeMask) {
        throw std::runtime_error(
            "candidate PCC probe is not enabled and isolated for capture");
      }
      return candidate;
    } catch (...) {
      pcc->destroyProbe(candidate);
      throw;
    }
  }

  [[nodiscard]] bool OnOwnerThread() const noexcept {
    return owner_thread == std::this_thread::get_id();
  }

  [[nodiscard]] bool SetPbsBinding(bool bind) noexcept {
    if (pbs == nullptr || pcc == nullptr) {
      return !bind;
    }
    try {
      Ogre::ParallaxCorrectedCubemapBase *current =
          pbs->getParallaxCorrectedCubemap();
      if (current != nullptr && current != pcc) {
        return false;
      }
      if (bind && current == nullptr) {
        pbs->setParallaxCorrectedCubemap(pcc);
      } else if (!bind && current == pcc) {
        pbs->setParallaxCorrectedCubemap(nullptr);
      }
      return pbs->getParallaxCorrectedCubemap() == (bind ? pcc : nullptr);
    } catch (...) {
      return false;
    }
  }

  [[nodiscard]] bool DestroyProbe(Ogre::CubemapProbe *probe) noexcept {
    if (probe == nullptr) {
      return true;
    }
    if (pcc == nullptr) {
      return false;
    }
    try {
      pcc->destroyProbe(probe);
      return true;
    } catch (...) {
      return false;
    }
  }

  [[nodiscard]] bool DrainDeferredProbes() noexcept {
    bool clean = true;
    for (Ogre::CubemapProbe *probe : deferred_probes) {
      clean = DestroyProbe(probe) && clean;
    }
    if (clean) {
      deferred_probes.clear();
    }
    return clean;
  }

  [[nodiscard]] bool DestroyUncommittedPcc() noexcept {
    if (pcc == nullptr || !states.empty() || !deferred_probes.empty()) {
      return false;
    }
    try {
      if (pbs == nullptr || pbs->getParallaxCorrectedCubemap() != nullptr) {
        return false;
      }
      Ogre::ParallaxCorrectedCubemap *retired = pcc;
      delete retired;
      pcc = nullptr;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      ++native_pcc_destroy_count;
#endif
      return ResetAutomaticIblMipmapPolicy();
    } catch (...) {
      return false;
    }
  }

  [[nodiscard]] bool AbortLocalPlan(std::uint64_t plan_id,
                                    Ogre::CubemapProbe *candidate,
                                    bool pbs_binding_changed,
                                    bool prior_pbs_bound,
                                    bool pcc_created) noexcept {
    bool clean = true;
    if (scheduler.has_pending_plan()) {
      try {
        clean = scheduler.Abort(plan_id).ok() && clean;
      } catch (...) {
        clean = false;
      }
    }
    clean = DestroyProbe(candidate) && clean;
    if (pbs_binding_changed) {
      clean = SetPbsBinding(prior_pbs_bound) && clean;
    }
    if (pcc_created) {
      clean = DestroyUncommittedPcc() && clean;
    }
    if (!clean) {
      faulted = true;
    }
    return clean;
  }

  void StageProspectiveStates(
      PendingFrame &staged,
      const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors,
      Ogre::CubemapProbe *candidate,
      const ReflectionProbeUpdateRequest *captured_request) {
    staged.candidate_states = states;
    for (const ReflectionProbeRuntimeDescriptor &descriptor : descriptors) {
      auto [entry, inserted] =
          staged.candidate_states.try_emplace(descriptor.probe_id);
      if (inserted || entry->second.committed == nullptr) {
        entry->second.descriptor = descriptor;
      }
    }
    if (captured_request != nullptr) {
      ProbeState &state =
          staged.candidate_states.at(captured_request->probe_id);
      state.committed = candidate;
      state.descriptor = captured_request->descriptor;
    }
    for (auto iterator = staged.candidate_states.begin();
         iterator != staged.candidate_states.end();) {
      const auto live_descriptor = std::lower_bound(
          descriptors.begin(), descriptors.end(), iterator->first,
          [](const ReflectionProbeRuntimeDescriptor &descriptor,
             std::uint64_t probe_id) {
            return descriptor.probe_id < probe_id;
          });
      const bool live = live_descriptor != descriptors.end() &&
                        live_descriptor->probe_id == iterator->first;
      if (!live) {
        iterator = staged.candidate_states.erase(iterator);
      } else {
        ++iterator;
      }
    }
    staged.retired_probes.reserve(states.size());
    for (const auto &entry : states) {
      Ogre::CubemapProbe *committed = entry.second.committed;
      const auto next = staged.candidate_states.find(entry.first);
      if (committed != nullptr &&
          (next == staged.candidate_states.end() ||
           next->second.committed != committed)) {
        staged.retired_probes.push_back(committed);
      }
    }
    for (const auto &entry : staged.candidate_states) {
      staged.prospective_live_probe_count +=
          entry.second.committed != nullptr ? 1U : 0U;
    }
  }

  void IssueDeferredReadback(Ogre::TextureGpu &raw_texture,
                             Ogre::TextureGpu &filtered_texture,
                             const ReflectionProbeCaptureMipMetadata &metadata,
                             std::uint64_t render_frame_id,
                             DeferredCaptureReadback &readback) {
    if (!TextureShapeMatches(filtered_texture, metadata.widths[0U],
                             metadata.mip_count) ||
        raw_texture.getTextureType() != Ogre::TextureTypes::TypeCube ||
        raw_texture.getPixelFormat() != Ogre::PFG_RGBA16_FLOAT ||
        raw_texture.getWidth() != metadata.widths[0U] ||
        raw_texture.getHeight() != metadata.heights[0U] ||
        raw_texture.getNumSlices() != metadata.face_count) {
      throw std::runtime_error(
          "deferred PCC source textures differ from their receipt layout");
    }
    Ogre::TextureGpuManager *manager = filtered_texture.getTextureManager();
    if (manager == nullptr || raw_texture.getTextureManager() != manager) {
      throw std::runtime_error(
          "raw and filtered PCC textures do not share one native manager");
    }

    DeferredCaptureReadback staged;
    staged.manager = manager;
    staged.metadata = metadata;
    staged.issue_frame_id = render_frame_id;
    staged.earliest_poll_frame_id =
        ComputeOgreNextPccEarliestReadbackFrame(render_frame_id);
    try {
      staged.raw_mip_zero = manager->createAsyncTextureTicket(
          raw_texture.getWidth(), raw_texture.getHeight(),
          raw_texture.getNumSlices(), raw_texture.getTextureType(),
          raw_texture.getPixelFormat());
      staged.raw_mip_zero->download(&raw_texture, 0U, false);
      staged.filtered_mips.reserve(metadata.mip_count);
      for (std::uint16_t mip = 0U; mip < metadata.mip_count; ++mip) {
        Ogre::AsyncTextureTicket *ticket =
            manager->createAsyncTextureTicket(
                metadata.widths[mip], metadata.heights[mip],
                metadata.face_count, filtered_texture.getTextureType(),
                filtered_texture.getPixelFormat());
        staged.filtered_mips.push_back(ticket);
        ticket->download(&filtered_texture, static_cast<Ogre::uint8>(mip),
                         false);
      }
    } catch (...) {
      static_cast<void>(DestroyDeferredCaptureReadback(staged));
      throw;
    }
    readback = std::move(staged);
  }

  [[nodiscard]] bool CancelInFlightCapture() noexcept {
    if (in_flight == nullptr || in_flight->staged == nullptr) {
      return false;
    }
    PendingFrame &staged = *in_flight->staged;
    bool clean = DestroyDeferredCaptureReadback(in_flight->readback);
    clean = AbortLocalPlan(staged.plan_id, staged.candidate,
                           staged.pbs_binding_changed,
                           staged.prior_pbs_bound,
                           staged.pcc_created) &&
            clean;
    staged.candidate = nullptr;
    in_flight.reset();
    if (!clean) {
      faulted = true;
    }
    return clean;
  }

  void StagePassThroughFrame(std::uint64_t render_frame_id) {
    auto staged = std::make_unique<PendingFrame>();
    staged->render_frame_id = render_frame_id;
    staged->prior_pbs_bound = audit.pbs_bound;
    staged->pass_through = true;
    pending = std::move(staged);
  }

  [[nodiscard]] RenderOperationResult CompleteDeferredCapture(
      std::uint64_t render_frame_id, const Double3 &origin,
      Ogre::Camera &camera) {
    if (in_flight == nullptr || in_flight->staged == nullptr) {
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "deferred PCC capture lost its staged transaction");
    }
    PendingFrame &staged = *in_flight->staged;
    const ReflectionProbeUpdateRequest &request = staged.captured_request;
    try {
      std::vector<std::vector<std::uint8_t>> filtered_storage(
          in_flight->readback.metadata.mip_count);
      for (std::uint16_t mip = 0U;
           mip < in_flight->readback.metadata.mip_count; ++mip) {
        CopyAsyncTicketCanonical(
            *in_flight->readback.filtered_mips[mip],
            in_flight->readback.metadata.widths[mip],
            in_flight->readback.metadata.heights[mip],
            in_flight->readback.metadata.face_count,
            filtered_storage[mip]);
      }
      std::vector<ReflectionProbeCapturedSubresourceView> views;
      BuildCapturedViews(in_flight->readback.metadata, filtered_storage,
                         views);
      ReflectionProbeCaptureMeasurementResult measurement =
          ComputeReflectionProbeCaptureMeasurement(
              request, CompiledBackend(),
              ReflectionProbeCapturePixelFormat::RGBA16_FLOAT, views);
      if (!measurement) {
        throw std::runtime_error(
            "canonical deferred readback failed measurement: " +
            measurement.validation.field + ": " +
            measurement.validation.detail);
      }
      const FilteredContentStats filtered_stats =
          MeasureFilteredContent(views);
      std::vector<std::uint8_t> raw_bytes;
      CopyAsyncTicketCanonical(
          *in_flight->readback.raw_mip_zero, request.resolution,
          request.resolution, kReflectionProbeCubemapFaceCount, raw_bytes);
      const std::uint64_t native_evidence =
          ComputeDeferredNativeExecutionEvidence(raw_bytes, request,
                                                 measurement);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      if (configuration.retain_capture_evidence) {
        staged.capture_evidence.raw_mip_zero_rgba16f = raw_bytes;
        for (const std::vector<std::uint8_t> &mip_bytes : filtered_storage) {
          staged.capture_evidence.filtered_rgba16f.insert(
              staged.capture_evidence.filtered_rgba16f.end(),
              mip_bytes.begin(), mip_bytes.end());
        }
        staged.capture_evidence.backend = CompiledBackend();
        staged.capture_evidence.render_system =
            root->getRenderSystem()->getName();
        const Ogre::RenderSystemCapabilities *capabilities =
            root->getRenderSystem()->getCapabilities();
        if (capabilities != nullptr) {
          staged.capture_evidence.device_name =
              capabilities->getDeviceName();
          staged.capture_evidence.driver_version =
              capabilities->getDriverVersion().toString();
        }
        staged.capture_evidence.render_frame_id = staged.capture_frame_id;
        staged.capture_evidence.simulation_tick = request.simulation_tick;
        staged.capture_evidence.probe_id = request.probe_id;
        staged.capture_evidence.content_revision = request.content_revision;
        staged.capture_evidence.candidate_generation =
            request.candidate_generation;
        staged.capture_evidence.deterministic_seed =
            request.deterministic_seed;
        staged.capture_evidence.capture_resolution = request.resolution;
        staged.capture_evidence.filtered_mips = measurement.mip_metadata;
        staged.capture_evidence.valid = true;
      }
#endif
      staged.measurement = measurement;
      staged.filtered_stats = filtered_stats;
      staged.native_execution_evidence = native_evidence;
      staged.receipts.push_back(
          ReflectionProbeCaptureReceipt::IssueFromConcreteAdapter(
              staged.plan_id, 0U, request, native_evidence, measurement));
      staged.captured = true;
      ApplyDescriptor(*staged.candidate, request.descriptor, origin, false);
      staged.candidate->mMask = kCandidateProbeMask;
      pcc->mMask = kCommittedProbeMask;
      const Ogre::Matrix4 view = camera.getViewMatrix();
      pcc->mTrackedPosition = view.inverseAffine().getTrans();
      pcc->mTrackedViewProjMatrix = camera.getProjectionMatrix() * view;
      if (!DestroyDeferredCaptureReadback(in_flight->readback)) {
        throw std::runtime_error(
            "deferred PCC readback tickets could not be retired");
      }
      staged.render_frame_id = render_frame_id;
      pending = std::move(in_flight->staged);
      in_flight.reset();
      return RenderOperationResult::Success();
    } catch (const std::bad_alloc &) {
      static_cast<void>(CancelInFlightCapture());
      return Failure(RenderOperationCode::OUT_OF_MEMORY,
                     "deferred PCC readback ran out of memory");
    } catch (const Ogre::Exception &error) {
      static_cast<void>(CancelInFlightCapture());
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     error.getFullDescription());
    } catch (const std::exception &error) {
      static_cast<void>(CancelInFlightCapture());
      return Failure(RenderOperationCode::BACKEND_FAILURE, error.what());
    }
  }

  [[nodiscard]] RenderOperationResult
  PrepareFrame(std::uint64_t render_frame_id, std::uint64_t simulation_tick,
               const Double3 &origin,
               const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors,
               const std::vector<OgreNextReflectionProbeItemBinding> &items,
               std::uintptr_t tracking_camera,
               const OgreNextReflectionProbeSkyBinding &sky) {
    if (!audit.initialized) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "runtime is not initialized");
    }
    if (!OnOwnerThread()) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "capture must run on the initialization thread");
    }
    if (faulted) {
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "runtime is fault-latched after native cleanup failure");
    }
    if (pending != nullptr ||
        (scheduler.has_pending_plan() && in_flight == nullptr)) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "a reflection frame transaction is already pending");
    }
    if (!DrainDeferredProbes()) {
      faulted = true;
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "retired native probes could not be destroyed");
    }
    if (tracking_camera == 0U) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "tracking camera is required");
    }
    auto *camera = reinterpret_cast<Ogre::Camera *>(tracking_camera);
    if (Ogre::Root::getSingletonPtr() != root ||
        root->getRenderSystem() == nullptr ||
        scene_manager->getDestinationRenderSystem() !=
            root->getRenderSystem() ||
        root->getHlmsManager()->getHlms(Ogre::HLMS_PBS) != pbs ||
        camera->getSceneManager() != scene_manager) {
      faulted = true;
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "borrowed Ogre owners changed before capture");
    }
    if ((audit.pbs_bound && pbs->getParallaxCorrectedCubemap() != pcc) ||
        (!audit.pbs_bound && pbs->getParallaxCorrectedCubemap() != nullptr)) {
      faulted = true;
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "HLMS PBS reflection ownership changed externally");
    }
    for (const OgreNextReflectionProbeItemBinding &binding : items) {
      if (binding.ogre_item == 0U) {
        return Failure(RenderOperationCode::INVALID_ARGUMENT,
                       "capture item binding contains a null native item");
      }
      auto *item = reinterpret_cast<Ogre::Item *>(binding.ogre_item);
      if (item->_getManager() != scene_manager ||
          item->getVisibilityFlags() != binding.authored_visibility_mask) {
        return Failure(
            RenderOperationCode::INVALID_ARGUMENT,
            "capture item does not belong to the scene or authored mask");
      }
    }
    Ogre::Item *sky_background_item = nullptr;
    Ogre::Item *sky_sun_item = nullptr;
    Ogre::SceneNode *sky_node = nullptr;
    Ogre::HlmsUnlitDatablock *sky_background_datablock = nullptr;
    Ogre::HlmsUnlitDatablock *sky_sun_datablock = nullptr;
    if (sky.enabled) {
      if (sky.background_item == 0U || sky.sun_item == 0U ||
          sky.sky_node == 0U || sky.background_datablock == 0U ||
          sky.sun_datablock == 0U) {
        return Failure(RenderOperationCode::INVALID_ARGUMENT,
                       "sky capture binding contains a null native object");
      }
      if (!std::isfinite(sky.capture_radiance_scale) ||
          sky.capture_radiance_scale <= 0.0F ||
          sky.capture_radiance_scale > 1.0F) {
        return Failure(
            RenderOperationCode::INVALID_ARGUMENT,
            "sky capture radiance scale must be a finite fraction in (0, 1]");
      }
      sky_background_item = reinterpret_cast<Ogre::Item *>(sky.background_item);
      sky_sun_item = reinterpret_cast<Ogre::Item *>(sky.sun_item);
      sky_node = reinterpret_cast<Ogre::SceneNode *>(sky.sky_node);
      sky_background_datablock = reinterpret_cast<Ogre::HlmsUnlitDatablock *>(
          sky.background_datablock);
      sky_sun_datablock =
          reinterpret_cast<Ogre::HlmsUnlitDatablock *>(sky.sun_datablock);
      if (sky_background_item->_getManager() != scene_manager ||
          sky_sun_item->_getManager() != scene_manager ||
          sky_background_item->getParentSceneNode() != sky_node ||
          sky_sun_item->getParentSceneNode() != sky_node ||
          sky_background_item->getVisibilityFlags() !=
              sky.authored_visibility_mask ||
          sky_sun_item->getVisibilityFlags() != sky.authored_visibility_mask ||
          sky_background_item->getNumSubItems() != 1U ||
          sky_sun_item->getNumSubItems() != 1U ||
          sky_background_item->getSubItem(0U)->getDatablock() !=
              sky_background_datablock ||
          sky_sun_item->getSubItem(0U)->getDatablock() != sky_sun_datablock ||
          !sky_background_datablock->hasColour() ||
          !sky_sun_datablock->hasColour()) {
        return Failure(
            RenderOperationCode::INVALID_ARGUMENT,
            "sky capture binding does not match the live analytic-sky scene");
      }
    }

    if (in_flight != nullptr) {
      if (!scheduler.has_pending_plan() || pcc == nullptr ||
          in_flight->staged == nullptr ||
          in_flight->staged->candidate == nullptr) {
        faulted = true;
        return Failure(RenderOperationCode::BACKEND_FAILURE,
                       "deferred PCC capture lost native or scheduler ownership");
      }
      try {
        for (auto &entry : states) {
          if (entry.second.committed != nullptr) {
            ApplyDescriptor(*entry.second.committed, entry.second.descriptor,
                            origin, false);
            entry.second.committed->mMask = kCommittedProbeMask;
          }
        }
        pcc->mMask = kCommittedProbeMask;
        const Ogre::Matrix4 view = camera->getViewMatrix();
        pcc->mTrackedPosition = view.inverseAffine().getTrans();
        pcc->mTrackedViewProjMatrix = camera->getProjectionMatrix() * view;
        if (!DeferredCaptureReadbackReady(in_flight->readback,
                                          render_frame_id)) {
          StagePassThroughFrame(render_frame_id);
          return RenderOperationResult::Success();
        }
      } catch (const Ogre::Exception &error) {
        static_cast<void>(CancelInFlightCapture());
        return Failure(RenderOperationCode::BACKEND_FAILURE,
                       error.getFullDescription());
      } catch (const std::exception &error) {
        static_cast<void>(CancelInFlightCapture());
        return Failure(RenderOperationCode::BACKEND_FAILURE, error.what());
      }

      if (AreReflectionProbeDescriptorSetsEquivalent(
              descriptors, in_flight->descriptors)) {
        return CompleteDeferredCapture(render_frame_id, origin, *camera);
      }
      // A newer descriptor set supersedes the completed but unpublished
      // candidate. Retire it without publishing stale pixels, then let this
      // same frame open a fresh scheduler transaction for the new set.
      if (!CancelInFlightCapture()) {
        return Failure(RenderOperationCode::BACKEND_FAILURE,
                       "superseded deferred PCC capture could not be retired");
      }
    }

    ReflectionProbePlanResult planned = scheduler.BeginFrame(
        render_frame_id, simulation_tick, origin, descriptors);
    if (!planned) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     planned.validation.field + ": " +
                         planned.validation.detail);
    }
    const ReflectionProbeUpdatePlan &plan = planned.plan;
    if (plan.requests.size() > 1U) {
      static_cast<void>(scheduler.Abort(plan.plan_id));
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "scheduler exceeded the one-capture native budget");
    }

    Ogre::CubemapProbe *candidate = nullptr;
    bool pbs_binding_changed = false;
    bool pcc_created = false;
    const bool prior_pbs_bound = audit.pbs_bound;
    bool native_capture_started = false;
    try {
      auto staged = std::make_unique<PendingFrame>();
      staged->render_frame_id = render_frame_id;
      staged->capture_frame_id = render_frame_id;
      staged->plan_id = plan.plan_id;
      staged->prior_pbs_bound = prior_pbs_bound;
      staged->receipts.reserve(plan.requests.size());
      if (!descriptors.empty() || !states.empty()) {
        const bool had_pcc = pcc != nullptr;
        EnsurePcc();
        pcc_created = !had_pcc && pcc != nullptr;
      }
      if (pcc != nullptr) {
        for (auto &entry : states) {
          ProbeState &state = entry.second;
          if (state.committed != nullptr) {
            ApplyDescriptor(*state.committed, state.descriptor, origin, false);
            state.committed->mMask = kCommittedProbeMask;
          }
        }
      }

      if (!plan.requests.empty()) {
        const ReflectionProbeUpdateRequest &request = plan.requests.front();
        candidate = CreateCandidate(request);
        std::vector<std::pair<Ogre::Item *, Ogre::uint32>> prior_flags;
        prior_flags.reserve(items.size() + 2U);
        bool restored = false;
        const auto restore_items = [&]() noexcept {
          if (restored) {
            return true;
          }
          bool clean = true;
          for (const auto &entry : prior_flags) {
            try {
              entry.first->setVisibilityFlags(entry.second);
            } catch (...) {
              clean = false;
            }
          }
          restored = clean;
          return clean;
        };
        const Ogre::uint32 prior_system_mask = pcc->mMask;
        const Ogre::uint32 prior_scene_visibility =
            scene_manager->getVisibilityMask();
        const Ogre::Vector3 prior_sky_position =
            sky_node != nullptr ? sky_node->getPosition()
                                : Ogre::Vector3::ZERO;
        const Ogre::ColourValue prior_sky_background_colour =
            sky_background_datablock != nullptr
                ? sky_background_datablock->getColour()
                : Ogre::ColourValue::White;
        const Ogre::ColourValue prior_sky_sun_colour =
            sky_sun_datablock != nullptr ? sky_sun_datablock->getColour()
                                         : Ogre::ColourValue::White;
        bool sky_moved = false;
        bool sky_scaled = false;
        const auto restore_capture_state = [&]() noexcept {
          bool clean = true;
          try {
            pcc->mMask = prior_system_mask;
          } catch (...) {
            clean = false;
          }
          try {
            scene_manager->setVisibilityMask(prior_scene_visibility);
            clean = scene_manager->getVisibilityMask() ==
                        prior_scene_visibility &&
                    clean;
          } catch (...) {
            clean = false;
          }
          if (sky_moved) {
            try {
              sky_node->setPosition(prior_sky_position);
              sky_moved = sky_node->getPosition() != prior_sky_position;
              clean = !sky_moved && clean;
            } catch (...) {
              clean = false;
            }
          }
          if (sky_scaled) {
            try {
              sky_background_datablock->setColour(
                  prior_sky_background_colour);
              sky_sun_datablock->setColour(prior_sky_sun_colour);
              sky_scaled =
                  sky_background_datablock->getColour() !=
                      prior_sky_background_colour ||
                  sky_sun_datablock->getColour() != prior_sky_sun_colour;
              clean = !sky_scaled && clean;
            } catch (...) {
              clean = false;
            }
          }
          clean = restore_items() && clean;
          return clean;
        };
        try {
          for (const OgreNextReflectionProbeItemBinding &binding : items) {
            auto *item = reinterpret_cast<Ogre::Item *>(binding.ogre_item);
            prior_flags.emplace_back(item, item->getVisibilityFlags());
            const bool visible_in_reflections =
                (binding.instance_flags &
                 MESH_INSTANCE_VISIBLE_IN_REFLECTIONS) != 0U;
            const bool mask_intersects =
                (binding.authored_visibility_mask &
                 (request.descriptor.visibility_mask &
                  kOgreNextRt4AuthoredVisibilityMask)) != 0U;
            const bool dynamic_allowed =
                request.descriptor.include_dynamic_geometry ||
                !binding.dynamic_mesh;
            item->setVisibilityFlags(visible_in_reflections &&
                                             mask_intersects && dynamic_allowed
                                         ? kOgreNextPccCaptureVisibilityBit
                                         : 0U);
          }
          if (sky_node != nullptr) {
            // The analytic sky is the environment, not scene content: it is
            // admitted into every capture unconditionally so the probe's
            // diffuse-GI mip chain carries real sky radiance instead of the
            // cleared black. The dome is camera-relative geometry, so it is
            // re-centred on the probe's capture position, and its authored
            // physical radiance is scaled onto the calibrated ambient level
            // (capture_radiance_scale, the SH seat gain) for exactly the
            // capture duration; restore_capture_state puts flag state,
            // position, and datablock colours back before any main-view
            // state verification can observe them.
            prior_flags.emplace_back(
                sky_background_item,
                sky_background_item->getVisibilityFlags());
            prior_flags.emplace_back(sky_sun_item,
                                     sky_sun_item->getVisibilityFlags());
            sky_background_item->setVisibilityFlags(
                kOgreNextPccCaptureVisibilityBit);
            sky_sun_item->setVisibilityFlags(kOgreNextPccCaptureVisibilityBit);
            sky_node->setPosition(candidate->getProbeCameraPos());
            sky_moved = true;
            const Ogre::ColourValue scaled_capture_colour(
                sky.capture_radiance_scale, sky.capture_radiance_scale,
                sky.capture_radiance_scale, 1.0F);
            sky_background_datablock->setColour(scaled_capture_colour);
            sky_sun_datablock->setColour(scaled_capture_colour);
            sky_scaled = true;
          }
          pcc->mMask = kCandidateProbeMask;
          if (!candidate->mEnabled || !candidate->mDirty ||
              candidate->mMask != kCandidateProbeMask) {
            throw std::runtime_error(
                "candidate PCC probe lost its enabled capture state");
          }
          native_capture_started = true;
          pcc->updateAllDirtyProbes();
          if (!restore_capture_state()) {
            faulted = true;
            throw std::runtime_error(
                "capture visibility state could not be restored");
          }
        } catch (...) {
          if (!restore_capture_state()) {
            faulted = true;
          }
          if (native_capture_started) {
            faulted = true;
          }
          throw;
        }

        Ogre::TextureGpu *filtered = candidate->getInternalTexture();
        const Ogre::CompositorChannelVec &external =
            candidate->getWorkspace()->getExternalRenderTargets();
        if (filtered == nullptr || external.size() < 2U ||
            external[0U] == nullptr || external[1U] != filtered) {
          throw std::runtime_error(
              "candidate workspace did not expose exact raw/filtered channels");
        }
        const ReflectionProbeCaptureMipMetadata metadata =
            ComputeReflectionProbeCaptureMipMetadata(request.resolution);
        if (configuration.defer_capture_readback) {
          staged->candidate = candidate;
          staged->captured_request = request;
          StageProspectiveStates(*staged, descriptors, candidate, &request);
          if (pcc != nullptr) {
            pcc->mMask = kCommittedProbeMask;
            const Ogre::Matrix4 view = camera->getViewMatrix();
            pcc->mTrackedPosition = view.inverseAffine().getTrans();
            pcc->mTrackedViewProjMatrix =
                camera->getProjectionMatrix() * view;
          }
          staged->pbs_binding_changed = pbs_binding_changed;
          staged->pcc_created = pcc_created;
          auto deferred = std::make_unique<InFlightCapture>();
          deferred->staged = std::move(staged);
          deferred->descriptors = descriptors;
          IssueDeferredReadback(*external[0U], *filtered, metadata,
                                render_frame_id, deferred->readback);
          in_flight = std::move(deferred);
          ++audit.deferred_capture_issue_count;
          StagePassThroughFrame(render_frame_id);
          return RenderOperationResult::Success();
        }
        FilteredReadback filtered_readback;
        ReadFilteredTexture(*filtered, metadata, filtered_readback);
        ReflectionProbeCaptureMeasurementResult measurement =
            ComputeReflectionProbeCaptureMeasurement(
                request, CompiledBackend(),
                ReflectionProbeCapturePixelFormat::RGBA16_FLOAT,
                filtered_readback.views);
        if (!measurement) {
          throw std::runtime_error(
              "canonical filtered readback failed measurement: " +
              measurement.validation.field + ": " +
              measurement.validation.detail);
        }
        const FilteredContentStats filtered_stats =
            MeasureFilteredContent(filtered_readback.views);
        if (std::getenv("ROR_PCC_CAPTURE_STATS") != nullptr) {
          // Diagnostic only, opt-in via environment: pure arithmetic on the
          // readback bytes the measurement above already produced, so it adds
          // no GPU synchronization. This is the live pass/fail evidence that
          // a capture carries content (the pre-F2 probes filtered to
          // all-black).
          std::fprintf(
              stderr,
              "[RoR|PccCaptureStats] frame=%llu probe=%llu resolution=%u "
              "sky_included=%d sky_capture_radiance_scale=%.6g "
              "filtered_mean_rgb=[%.6g,%.6g,%.6g] "
              "filtered_max_abs_rgb=%.6g nonzero_rgb_components=%llu\n",
              static_cast<unsigned long long>(render_frame_id),
              static_cast<unsigned long long>(request.probe_id),
              static_cast<unsigned int>(request.resolution),
              sky_node != nullptr ? 1 : 0,
              static_cast<double>(sky.capture_radiance_scale),
              filtered_stats.mean_r,
              filtered_stats.mean_g, filtered_stats.mean_b,
              static_cast<double>(filtered_stats.max_absolute_rgb),
              static_cast<unsigned long long>(
                  filtered_stats.nonzero_rgb_component_count));
        }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        std::vector<std::uint8_t> *raw_evidence =
            configuration.retain_capture_evidence
                ? &staged->capture_evidence.raw_mip_zero_rgba16f
                : nullptr;
#else
        std::vector<std::uint8_t> *raw_evidence = nullptr;
#endif
        const std::uint64_t native_evidence =
            ComputeNativeExecutionEvidence(*external[0U], request, measurement,
                                           raw_evidence);
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        if (configuration.retain_capture_evidence) {
          CopyCanonicalSubresources(
              filtered_readback.views,
          staged->capture_evidence.filtered_rgba16f);
          staged->capture_evidence.backend = CompiledBackend();
          staged->capture_evidence.render_system =
              root->getRenderSystem()->getName();
          const Ogre::RenderSystemCapabilities *capabilities =
              root->getRenderSystem()->getCapabilities();
          if (capabilities != nullptr) {
            staged->capture_evidence.device_name =
                capabilities->getDeviceName();
            staged->capture_evidence.driver_version =
                capabilities->getDriverVersion().toString();
          }
          staged->capture_evidence.render_frame_id = render_frame_id;
          staged->capture_evidence.simulation_tick = request.simulation_tick;
          staged->capture_evidence.probe_id = request.probe_id;
          staged->capture_evidence.content_revision =
              request.content_revision;
          staged->capture_evidence.candidate_generation =
              request.candidate_generation;
          staged->capture_evidence.deterministic_seed =
              request.deterministic_seed;
          staged->capture_evidence.capture_resolution = request.resolution;
          staged->capture_evidence.filtered_mips = measurement.mip_metadata;
          staged->capture_evidence.valid = true;
        }
#endif
        staged->receipts.push_back(
            ReflectionProbeCaptureReceipt::IssueFromConcreteAdapter(
                plan.plan_id, 0U, request, native_evidence, measurement));
        staged->candidate = candidate;
        staged->captured_request = request;
        staged->measurement = measurement;
        staged->filtered_stats = filtered_stats;
        staged->native_execution_evidence = native_evidence;
        staged->captured = true;
      }

      StageProspectiveStates(
          *staged, descriptors, candidate,
          plan.requests.empty() ? nullptr : &plan.requests.front());

      if (pcc != nullptr) {
        pcc->mMask = kCommittedProbeMask;
        const Ogre::Matrix4 view = camera->getViewMatrix();
        pcc->mTrackedPosition = view.inverseAffine().getTrans();
        pcc->mTrackedViewProjMatrix = camera->getProjectionMatrix() * view;
      }
      // Keep the main pass bound only to the last committed generation. In
      // particular, the first capture must not expose PCC's newly allocated,
      // not-yet-populated blend texture. FinalizeFrame changes the PBS binding
      // only after the main pass has completed and before scheduler commit.
      staged->pbs_binding_changed = pbs_binding_changed;
      staged->pcc_created = pcc_created;
      pending = std::move(staged);
      return RenderOperationResult::Success();
    } catch (const std::bad_alloc &) {
      static_cast<void>(AbortLocalPlan(plan.plan_id, candidate,
                                       pbs_binding_changed, prior_pbs_bound,
                                       pcc_created));
      return Failure(RenderOperationCode::OUT_OF_MEMORY,
                     "native capture ran out of memory");
    } catch (const Ogre::Exception &error) {
      static_cast<void>(AbortLocalPlan(plan.plan_id, candidate,
                                       pbs_binding_changed, prior_pbs_bound,
                                       pcc_created));
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     error.getFullDescription());
    } catch (const std::exception &error) {
      static_cast<void>(AbortLocalPlan(plan.plan_id, candidate,
                                       pbs_binding_changed, prior_pbs_bound,
                                       pcc_created));
      return Failure(RenderOperationCode::BACKEND_FAILURE, error.what());
    }
  }

  [[nodiscard]] RenderOperationResult
  FinalizeFrame(std::uint64_t render_frame_id) {
    if (!audit.initialized) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "runtime is not initialized");
    }
    if (!OnOwnerThread()) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "finalization must run on the initialization thread");
    }
    if (faulted) {
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "runtime is fault-latched after native cleanup failure");
    }
    if (pending == nullptr || pending->render_frame_id != render_frame_id) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "finalization does not match the prepared frame");
    }
    const bool should_bind = pending->prospective_live_probe_count != 0U;
    if (pbs->getParallaxCorrectedCubemap() !=
        (pending->prior_pbs_bound ? pcc : nullptr)) {
      faulted = true;
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "HLMS PBS ownership changed before finalization");
    }
    if (pending->pass_through) {
      if (in_flight == nullptr || !scheduler.has_pending_plan()) {
        faulted = true;
        return Failure(
            RenderOperationCode::BACKEND_FAILURE,
            "deferred PCC pass-through lost its capture transaction");
      }
      pending.reset();
      return RenderOperationResult::Success();
    }
    if (!deferred_probes.empty()) {
      faulted = true;
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "retired native probes were not drained before commit");
    }

    if (should_bind != pending->prior_pbs_bound) {
      if (!SetPbsBinding(should_bind)) {
        const bool restored = SetPbsBinding(pending->prior_pbs_bound);
        if (!restored) {
          faulted = true;
        }
        return Failure(RenderOperationCode::BACKEND_FAILURE,
                       "PBS rejected finalized PCC ownership");
      }
      pending->pbs_binding_changed = true;
    }

    ReflectionProbeCommitResult committed;
    try {
      committed = scheduler.Commit(pending->plan_id, pending->receipts);
    } catch (const std::bad_alloc &) {
      return Failure(RenderOperationCode::OUT_OF_MEMORY,
                     "scheduler commit ran out of memory");
    } catch (const std::exception &error) {
      return Failure(RenderOperationCode::BACKEND_FAILURE, error.what());
    }
    if (!committed) {
      const std::string detail =
          "scheduler rejected native receipt: " + committed.validation.field +
          ": " + committed.validation.detail;
      return Failure(RenderOperationCode::BACKEND_FAILURE, detail);
    }

    PendingFrame *published = pending.get();
    if (published->candidate != nullptr) {
      published->candidate->mMask = kCommittedProbeMask;
      published->candidate->mEnabled = true;
      published->candidate->mDirty = false;
    }
    states.swap(published->candidate_states);
    deferred_probes.swap(published->retired_probes);
    bool blend_texture_ready = false;
    if (!published->captured && pcc != nullptr && should_bind) {
      Ogre::TextureGpu *bind_texture = pcc->getBindTexture();
      blend_texture_ready =
          pcc->getNumCollectedProbes() != 0U && bind_texture != nullptr &&
          TextureShapeMatches(*bind_texture,
                              configuration.maximum_blend_resolution,
                              bind_texture->getNumMipmaps());
    }
    audit.committed_state_digest = committed.committed_state_digest;
    audit.pcc_enabled = pcc != nullptr;
    audit.successful_capture_count += committed.completed_capture_count;
    audit.failed_capture_count += committed.failed_capture_count;
    audit.live_probe_count = published->prospective_live_probe_count;
    audit.pbs_bound = should_bind;
    audit.blend_texture_ready = blend_texture_ready;
    if (published->captured) {
      audit.native_execution_evidence =
          published->native_execution_evidence;
      audit.last_capture_frame_id = published->capture_frame_id;
      audit.last_capture_publication_frame_id = render_frame_id;
      audit.last_capture_readback_latency_frames =
          ComputeOgreNextPccReadbackLatencyFrames(
              published->capture_frame_id, render_frame_id);
      if (configuration.defer_capture_readback) {
        ++audit.deferred_capture_completion_count;
      }
      audit.last_capture_simulation_tick =
          published->captured_request.simulation_tick;
      audit.last_probe_id = published->captured_request.probe_id;
      audit.last_content_revision =
          published->captured_request.content_revision;
      audit.last_candidate_generation =
          published->captured_request.candidate_generation;
      audit.last_deterministic_seed =
          published->captured_request.deterministic_seed;
      audit.last_capture_digest =
          published->measurement.canonical_capture_digest;
      audit.last_canonical_payload_bytes =
          published->measurement.canonical_payload_bytes;
      audit.filtered_finite_component_count =
          published->filtered_stats.finite_component_count;
      audit.filtered_nonzero_rgb_component_count =
          published->filtered_stats.nonzero_rgb_component_count;
      audit.filtered_max_absolute_rgb =
          published->filtered_stats.max_absolute_rgb;
      audit.completed_face_count =
          published->measurement.completed_face_count;
      audit.completed_mip_count =
          published->measurement.completed_mip_count;
      audit.last_probe_resolution = published->captured_request.resolution;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
      if (published->capture_evidence.valid) {
        last_capture_evidence =
            std::move(published->capture_evidence);
      }
#endif
    }
    published->candidate = nullptr;
    pending.reset();
    return RenderOperationResult::Success();
  }

  [[nodiscard]] bool AbortFrame(std::uint64_t render_frame_id) noexcept {
    if (!OnOwnerThread() || pending == nullptr ||
        pending->render_frame_id != render_frame_id) {
      return false;
    }
    if (pending->pass_through) {
      pending.reset();
      return CancelInFlightCapture();
    }
    bool clean = true;
    if (scheduler.has_pending_plan()) {
      try {
        clean = scheduler.Abort(pending->plan_id).ok() && clean;
      } catch (...) {
        clean = false;
      }
    } else {
      clean = false;
    }
    clean = DestroyProbe(pending->candidate) && clean;
    pending->candidate = nullptr;
    if (pending->pbs_binding_changed) {
      clean = SetPbsBinding(pending->prior_pbs_bound) && clean;
    }
    if (pending->pcc_created) {
      clean = DestroyUncommittedPcc() && clean;
    }
    pending.reset();
    if (!clean) {
      faulted = true;
    }
    return clean;
  }

  // The render path retires probes as a Prepare/Finalize transaction: an empty
  // descriptor list erases every candidate state (1211-1226), collects the
  // committed natives into retired_probes (1227-1236), and FinalizeFrame swaps
  // the live set empty, moves those natives to deferred_probes and calls
  // SetPbsBinding(false) because prospective_live_probe_count is zero
  // (1294-1342). A generation whose final scene is retired never renders, so
  // none of that runs. Reproduce that exact end state -- and nothing else.
  //
  // This deliberately does NOT call PrepareFrame. PrepareFrame requires a
  // tracking camera (961), opens a scheduler transaction, and for any due
  // descriptor performs a real cubemap capture. A retirement has no item
  // bindings at all -- reflection_items is built only from the frontend's
  // retained instances on the render path, and unlisted items never carry
  // kOgreNextPccCaptureVisibilityBit -- so a capture taken from here would
  // render a black cubemap and commit it as an adapter-authoritative receipt.
  [[nodiscard]] RenderOperationResult RetireProbesForSceneGeneration() {
    if (!audit.initialized) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "runtime is not initialized");
    }
    if (!OnOwnerThread()) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "probe retirement must run on the initialization thread");
    }
    if (faulted) {
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "runtime is fault-latched after native cleanup failure");
    }
    if (pending != nullptr) {
      return Failure(RenderOperationCode::OUTSTANDING_LEASES,
                     "reflection work remains pending at probe retirement");
    }
    // A deferred PCC readback may legitimately outlive the last rendered
    // frame. The capture has not published native probe state yet, so scene
    // retirement must abort that scheduler transaction instead of requiring
    // another render merely to drain it. A prepared frame above remains an
    // outstanding lease and is never canceled implicitly.
    if (in_flight != nullptr && !CancelInFlightCapture()) {
      faulted = true;
      return Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "deferred reflection capture could not be canceled at probe retirement");
    }
    if (scheduler.has_pending_plan()) {
      return Failure(RenderOperationCode::OUTSTANDING_LEASES,
                     "reflection work remains pending at probe retirement");
    }
    if (states.empty() && !audit.pbs_bound) {
      // The final empty scene was rendered: FinalizeFrame already did this.
      // Touch nothing, count nothing, and leave ResetSceneGeneration's own
      // check as the authority for that path.
      return RenderOperationResult::Success();
    }
    // The only allocation, taken before any native mutation, so the steps
    // below cannot fail halfway and leave PBS unbound while the live set still
    // names probes.
    try {
      deferred_probes.reserve(deferred_probes.size() + states.size());
    } catch (const std::bad_alloc &) {
      return Failure(RenderOperationCode::OUT_OF_MEMORY,
                     "probe retirement could not stage the deferred list");
    }
    if (!SetPbsBinding(false)) {
      faulted = true;
      return Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "HLMS PBS rejected reflection unbinding at generation retirement");
    }
    audit.pbs_bound = false;
    std::uint64_t retired = 0U;
    for (auto &entry : states) {
      if (entry.second.committed != nullptr) {
        deferred_probes.push_back(entry.second.committed);
        entry.second.committed = nullptr;
        ++retired;
      }
    }
    states.clear();
    audit.live_probe_count = 0U;
    audit.blend_texture_ready = false;
    audit.scene_reset_retired_probe_count += retired;
    ++audit.scene_reset_teardowns;
    return RenderOperationResult::Success();
  }

  [[nodiscard]] RenderOperationResult ResetSceneGeneration() {
    if (!audit.initialized) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "runtime is not initialized");
    }
    if (!OnOwnerThread()) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "scene reset must run on the initialization thread");
    }
    if (faulted) {
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "runtime is fault-latched after native cleanup failure");
    }
    if (pending != nullptr || scheduler.has_pending_plan()) {
      return Failure(RenderOperationCode::OUTSTANDING_LEASES,
                     "reflection work remains pending at scene reset");
    }
    bool native_pbs_bound = false;
    if (pbs != nullptr) {
      try {
        native_pbs_bound = pbs->getParallaxCorrectedCubemap() != nullptr;
      } catch (...) {
        faulted = true;
        return Failure(RenderOperationCode::BACKEND_FAILURE,
                       "could not validate native PBS binding at scene reset");
      }
    }
    if (!states.empty() || audit.live_probe_count != 0U || audit.pbs_bound ||
        native_pbs_bound) {
      return Failure(RenderOperationCode::INVALID_ARGUMENT,
                     "final empty scene did not retire all reflection probes");
    }
    if (!DrainDeferredProbes()) {
      faulted = true;
      return Failure(RenderOperationCode::BACKEND_FAILURE,
                     "retired probes could not be destroyed at scene reset");
    }
    scheduler.Reset();
    audit.committed_state_digest = scheduler.committed_state_digest();
    audit.native_execution_evidence = 0U;
    audit.last_capture_frame_id = 0U;
    audit.last_capture_publication_frame_id = 0U;
    audit.last_capture_readback_latency_frames = 0U;
    audit.last_capture_simulation_tick = 0U;
    audit.last_probe_id = 0U;
    audit.last_content_revision = 0U;
    audit.last_candidate_generation = 0U;
    audit.last_deterministic_seed = 0U;
    audit.last_capture_digest = 0U;
    audit.last_canonical_payload_bytes = 0U;
    audit.filtered_finite_component_count = 0U;
    audit.filtered_nonzero_rgb_component_count = 0U;
    audit.filtered_max_absolute_rgb = 0.0F;
    audit.completed_face_count = 0U;
    audit.completed_mip_count = 0U;
    audit.last_probe_resolution = 0U;
    audit.blend_texture_ready = false;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    last_capture_evidence = {};
#endif
    return RenderOperationResult::Success();
  }

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  [[nodiscard]] OgreNextReflectionProbeNativeOwnershipEvidence
  NativeOwnershipEvidence() const noexcept {
    OgreNextReflectionProbeNativeOwnershipEvidence evidence;
    evidence.pcc_create_count = native_pcc_create_count;
    evidence.pcc_destroy_count = native_pcc_destroy_count;
    evidence.live_pcc_count = pcc != nullptr ? 1U : 0U;
    if (pbs == nullptr) {
      return evidence;
    }
    try {
      Ogre::ParallaxCorrectedCubemapBase *binding =
          pbs->getParallaxCorrectedCubemap();
      evidence.pbs_unbound = binding == nullptr;
      evidence.pbs_bound_to_runtime = pcc != nullptr && binding == pcc;
      evidence.pbs_query_succeeded = true;
    } catch (...) {
      return evidence;
    }
    return evidence;
  }
#endif

  [[nodiscard]] bool Shutdown() noexcept {
    if (audit.initialized && !OnOwnerThread()) {
      return false;
    }
    bool clean = true;
    if (pending != nullptr) {
      clean = AbortFrame(pending->render_frame_id) && clean;
    } else if (in_flight != nullptr) {
      clean = CancelInFlightCapture() && clean;
    } else if (scheduler.has_pending_plan()) {
      clean = false;
    }
    if (pbs != nullptr) {
      try {
        if (pbs->getParallaxCorrectedCubemap() == pcc) {
          pbs->setParallaxCorrectedCubemap(nullptr);
        } else if (pbs->getParallaxCorrectedCubemap() != nullptr) {
          clean = false;
        }
      } catch (...) {
        clean = false;
      }
    }
    audit.pbs_bound = false;
    if (pcc != nullptr) {
      bool destroyed = false;
      try {
        Ogre::ParallaxCorrectedCubemap *retired = pcc;
        delete retired;
        pcc = nullptr;
        destroyed = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
        ++native_pcc_destroy_count;
#endif
      } catch (...) {
        clean = false;
      }
      if (destroyed) {
        clean = ResetAutomaticIblMipmapPolicy() && clean;
      }
    }
    if (root != nullptr) {
      try {
        Ogre::CompositorManager2 *compositors = root->getCompositorManager2();
        if (owns_probe_workspace_definition && compositors != nullptr &&
            compositors->hasWorkspaceDefinition(kProbeWorkspaceName)) {
          compositors->removeWorkspaceDefinition(kProbeWorkspaceName);
          owns_probe_workspace_definition = false;
        }
        if (owns_probe_node_definition && compositors != nullptr &&
            compositors->hasNodeDefinition(kProbeNodeName)) {
          compositors->removeNodeDefinition(kProbeNodeName);
          owns_probe_node_definition = false;
        }
      } catch (...) {
        clean = false;
      }
      try {
        Ogre::ResourceGroupManager &resources =
            Ogre::ResourceGroupManager::getSingleton();
        for (std::size_t remaining = added_resource_location_count;
             remaining != 0U; --remaining) {
          const Ogre::String &location = resource_locations[remaining - 1U];
          if (resources.resourceLocationExists(
                  location,
                  Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)) {
            resources.removeResourceLocation(
                location,
                Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
          }
          added_resource_location_count = remaining - 1U;
        }
      } catch (...) {
        clean = false;
      }
    }
    resource_locations = {};
    added_resource_location_count = 0U;
    owns_probe_node_definition = false;
    owns_probe_workspace_definition = false;
    states.clear();
    deferred_probes.clear();
    pending.reset();
    in_flight.reset();
    scheduler.Reset();
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    last_capture_evidence = {};
#endif
    root = nullptr;
    scene_manager = nullptr;
    pbs = nullptr;
    owner_thread = {};
    faulted = false;
    audit = {};
    configuration = {};
    return clean;
  }

  OgreNextReflectionProbeRuntimeConfiguration configuration;
  Ogre::Root *root = nullptr;
  Ogre::SceneManager *scene_manager = nullptr;
  Ogre::HlmsPbs *pbs = nullptr;
  Ogre::ParallaxCorrectedCubemap *pcc = nullptr;
  ReflectionProbeUpdateScheduler scheduler;
  std::map<std::uint64_t, ProbeState> states;
  std::vector<Ogre::CubemapProbe *> deferred_probes;
  std::unique_ptr<PendingFrame> pending;
  std::unique_ptr<InFlightCapture> in_flight;
  std::array<Ogre::String, kReflectionResourceLocationCount>
      resource_locations{};
  std::size_t added_resource_location_count = 0U;
  bool owns_probe_node_definition = false;
  bool owns_probe_workspace_definition = false;
  OgreNextReflectionProbeAudit audit;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  OgreNextReflectionProbeCaptureEvidence last_capture_evidence;
  std::uint64_t native_pcc_create_count = 0U;
  std::uint64_t native_pcc_destroy_count = 0U;
#endif
  std::thread::id owner_thread;
  bool faulted = false;
};

OgreNextReflectionProbeRuntime::OgreNextReflectionProbeRuntime()
    : impl_(std::make_unique<Impl>()) {}

OgreNextReflectionProbeRuntime::~OgreNextReflectionProbeRuntime() = default;

RenderOperationResult OgreNextReflectionProbeRuntime::Initialize(
    OgreNextReflectionProbeRuntimeConfiguration configuration) {
  return impl_->Initialize(std::move(configuration));
}

RenderOperationResult OgreNextReflectionProbeRuntime::PrepareFrame(
    std::uint64_t render_frame_id, std::uint64_t simulation_tick,
    const Double3 &absolute_world_origin_meters,
    const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors,
    const std::vector<OgreNextReflectionProbeItemBinding> &items,
    std::uintptr_t tracking_camera,
    const OgreNextReflectionProbeSkyBinding &sky) {
  return impl_->PrepareFrame(render_frame_id, simulation_tick,
                            absolute_world_origin_meters, descriptors, items,
                            tracking_camera, sky);
}

RenderOperationResult OgreNextReflectionProbeRuntime::FinalizeFrame(
    std::uint64_t render_frame_id) {
  return impl_->FinalizeFrame(render_frame_id);
}

bool OgreNextReflectionProbeRuntime::AbortFrame(
    std::uint64_t render_frame_id) noexcept {
  return impl_->AbortFrame(render_frame_id);
}

RenderOperationResult
OgreNextReflectionProbeRuntime::RetireProbesForSceneGeneration() {
  return impl_->RetireProbesForSceneGeneration();
}

RenderOperationResult
OgreNextReflectionProbeRuntime::ResetSceneGeneration() {
  return impl_->ResetSceneGeneration();
}

OgreNextReflectionProbeAudit
OgreNextReflectionProbeRuntime::QueryAudit() const noexcept {
  return impl_->audit;
}

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
OgreNextReflectionProbeCaptureEvidence
OgreNextReflectionProbeRuntime::QueryLastCaptureEvidence() const {
  return impl_->last_capture_evidence;
}

OgreNextReflectionProbeNativeOwnershipEvidence
OgreNextReflectionProbeRuntime::QueryNativeOwnershipEvidence() const noexcept {
  return impl_->NativeOwnershipEvidence();
}
#endif

bool OgreNextReflectionProbeRuntime::Shutdown() noexcept {
  return impl_->Shutdown();
}

} // namespace RoR::Render
