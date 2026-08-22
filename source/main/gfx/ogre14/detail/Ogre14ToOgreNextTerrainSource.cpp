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
#include <OgreCodec.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreImage.h>
#include <OgreMaterial.h>
#include <OgrePass.h>
#include <OgrePixelFormat.h>
#include <OgreResource.h>
#include <OgreResourceGroupManager.h>
#include <OgreSceneManager.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>
#include <OgreLogManager.h>
#include <OgreStringConverter.h>
#include <Terrain/OgreTerrain.h>
#include <Terrain/OgreTerrainGroup.h>
#include <Terrain/OgreTerrainLayerBlendMap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <string>
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
/// Ceiling for the published layer weight mask. See BuildLayerWeightMask.
constexpr std::size_t kMaximumWeightMaskDimension = 2048U;
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
constexpr char kWeightTextureIdDomain[] =
    "RoR/OgreNextDemo/Terrain/WeightTextureSourceAsset/v1";
constexpr char kWeightSamplerIdDomain[] =
    "RoR/OgreNextDemo/Terrain/WeightSamplerSourceAsset/v1";
constexpr char kDetailTextureIdDomain[] =
    "RoR/OgreNextDemo/Terrain/DetailTextureSourceAsset/v1";
constexpr char kDetailSamplerIdDomain[] =
    "RoR/OgreNextDemo/Terrain/DetailSamplerSourceAsset/v1";

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

/// One authored terrain layer lifted to a portable descriptor, together with
/// the UV repeat rate that reproduces its authored world size.
struct AuthoredLayer final {
  Render::TextureResourceDescriptor texture;
  Render::SamplerResourceDescriptor sampler;
  float uv_repeats = 1.0F;
};

struct NativePageReadback final {
  Ogre::Terrain *terrain = nullptr;
  Render::TextureResourceDescriptor texture;
  Render::SamplerResourceDescriptor sampler;

  /// Set when the page publishes authored-density layers instead of the baked
  /// composite. `texture`/`sampler` then carry layer 0 rather than the
  /// composite map, and `base_uv_repeats` tiles it at its authored rate.
  bool authored_density = false;
  float base_uv_repeats = 1.0F;
  Render::TextureResourceDescriptor weight_texture;
  Render::SamplerResourceDescriptor weight_sampler;
  std::vector<AuthoredLayer> detail_layers;
};

/// Completes an opaque RGBA8 mip chain for either color space.
///
/// The shared opaque lowering accepts sRGB only, because it exists for the
/// display-domain composite. The layer weight mask is a linear selection
/// texture and must keep that color space, so this mirrors the same box
/// filter and the same deterministic half-up rounding without the sRGB
/// precondition.
Render::ValidationResult CompleteAuthoredMipChain(
    Render::TextureResourceDescriptor &texture) {
  if (texture.type != Render::TextureResourceType::TEXTURE_2D ||
      texture.format != Render::TextureResourceFormat::RGBA8_UNORM ||
      texture.array_layers != 1U || texture.width == 0U ||
      texture.height == 0U || texture.mip_levels.size() != 1U) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.layer.full_mip_chain",
                   "authored lowering requires exactly one fresh RGBA8 2D base level");
  }
  const Render::TextureMipLevelDescriptor &base = texture.mip_levels.front();
  const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(texture.width) * 4U;
  std::uint64_t layer_bytes = 0U;
  if (!CheckedMultiply(row_bytes, texture.height, layer_bytes) ||
      layer_bytes > static_cast<std::uint64_t>(
                        (std::numeric_limits<std::size_t>::max)()) ||
      base.width != texture.width || base.height != texture.height ||
      base.row_pitch_bytes != row_bytes ||
      base.layer_pitch_bytes != layer_bytes ||
      base.bytes.size() != static_cast<std::size_t>(layer_bytes)) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.layer.mip_layout",
                   "authored lowering requires an exact tight RGBA8 base layout",
                   0U);
  }
  for (std::size_t alpha = 3U; alpha < texture.mip_levels.front().bytes.size();
       alpha += 4U) {
    texture.mip_levels.front().bytes[alpha] = 255U;
  }

  while (texture.mip_levels.size() <
         CompleteMipCount(texture.width, texture.height)) {
    const Render::TextureMipLevelDescriptor &source = texture.mip_levels.back();
    Render::TextureMipLevelDescriptor destination;
    destination.width = (std::max)(1U, source.width / 2U);
    destination.height = (std::max)(1U, source.height / 2U);
    destination.row_pitch_bytes =
        static_cast<std::uint64_t>(destination.width) * 4U;
    destination.layer_pitch_bytes =
        destination.row_pitch_bytes * destination.height;
    if (destination.layer_pitch_bytes >
        static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.terrain.layer.generated_mip",
                     "generated mip allocation exceeds host address space");
    }
    destination.bytes.resize(
        static_cast<std::size_t>(destination.layer_pitch_bytes));
    for (std::uint32_t y = 0U; y < destination.height; ++y) {
      const std::uint32_t source_y0 = y * 2U;
      const std::uint32_t source_y1 =
          (std::min)(source_y0 + 1U, source.height - 1U);
      for (std::uint32_t x = 0U; x < destination.width; ++x) {
        const std::uint32_t source_x0 = x * 2U;
        const std::uint32_t source_x1 =
            (std::min)(source_x0 + 1U, source.width - 1U);
        const std::size_t offsets[4U] = {
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
        };
        const std::size_t output =
            static_cast<std::size_t>(y) * destination.row_pitch_bytes +
            static_cast<std::size_t>(x) * 4U;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const std::uint32_t sum =
              static_cast<std::uint32_t>(source.bytes[offsets[0U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[1U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[2U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[3U] + channel]);
          destination.bytes[output + channel] =
              static_cast<std::uint8_t>((sum + 2U) / 4U);
        }
        destination.bytes[output + 3U] = 255U;
      }
    }
    texture.mip_levels.push_back(std::move(destination));
  }
  return Render::ValidationResult::Success();
}

/// Wrapping sampler for a layer that repeats many times across one page.
Render::SamplerResourceDescriptor MakeRepeatSampler(std::string debug_name,
                                                    std::size_t mip_levels) {
  Render::SamplerResourceDescriptor sampler;
  sampler.debug_name = std::move(debug_name);
  sampler.minification_filter = Render::SamplerFilter::LINEAR;
  sampler.magnification_filter = Render::SamplerFilter::LINEAR;
  // A layer repeating hundreds of times per page is minified hard in the
  // distance; nearest mip selection would alias into visible banding.
  sampler.mip_filter = Render::SamplerFilter::LINEAR;
  sampler.address_u = Render::SamplerAddressMode::REPEAT;
  sampler.address_v = Render::SamplerAddressMode::REPEAT;
  sampler.address_w = Render::SamplerAddressMode::REPEAT;
  sampler.mip_lod_bias = 0.0F;
  sampler.minimum_lod = 0.0F;
  sampler.maximum_lod = static_cast<float>(mip_levels - 1U);
  sampler.anisotropy_enabled = false;
  sampler.maximum_anisotropy = 1.0F;
  sampler.compare_enabled = false;
  sampler.compare_operation = Render::SamplerCompareOperation::ALWAYS;
  sampler.border_color = {};
  return sampler;
}

/// Decodes one authored layer image into an opaque sRGB RGBA8 descriptor.
///
/// The authored layers are ordinary content files, including DXT-compressed
/// DDS. OGRE's DDS codec keeps blocks compressed whenever the render system
/// advertises DXT support, so the codec's own software decode is enforced for
/// the duration of the load rather than reimplementing block decoding here.
Render::ValidationResult LoadAuthoredLayerImage(
    const Ogre::String &texture_name, std::string debug_name,
    Render::TextureResourceDescriptor &output) {
  Ogre::Image image;
  Ogre::Codec *const dds_codec = Ogre::Codec::getCodec("dds");
  const bool enforced =
      dds_codec != nullptr &&
      dds_codec->setParameter("decode_enforce", "true");
  try {
    image.load(texture_name,
               Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
  } catch (const Ogre::Exception &) {
    if (enforced) {
      dds_codec->setParameter("decode_enforce", "false");
    }
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.terrain.layer.image",
                   "an authored terrain layer texture could not be decoded");
  }
  if (enforced) {
    dds_codec->setParameter("decode_enforce", "false");
  }

  const std::size_t width = image.getWidth();
  const std::size_t height = image.getHeight();
  if (width == 0U || height == 0U ||
      width > kMaximumCompositeDimension ||
      height > kMaximumCompositeDimension) {
    return Failure(Render::ValidationCode::INVALID_DIMENSIONS,
                   "ogre_next_demo.terrain.layer.dimensions",
                   "authored terrain layer dimensions are empty or exceed the cap");
  }
  if (Ogre::PixelUtil::isCompressed(image.getFormat())) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.layer.format",
                   "authored terrain layer stayed block compressed after decode");
  }

  output = Render::TextureResourceDescriptor{};
  output.debug_name = std::move(debug_name);
  output.type = Render::TextureResourceType::TEXTURE_2D;
  output.format = Render::TextureResourceFormat::RGBA8_UNORM;
  output.color_space = Render::TextureColorSpace::SRGB;
  output.width = static_cast<std::uint32_t>(width);
  output.height = static_cast<std::uint32_t>(height);
  output.array_layers = 1U;

  Render::TextureMipLevelDescriptor mip;
  mip.width = output.width;
  mip.height = output.height;
  mip.row_pitch_bytes = static_cast<std::uint64_t>(output.width) * 4U;
  mip.layer_pitch_bytes = mip.row_pitch_bytes * output.height;
  mip.bytes.resize(static_cast<std::size_t>(mip.layer_pitch_bytes));
  Ogre::PixelBox destination(output.width, output.height, 1U,
                             Ogre::PF_BYTE_RGBA, mip.bytes.data());
  Ogre::PixelUtil::bulkPixelConversion(image.getPixelBox(), destination);
  // The opaque PBS admission gate requires alpha 255 in every texel of every
  // mip; an authored layer's alpha is a legacy specular or coverage channel
  // that this path never consumes.
  const std::size_t texel_count =
      static_cast<std::size_t>(output.width) * output.height;
  for (std::size_t texel = 0U; texel < texel_count; ++texel) {
    mip.bytes[texel * 4U + 3U] = 255U;
  }
  output.mip_levels.reserve(CompleteMipCount(output.width, output.height));
  output.mip_levels.push_back(std::move(mip));
  return CompleteAuthoredMipChain(output);
}

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

/// Packs the terrain's per-layer blend maps into one linear RGBA selection
/// mask: R selects layer 1, G layer 2, B layer 3.
///
/// OGRE's TerrainLayerBlendMap uploads row r of its float array to row r of
/// its blend texture, and this descriptor's first byte row is likewise V=0, so
/// rows copy across without a vertical flip. The terrain's own material
/// samples these weights with the same unscaled page UV that the published
/// mesh carries in UV0.
Render::ValidationResult BuildLayerWeightMask(
    Ogre::Terrain &terrain, std::uint8_t detail_layer_count,
    std::string debug_name, Render::TextureResourceDescriptor &output) {
  if (detail_layer_count == 0U || detail_layer_count > 3U) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.terrain.weight.layers",
                   "the packed weight mask carries one to three detail layers");
  }
  const std::size_t source_size = terrain.getLayerBlendMapSize();
  if (source_size == 0U || source_size > kMaximumCompositeDimension) {
    return Failure(Render::ValidationCode::INVALID_DIMENSIONS,
                   "ogre_next_demo.terrain.weight.dimensions",
                   "terrain blend map size is empty or exceeds the cap");
  }
  // Publish the mask no finer than kMaximumWeightMaskDimension. It is a
  // low-frequency selection weight that the shader filters bilinearly anyway,
  // and at the authored size it dominates the page's VRAM: CityWorld's 4096
  // mask is 85 MiB against 3 MiB for every layer texture combined.
  //
  // The cap is measured, not assumed. On CityWorld's derived coverage the
  // narrowest tenth of features run 8.8 m (concrete) and 17.6 m (asphalt);
  // halving to 2048 holds ~1.5 texels across the narrowest of those for a mean
  // absolute error of 0.4/255, while quartering to 1024 costs 1.0/255 and
  // drops below one texel on them. Whole blocks are box-averaged so the cap
  // only ever integrates authored weights.
  std::size_t size = source_size;
  std::size_t decimation = 1U;
  while (size > kMaximumWeightMaskDimension && (size % 2U) == 0U) {
    size /= 2U;
    decimation *= 2U;
  }

  output = Render::TextureResourceDescriptor{};
  output.debug_name = std::move(debug_name);
  output.type = Render::TextureResourceType::TEXTURE_2D;
  output.format = Render::TextureResourceFormat::RGBA8_UNORM;
  // The mask selects layers and is never displayed, so it must not carry a
  // transfer function that sampling would decode.
  output.color_space = Render::TextureColorSpace::LINEAR;
  output.width = static_cast<std::uint32_t>(size);
  output.height = static_cast<std::uint32_t>(size);
  output.array_layers = 1U;

  Render::TextureMipLevelDescriptor mip;
  mip.width = output.width;
  mip.height = output.height;
  mip.row_pitch_bytes = static_cast<std::uint64_t>(output.width) * 4U;
  mip.layer_pitch_bytes = mip.row_pitch_bytes * output.height;
  mip.bytes.assign(static_cast<std::size_t>(mip.layer_pitch_bytes), 0U);
  const std::size_t texel_count =
      static_cast<std::size_t>(output.width) * output.height;
  for (std::uint8_t detail = 0U; detail < detail_layer_count; ++detail) {
    // Terrain layer N+1 is detail channel N.
    Ogre::TerrainLayerBlendMap *const blend_map =
        terrain.getLayerBlendMap(static_cast<std::uint8_t>(detail + 1U));
    if (blend_map == nullptr) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.terrain.weight.blend_map",
                     "a terrain layer is missing its blend map", detail);
    }
    const float *const weights = blend_map->getBlendPointer();
    if (weights == nullptr) {
      return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                     "ogre_next_demo.terrain.weight.blend_map",
                     "a terrain blend map exposed no CPU weights", detail);
    }
    for (std::size_t y = 0U; y < output.height; ++y) {
      for (std::size_t x = 0U; x < output.width; ++x) {
        double sum = 0.0;
        for (std::size_t block_y = 0U; block_y < decimation; ++block_y) {
          const std::size_t source_row =
              ((y * decimation) + block_y) * source_size;
          for (std::size_t block_x = 0U; block_x < decimation; ++block_x) {
            const float weight =
                weights[source_row + (x * decimation) + block_x];
            if (!std::isfinite(weight)) {
              return Failure(Render::ValidationCode::NON_FINITE_VALUE,
                             "ogre_next_demo.terrain.weight.blend_map",
                             "a terrain blend weight is not finite", detail);
            }
            sum += std::min(std::max(weight, 0.0F), 1.0F);
          }
        }
        mip.bytes[((y * output.width) + x) * 4U + detail] =
            static_cast<std::uint8_t>(std::lround(
                (sum / static_cast<double>(decimation * decimation)) * 255.0));
      }
    }
  }
  for (std::size_t texel = 0U; texel < texel_count; ++texel) {
    mip.bytes[texel * 4U + 3U] = 255U;
  }
  output.mip_levels.reserve(CompleteMipCount(output.width, output.height));
  output.mip_levels.push_back(std::move(mip));
  return CompleteAuthoredMipChain(output);
}

/// Lifts every authored layer of one page to authored-density descriptors.
///
/// Fails softly: the caller keeps the baked composite whenever this cannot
/// produce a complete, self-consistent layer set, so a page never disappears
/// because its authored content could not be lifted.
Render::ValidationResult BuildAuthoredDensityLayers(
    Ogre::Terrain &terrain, std::int32_t slot_x, std::int32_t slot_y,
    NativePageReadback &readback) {
  const std::uint8_t layer_count = terrain.getLayerCount();
  if (layer_count == 0U) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.terrain.layer.count",
                   "the terrain page declares no layers");
  }
  const float page_world_size = static_cast<float>(terrain.getWorldSize());
  if (!std::isfinite(page_world_size) || page_world_size <= 0.0F) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.terrain.layer.page_world_size",
                   "the terrain page world size is not positive and finite");
  }
  // Ogre-Next carries four detail slots; layer 0 is the base color, so at most
  // four more layers can be composited over it. Three of them fit the packed
  // RGB weight mask this path builds.
  const std::uint8_t detail_layer_count =
      static_cast<std::uint8_t>(std::min<int>(layer_count - 1, 3));

  const auto layer_repeats = [&](std::uint8_t layer,
                                 float &repeats) -> Render::ValidationResult {
    const float layer_world_size =
        static_cast<float>(terrain.getLayerWorldSize(layer));
    if (!std::isfinite(layer_world_size) || layer_world_size <= 0.0F) {
      return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                     "ogre_next_demo.terrain.layer.world_size",
                     "an authored layer world size is not positive and finite",
                     layer);
    }
    repeats = page_world_size / layer_world_size;
    if (!std::isfinite(repeats) || repeats <= 0.0F) {
      return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                     "ogre_next_demo.terrain.layer.repeats",
                     "an authored layer repeat rate is not positive and finite",
                     layer);
    }
    return Render::ValidationResult::Success();
  };

  Render::TextureResourceDescriptor base_texture;
  Render::ValidationResult validation = LoadAuthoredLayerImage(
      terrain.getLayerTextureName(0U, 0U),
      PageDebugName(slot_x, slot_y, "Layer0"), base_texture);
  if (!validation) {
    return validation;
  }
  float base_repeats = 1.0F;
  validation = layer_repeats(0U, base_repeats);
  if (!validation) {
    return validation;
  }

  std::vector<AuthoredLayer> detail_layers;
  detail_layers.reserve(detail_layer_count);
  for (std::uint8_t detail = 0U; detail < detail_layer_count; ++detail) {
    const auto layer = static_cast<std::uint8_t>(detail + 1U);
    AuthoredLayer authored;
    validation = LoadAuthoredLayerImage(
        terrain.getLayerTextureName(layer, 0U),
        PageDebugName(slot_x, slot_y,
                      ("Layer" + std::to_string(layer)).c_str()),
        authored.texture);
    if (!validation) {
      return validation;
    }
    validation = layer_repeats(layer, authored.uv_repeats);
    if (!validation) {
      return validation;
    }
    authored.sampler = MakeRepeatSampler(
        PageDebugName(slot_x, slot_y,
                      ("Layer" + std::to_string(layer) + "Sampler").c_str()),
        authored.texture.mip_levels.size());
    detail_layers.push_back(std::move(authored));
  }

  // A single-layer page needs no selection: it is the SIMPLEST authored-
  // density case, just the base layer tiled at its authored world size. The
  // base CityWorld map is exactly this (one grass layer, 8 m world size), and
  // refusing it here used to throw the page all the way back to the blurry
  // page-wide composite -- the very defect this path exists to fix.
  Render::TextureResourceDescriptor weight_texture;
  Render::SamplerResourceDescriptor weight_sampler;
  if (detail_layer_count > 0U) {
    validation = BuildLayerWeightMask(
        terrain, detail_layer_count,
        PageDebugName(slot_x, slot_y, "LayerWeight"), weight_texture);
    if (!validation) {
      return validation;
    }
    weight_sampler = MakeRepeatSampler(
        PageDebugName(slot_x, slot_y, "LayerWeightSampler"),
        weight_texture.mip_levels.size());
    // The mask spans the page exactly once, so it clamps rather than repeats.
    weight_sampler.address_u = Render::SamplerAddressMode::CLAMP_TO_EDGE;
    weight_sampler.address_v = Render::SamplerAddressMode::CLAMP_TO_EDGE;
    weight_sampler.address_w = Render::SamplerAddressMode::CLAMP_TO_EDGE;
  }

  readback.texture = std::move(base_texture);
  readback.sampler = MakeRepeatSampler(
      PageDebugName(slot_x, slot_y, "Layer0Sampler"),
      readback.texture.mip_levels.size());
  readback.base_uv_repeats = base_repeats;
  readback.weight_texture = std::move(weight_texture);
  readback.weight_sampler = std::move(weight_sampler);
  readback.detail_layers = std::move(detail_layers);
  readback.authored_density = true;
  return Render::ValidationResult::Success();
}

Render::ValidationResult JoinNativePage(
    Ogre::TerrainGroup &group, std::int32_t slot_x, std::int32_t slot_y,
    Ogre::Terrain *&joined_terrain) {
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

  // Initial capture runs on OGRE's render/main thread. The bridge does not
  // execute the legacy render traversal, so synchronously join and pump
  // WorkQueue response tasks before freezing the map-generation publication.
  terrain->waitForDerivedProcesses();
  if (!terrain->isLoaded() || terrain->isDerivedDataUpdateInProgress()) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.native_page",
                   "requested terrain page remained mutable after the demo join");
  }

  joined_terrain = terrain;
  return Render::ValidationResult::Success();
}

Render::ValidationResult CaptureNativePage(
    Ogre::TerrainGroup &group, std::int32_t slot_x, std::int32_t slot_y,
    NativePageReadback &readback) {
  Ogre::Terrain *terrain = nullptr;
  Render::ValidationResult validation =
      JoinNativePage(group, slot_x, slot_y, terrain);
  if (!validation) {
    return validation;
  }

  // The first render-thread capture flushes and reads the exact runtime
  // composite. The disposable product freezes this result until the next map
  // generation; Texture::stateCount is not treated as an exact revision.
  terrain->updateCompositeMap();
  const Ogre::TexturePtr texture = terrain->getCompositeMap();
  if (!texture) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.terrain.texture",
                   "terrain composite flush produced no texture");
  }
  std::vector<NativeMip> mips;
  validation = AcquireNativeMipInventory(*texture, mips);
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
  {
    // The composite bake renders lit LINEAR values: GL3Plus encodes on
    // write only for sRGB storage formats, and the sampling-identity policy
    // above requires the native composite to stay non-sRGB. The published
    // display-domain contract (SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE)
    // carries display-encoded bytes, so encode exactly once here; without
    // this the presenter re-decodes linear bytes and the whole terrain
    // lands near black (observed base readback mean 35/255).
    static const std::array<std::uint8_t, 256U> linear_to_srgb = [] {
      std::array<std::uint8_t, 256U> table{};
      for (std::size_t value = 0U; value < table.size(); ++value) {
        const double linear = static_cast<double>(value) / 255.0;
        const double srgb = linear <= 0.0031308
            ? linear * 12.92
            : (1.055 * std::pow(linear, 1.0 / 2.4)) - 0.055;
        table[value] = static_cast<std::uint8_t>(
            std::lround(std::min(std::max(srgb, 0.0), 1.0) * 255.0));
      }
      return table;
    }();
    const std::size_t encode_texel_count =
        static_cast<std::size_t>(mip.width) * mip.height;
    for (std::size_t texel = 0U; texel < encode_texel_count; ++texel) {
      const std::size_t texel_base = texel * 4U;
      mip.bytes[texel_base] = linear_to_srgb[mip.bytes[texel_base]];
      mip.bytes[texel_base + 1U] =
          linear_to_srgb[mip.bytes[texel_base + 1U]];
      mip.bytes[texel_base + 2U] =
          linear_to_srgb[mip.bytes[texel_base + 2U]];
      // The RTT alpha channel is not meaningful; the opaque PBS admission
      // gate requires alpha 255 in every texel of every mip.
      mip.bytes[texel_base + 3U] = 255U;
    }
  }
  {
    // One-shot content probe. The composite ships content-unchecked, so a
    // bake that froze black texels into the blend-selected regions would
    // otherwise be invisible to every validation and every log line.
    std::uint8_t rgb_min = 255U;
    std::uint8_t rgb_max = 0U;
    std::uint64_t rgb_sum = 0U;
    const std::size_t texel_count =
        static_cast<std::size_t>(mip.width) * mip.height;
    for (std::size_t texel = 0U; texel < texel_count; ++texel) {
      const std::size_t texel_base = texel * 4U;
      for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const std::uint8_t value = mip.bytes[texel_base + channel];
        rgb_min = std::min(rgb_min, value);
        rgb_max = std::max(rgb_max, value);
        rgb_sum += value;
      }
    }
    std::string layer_textures;
    std::string layer_world_sizes;
    for (std::uint8_t layer = 0U; layer < terrain->getLayerCount();
         ++layer) {
      if (!layer_textures.empty())
        layer_textures += "|";
      layer_textures += terrain->getLayerTextureName(layer, 0U);
      if (!layer_world_sizes.empty())
        layer_world_sizes += "|";
      layer_world_sizes += Ogre::StringConverter::toString(
          static_cast<float>(terrain->getLayerWorldSize(layer)));
    }
    // Texture density is the whole reason this composite reads as a flat
    // wash: one baked page-wide map covers getWorldSize() metres, while the
    // authored layers repeat every getLayerWorldSize() metres. Report the
    // composite's metres-per-texel so the regression is measurable from the
    // log rather than argued from screenshots.
    const float page_world_size = static_cast<float>(terrain->getWorldSize());
    const float composite_metres_per_texel =
        mip.width == 0U ? 0.0F
                        : page_world_size / static_cast<float>(mip.width);
    Ogre::LogManager::getSingleton().logMessage(
        "[RoR|SceneSource|TerrainComposite] page=" +
        Ogre::StringConverter::toString(slot_x) + "," +
        Ogre::StringConverter::toString(slot_y) + " size=" +
        Ogre::StringConverter::toString(mip.width) + "x" +
        Ogre::StringConverter::toString(mip.height) + " rgb_min=" +
        Ogre::StringConverter::toString(
            static_cast<unsigned int>(rgb_min)) +
        " rgb_max=" +
        Ogre::StringConverter::toString(
            static_cast<unsigned int>(rgb_max)) +
        " rgb_mean=" +
        Ogre::StringConverter::toString(
            texel_count == 0U
                ? 0U
                : static_cast<unsigned int>(
                      rgb_sum / (static_cast<std::uint64_t>(texel_count) *
                                 3U))) +
        " layers=" +
        Ogre::StringConverter::toString(
            static_cast<unsigned int>(terrain->getLayerCount())) +
        " page_world_size=" +
        Ogre::StringConverter::toString(page_world_size) +
        " composite_m_per_texel=" +
        Ogre::StringConverter::toString(composite_metres_per_texel) +
        " layer_world_sizes=" + layer_world_sizes +
        " layer_textures=" + layer_textures);
  }
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
  candidate.terrain = terrain;
  candidate.texture = std::move(output_texture);
  candidate.sampler = std::move(output_sampler);

  // Prefer the authored layers over the baked composite.
  //
  // The composite is OGRE's distant-LOD approximation: one page-wide bake that
  // the legacy renderer only ever showed beyond its composite map distance,
  // falling back to per-layer splatting up close. Publishing it as the only
  // terrain texture stretches a single map over the whole page, which is why
  // the ground reads as a featureless wash regardless of its geometry.
  //
  // This fails soft on purpose. A page that cannot lift its authored content
  // keeps the composite and stays visible; it must never vanish because a
  // layer file was missing or undecodable.
  NativePageReadback authored = candidate;
  const Render::ValidationResult authored_validation =
      BuildAuthoredDensityLayers(*terrain, slot_x, slot_y, authored);
  if (authored_validation) {
    std::string detail_repeats;
    for (const AuthoredLayer &layer : authored.detail_layers) {
      if (!detail_repeats.empty())
        detail_repeats += "|";
      detail_repeats += Ogre::StringConverter::toString(layer.uv_repeats);
    }
    Ogre::LogManager::getSingleton().logMessage(
        "[RoR|SceneSource|TerrainLayers] page=" +
        Ogre::StringConverter::toString(slot_x) + "," +
        Ogre::StringConverter::toString(slot_y) + " authored_density=1" +
        " base=" + Ogre::StringConverter::toString(authored.texture.width) +
        "x" + Ogre::StringConverter::toString(authored.texture.height) +
        " base_repeats=" +
        Ogre::StringConverter::toString(authored.base_uv_repeats) +
        " base_m_per_texel=" +
        Ogre::StringConverter::toString(
            authored.texture.width == 0U || authored.base_uv_repeats <= 0.0F
                ? 0.0F
                : static_cast<float>(terrain->getWorldSize()) /
                      (authored.base_uv_repeats *
                       static_cast<float>(authored.texture.width))) +
        " weight=" +
        Ogre::StringConverter::toString(authored.weight_texture.width) + "x" +
        Ogre::StringConverter::toString(authored.weight_texture.height) +
        " detail_layers=" +
        Ogre::StringConverter::toString(
            static_cast<unsigned int>(authored.detail_layers.size())) +
        " detail_repeats=" + detail_repeats);
    candidate = std::move(authored);
  } else {
    Ogre::LogManager::getSingleton().logMessage(
        "[RoR|SceneSource|TerrainLayers] page=" +
        Ogre::StringConverter::toString(slot_x) + "," +
        Ogre::StringConverter::toString(slot_y) +
        " authored_density=0 layers=" +
        Ogre::StringConverter::toString(
            static_cast<unsigned int>(terrain->getLayerCount())) +
        " composite_fallback_field=" + authored_validation.field +
        " detail=" + std::string(authored_validation.detail));
  }

  readback = std::move(candidate);
  return Render::ValidationResult::Success();
}

} // namespace

struct Ogre14ToOgreNextTerrainSource::State final {
  struct PageOwner final {
    std::int32_t slot_x = 0;
    std::int32_t slot_y = 0;
    const void *native_terrain = nullptr;
    std::vector<Render::GraphicsSceneAssetInput> assets;
    Render::GraphicsSceneStaticMeshInput instance;
  };

  bool captured = false;
  const void *native_group = nullptr;
  OgreNextDemoIdentityRegistry identities;
  std::set<std::string, std::less<>> known_pages;
  std::set<std::string, std::less<>> live_pages;
  std::map<std::string, PageOwner, std::less<>> page_owners;
};

namespace {

template <typename StateType>
Render::ValidationResult BuildCommittedCapture(
    const StateType &state,
    OgreNextDemoTerrainCapture &capture) {
  if (!state.captured || state.live_pages.size() != state.page_owners.size()) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.terrain.frozen_state",
                   "committed terrain capture state is incomplete");
  }
  OgreNextDemoTerrainCapture candidate;
  candidate.assets.reserve(state.page_owners.size() * 4U);
  candidate.static_meshes.reserve(state.page_owners.size());
  for (const auto &entry : state.page_owners) {
    // A page publishes mesh, base texture, base sampler and material, plus a
    // texture/sampler pair for the weight mask and for each authored detail
    // layer. The composite fallback publishes the bare four.
    constexpr std::size_t kMinimumPageAssets = 4U;
    constexpr std::size_t kMaximumPageAssets =
        kMinimumPageAssets + 2U + 2U * Render::kMaterialDetailMapCount;
    if (state.live_pages.find(entry.first) == state.live_pages.end() ||
        entry.second.native_terrain == nullptr ||
        entry.second.assets.size() < kMinimumPageAssets ||
        entry.second.assets.size() > kMaximumPageAssets ||
        (entry.second.assets.size() - kMinimumPageAssets) % 2U != 0U) {
      return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                     "ogre_next_demo.terrain.frozen_owner",
                     "committed terrain page owner is incomplete");
    }
    candidate.assets.insert(candidate.assets.end(),
                            entry.second.assets.begin(),
                            entry.second.assets.end());
    candidate.static_meshes.push_back(entry.second.instance);
  }
  std::sort(candidate.assets.begin(), candidate.assets.end(),
            [](const auto &first, const auto &second) {
              return first.source_asset_id < second.source_asset_id;
            });
  std::sort(candidate.static_meshes.begin(), candidate.static_meshes.end(),
            [](const auto &first, const auto &second) {
              return first.source_object_id < second.source_object_id;
            });
  capture = std::move(candidate);
  return Render::ValidationResult::Success();
}

} // namespace

Ogre14ToOgreNextTerrainSource::Ogre14ToOgreNextTerrainSource()
    : committed_(std::make_unique<State>()) {}

Ogre14ToOgreNextTerrainSource::~Ogre14ToOgreNextTerrainSource() = default;

bool Ogre14ToOgreNextTerrainSource::HasCommittedCapture() const noexcept {
  return committed_ != nullptr && committed_->captured;
}

Render::ValidationResult
Ogre14ToOgreNextTerrainSource::VerifyCommittedIdentity(
    Ogre::TerrainGroup *terrain_group) const try {
  if (pending_ != nullptr) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.terrain.pending",
                   "the preceding terrain capture must be committed or discarded");
  }
  if (!HasCommittedCapture()) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.terrain.frozen_state",
                   "no committed map-generation terrain capture exists");
  }
  if (terrain_group != committed_->native_group) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.group",
                   "TerrainGroup identity changed inside one map generation");
  }
  if (terrain_group == nullptr) {
    if (!committed_->page_owners.empty()) {
      return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                     "ogre_next_demo.terrain.frozen_owner",
                     "null TerrainGroup retained committed page owners");
    }
  } else {
    const Ogre::TerrainGroup::TerrainSlotMap &slots =
        terrain_group->getTerrainSlots();
    if (slots.size() != committed_->page_owners.size() ||
        terrain_group->getNumTerrainPrepareRequests() != 0U) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.terrain.slot_inventory",
                     "TerrainGroup slot inventory changed inside one map generation");
    }
    std::map<std::uint32_t, const State::PageOwner *> owners_by_slot;
    for (const auto &entry : committed_->page_owners) {
      const std::uint32_t packed = terrain_group->packIndex(
          entry.second.slot_x, entry.second.slot_y);
      if (!owners_by_slot.emplace(packed, &entry.second).second) {
        return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                       "ogre_next_demo.terrain.slot_inventory",
                       "committed terrain slots are duplicated");
      }
    }
    for (const auto &slot_entry : slots) {
      const Ogre::TerrainGroup::TerrainSlot *const slot = slot_entry.second;
      const auto expected = owners_by_slot.find(slot_entry.first);
      if (slot == nullptr || expected == owners_by_slot.end() ||
          terrain_group->packIndex(slot->x, slot->y) != slot_entry.first ||
          slot->x != expected->second->slot_x ||
          slot->y != expected->second->slot_y) {
        return Failure(Render::ValidationCode::REVISION_MISMATCH,
                       "ogre_next_demo.terrain.slot_inventory",
                       "TerrainGroup packed slot keys changed inside one map generation");
      }
      if (slot->instance == nullptr || !slot->instance->isLoaded() ||
          slot->instance != expected->second->native_terrain) {
        return Failure(Render::ValidationCode::REVISION_MISMATCH,
                       "ogre_next_demo.terrain.native_page",
                       "terrain page identity changed inside one map generation");
      }
    }
  }

  return Render::ValidationResult::Success();
} catch (const Ogre::Exception &) {
  return Failure(Render::ValidationCode::MISSING_REFERENCE,
                 "ogre_next_demo.terrain.ogre_exception",
                 "OGRE failed before the frozen terrain capture was published");
} catch (const std::bad_alloc &) {
  return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                 "ogre_next_demo.terrain.allocation",
                 "allocation failed before the frozen terrain capture was published");
} catch (...) {
  return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                 "ogre_next_demo.terrain.exception",
                 "unexpected exception before the frozen terrain capture was published");
}

Render::ValidationResult Ogre14ToOgreNextTerrainSource::CaptureCommitted(
    Ogre::TerrainGroup *terrain_group,
    OgreNextDemoTerrainCapture &capture) try {
  const Render::ValidationResult identity =
      VerifyCommittedIdentity(terrain_group);
  if (!identity) {
    return identity;
  }
  return BuildCommittedCapture(*committed_, capture);
} catch (const Ogre::Exception &) {
  return Failure(Render::ValidationCode::MISSING_REFERENCE,
                 "ogre_next_demo.terrain.ogre_exception",
                 "OGRE failed before the frozen terrain capture was published");
} catch (const std::bad_alloc &) {
  return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                 "ogre_next_demo.terrain.allocation",
                 "allocation failed before the frozen terrain capture was published");
} catch (...) {
  return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                 "ogre_next_demo.terrain.exception",
                 "unexpected exception before the frozen terrain capture was published");
}

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
  if (committed_->captured) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.terrain.frozen_state",
                   "fresh terrain capture is closed until the next map generation");
  }
  if (terrain_group == nullptr && !pages.empty()) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.terrain.group",
                   "TerrainGroup and exact page inventory must coexist");
  }
  auto candidate_state = std::make_unique<State>(*committed_);
  candidate_state->native_group = terrain_group;
  candidate_state->live_pages.clear();
  candidate_state->page_owners.clear();
  OgreNextDemoTerrainCapture candidate_capture;
  if (pages.empty()) {
    candidate_state->captured = true;
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
    const Render::MeshResourceDescriptor &captured_mesh =
        std::get<Render::MeshResourceDescriptor>(*page.mesh_payload);
    if (captured_mesh.dynamic ||
        captured_mesh.texture_coordinates_0.size() !=
            captured_mesh.positions.size()) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.terrain.mesh.uv0",
                     "display-domain terrain requires a static mesh with complete authored UV0",
                     index);
    }
    // Terrain remains unlit and shadow-free in this disposable lowering, so
    // only its authored position/UV geometry is authoritative. Sanitize a
    // private candidate before the full descriptor/material checks and never
    // replace the cached OGRE14 CPU payload on failure.
    Render::MeshResourceDescriptor mesh = captured_mesh;
    Render::ValidationResult validation =
        NormalizeOgreNextDemoMatteMesh(mesh);
    if (!validation) {
      validation.field = "ogre_next_demo.terrain.mesh." + validation.field;
      validation.element_index = index;
      return validation;
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
    std::uint64_t weight_texture_id = 0U;
    std::uint64_t weight_sampler_id = 0U;
    std::array<std::uint64_t, Render::kMaterialDetailMapCount>
        detail_texture_ids{};
    std::array<std::uint64_t, Render::kMaterialDetailMapCount>
        detail_sampler_ids{};
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

    if (native.authored_density && !native.detail_layers.empty()) {
      // Each authored layer needs its own stable asset identity. The layer
      // ordinal joins the page key so a page never mints two identities for
      // one domain, which the registry rejects as a duplicate. A single-layer
      // page mints none of these: it publishes only the base four assets.
      const auto register_layer_identity =
          [&](std::string_view domain, std::size_t ordinal,
              std::uint64_t &output) -> Render::ValidationResult {
        std::string key(page.exact_page_key);
        key.push_back('/');
        key.append(std::to_string(ordinal));
        Render::ValidationResult derived =
            DeriveOgreNextDemoSourceId(domain, key, output);
        if (!derived) {
          return derived;
        }
        std::string exact_identity(domain);
        exact_identity.push_back('\0');
        exact_identity.append(key);
        return candidate_state->identities.Register(std::move(exact_identity),
                                                    output);
      };
      validation = register_layer_identity(kWeightTextureIdDomain, 0U,
                                           weight_texture_id);
      if (!validation) {
        validation.element_index = index;
        return validation;
      }
      validation = register_layer_identity(kWeightSamplerIdDomain, 0U,
                                           weight_sampler_id);
      if (!validation) {
        validation.element_index = index;
        return validation;
      }
      for (std::size_t layer = 0U; layer < native.detail_layers.size();
           ++layer) {
        validation = register_layer_identity(kDetailTextureIdDomain, layer,
                                             detail_texture_ids[layer]);
        if (!validation) {
          validation.element_index = index;
          return validation;
        }
        validation = register_layer_identity(kDetailSamplerIdDomain, layer,
                                             detail_sampler_ids[layer]);
        if (!validation) {
          validation.element_index = index;
          return validation;
        }
      }
    }

    Render::MaterialDescriptor material;
    material.debug_name = PageDebugName(page.slot_x, page.slot_y, "Material");
    // The page is lit by the presenter like every other opaque surface: the
    // producer bakes a pure-albedo composite (unit ambient, no directional
    // term), and a display-domain unlit surface would be crushed to black
    // by an exposure calibrated for the analytic sun.
    material.model = Render::MaterialModel::PBR_METALLIC_ROUGHNESS;
    material.blend_mode = Render::MaterialBlendMode::REPLACE;
    material.alpha_test_mode = Render::MaterialAlphaTestMode::DISABLED;
    material.base_color_transfer =
        Render::BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER;
    material.double_sided = false;
    material.base_color_factor = {1.0F, 1.0F, 1.0F, 1.0F};
    material.metallic_factor = 0.0F;
    material.roughness_factor = 1.0F;
    material.base_color_texture.texture_coordinate_set = 0U;
    material.base_color_texture.scale = {1.0F, 1.0F};
    material.base_color_texture.offset = {};
    material.base_color_texture.rotation_radians = 0.0F;
    if (native.authored_density) {
      // UV0 spans the page exactly once, so repeating the base layer at its
      // authored world size is a plain UV scale. That scale is ordinary v4
      // UV0-affine state, so a single-layer page (the base CityWorld map)
      // needs nothing beyond it and keeps the v4 material.
      material.base_color_texture.scale = {native.base_uv_repeats,
                                           native.base_uv_repeats};
    }
    if (native.authored_density && !native.detail_layers.empty()) {
      // Every detail layer repeats at its own rate through the native
      // per-detail offset/scale, while the weight mask stays unscaled across
      // the page. Detail bindings are what require the v6 profile.
      material.version = Render::kMaterialDescriptorDetailVersion;
      material.detail_weight_texture.texture_coordinate_set = 0U;
      material.detail_weight_texture.scale = {1.0F, 1.0F};
      material.detail_weight_texture.offset = {};
      material.detail_weight_texture.rotation_radians = 0.0F;
      for (std::size_t layer = 0U; layer < native.detail_layers.size();
           ++layer) {
        Render::TextureBinding &binding = material.detail_textures[layer];
        binding.texture_coordinate_set = 0U;
        binding.scale = {native.detail_layers[layer].uv_repeats,
                         native.detail_layers[layer].uv_repeats};
        binding.offset = {};
        binding.rotation_radians = 0.0F;
        material.detail_weights[layer] = 1.0F;
      }
    }
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
    if (native.authored_density && !native.detail_layers.empty()) {
      validation = Render::ValidateMaterialTextureCompatibility(
          Render::MaterialTextureSlot::DETAIL_WEIGHT, native.weight_texture,
          native.weight_sampler);
      if (!validation) {
        validation.field = "ogre_next_demo.terrain.weight_texture." +
                           validation.field;
        validation.element_index = index;
        return validation;
      }
      constexpr std::array<Render::MaterialTextureSlot,
                           Render::kMaterialDetailMapCount>
          kDetailSlots{Render::MaterialTextureSlot::DETAIL0,
                       Render::MaterialTextureSlot::DETAIL1,
                       Render::MaterialTextureSlot::DETAIL2,
                       Render::MaterialTextureSlot::DETAIL3};
      for (std::size_t layer = 0U; layer < native.detail_layers.size();
           ++layer) {
        validation = Render::ValidateMaterialTextureCompatibility(
            kDetailSlots[layer], native.detail_layers[layer].texture,
            native.detail_layers[layer].sampler);
        if (!validation) {
          validation.field = "ogre_next_demo.terrain.detail_texture." +
                             validation.field;
          validation.element_index = index;
          return validation;
        }
      }
    }

    State::PageOwner owner;
    owner.slot_x = page.slot_x;
    owner.slot_y = page.slot_y;
    owner.native_terrain = native.terrain;
    owner.assets.reserve(4U + (native.authored_density &&
                                       !native.detail_layers.empty()
                                   ? 2U + 2U * native.detail_layers.size()
                                   : 0U));
    Render::GraphicsSceneAssetInput mesh_asset;
    mesh_asset.source_asset_id = mesh_id;
    mesh_asset.payload = std::make_shared<const Render::RenderAssetPayload>(
        std::move(mesh));
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
    if (native.authored_density && !native.detail_layers.empty()) {
      Render::GraphicsSceneAssetInput weight_texture_asset;
      weight_texture_asset.source_asset_id = weight_texture_id;
      weight_texture_asset.payload =
          std::make_shared<const Render::RenderAssetPayload>(
              std::move(native.weight_texture));
      owner.assets.push_back(std::move(weight_texture_asset));
      Render::GraphicsSceneAssetInput weight_sampler_asset;
      weight_sampler_asset.source_asset_id = weight_sampler_id;
      weight_sampler_asset.payload =
          std::make_shared<const Render::RenderAssetPayload>(
              std::move(native.weight_sampler));
      owner.assets.push_back(std::move(weight_sampler_asset));
      for (std::size_t layer = 0U; layer < native.detail_layers.size();
           ++layer) {
        Render::GraphicsSceneAssetInput detail_texture_asset;
        detail_texture_asset.source_asset_id = detail_texture_ids[layer];
        detail_texture_asset.payload =
            std::make_shared<const Render::RenderAssetPayload>(
                std::move(native.detail_layers[layer].texture));
        owner.assets.push_back(std::move(detail_texture_asset));
        Render::GraphicsSceneAssetInput detail_sampler_asset;
        detail_sampler_asset.source_asset_id = detail_sampler_ids[layer];
        detail_sampler_asset.payload =
            std::make_shared<const Render::RenderAssetPayload>(
                std::move(native.detail_layers[layer].sampler));
        owner.assets.push_back(std::move(detail_sampler_asset));
      }
    }

    Render::GraphicsSceneAssetInput material_asset;
    material_asset.source_asset_id = material_id;
    material_asset.payload = std::make_shared<const Render::RenderAssetPayload>(
        std::move(material));
    material_asset.material_bindings[static_cast<std::size_t>(
        Render::MaterialTextureSlot::BASE_COLOR)] = {texture_id, sampler_id};
    if (native.authored_density && !native.detail_layers.empty()) {
      material_asset.material_bindings[static_cast<std::size_t>(
          Render::MaterialTextureSlot::DETAIL_WEIGHT)] = {weight_texture_id,
                                                          weight_sampler_id};
      for (std::size_t layer = 0U; layer < native.detail_layers.size();
           ++layer) {
        material_asset.material_bindings[static_cast<std::size_t>(
            Render::MaterialTextureSlot::DETAIL0) + layer] = {
            detail_texture_ids[layer], detail_sampler_ids[layer]};
      }
    }
    owner.assets.push_back(std::move(material_asset));

    owner.instance.source_object_id = object_id;
    owner.instance.mesh_source_asset_id = mesh_id;
    owner.instance.material_source_asset_id = material_id;
    owner.instance.render_from_object.elements[12U] = page.page_world_position.x;
    owner.instance.render_from_object.elements[13U] = page.page_world_position.y;
    owner.instance.render_from_object.elements[14U] = page.page_world_position.z;
    owner.instance.visibility_mask = page.visible ? page.visibility_mask : 0U;
    // The page is a lit PBS surface with authored normals and tangents, so it
    // takes the analytic sun's cascaded shadows like any other opaque ground.
    // Without this the buildings and vehicles standing on the terrain cast
    // onto nothing, and unshadowed ground reads flatter than it is.
    //
    // The page deliberately does not CAST: it is a single ground plane under
    // everything else, so it can only self-shadow its own skirt and would
    // otherwise pay a full extra cascade draw for no visible occluder.
    owner.instance.flags = Render::MESH_INSTANCE_VISIBLE_IN_REFLECTIONS |
                           Render::MESH_INSTANCE_RECEIVES_SHADOW;

    for (const Render::GraphicsSceneAssetInput &asset : owner.assets) {
      candidate_capture.assets.push_back(asset);
    }
    candidate_capture.static_meshes.push_back(owner.instance);
    candidate_state->page_owners.emplace(page.exact_page_key,
                                         std::move(owner));
  }

  candidate_state->known_pages.insert(candidate_state->live_pages.begin(),
                                      candidate_state->live_pages.end());
  candidate_state->captured = true;
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

void Ogre14ToOgreNextTerrainSource::CommitMapGenerationCapture() noexcept {
  if (pending_ != nullptr) {
    committed_.swap(pending_);
    pending_.reset();
  }
}

void Ogre14ToOgreNextTerrainSource::DiscardMapGenerationCapture() noexcept {
  pending_.reset();
}

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
