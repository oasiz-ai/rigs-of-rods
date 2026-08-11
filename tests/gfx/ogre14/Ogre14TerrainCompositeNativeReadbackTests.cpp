/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"
#include "gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.h"
#include "gfx/render/Ogre14GraphicsSceneSource.h"

#include <OgreBuildSettings.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgrePixelFormat.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                  OGRE_VERSION_PATCH == 2,
              "native readback probe must use pinned OGRE 14.5.2");

namespace RoR::Render::Testing {

ValidationResult Ogre14TerrainCompositeCaptureTestAccess::Capture(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &observation,
    const void *bytes, std::size_t byte_count,
    const Ogre14TerrainCompositeNativeObservation &after,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  return Ogre14TerrainCompositeNativeAdapter::CaptureSyntheticForTesting(
      configuration, observation, bytes, byte_count, after, receipt,
      fault_injector);
}

} // namespace RoR::Render::Testing

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

constexpr std::array<std::uint8_t, 16U> kNativeRows{{
    1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,
    9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
}};

class ExactRgbaPixelBuffer final : public Ogre::HardwarePixelBuffer {
public:
  using Ogre::HardwarePixelBuffer::blitToMemory;

  ExactRgbaPixelBuffer()
      : Ogre::HardwarePixelBuffer(2U, 2U, 1U, Ogre::PF_BYTE_RGBA,
                                  Ogre::HBU_CPU_ONLY, false) {}

  void blitFromMemory(const Ogre::PixelBox &source,
                      const Ogre::Box &destination) override {
    Require(destination.left == 0U && destination.top == 0U &&
                destination.front == 0U && destination.getWidth() == 2U &&
                destination.getHeight() == 2U &&
                destination.getDepth() == 1U,
            "native probe destination extent changed");
    Ogre::PixelBox target(2U, 2U, 1U, Ogre::PF_BYTE_RGBA, bytes_.data());
    Ogre::PixelUtil::bulkPixelConversion(source, target);
  }

  void blitToMemory(const Ogre::Box &source,
                    const Ogre::PixelBox &destination) override {
    Require(source.left == 0U && source.top == 0U && source.front == 0U &&
                source.getWidth() == 2U && source.getHeight() == 2U &&
                source.getDepth() == 1U,
            "native probe source extent changed");
    Ogre::PixelBox native(2U, 2U, 1U, Ogre::PF_BYTE_RGBA, bytes_.data());
    Ogre::PixelUtil::bulkPixelConversion(native, destination);
  }

protected:
  Ogre::PixelBox lockImpl(const Ogre::Box &, LockOptions) override {
    return Ogre::PixelBox(2U, 2U, 1U, Ogre::PF_BYTE_RGBA, bytes_.data());
  }

  void unlockImpl() override {}

private:
  std::array<std::uint8_t, 16U> bytes_ = kNativeRows;
};

RoR::Render::Ogre14TerrainCompositeNativeObservation Observation(
    const ExactRgbaPixelBuffer &buffer) {
  using namespace RoR::Render;
  Ogre14TerrainCompositeNativeObservation observation;
  observation.terrain_group_pointer_token = 0x100U;
  observation.terrain_slot_pointer_token = 0x180U;
  observation.terrain_pointer_token = 0x200U;
  observation.packed_slot_key = 0U;
  observation.slot_x = 0;
  observation.slot_y = 0;
  observation.exact_terrain_resource_group = "NativeProbe";
  observation.exact_filename_prefix = "terrain";
  observation.exact_filename_extension = "dat";
  observation.page_definition_kind =
      Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED;
  observation.exact_definition_filename = "terrain_0.dat";
  observation.generated_save_filename = "terrain_0.dat";
  observation.exact_terrain_material_name = "NativeProbe/Terrain";
  observation.terrain_size = 513U;
  observation.terrain_world_size = 1000.0F;
  observation.terrain_world_position = {0.0F, 0.0F, 0.0F};
  observation.terrain_is_loaded = true;
  observation.terrain_derived_data_update_in_progress = false;
  observation.texture_pointer_token = 0x300U;
  observation.pixel_buffer_pointer_token =
      reinterpret_cast<std::uintptr_t>(&buffer);
  observation.texture_handle = 1U;
  observation.exact_texture_resource_group = "NativeProbe";
  observation.exact_texture_name = "NativeProbe/Terrain/comp";
  observation.texture_type = Ogre14TerrainCompositeTextureType::TEXTURE_2D;
  observation.texture_loading_state =
      Ogre14TerrainCompositeTextureLoadingState::LOADED;
  observation.texture_width = 2U;
  observation.texture_height = 2U;
  observation.texture_depth = 1U;
  observation.texture_face_count = 1U;
  observation.texture_mip_count = 1U;
  observation.texture_is_loaded = true;
  observation.texture_is_manual = true;
  observation.texture_resource_revision = 2U;
  observation.tight_row_pitch_bytes = 8U;
  observation.tight_slice_pitch_bytes = 16U;
  return observation;
}

RoR::Render::Ogre14GraphicsSceneCapture MakeCompleteJoinedCapture() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneCapture capture;
  capture.joined_buffer_epoch = 1U;
  capture.post_update_scene_epoch = 1U;
  capture.available_fields = kOgre14GraphicsSceneRequiredFields;
  capture.frame.simulation_tick = 1U;
  capture.frame.simulation_time_seconds = 0.01;
  Ogre14CameraCaptureInput camera;
  camera.view_id = 1U;
  camera.width = 1280U;
  camera.height = 720U;
  camera.left = -0.16F;
  camera.right = 0.16F;
  camera.top = 0.09F;
  camera.bottom = -0.09F;
  camera.near_plane = 0.1F;
  camera.far_plane = 1000.0F;
  const ValidationResult validation =
      BuildOgre14GraphicsSceneCamera(camera, capture.frame.camera);
  Require(validation.ok(), "durable-terrain fixture camera was rejected");
  return capture;
}

class InjectedDownstreamTerrainProvider final
    : public RoR::Render::IOgre14GraphicsSceneCaptureProvider {
public:
  [[nodiscard]] RoR::Render::ValidationResult CaptureOgre14GraphicsScene(
      RoR::Render::Ogre14GraphicsSceneCapture &capture) override {
    using namespace RoR::Render;
    RoR::Gfx::Detail::OgreNextDemoTerrainCapture terrain_capture;
    ValidationResult validation;
    if (!terrain_source.HasCommittedCapture()) {
      validation = terrain_source.Capture(nullptr, {}, terrain_capture);
      if (!validation) {
        return validation;
      }
      terrain_source.CommitMapGenerationCapture();
      ++initial_terrain_captures;
    } else {
      validation =
          terrain_source.CaptureCommitted(nullptr, terrain_capture);
      if (!validation) {
        return validation;
      }
      ++committed_terrain_republications;
    }
    if (!terrain_capture.assets.empty() ||
        !terrain_capture.static_meshes.empty()) {
      return ValidationResult::Failure(
          ValidationCode::SEQUENCE_MISMATCH,
          "ogre_next_demo.terrain.empty_fixture",
          "empty map generation unexpectedly published terrain records");
    }
    if (injected_rejections != 0U) {
      --injected_rejections;
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "continuous_particles.tracks_Dust.material",
          "injected downstream particle-material rejection");
    }
    capture = MakeCompleteJoinedCapture();
    return ValidationResult::Success();
  }

  void CommitOgre14GraphicsSceneCapture() noexcept override {
    ++joined_commits;
  }

  void DiscardOgre14GraphicsSceneCapture() noexcept override {
    // The joined transaction deliberately does not own map-generation terrain.
    ++joined_discards;
  }

  RoR::Gfx::Detail::Ogre14ToOgreNextTerrainSource terrain_source;
  std::uint32_t injected_rejections = 2U;
  std::uint32_t initial_terrain_captures = 0U;
  std::uint32_t committed_terrain_republications = 0U;
  std::uint32_t joined_commits = 0U;
  std::uint32_t joined_discards = 0U;
};

void TestCommittedTerrainSurvivesDownstreamJoinedRejection() {
  using namespace RoR::Render;
  InjectedDownstreamTerrainProvider provider;
  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x5445525241494E31ULL;
  GraphicsSceneSnapshotProducer producer(configuration);

  for (std::uint32_t attempt = 0U; attempt < 2U; ++attempt) {
    const GraphicsSceneSnapshotProduceResult rejected =
        producer.ProduceJoinedFrame(source);
    Require(!rejected &&
                rejected.validation.field ==
                    "continuous_particles.tracks_Dust.material",
            "injected downstream rejection did not remain exact");
  }
  const GraphicsSceneSnapshotProduceResult accepted =
      producer.ProduceJoinedFrame(source);
  Require(accepted.ok(),
          "joined scene could not recover after downstream rejection");
  Require(provider.terrain_source.HasCommittedCapture() &&
              provider.initial_terrain_captures == 1U &&
              provider.committed_terrain_republications == 2U,
          "downstream rejection rebuilt or invalidated durable terrain");
  Require(provider.joined_discards == 2U && provider.joined_commits == 1U,
          "joined rejection fixture did not exercise discard then recovery");

  provider.terrain_source.Reset();
  Require(!provider.terrain_source.HasCommittedCapture(),
          "map-generation reset retained durable terrain owners");
  provider.injected_rejections = 1U;
  const GraphicsSceneSnapshotProduceResult rejected_after_reset =
      producer.ProduceJoinedFrame(source);
  Require(!rejected_after_reset &&
              provider.terrain_source.HasCommittedCapture() &&
              provider.initial_terrain_captures == 2U,
          "map-generation reset did not reopen one fresh terrain capture");
}

} // namespace

int main() {
  using namespace RoR::Render;

  // Taking the exact function address makes signature drift against the
  // pinned public TerrainGroup API a native compile failure.
  using CaptureFunction = ValidationResult (*)(
      Ogre::TerrainGroup &, std::int32_t, std::int32_t,
      const Ogre14TerrainCompositeCaptureConfiguration &,
      Ogre14TerrainCompositeCaptureReceipt &,
      IOgre14TerrainCompositeCaptureFaultInjector *);
  CaptureFunction native_capture =
      &Ogre14TerrainCompositeNativeAdapter::Capture;
  Require(native_capture != nullptr, "native capture entry point is absent");

  ExactRgbaPixelBuffer pixel_buffer;
  std::array<std::uint8_t, 16U> readback{};
  Ogre::PixelBox destination(2U, 2U, 1U, Ogre::PF_BYTE_RGBA,
                             readback.data());
  Require(destination.rowPitch == 2U && destination.slicePitch == 4U,
          "pinned OGRE PixelBox is not tightly packed");
  pixel_buffer.blitToMemory(destination);
  Require(readback == kNativeRows,
          "pinned OGRE public PF_BYTE_RGBA conversion changed channels or row order");

  Ogre14TerrainCompositeCaptureReceipt receipt;
  const Ogre14TerrainCompositeNativeObservation observation =
      Observation(pixel_buffer);
  const ValidationResult result =
      RoR::Render::Testing::Ogre14TerrainCompositeCaptureTestAccess::Capture(
          {}, observation, readback.data(), readback.size(), observation,
          receipt);
  Require(result.ok() && receipt.initialized(),
          "native PF_BYTE_RGBA readback did not mint a synthetic receipt");
  Require(receipt.rgba_size() == readback.size() &&
              std::equal(readback.begin(), readback.end(),
                         receipt.rgba_bytes()),
          "receipt did not retain native readback losslessly");
  Require(receipt.rgba_bytes()[3U] == 4U &&
              receipt.rgba_bytes()[7U] == 8U &&
              receipt.rgba_bytes()[11U] == 12U &&
              receipt.rgba_bytes()[15U] == 16U,
          "native alpha/specular bytes were not preserved");

  TestCommittedTerrainSurvivesDownstreamJoinedRejection();

  std::cout << "Pinned OGRE 14.5.2 terrain readback and durable publication "
               "tests passed\n";
  return EXIT_SUCCESS;
}
