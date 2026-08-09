/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"

#include <OgreBuildSettings.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreLodStrategyManager.h>
#include <OgreLogManager.h>
#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgrePixelFormat.h>
#include <OgreResourceGroupManager.h>
#include <OgreSceneManager.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                  OGRE_VERSION_PATCH == 2,
              "native readback probe must use pinned OGRE 14.5.2");

namespace RoR::Render::Testing {

ValidationResult Ogre14TerrainCompositeCaptureTestAccess::Capture(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before, const void *bytes,
    std::size_t byte_count,
    const Ogre14TerrainCompositeNativeObservation &after,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  if (bytes == nullptr && byte_count != 0U) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD,
        "terrain_composite.readback.mip_rgba_bytes",
        "nonempty synthetic level-zero readback has no source bytes");
  }
  std::vector<std::vector<std::uint8_t>> chain(1U);
  if (byte_count != 0U) {
    const auto *const first = static_cast<const std::uint8_t *>(bytes);
    chain.front().assign(first, first + byte_count);
  }
  return Ogre14TerrainCompositeNativeAdapter::CaptureSyntheticForTesting(
      configuration, before, chain, after, receipt, fault_injector);
}

ValidationResult Ogre14TerrainCompositeCaptureTestAccess::CaptureMipChain(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before,
    const std::vector<std::vector<std::uint8_t>> &mip_rgba_bytes,
    const Ogre14TerrainCompositeNativeObservation &after,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  return Ogre14TerrainCompositeNativeAdapter::CaptureSyntheticForTesting(
      configuration, before, mip_rgba_bytes, after, receipt, fault_injector);
}

} // namespace RoR::Render::Testing

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

class ExactRgbaPixelBuffer final : public Ogre::HardwarePixelBuffer {
public:
  using Ogre::HardwarePixelBuffer::blitToMemory;

  ExactRgbaPixelBuffer(std::uint32_t width, std::uint32_t height,
                       std::vector<std::uint8_t> bytes)
      : Ogre::HardwarePixelBuffer(width, height, 1U, Ogre::PF_BYTE_RGBA,
                                  Ogre::HBU_CPU_ONLY, false),
        bytes_(std::move(bytes)) {
    Require(bytes_.size() == static_cast<std::size_t>(width) * height * 4U,
            "native probe pixel payload has the wrong extent");
  }

  void blitFromMemory(const Ogre::PixelBox &source,
                      const Ogre::Box &destination) override {
    Require(destination.left == 0U && destination.top == 0U &&
                destination.front == 0U &&
                destination.getWidth() == getWidth() &&
                destination.getHeight() == getHeight() &&
                destination.getDepth() == 1U,
            "native probe destination extent changed");
    Ogre::PixelBox target(getWidth(), getHeight(), 1U, Ogre::PF_BYTE_RGBA,
                          bytes_.data());
    Ogre::PixelUtil::bulkPixelConversion(source, target);
  }

  void blitToMemory(const Ogre::Box &source,
                    const Ogre::PixelBox &destination) override {
    Require(source.left == 0U && source.top == 0U && source.front == 0U &&
                source.getWidth() == getWidth() &&
                source.getHeight() == getHeight() && source.getDepth() == 1U,
            "native probe source extent changed");
    Ogre::PixelBox native(getWidth(), getHeight(), 1U, Ogre::PF_BYTE_RGBA,
                          bytes_.data());
    Ogre::PixelUtil::bulkPixelConversion(native, destination);
  }

protected:
  Ogre::PixelBox lockImpl(const Ogre::Box &, LockOptions) override {
    return Ogre::PixelBox(getWidth(), getHeight(), 1U, Ogre::PF_BYTE_RGBA,
                          bytes_.data());
  }
  void unlockImpl() override {}

private:
  std::vector<std::uint8_t> bytes_;
};

class ExactMipTexture final : public Ogre::Texture {
public:
  ExactMipTexture()
      : Ogre::Texture(nullptr, "NativeProbe/Terrain/comp", 2U, "NativeProbe",
                      false, nullptr) {
    mWidth = 2U;
    mHeight = 2U;
    mDepth = 1U;
    mSrcWidth = 2U;
    mSrcHeight = 2U;
    mSrcDepth = 1U;
    mTextureType = Ogre::TEX_TYPE_2D;
    mNumRequestedMipmaps = 1U;
    // Pinned public semantics: one level additional to level zero.
    mNumMipmaps = 1U;
    mFormat = Ogre::PF_BYTE_RGBA;
    mSrcFormat = Ogre::PF_BYTE_RGBA;
    mUsage = Ogre::HBU_GPU_TO_CPU;
    mHwGamma = true;
    mSurfaceList.push_back(std::make_shared<ExactRgbaPixelBuffer>(
        2U, 2U,
        std::vector<std::uint8_t>{1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
                                  9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U}));
    mSurfaceList.push_back(std::make_shared<ExactRgbaPixelBuffer>(
        1U, 1U, std::vector<std::uint8_t>{21U, 22U, 23U, 24U}));
  }

protected:
  void prepareImpl() override {}
  void loadImpl() override {}
  void createInternalResourcesImpl() override {}
  void freeInternalResourcesImpl() override {}
};

struct SamplingGraph final {
  SamplingGraph()
      : texture(std::make_shared<ExactMipTexture>()),
        material(nullptr, "NativeProbe/Terrain", 1U, "NativeProbe") {
    material.createTechnique()->createPass();
    technique = material.createTechnique();
    technique->setLodIndex(1U);
    pass = technique->createPass();
    unit = pass->createTextureUnitState();
    sampler = std::make_shared<Ogre::Sampler>();
    sampler->setAddressingMode(Ogre::TAM_CLAMP);
    unit->setSampler(sampler);
    unit->setTexture(texture);
    unit->setHardwareGammaEnabled(true);
  }

  Ogre::TexturePtr texture;
  Ogre::Material material;
  Ogre::Technique *technique = nullptr;
  Ogre::Pass *pass = nullptr;
  Ogre::TextureUnitState *unit = nullptr;
  Ogre::SamplerPtr sampler;
};

std::array<float, 16U> Flatten(const Ogre::Matrix4 &matrix) {
  std::array<float, 16U> output{};
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      output[row * 4U + column] = matrix[row][column];
    }
  }
  return output;
}

RoR::Render::Ogre14TerrainCompositeNativeObservation Observation(
    const SamplingGraph &graph) {
  using namespace RoR::Render;
  Ogre14TerrainCompositeNativeObservation observation;
  observation.terrain_group_pointer_token = 0x100U;
  observation.terrain_slot_pointer_token = 0x180U;
  observation.terrain_pointer_token = 0x200U;
  observation.exact_terrain_resource_group = "NativeProbe";
  observation.exact_filename_prefix = "terrain";
  observation.exact_filename_extension = "dat";
  observation.page_definition_kind =
      Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED;
  observation.exact_definition_filename = "terrain_0.dat";
  observation.generated_save_filename = "terrain_0.dat";
  observation.exact_terrain_material_name = graph.material.getName();
  observation.terrain_size = 513U;
  observation.terrain_world_size = 1000.0F;
  observation.terrain_world_position = {0.0F, 0.0F, 0.0F};
  observation.terrain_is_loaded = true;
  observation.terrain_derived_data_update_in_progress = false;
  observation.texture_pointer_token =
      reinterpret_cast<std::uintptr_t>(graph.texture.get());
  observation.pixel_buffer_pointer_token =
      reinterpret_cast<std::uintptr_t>(graph.texture->getBuffer(0U, 0U).get());
  observation.texture_handle = graph.texture->getHandle();
  observation.exact_texture_resource_group = graph.texture->getGroup();
  observation.exact_texture_name = graph.texture->getName();
  observation.texture_type = Ogre14TerrainCompositeTextureType::TEXTURE_2D;
  observation.texture_loading_state =
      Ogre14TerrainCompositeTextureLoadingState::LOADED;
  observation.texture_width = 2U;
  observation.texture_height = 2U;
  observation.texture_depth = 1U;
  observation.texture_face_count = 1U;
  observation.texture_additional_mip_count = 1U;
  observation.texture_mip_count = 2U;
  observation.texture_is_loaded = true;
  observation.texture_is_manual = true;
  observation.texture_hardware_gamma_enabled = true;
  observation.texture_resource_revision = 2U;
  observation.tight_row_pitch_bytes = 8U;
  observation.tight_slice_pitch_bytes = 16U;
  observation.mip_chain = {
      {0U,
       reinterpret_cast<std::uintptr_t>(graph.texture->getBuffer(0U, 0U).get()),
       2U, 2U, 1U, 8U, 16U},
      {1U,
       reinterpret_cast<std::uintptr_t>(graph.texture->getBuffer(0U, 1U).get()),
       1U, 1U, 1U, 4U, 4U},
  };

  Ogre14TerrainCompositeSamplingObservation &sampling = observation.sampling;
  sampling.scene_manager_pointer_token = 0x250U;
  sampling.texture_unit_pointer_token =
      reinterpret_cast<std::uintptr_t>(graph.unit);
  sampling.sampler_pointer_token =
      reinterpret_cast<std::uintptr_t>(graph.sampler.get());
  sampling.bound_texture_pointer_token =
      reinterpret_cast<std::uintptr_t>(graph.unit->_getTexturePtr().get());
  sampling.texture_unit_content_named =
      graph.unit->getContentType() == Ogre::TextureUnitState::CONTENT_NAMED;
  sampling.texture_unit_frame_count = graph.unit->getNumFrames();
  sampling.texture_unit_current_frame = graph.unit->getCurrentFrame();
  sampling.texture_unit_texture_2d =
      graph.unit->getTextureType() == Ogre::TEX_TYPE_2D;
  sampling.texture_unit_is_blank = graph.unit->isBlank();
  sampling.texture_unit_load_failing = graph.unit->isTextureLoadFailing();
  sampling.unordered_access_mip_level =
      graph.unit->getUnorderedAccessMipLevel();
  sampling.texture_coord_set = graph.unit->getTextureCoordSet();
  sampling.texcoord_calculation_none =
      graph.unit->_deriveTexCoordCalcMethod() == Ogre::TEXCALC_NONE;
  sampling.texture_effect_count = graph.unit->getEffects().size();
  sampling.texture_u_scroll = graph.unit->getTextureUScroll();
  sampling.texture_v_scroll = graph.unit->getTextureVScroll();
  sampling.texture_u_scale = graph.unit->getTextureUScale();
  sampling.texture_v_scale = graph.unit->getTextureVScale();
  sampling.texture_rotation_radians =
      graph.unit->getTextureRotate().valueRadians();
  sampling.texture_transform = Flatten(graph.unit->getTextureTransform());
  sampling.address_u = sampling.address_v = sampling.address_w =
      Ogre14TerrainCompositeAddressMode::CLAMP;
  sampling.min_filter = sampling.mag_filter =
      Ogre14TerrainCompositeFilter::LINEAR;
  sampling.mip_filter = Ogre14TerrainCompositeFilter::POINT;
  sampling.maximum_anisotropy = graph.sampler->getAnisotropy();
  sampling.mipmap_bias = graph.sampler->getMipmapBias();
  sampling.compare_enabled = graph.sampler->getCompareEnabled();
  sampling.compare_function =
      Ogre14TerrainCompositeCompareFunction::GREATER_EQUAL;
  const Ogre::ColourValue &border = graph.sampler->getBorderColour();
  sampling.border_colour = {border.r, border.g, border.b, border.a};
  sampling.texture_unit_hardware_gamma_enabled =
      graph.unit->isHardwareGammaEnabled();
  sampling.scene_fog_mode = Ogre14TerrainCompositeSceneFogMode::FOG_NONE;
  return observation;
}

} // namespace

int main() {
  using namespace RoR::Render;

  Ogre::LogManager log_manager;
  Ogre::LodStrategyManager lod_strategy_manager;
  Ogre::ResourceGroupManager resource_group_manager;
  Ogre::MaterialManager material_manager;

  using CaptureFunction =
      ValidationResult (*)(Ogre::TerrainGroup &, std::int32_t, std::int32_t,
                           const Ogre14TerrainCompositeCaptureConfiguration &,
                           Ogre14TerrainCompositeCaptureReceipt &);
  CaptureFunction native_capture =
      static_cast<CaptureFunction>(&Ogre14TerrainCompositeNativeAdapter::Capture);
  Require(native_capture != nullptr, "native capture entry point is absent");

  using FogGetter = Ogre::FogMode (Ogre::SceneManager::*)() const;
  using TusGammaGetter = bool (Ogre::TextureUnitState::*)() const;
  using UnorderedMipGetter = int (Ogre::TextureUnitState::*)() const;
  FogGetter scene_fog = &Ogre::SceneManager::getFogMode;
  TusGammaGetter tus_gamma = &Ogre::TextureUnitState::isHardwareGammaEnabled;
  UnorderedMipGetter unordered_mip =
      &Ogre::TextureUnitState::getUnorderedAccessMipLevel;
  Require(scene_fog != nullptr && tus_gamma != nullptr &&
              unordered_mip != nullptr,
          "pinned direct fog/gamma/unordered-mip API is absent");

  SamplingGraph graph;
  Require(graph.texture->getNumMipmaps() == 1U,
          "getNumMipmaps no longer reports levels additional to zero");
  Require(graph.texture->getBuffer(0U, 0U).get() !=
              graph.texture->getBuffer(0U, 1U).get(),
          "distinct mip buffer identity changed");
  Require(graph.unit->_getTexturePtr().get() == graph.texture.get() &&
              graph.unit->_deriveTexCoordCalcMethod() == Ogre::TEXCALC_NONE &&
              graph.unit->getEffects().empty() &&
              graph.unit->getTextureCoordSet() == 0U &&
              graph.unit->getUnorderedAccessMipLevel() == -1 &&
              !graph.unit->isBlank() && !graph.unit->isTextureLoadFailing(),
          "ordinary exact UV0 texture-unit binding changed");
  Require(graph.sampler->getAddressingMode().u == Ogre::TAM_CLAMP &&
              graph.sampler->getAddressingMode().v == Ogre::TAM_CLAMP &&
              graph.sampler->getAddressingMode().w == Ogre::TAM_CLAMP &&
              graph.sampler->getFiltering(Ogre::FT_MIN) == Ogre::FO_LINEAR &&
              graph.sampler->getFiltering(Ogre::FT_MAG) == Ogre::FO_LINEAR &&
              graph.sampler->getFiltering(Ogre::FT_MIP) == Ogre::FO_POINT &&
              graph.sampler->getAnisotropy() == 1U &&
              graph.sampler->getMipmapBias() == 0.0F &&
              !graph.sampler->getCompareEnabled() &&
              graph.sampler->getCompareFunction() ==
                  Ogre::CMPF_GREATER_EQUAL &&
              graph.sampler->getBorderColour().r == 0.0F &&
              graph.sampler->getBorderColour().g == 0.0F &&
              graph.sampler->getBorderColour().b == 0.0F &&
              graph.sampler->getBorderColour().a == 1.0F &&
              graph.unit->isHardwareGammaEnabled() &&
              graph.texture->isHardwareGammaEnabled(),
          "exact clamp/bilinear/point-mip sampler state changed");

  std::vector<std::vector<std::uint8_t>> readbacks(2U);
  readbacks[0U].resize(16U);
  readbacks[1U].resize(4U);
  for (std::uint32_t level = 0U; level < 2U; ++level) {
    const Ogre::HardwarePixelBufferPtr buffer =
        graph.texture->getBuffer(0U, level);
    Ogre::PixelBox destination(buffer->getWidth(), buffer->getHeight(), 1U,
                               Ogre::PF_BYTE_RGBA, readbacks[level].data());
    buffer->blitToMemory(destination);
  }
  Require(readbacks[0U][3U] == 4U && readbacks[0U][15U] == 16U &&
              readbacks[1U][3U] == 24U,
          "public full-mip readback lost arbitrary alpha");

  Ogre14TerrainCompositeCaptureReceipt receipt;
  const Ogre14TerrainCompositeNativeObservation observation =
      Observation(graph);
  const ValidationResult result = RoR::Render::Testing::
      Ogre14TerrainCompositeCaptureTestAccess::CaptureMipChain(
          {}, observation, readbacks, observation, receipt);
  Require(result.ok() && receipt.initialized() &&
              receipt.mip_level_count() == 2U &&
              receipt.mip_rgba_size(0U) == 16U &&
              receipt.mip_rgba_size(1U) == 4U,
          "native PF_BYTE_RGBA full-mip path did not mint V2 receipt");
  Ogre14TerrainCompositeOpaqueLowering lowering;
  Require(LowerOgre14TerrainCompositeOpaque(receipt, lowering).ok() &&
              lowering.mip_chain.size() == 2U &&
              lowering.sampler.maximum_lod == 1.0F &&
              lowering.mip_chain[0U].rgba_bytes[3U] == 255U &&
              lowering.mip_chain[1U].rgba_bytes[3U] == 255U,
          "native transport receipt did not lower exactly");

  std::cout << "Pinned OGRE 14.5.2 terrain-composite transport ABI probe passed\n";
  return EXIT_SUCCESS;
}
