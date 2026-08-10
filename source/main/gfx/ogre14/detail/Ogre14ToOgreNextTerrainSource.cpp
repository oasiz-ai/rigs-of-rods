/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14ToOgreNextTerrainSource.h"
#include "OgreNextDemoPrivatePolicy.h"

#include "gfx/render/MaterialDescriptor.h"
#include "gfx/render/RenderResourceDescriptors.h"

#include <OgreBuildSettings.h>
#include <OgreException.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreMaterial.h>
#include <OgrePass.h>
#include <OgrePixelFormat.h>
#include <OgreResource.h>
#include <OgreSceneManager.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>
#include <Terrain/OgreTerrain.h>
#include <Terrain/OgreTerrainGroup.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <string_view>
#include <utility>

static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                  OGRE_VERSION_PATCH == 2,
              "the disposable OgreNext demo source is pinned to OGRE 14.5.2");
static_assert(sizeof(Ogre::Real) == sizeof(float),
              "the OgreNext demo source requires binary32 Ogre::Real");

namespace RoR::Gfx::Detail {
namespace {

constexpr std::uint32_t kMaximumCompositeDimension = 8192U;
constexpr std::uint32_t kMaximumCompositeMipLevels = 32U;
constexpr std::uint64_t kMaximumCompositeBytes = 384ULL * 1024ULL * 1024ULL;
constexpr char kMeshIdDomain[] =
    "RoR/OgreNextDemo/Terrain/MeshSourceAsset/v1";
constexpr char kTextureIdDomain[] =
    "RoR/OgreNextDemo/Terrain/TextureSourceAsset/v1";
constexpr char kSamplerIdDomain[] =
    "RoR/OgreNextDemo/Terrain/SamplerSourceAsset/v1";
constexpr char kMaterialIdDomain[] =
    "RoR/OgreNextDemo/Terrain/MaterialSourceAsset/v1";
constexpr char kObjectIdDomain[] =
    "RoR/OgreNextDemo/Terrain/StaticObject/v1";

Render::ValidationResult Failure(Render::ValidationCode code,
                                 const char *field, const char *detail,
                                 std::size_t index =
                                     Render::ValidationResult::kNoElement) {
  return Render::ValidationResult::Failure(code, field, detail, index);
}

bool CheckedMultiply(std::uint64_t first, std::uint64_t second,
                     std::uint64_t &output) noexcept {
  if (first != 0U &&
      second > (std::numeric_limits<std::uint64_t>::max)() / first) {
    return false;
  }
  output = first * second;
  return true;
}

bool CheckedAdd(std::uint64_t first, std::uint64_t second,
                std::uint64_t &output) noexcept {
  if (second > (std::numeric_limits<std::uint64_t>::max)() - first) {
    return false;
  }
  output = first + second;
  return true;
}

std::uint32_t CompleteMipCount(std::uint32_t width,
                               std::uint32_t height) noexcept {
  std::uint32_t count = 1U;
  while (width > 1U || height > 1U) {
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
    ++count;
  }
  return count;
}

void AppendU32(std::string &bytes, std::uint32_t value) {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

void AppendU64(std::string &bytes, std::uint64_t value) {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

void AppendPointer(std::string &bytes, const void *pointer) {
  AppendU64(bytes, static_cast<std::uint64_t>(
                       reinterpret_cast<std::uintptr_t>(pointer)));
}

void AppendFloat(std::string &bytes, float value) {
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(bytes, bits);
}

void AppendString(std::string &bytes, std::string_view value) {
  AppendU64(bytes, static_cast<std::uint64_t>(value.size()));
  bytes.append(value.data(), value.size());
}

bool IsIdentityFloat(float value, float expected) noexcept {
  std::uint32_t value_bits = 0U;
  std::uint32_t expected_bits = 0U;
  std::memcpy(&value_bits, &value, sizeof(value_bits));
  std::memcpy(&expected_bits, &expected, sizeof(expected_bits));
  return value_bits == expected_bits;
}

std::string PageDebugName(std::int32_t x, std::int32_t y,
                          std::string_view suffix) {
  return "OgreNextDemo/Terrain/" + std::to_string(x) + "/" +
         std::to_string(y) + "/" + std::string(suffix);
}

struct NativeMip final {
  std::uint32_t level = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  Ogre::HardwarePixelBufferPtr buffer;
};

struct NativePageReadback final {
  Render::TextureResourceDescriptor texture;
  Render::SamplerResourceDescriptor sampler;
};

Render::ValidationResult AcquireNativeMipInventory(
    Ogre::Texture &texture, std::vector<NativeMip> &mips) {
  const std::size_t native_width = texture.getWidth();
  const std::size_t native_height = texture.getHeight();
  if (native_width == 0U || native_height == 0U ||
      native_width > kMaximumCompositeDimension ||
      native_height > kMaximumCompositeDimension ||
      native_width > (std::numeric_limits<std::uint32_t>::max)() ||
      native_height > (std::numeric_limits<std::uint32_t>::max)()) {
    return Failure(Render::ValidationCode::INVALID_DIMENSIONS,
                   "ogre_next_demo.terrain.texture.dimensions",
                   "composite dimensions are empty, truncated, or exceed the demo cap");
  }
  const std::uint32_t width = static_cast<std::uint32_t>(native_width);
  const std::uint32_t height = static_cast<std::uint32_t>(native_height);
  const std::size_t additional = texture.getNumMipmaps();
  if (additional >= (std::numeric_limits<std::uint32_t>::max)()) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.terrain.texture.mip_levels",
                   "native additional mip count cannot be represented");
  }
  const std::uint32_t count = static_cast<std::uint32_t>(additional) + 1U;
  if (count > CompleteMipCount(width, height) ||
      count > kMaximumCompositeMipLevels) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.native_mip_inventory",
                   "composite native mips are not a contiguous base-to-N inventory");
  }

  std::vector<NativeMip> candidate;
  candidate.reserve(count);
  std::uint64_t total_bytes = 0U;
  std::uint32_t mip_width = width;
  std::uint32_t mip_height = height;
  for (std::uint32_t level = 0U; level < count; ++level) {
    Ogre::HardwarePixelBufferPtr buffer = texture.getBuffer(0U, level);
    std::uint64_t row_bytes = 0U;
    std::uint64_t slice_bytes = 0U;
    std::uint64_t next_total = 0U;
    if (!buffer || buffer->getFormat() != Ogre::PF_BYTE_RGBA ||
        buffer->getWidth() != mip_width ||
        buffer->getHeight() != mip_height || buffer->getDepth() != 1U ||
        !CheckedMultiply(mip_width, 4U, row_bytes) ||
        !CheckedMultiply(row_bytes, mip_height, slice_bytes) ||
        !CheckedAdd(total_bytes, slice_bytes, next_total) ||
        next_total > kMaximumCompositeBytes) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.terrain.texture.mip_layout",
                     "composite mip identity, RGBA layout, or aggregate size is invalid",
                     level);
    }
    NativeMip mip;
    mip.level = level;
    mip.width = mip_width;
    mip.height = mip_height;
    mip.buffer = std::move(buffer);
    candidate.push_back(std::move(mip));
    total_bytes = next_total;
    mip_width = (std::max)(1U, mip_width / 2U);
    mip_height = (std::max)(1U, mip_height / 2U);
  }
  mips = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult CaptureExactNativeState(
    Ogre::TerrainGroup &group, std::int32_t slot_x, std::int32_t slot_y,
    Ogre::Texture &texture, const std::vector<NativeMip> &mips,
    OgreNextDemoSamplingObservation &observation) {
  if (slot_x < -32768 || slot_x > 32767 || slot_y < -32768 ||
      slot_y > 32767) {
    return Failure(Render::ValidationCode::INVALID_DIMENSIONS,
                   "ogre_next_demo.terrain.slot",
                   "TerrainGroup slot coordinates exceed the signed identity range");
  }
  const std::uint32_t packed = group.packIndex(slot_x, slot_y);
  const auto found = group.getTerrainSlots().find(packed);
  Ogre::TerrainGroup::TerrainSlot *const slot =
      found != group.getTerrainSlots().end() ? found->second : nullptr;
  Ogre::Terrain *const terrain = group.getTerrain(slot_x, slot_y);
  if (slot == nullptr || terrain == nullptr || slot->instance != terrain ||
      slot->x != slot_x || slot->y != slot_y ||
      group.packIndex(slot->x, slot->y) != packed || !terrain->isLoaded() ||
      terrain->isDerivedDataUpdateInProgress() ||
      terrain->getAlignment() != group.getAlignment() ||
      terrain->getSize() != group.getTerrainSize() ||
      terrain->getWorldSize() != group.getTerrainWorldSize()) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.native_page",
                   "TerrainGroup slot identity, residency, or layout is unstable");
  }
  const Ogre::TexturePtr &current_texture = terrain->getCompositeMap();
  if (!current_texture || current_texture.get() != &texture ||
      texture.getLoadingState() != Ogre::Resource::LOADSTATE_LOADED ||
      !texture.isLoaded() || texture.getTextureType() != Ogre::TEX_TYPE_2D ||
      texture.getFormat() != Ogre::PF_BYTE_RGBA || texture.getDepth() != 1U ||
      texture.getNumFaces() != 1U || texture.getHandle() == 0U ||
      texture.getUsage() < 0) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.texture.native_state",
                   "composite is not a stable loaded display-domain 2D PF_BYTE_RGBA texture");
  }
  const std::size_t revision = texture.getStateCount();
  if (revision == 0U ||
      revision == (std::numeric_limits<std::size_t>::max)()) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.texture.revision",
                   "composite resource has no stable nonzero revision");
  }
  if (mips.size() != texture.getNumMipmaps() + 1U) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.texture.mip_identity",
                   "retained mip inventory changed during observation");
  }
  for (std::size_t index = 0U; index < mips.size(); ++index) {
    const Ogre::HardwarePixelBufferPtr &current =
        texture.getBuffer(0U, static_cast<unsigned int>(index));
    if (!current || current.get() != mips[index].buffer.get() ||
        current->getWidth() != mips[index].width ||
        current->getHeight() != mips[index].height ||
        current->getDepth() != 1U ||
        current->getFormat() != Ogre::PF_BYTE_RGBA) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.terrain.texture.mip_identity",
                     "retained composite mip pointer or layout changed",
                     index);
    }
  }

  const Ogre::MaterialPtr material = terrain->getMaterial();
  if (!material || material->getNumTechniques() <= 1U) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.terrain.sampling.material",
                   "terrain material has no LOD-one composite sampling container");
  }
  Ogre::Technique *const technique = material->getTechnique(1U);
  Ogre::Pass *const pass = technique != nullptr &&
                                  technique->getNumPasses() > 0U
                              ? technique->getPass(0U)
                              : nullptr;
  Ogre::TextureUnitState *const unit =
      pass != nullptr && pass->getNumTextureUnitStates() > 0U
          ? pass->getTextureUnitState(0U)
          : nullptr;
  const Ogre::TexturePtr bound_texture =
      unit != nullptr ? unit->_getTexturePtr() : Ogre::TexturePtr{};
  const Ogre::SamplerPtr sampler =
      unit != nullptr ? unit->getSampler() : Ogre::SamplerPtr{};
  Ogre::SceneManager *const scene_manager = terrain->getSceneManager();
  if (technique == nullptr || technique->getParent() != material.get() ||
      technique->getLodIndex() != 1U || pass == nullptr ||
      pass->getParent() != technique || pass->getIndex() != 0U ||
      unit == nullptr || unit->getParent() != pass || !bound_texture ||
      bound_texture.get() != &texture || !sampler || scene_manager == nullptr) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.sampling.binding",
                   "LOD-one TUS0 no longer owns the exact composite, sampler, and scene");
  }
  OgreNextDemoSamplingObservation candidate_observation;
  candidate_observation.ordinary_texture =
      unit->getContentType() == Ogre::TextureUnitState::CONTENT_NAMED &&
      unit->getNumFrames() == 1U && unit->getCurrentFrame() == 0U &&
      unit->getTextureType() == Ogre::TEX_TYPE_2D && !unit->isBlank() &&
      !unit->isTextureLoadFailing() &&
      unit->getUnorderedAccessMipLevel() == -1;
  candidate_observation.uv0_identity =
      unit->getTextureCoordSet() == 0U &&
      unit->_deriveTexCoordCalcMethod() == Ogre::TEXCALC_NONE &&
      unit->getEffects().empty() &&
      IsIdentityFloat(unit->getTextureUScroll(), 0.0F) &&
      IsIdentityFloat(unit->getTextureVScroll(), 0.0F) &&
      IsIdentityFloat(unit->getTextureUScale(), 1.0F) &&
      IsIdentityFloat(unit->getTextureVScale(), 1.0F) &&
      IsIdentityFloat(unit->getTextureRotate().valueRadians(), 0.0F);
  const Ogre::Matrix4 &transform = unit->getTextureTransform();
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      if (!IsIdentityFloat(static_cast<float>(transform[row][column]),
                           row == column ? 1.0F : 0.0F)) {
        candidate_observation.uv0_identity = false;
      }
    }
  }
  const Ogre::Sampler::UVWAddressingMode address =
      sampler->getAddressingMode();
  candidate_observation.sampler_identity =
      address.u == Ogre::TAM_CLAMP && address.v == Ogre::TAM_CLAMP &&
      address.w == Ogre::TAM_CLAMP &&
      sampler->getFiltering(Ogre::FT_MIN) == Ogre::FO_LINEAR &&
      sampler->getFiltering(Ogre::FT_MAG) == Ogre::FO_LINEAR &&
      sampler->getFiltering(Ogre::FT_MIP) == Ogre::FO_POINT &&
      sampler->getAnisotropy() >= 1U &&
      IsIdentityFloat(sampler->getMipmapBias(), 0.0F) &&
      !sampler->getCompareEnabled();
  candidate_observation.gamma_disabled =
      !texture.isHardwareGammaEnabled() &&
      !unit->isHardwareGammaEnabled();
  candidate_observation.fog_disabled =
      scene_manager->getFogMode() == Ogre::FOG_NONE;
  Render::ValidationResult validation =
      ValidateOgreNextDemoSampling(candidate_observation);
  if (!validation) {
    return validation;
  }

  std::string candidate;
  candidate.reserve(1024U + mips.size() * 32U);
  AppendString(candidate, "RoR/OgreNextDemo/Terrain/NativeState/v1");
  AppendPointer(candidate, &group);
  AppendPointer(candidate, slot);
  AppendPointer(candidate, terrain);
  AppendU32(candidate, packed);
  AppendU32(candidate, static_cast<std::uint32_t>(slot_x));
  AppendU32(candidate, static_cast<std::uint32_t>(slot_y));
  AppendString(candidate, group.getResourceGroup());
  AppendString(candidate, group.getFilenamePrefix());
  AppendString(candidate, group.getFilenameExtension());
  AppendString(candidate, slot->def.filename);
  AppendPointer(candidate, slot->def.importData);
  AppendString(candidate, group.generateFilename(slot_x, slot_y));
  AppendString(candidate, terrain->getMaterialName());
  AppendU32(candidate, static_cast<std::uint32_t>(terrain->getAlignment()));
  AppendU32(candidate, terrain->getSize());
  AppendFloat(candidate, static_cast<float>(terrain->getWorldSize()));
  const Ogre::Vector3 &position = terrain->getPosition();
  AppendFloat(candidate, static_cast<float>(position.x));
  AppendFloat(candidate, static_cast<float>(position.y));
  AppendFloat(candidate, static_cast<float>(position.z));
  AppendPointer(candidate, material.get());
  AppendPointer(candidate, technique);
  AppendPointer(candidate, pass);
  AppendPointer(candidate, unit);
  AppendPointer(candidate, sampler.get());
  AppendPointer(candidate, scene_manager);
  AppendPointer(candidate, &texture);
  AppendU64(candidate, static_cast<std::uint64_t>(texture.getHandle()));
  AppendString(candidate, texture.getGroup());
  AppendString(candidate, texture.getName());
  AppendU64(candidate, static_cast<std::uint64_t>(revision));
  AppendU32(candidate, static_cast<std::uint32_t>(texture.getUsage()));
  AppendU32(candidate, static_cast<std::uint32_t>(texture.getWidth()));
  AppendU32(candidate, static_cast<std::uint32_t>(texture.getHeight()));
  AppendU32(candidate, static_cast<std::uint32_t>(mips.size()));
  AppendU32(candidate, unit->getTextureCoordSet());
  AppendFloat(candidate, unit->getTextureUScroll());
  AppendFloat(candidate, unit->getTextureVScroll());
  AppendFloat(candidate, unit->getTextureUScale());
  AppendFloat(candidate, unit->getTextureVScale());
  AppendFloat(candidate, unit->getTextureRotate().valueRadians());
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      AppendFloat(candidate, static_cast<float>(transform[row][column]));
    }
  }
  AppendU32(candidate, static_cast<std::uint32_t>(address.u));
  AppendU32(candidate, static_cast<std::uint32_t>(address.v));
  AppendU32(candidate, static_cast<std::uint32_t>(address.w));
  AppendU32(candidate,
            static_cast<std::uint32_t>(sampler->getFiltering(Ogre::FT_MIN)));
  AppendU32(candidate,
            static_cast<std::uint32_t>(sampler->getFiltering(Ogre::FT_MAG)));
  AppendU32(candidate,
            static_cast<std::uint32_t>(sampler->getFiltering(Ogre::FT_MIP)));
  AppendU32(candidate, sampler->getAnisotropy());
  AppendFloat(candidate, sampler->getMipmapBias());
  AppendU32(candidate,
            static_cast<std::uint32_t>(sampler->getCompareFunction()));
  const Ogre::ColourValue border = sampler->getBorderColour();
  AppendFloat(candidate, static_cast<float>(border.r));
  AppendFloat(candidate, static_cast<float>(border.g));
  AppendFloat(candidate, static_cast<float>(border.b));
  AppendFloat(candidate, static_cast<float>(border.a));
  for (const NativeMip &mip : mips) {
    AppendU32(candidate, mip.level);
    AppendU32(candidate, mip.width);
    AppendU32(candidate, mip.height);
    AppendPointer(candidate, mip.buffer.get());
  }
  candidate_observation.exact_native_state = std::move(candidate);
  observation = std::move(candidate_observation);
  return Render::ValidationResult::Success();
}

Render::ValidationResult CaptureNativePage(
    Ogre::TerrainGroup &group, std::int32_t slot_x, std::int32_t slot_y,
    NativePageReadback &readback) {
  const std::uint32_t packed = group.packIndex(slot_x, slot_y);
  const auto found = group.getTerrainSlots().find(packed);
  Ogre::Terrain *const terrain =
      found != group.getTerrainSlots().end() && found->second != nullptr
          ? found->second->instance
          : nullptr;
  if (terrain == nullptr || !terrain->isLoaded()) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.terrain.native_page",
                   "requested terrain page is absent or unloaded");
  }

  // Capture runs on OGRE's render/main thread. The bridge does not execute the
  // legacy render traversal, so synchronously join and pump WorkQueue response
  // tasks before rejecting mutable derived state. Repeating this boundary is
  // intentional: hourly SkyX terrain-light updates can occur after startup.
  terrain->waitForDerivedProcesses();
  if (!terrain->isLoaded() || terrain->isDerivedDataUpdateInProgress()) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.native_page",
                   "requested terrain page remained mutable after the demo join");
  }

  // This render-thread call flushes the exact runtime composite. Even when no
  // dirty rectangle remains, every capture below performs a fresh readback;
  // Texture::stateCount is observation evidence, never a payload cache key.
  terrain->updateCompositeMap();
  const Ogre::TexturePtr texture = terrain->getCompositeMap();
  if (!texture) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.terrain.texture",
                   "terrain composite flush produced no texture");
  }
  std::vector<NativeMip> mips;
  Render::ValidationResult validation =
      AcquireNativeMipInventory(*texture, mips);
  if (!validation) {
    return validation;
  }
  OgreNextDemoSamplingObservation before_state;
  validation = CaptureExactNativeState(group, slot_x, slot_y, *texture, mips,
                                       before_state);
  if (!validation) {
    return validation;
  }

  Render::TextureResourceDescriptor output_texture;
  output_texture.debug_name = PageDebugName(slot_x, slot_y, "Composite");
  output_texture.type = Render::TextureResourceType::TEXTURE_2D;
  output_texture.format = Render::TextureResourceFormat::RGBA8_UNORM;
  output_texture.color_space = Render::TextureColorSpace::SRGB;
  output_texture.width = mips.front().width;
  output_texture.height = mips.front().height;
  output_texture.array_layers = 1U;
  output_texture.mip_levels.reserve(
      CompleteMipCount(output_texture.width, output_texture.height));
  const NativeMip &native_base = mips.front();
  const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(native_base.width) * 4U;
  const std::uint64_t slice_bytes = row_bytes * native_base.height;
  if (slice_bytes >
      static_cast<std::uint64_t>(
          (std::numeric_limits<std::size_t>::max)())) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.readback",
                   "composite base allocation exceeds host address space",
                   0U);
  }
  Render::TextureMipLevelDescriptor mip;
  mip.width = native_base.width;
  mip.height = native_base.height;
  mip.row_pitch_bytes = row_bytes;
  mip.layer_pitch_bytes = slice_bytes;
  mip.bytes.resize(static_cast<std::size_t>(slice_bytes));
  Ogre::PixelBox destination(mip.width, mip.height, 1U,
                             Ogre::PF_BYTE_RGBA, mip.bytes.data());
  if (destination.rowPitch != mip.width ||
      destination.slicePitch !=
          static_cast<std::size_t>(mip.width) * mip.height) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.pixel_box",
                   "OGRE did not retain a tight PF_BYTE_RGBA base destination",
                   0U);
  }
  // Pinned OGRE 14 Metal's HardwarePixelBuffer::blitToMemory ignores the
  // retained buffer's mLevel and aliases every nonzero request to mip 0. Read
  // exactly the base buffer on every backend, then complete all tail mips with
  // the cross-platform private CPU rule below.
  native_base.buffer->blitToMemory(destination);
  output_texture.mip_levels.push_back(std::move(mip));

  validation = CompleteOgreNextDemoOpaqueMipChain(output_texture);
  if (!validation) {
    return validation;
  }

  Ogre::Terrain *const after_terrain = group.getTerrain(slot_x, slot_y);
  const Ogre::TexturePtr after_texture =
      after_terrain != nullptr ? after_terrain->getCompositeMap()
                               : Ogre::TexturePtr{};
  if (after_terrain != terrain || !after_texture ||
      after_texture.get() != texture.get()) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.readback.native_identity",
                   "terrain or composite owner changed during readback");
  }
  std::vector<NativeMip> after_mips;
  validation = AcquireNativeMipInventory(*after_texture, after_mips);
  if (!validation) {
    return validation;
  }
  OgreNextDemoSamplingObservation after_state;
  validation = CaptureExactNativeState(group, slot_x, slot_y, *after_texture,
                                       after_mips, after_state);
  if (!validation) {
    return validation;
  }
  validation = RevalidateOgreNextDemoSampling(before_state, after_state);
  if (!validation) {
    return validation;
  }

  Render::SamplerResourceDescriptor output_sampler;
  output_sampler.debug_name = PageDebugName(slot_x, slot_y, "Sampler");
  output_sampler.minification_filter = Render::SamplerFilter::LINEAR;
  output_sampler.magnification_filter = Render::SamplerFilter::LINEAR;
  output_sampler.mip_filter = Render::SamplerFilter::NEAREST;
  output_sampler.address_u = Render::SamplerAddressMode::CLAMP_TO_EDGE;
  output_sampler.address_v = Render::SamplerAddressMode::CLAMP_TO_EDGE;
  output_sampler.address_w = Render::SamplerAddressMode::CLAMP_TO_EDGE;
  output_sampler.mip_lod_bias = 0.0F;
  output_sampler.minimum_lod = 0.0F;
  output_sampler.maximum_lod =
      static_cast<float>(output_texture.mip_levels.size() - 1U);
  output_sampler.anisotropy_enabled = false;
  output_sampler.maximum_anisotropy = 1.0F;
  output_sampler.compare_enabled = false;
  output_sampler.compare_operation = Render::SamplerCompareOperation::ALWAYS;
  output_sampler.border_color = {};

  validation = Render::ValidateTextureResourceDescriptor(output_texture);
  if (!validation) {
    validation.field = "ogre_next_demo.terrain.texture." + validation.field;
    return validation;
  }
  validation = Render::ValidateSamplerResourceDescriptor(output_sampler);
  if (!validation) {
    validation.field = "ogre_next_demo.terrain.sampler." + validation.field;
    return validation;
  }
  NativePageReadback candidate;
  candidate.texture = std::move(output_texture);
  candidate.sampler = std::move(output_sampler);
  readback = std::move(candidate);
  return Render::ValidationResult::Success();
}

} // namespace

struct Ogre14ToOgreNextTerrainSource::State final {
  struct PageOwner final {
    std::vector<Render::GraphicsSceneAssetInput> assets;
    Render::GraphicsSceneStaticMeshInput instance;
  };

  OgreNextDemoIdentityRegistry identities;
  std::set<std::string, std::less<>> known_pages;
  std::set<std::string, std::less<>> live_pages;
  std::map<std::string, PageOwner, std::less<>> page_owners;
};

Ogre14ToOgreNextTerrainSource::Ogre14ToOgreNextTerrainSource()
    : committed_(std::make_unique<State>()) {}

Ogre14ToOgreNextTerrainSource::~Ogre14ToOgreNextTerrainSource() = default;

Render::ValidationResult Ogre14ToOgreNextTerrainSource::Capture(
    Ogre::TerrainGroup *terrain_group,
    const std::vector<OgreNextDemoTerrainPageMesh> &pages,
    OgreNextDemoTerrainCapture &capture) try {
  if (pending_ != nullptr) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.terrain.pending",
                   "the preceding terrain capture must be committed or discarded");
  }
  if (committed_ == nullptr) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.terrain.committed",
                   "terrain source has no committed identity state");
  }
  if (terrain_group == nullptr && !pages.empty()) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.terrain.group",
                   "TerrainGroup and exact page inventory must coexist");
  }
  auto candidate_state = std::make_unique<State>(*committed_);
  candidate_state->live_pages.clear();
  candidate_state->page_owners.clear();
  OgreNextDemoTerrainCapture candidate_capture;
  if (pages.empty()) {
    pending_ = std::move(candidate_state);
    capture = std::move(candidate_capture);
    return Render::ValidationResult::Success();
  }

  std::vector<const OgreNextDemoTerrainPageMesh *> ordered_pages;
  ordered_pages.reserve(pages.size());
  for (const OgreNextDemoTerrainPageMesh &page : pages) {
    ordered_pages.push_back(&page);
  }
  std::sort(ordered_pages.begin(), ordered_pages.end(),
            [](const auto *first, const auto *second) {
              if (first->slot_y != second->slot_y) {
                return first->slot_y < second->slot_y;
              }
              return first->slot_x < second->slot_x;
            });
  std::set<std::pair<std::int32_t, std::int32_t>> coordinates;
  for (std::size_t index = 0U; index < ordered_pages.size(); ++index) {
    const OgreNextDemoTerrainPageMesh &page = *ordered_pages[index];
    if (page.exact_page_key.empty() ||
        !coordinates.emplace(page.slot_x, page.slot_y).second ||
        !candidate_state->live_pages.insert(page.exact_page_key).second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.terrain.page_identity",
                     "terrain page coordinates or exact identity are duplicated",
                     index);
    }
    if (candidate_state->known_pages.find(page.exact_page_key) !=
            candidate_state->known_pages.end() &&
        committed_->live_pages.find(page.exact_page_key) ==
            committed_->live_pages.end()) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.terrain.page_identity",
                     "a removed terrain page identity may never return",
                     index);
    }
    if (page.mesh_payload == nullptr ||
        page.mesh_payload->valueless_by_exception() ||
        Render::RenderAssetPayloadKind(*page.mesh_payload) !=
            Render::RenderAssetKind::MESH) {
      return Failure(Render::ValidationCode::WRONG_ASSET_KIND,
                     "ogre_next_demo.terrain.mesh_payload",
                     "terrain page requires one immutable generic mesh",
                     index);
    }
    const Render::MeshResourceDescriptor &mesh =
        std::get<Render::MeshResourceDescriptor>(*page.mesh_payload);
    Render::ValidationResult validation =
        Render::ValidateMeshResourceDescriptor(mesh);
    if (!validation) {
      validation.field = "ogre_next_demo.terrain.mesh." + validation.field;
      validation.element_index = index;
      return validation;
    }
    if (mesh.dynamic || mesh.texture_coordinates_0.size() !=
                            mesh.positions.size()) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.terrain.mesh.uv0",
                     "display-domain terrain requires a static mesh with complete authored UV0",
                     index);
    }

    NativePageReadback native;
    validation = CaptureNativePage(*terrain_group, page.slot_x, page.slot_y,
                                   native);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }

    std::uint64_t mesh_id = 0U;
    std::uint64_t texture_id = 0U;
    std::uint64_t sampler_id = 0U;
    std::uint64_t material_id = 0U;
    std::uint64_t object_id = 0U;
    const std::array<std::pair<std::string_view, std::uint64_t *>, 5U>
        identities{{
            {kMeshIdDomain, &mesh_id},
            {kTextureIdDomain, &texture_id},
            {kSamplerIdDomain, &sampler_id},
            {kMaterialIdDomain, &material_id},
            {kObjectIdDomain, &object_id},
        }};
    for (const auto &identity : identities) {
      validation =
          DeriveOgreNextDemoSourceId(identity.first, page.exact_page_key,
                                     *identity.second);
      if (!validation) {
        return validation;
      }
      std::string exact_identity(identity.first);
      exact_identity.push_back('\0');
      exact_identity.append(page.exact_page_key);
      validation = candidate_state->identities.Register(
          std::move(exact_identity), *identity.second);
      if (!validation) {
        validation.element_index = index;
        return validation;
      }
    }

    Render::MaterialDescriptor material;
    material.debug_name = PageDebugName(page.slot_x, page.slot_y, "Material");
    material.model = Render::MaterialModel::UNLIT;
    material.alpha_mode = Render::MaterialAlphaMode::OPAQUE;
    material.base_color_transfer =
        Render::BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE;
    material.double_sided = false;
    material.base_color_factor = {1.0F, 1.0F, 1.0F, 1.0F};
    material.metallic_factor = 0.0F;
    material.roughness_factor = 1.0F;
    material.base_color_texture.texture_coordinate_set = 0U;
    material.base_color_texture.scale = {1.0F, 1.0F};
    material.base_color_texture.offset = {};
    material.base_color_texture.rotation_radians = 0.0F;
    validation = Render::ValidateMaterialDescriptor(material);
    if (!validation) {
      validation.field = "ogre_next_demo.terrain.material." + validation.field;
      validation.element_index = index;
      return validation;
    }
    validation = Render::ValidateMaterialMeshCompatibility(material, mesh);
    if (!validation) {
      validation.field = "ogre_next_demo.terrain.material_mesh." +
                         validation.field;
      validation.element_index = index;
      return validation;
    }
    validation = Render::ValidateMaterialTextureCompatibility(
        Render::MaterialTextureSlot::BASE_COLOR, native.texture,
        native.sampler);
    if (!validation) {
      validation.field = "ogre_next_demo.terrain.material_texture." +
                         validation.field;
      validation.element_index = index;
      return validation;
    }

    State::PageOwner owner;
    owner.assets.reserve(4U);
    Render::GraphicsSceneAssetInput mesh_asset;
    mesh_asset.source_asset_id = mesh_id;
    mesh_asset.payload = page.mesh_payload;
    owner.assets.push_back(std::move(mesh_asset));
    Render::GraphicsSceneAssetInput texture_asset;
    texture_asset.source_asset_id = texture_id;
    texture_asset.payload = std::make_shared<const Render::RenderAssetPayload>(
        std::move(native.texture));
    owner.assets.push_back(std::move(texture_asset));
    Render::GraphicsSceneAssetInput sampler_asset;
    sampler_asset.source_asset_id = sampler_id;
    sampler_asset.payload = std::make_shared<const Render::RenderAssetPayload>(
        std::move(native.sampler));
    owner.assets.push_back(std::move(sampler_asset));
    Render::GraphicsSceneAssetInput material_asset;
    material_asset.source_asset_id = material_id;
    material_asset.payload = std::make_shared<const Render::RenderAssetPayload>(
        std::move(material));
    material_asset.material_bindings[static_cast<std::size_t>(
        Render::MaterialTextureSlot::BASE_COLOR)] = {texture_id, sampler_id};
    owner.assets.push_back(std::move(material_asset));

    owner.instance.source_object_id = object_id;
    owner.instance.mesh_source_asset_id = mesh_id;
    owner.instance.material_source_asset_id = material_id;
    owner.instance.render_from_object.elements[12U] = page.page_world_position.x;
    owner.instance.render_from_object.elements[13U] = page.page_world_position.y;
    owner.instance.render_from_object.elements[14U] = page.page_world_position.z;
    owner.instance.visibility_mask = page.visible ? page.visibility_mask : 0U;
    // The exact display-domain Unlit bootstrap neither casts nor receives
    // shadows. Native terrain/shadow integration replaces this demo source.
    owner.instance.flags = Render::MESH_INSTANCE_VISIBLE_IN_REFLECTIONS;

    for (const Render::GraphicsSceneAssetInput &asset : owner.assets) {
      candidate_capture.assets.push_back(asset);
    }
    candidate_capture.static_meshes.push_back(owner.instance);
    candidate_state->page_owners.emplace(page.exact_page_key,
                                         std::move(owner));
  }

  candidate_state->known_pages.insert(candidate_state->live_pages.begin(),
                                      candidate_state->live_pages.end());
  std::sort(candidate_capture.assets.begin(), candidate_capture.assets.end(),
            [](const auto &first, const auto &second) {
              return first.source_asset_id < second.source_asset_id;
            });
  std::sort(candidate_capture.static_meshes.begin(),
            candidate_capture.static_meshes.end(),
            [](const auto &first, const auto &second) {
              return first.source_object_id < second.source_object_id;
            });
  for (std::size_t index = 1U; index < candidate_capture.assets.size();
       ++index) {
    if (candidate_capture.assets[index - 1U].source_asset_id ==
        candidate_capture.assets[index].source_asset_id) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.terrain.assets",
                     "terrain asset source IDs collide inside the candidate");
    }
  }
  for (std::size_t index = 1U; index < candidate_capture.static_meshes.size();
       ++index) {
    if (candidate_capture.static_meshes[index - 1U].source_object_id ==
        candidate_capture.static_meshes[index].source_object_id) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.terrain.static_meshes",
                     "terrain object source IDs collide inside the candidate");
    }
  }

  pending_ = std::move(candidate_state);
  capture = std::move(candidate_capture);
  return Render::ValidationResult::Success();
} catch (const Ogre::Exception &) {
  return Failure(Render::ValidationCode::MISSING_REFERENCE,
                 "ogre_next_demo.terrain.ogre_exception",
                 "OGRE failed before the private terrain capture was published");
} catch (const std::bad_alloc &) {
  return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                 "ogre_next_demo.terrain.allocation",
                 "allocation failed before the private terrain capture was published");
} catch (...) {
  return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                 "ogre_next_demo.terrain.exception",
                 "unexpected exception before the private terrain capture was published");
}

void Ogre14ToOgreNextTerrainSource::Commit() noexcept {
  if (pending_ != nullptr) {
    committed_.swap(pending_);
    pending_.reset();
  }
}

void Ogre14ToOgreNextTerrainSource::Discard() noexcept { pending_.reset(); }

void Ogre14ToOgreNextTerrainSource::Reset() noexcept {
  pending_.reset();
  try {
    committed_ = std::make_unique<State>();
  } catch (...) {
    // Reset is called during teardown. A null committed state fails closed if
    // capture is attempted after allocation failure.
    committed_.reset();
  }
}

} // namespace RoR::Gfx::Detail
