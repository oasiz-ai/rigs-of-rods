/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "NativeVisualShowcaseSceneSource.h"

#include "GraphicsSceneSnapshotProducer.h"
#include "OgreNextN1Policy.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#ifndef ROR_NATIVE_VISUAL_SHOWCASE_PACKAGE_FIXTURE
#error                                                                         \
    "ROR_NATIVE_VISUAL_SHOWCASE_PACKAGE_FIXTURE must name the reviewed package"
#endif

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "native visual showcase scene source test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

template <typename T>
bool SameSharedOwner(const std::shared_ptr<const T> &lhs,
                     const std::shared_ptr<const T> &rhs) noexcept {
  return lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

NativeVisualShowcaseSceneSourceLoadResult LoadFixture() {
  return LoadNativeVisualShowcaseSceneSource(
      ROR_NATIVE_VISUAL_SHOWCASE_PACKAGE_FIXTURE);
}

const GraphicsSceneStaticMeshInput &
FindGate(const GraphicsSceneFrameInput &frame,
         std::uint64_t gate_source_object_id) {
  for (const GraphicsSceneStaticMeshInput &instance : frame.static_meshes) {
    if (instance.source_object_id == gate_source_object_id) {
      return instance;
    }
  }
  Require(false, "captured frame is missing the authored gate");
  return frame.static_meshes.front();
}

const MeshInstanceDescriptor &FindGate(const SceneSnapshot &scene,
                                       std::uint64_t gate_source_object_id) {
  for (const MeshInstanceDescriptor &instance : scene.mesh_instances()) {
    if (instance.instance_id == gate_source_object_id) {
      return instance;
    }
  }
  Require(false, "produced scene is missing the authored gate");
  return scene.mesh_instances().front();
}

std::vector<std::uint8_t> ReadFixtureBytes() {
  std::ifstream stream(ROR_NATIVE_VISUAL_SHOWCASE_PACKAGE_FIXTURE,
                       std::ios::binary | std::ios::ate);
  Require(stream.good(), "reviewed package fixture is unavailable");
  const std::streamoff size = stream.tellg();
  Require(size > 0, "reviewed package fixture is empty");
  stream.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char *>(bytes.data()), size);
  Require(stream.good(), "reviewed package fixture could not be read");
  return bytes;
}

void TestExactCheckpointLoadsOnceAndRetainsImmutableOwners() {
  NativeVisualShowcaseSceneSourceLoadResult loaded = LoadFixture();
  Require(loaded.ok(), "reviewed forward-native package did not load");
  NativeVisualShowcaseSceneSource &source = *loaded.source;
  Require(source.package_load_count() == 1U &&
              source.package_path() ==
                  ROR_NATIVE_VISUAL_SHOWCASE_PACKAGE_FIXTURE,
          "source did not bind exactly one requested package load");
  const std::shared_ptr<const NativeRenderAssetPackage> package =
      source.package_owner();
  Require(package != nullptr &&
              package->package_sha256 == kNativeVisualShowcasePackageSha256 &&
              package->package_id == kNativeVisualShowcasePackageId &&
              package->origin_class == "project_original" &&
              package->assets.size() == 21U &&
              package->static_meshes.size() == 5U,
          "source did not retain the exact project-original checkpoint");

  GraphicsSceneFrameInput frame;
  Require(source.CaptureJoinedGraphicsFrame(frame).ok(),
          "initial native showcase capture failed");
  Require(SameSharedOwner(package, source.package_owner()) &&
              frame.assets.size() == package->assets.size(),
          "capture replaced the immutable package owner");
  for (std::size_t index = 0U; index < frame.assets.size(); ++index) {
    Require(frame.assets[index].source_asset_id ==
                    package->assets[index].source_asset_id &&
                SameSharedOwner(frame.assets[index].payload,
                                package->assets[index].payload),
            "capture copied or renumbered an immutable package payload");
  }
  Require(frame.simulation_tick == 0U && frame.simulation_time_seconds == 0.0 &&
              frame.dynamic_meshes.empty() && frame.reflection_probes.empty() &&
              !frame.continuous_particles.has_value(),
          "initial showcase simulation boundary is not deterministic");
  Require(frame.lights.size() == 1U &&
              frame.lights.front().source_light_id ==
                  kNativeVisualShowcaseSunLightId &&
              frame.lights.front().type == LightType::DIRECTIONAL &&
              frame.lights.front().intensity == 110000.0F &&
              frame.lights.front().direction == Float3{0.6F, -0.64F, 0.48F} &&
              frame.environment.analytic_sky.enabled &&
              frame.environment.analytic_sky.sun_light_id ==
                  frame.lights.front().source_light_id &&
              frame.environment.analytic_sky.zenith_radiance ==
                  Float3{0.08F, 0.12F, 0.2F} &&
              frame.environment.analytic_sky.horizon_radiance ==
                  Float3{0.3F, 0.24F, 0.18F} &&
              frame.environment.analytic_sky.ground_radiance ==
                  Float3{0.01F, 0.009F, 0.008F} &&
              frame.environment.analytic_sky.sun_disk_radiance ==
                  Float3{24.0F, 20.0F, 16.0F} &&
              frame.environment.analytic_sky.sun_angular_radius_radians ==
                  0.00465047F,
          "showcase did not publish exactly one sky-linked directional sun");
  Require(
      frame.camera.view_id == kNativeVisualShowcaseCameraViewId &&
          frame.camera.width == 1920U && frame.camera.height == 1080U &&
          frame.camera.near_plane == 0.1F && frame.camera.far_plane == 50.0F &&
          frame.camera.view_from_render.elements[0U] == 0.7868534326553345F &&
          frame.camera.view_from_render.elements[5U] == 0.8799063563346863F &&
          frame.camera.view_from_render.elements[10U] == 0.6923573017120361F &&
          frame.camera.view_from_render.elements[12U] ==
              -0.12342798709869385F &&
          frame.camera.view_from_render.elements[13U] ==
              -0.07477423548698425F &&
          frame.camera.view_from_render.elements[14U] == -14.593806266784668F &&
          frame.camera.clip_from_view.elements[0U] == 1.2062851190567017F &&
          frame.camera.clip_from_view.elements[5U] == 2.1445069313049316F,
      "explicit showcase camera lineage changed");
  source.DiscardJoinedGraphicsFrame();
}

void TestCaptureCommitDiscardIsTransactional() {
  NativeVisualShowcaseSceneSourceLoadResult loaded = LoadFixture();
  Require(loaded.ok(), "transaction fixture failed to load");
  NativeVisualShowcaseSceneSource &source = *loaded.source;

  GraphicsSceneFrameInput first;
  Require(source.CaptureJoinedGraphicsFrame(first).ok() &&
              source.has_pending_capture() && source.capture_count() == 1U,
          "capture did not open exactly one source transaction");
  GraphicsSceneFrameInput untouched;
  untouched.simulation_tick = 777U;
  const ValidationResult duplicate =
      source.CaptureJoinedGraphicsFrame(untouched);
  Require(!duplicate && duplicate.code == ValidationCode::SEQUENCE_MISMATCH &&
              untouched.simulation_tick == 777U && source.capture_count() == 1U,
          "overlapping capture modified output or source lineage");
  Require(!source.SetGatePose(NativeVisualShowcaseGatePose::MOVED) &&
              source.requested_gate_pose() ==
                  NativeVisualShowcaseGatePose::HOME,
          "pending capture allowed its gate pose to be rewritten");

  source.CommitJoinedGraphicsFrame();
  source.CommitJoinedGraphicsFrame();
  Require(!source.has_pending_capture() && source.commit_count() == 1U &&
              source.next_simulation_tick() == 1U &&
              source.committed_gate_pose() ==
                  NativeVisualShowcaseGatePose::HOME,
          "commit was not idempotent or did not advance exactly once");

  GraphicsSceneFrameInput second;
  Require(source.CaptureJoinedGraphicsFrame(second).ok() &&
              second.simulation_tick == 1U &&
              second.simulation_time_seconds ==
                  kNativeVisualShowcaseFixedStepSeconds,
          "committed simulation time did not advance by one fixed step");
  source.DiscardJoinedGraphicsFrame();
  source.DiscardJoinedGraphicsFrame();
  Require(source.discard_count() == 1U && source.next_simulation_tick() == 1U &&
              source.committed_gate_pose() ==
                  NativeVisualShowcaseGatePose::HOME,
          "discard advanced source time, pose, or count more than once");
  Require(
      !source.SetGatePose(static_cast<NativeVisualShowcaseGatePose>(255U)) &&
          source.requested_gate_pose() == NativeVisualShowcaseGatePose::HOME,
      "unknown gate pose was accepted or changed state");
}

void TestMovedEvidenceChangesOnlyGateTransform() {
  NativeVisualShowcaseSceneSourceLoadResult loaded = LoadFixture();
  Require(loaded.ok(), "moved-gate fixture failed to load");
  NativeVisualShowcaseSceneSource &source = *loaded.source;

  GraphicsSceneFrameInput home;
  Require(source.CaptureJoinedGraphicsFrame(home).ok(),
          "home gate capture failed");
  source.DiscardJoinedGraphicsFrame();
  Require(source.SetGatePose(NativeVisualShowcaseGatePose::MOVED).ok(),
          "bounded moved gate pose was rejected");
  GraphicsSceneFrameInput moved;
  Require(source.CaptureJoinedGraphicsFrame(moved).ok(),
          "moved gate capture failed");

  Require(home.version == moved.version &&
              home.simulation_tick == moved.simulation_tick &&
              home.simulation_time_seconds == moved.simulation_time_seconds &&
              home.absolute_world_origin_meters ==
                  moved.absolute_world_origin_meters &&
              home.assets.size() == moved.assets.size() &&
              home.static_meshes.size() == moved.static_meshes.size() &&
              home.lights.size() == moved.lights.size() &&
              home.camera.view_id == moved.camera.view_id &&
              home.camera.view_from_render == moved.camera.view_from_render &&
              home.camera.clip_from_view == moved.camera.clip_from_view &&
              home.environment.analytic_sky.sun_light_id ==
                  moved.environment.analytic_sky.sun_light_id,
          "moved evidence changed frame, camera, sky, or time lineage");
  for (std::size_t index = 0U; index < home.assets.size(); ++index) {
    Require(home.assets[index].source_asset_id ==
                    moved.assets[index].source_asset_id &&
                home.assets[index].material_bindings ==
                    moved.assets[index].material_bindings &&
                SameSharedOwner(home.assets[index].payload,
                                moved.assets[index].payload),
            "moved evidence changed an asset identity, binding, or owner");
  }

  const std::uint64_t gate_id = source.gate_source_object_id();
  for (std::size_t index = 0U; index < home.static_meshes.size(); ++index) {
    const GraphicsSceneStaticMeshInput &before = home.static_meshes[index];
    const GraphicsSceneStaticMeshInput &after = moved.static_meshes[index];
    Require(before.source_object_id == after.source_object_id &&
                before.mesh_source_asset_id == after.mesh_source_asset_id &&
                before.material_source_asset_id ==
                    after.material_source_asset_id &&
                before.visibility_mask == after.visibility_mask &&
                before.flags == after.flags,
            "moved evidence changed static instance metadata");
    for (std::size_t element = 0U; element < 16U; ++element) {
      const float expected =
          before.source_object_id == gate_id && element == 12U
              ? before.render_from_object.elements[element] +
                    kNativeVisualShowcaseMovedGateOffsetMeters
              : before.render_from_object.elements[element];
      Require(after.render_from_object.elements[element] == expected,
              "moved evidence changed more than gate X translation");
    }
  }
  Require(FindGate(moved, gate_id).render_from_object.elements[12U] ==
              FindGate(home, gate_id).render_from_object.elements[12U] +
                  kNativeVisualShowcaseMovedGateOffsetMeters,
          "moved gate translation did not use the bounded evidence offset");
  source.DiscardJoinedGraphicsFrame();
}

void TestSnapshotProducerAcceptsStableAndMovedFrames() {
  NativeVisualShowcaseSceneSourceLoadResult loaded = LoadFixture();
  Require(loaded.ok(), "producer fixture failed to load");
  NativeVisualShowcaseSceneSource &source = *loaded.source;

  GraphicsSceneSnapshotProducerConfiguration rejected_configuration;
  rejected_configuration.registry_id = 0x524F524E4752454AULL;
  rejected_configuration.maximum_asset_records = 1U;
  GraphicsSceneSnapshotProducer rejected_producer(rejected_configuration);
  const GraphicsSceneSnapshotProduceResult rejected =
      rejected_producer.ProduceJoinedFrame(source);
  Require(!rejected && source.capture_count() == 1U &&
              source.discard_count() == 1U && source.commit_count() == 0U &&
              source.next_simulation_tick() == 0U &&
              !source.has_pending_capture(),
          "producer rejection did not discard without advancing source state");

  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x524F524E4753434EULL;
  GraphicsSceneSnapshotProducer producer(configuration);
  const GraphicsSceneSnapshotProduceResult first =
      producer.ProduceJoinedFrame(source);
  Require(first.ok() && source.commit_count() == 1U &&
              first.production.asset_delta.has_value() &&
              first.production.asset_delta->mutations.size() == 21U &&
              first.production.scene_snapshot->mesh_instances().size() == 5U &&
              first.production.scene_snapshot->lights().size() == 1U &&
              first.production.scene_snapshot->lights().front().type ==
                  LightType::DIRECTIONAL &&
              first.production.scene_snapshot->environment()
                      .analytic_sky.sun_light_id ==
                  first.production.scene_snapshot->lights().front().light_id &&
              first.production.camera.view_id ==
                  kNativeVisualShowcaseCameraViewId,
          "snapshot producer rejected or rewrote the native showcase scene");

  const GraphicsSceneSnapshotProduceResult stable =
      producer.ProduceJoinedFrame(source);
  Require(
      stable.ok() && !stable.production.asset_delta.has_value() &&
          stable.production.diagnostics.asset_payload_full_validations == 0U &&
          stable.production.diagnostics.asset_payload_fallback_comparisons ==
              0U &&
          stable.production.scene_snapshot->simulation_tick() == 1U,
      "stable frame did not reuse immutable package owners and IDs");

  Require(source.SetGatePose(NativeVisualShowcaseGatePose::MOVED).ok(),
          "producer moved-gate request failed");
  const GraphicsSceneSnapshotProduceResult moved =
      producer.ProduceJoinedFrame(source);
  Require(moved.ok() && !moved.production.asset_delta.has_value() &&
              moved.production.diagnostics.asset_payload_full_validations ==
                  0U &&
              moved.production.scene_snapshot->simulation_tick() == 2U &&
              moved.production.scene_snapshot->lights().size() == 1U &&
              moved.production.camera.view_from_render ==
                  stable.production.camera.view_from_render,
          "moved frame changed assets, lighting, camera, or stable owner path");

  const std::uint64_t gate_id = source.gate_source_object_id();
  const MeshInstanceDescriptor &stable_gate =
      FindGate(*stable.production.scene_snapshot, gate_id);
  const MeshInstanceDescriptor &moved_gate =
      FindGate(*moved.production.scene_snapshot, gate_id);
  Require(moved_gate.previous_render_from_object ==
                  stable_gate.render_from_object &&
              moved_gate.render_from_object.elements[12U] ==
                  stable_gate.render_from_object.elements[12U] +
                      kNativeVisualShowcaseMovedGateOffsetMeters,
          "producer did not preserve exact home-to-moved gate history");
  for (const MeshInstanceDescriptor &instance :
       moved.production.scene_snapshot->mesh_instances()) {
    if (instance.instance_id != gate_id) {
      Require(instance.render_from_object ==
                  instance.previous_render_from_object,
              "moved evidence changed a non-gate transform");
    }
  }
}

void TestCurrentRt4UvTransformAdmissionRemainsExplicitlyBlocked() {
  NativeVisualShowcaseSceneSourceLoadResult loaded = LoadFixture();
  Require(loaded.ok(), "RT4 admission fixture failed to load");
  NativeVisualShowcaseSceneSource &source = *loaded.source;

  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x524F524E47555654ULL;
  GraphicsSceneSnapshotProducer producer(configuration);
  const GraphicsSceneSnapshotProduceResult produced =
      producer.ProduceJoinedFrame(source);
  Require(produced.ok() && produced.production.asset_delta.has_value(),
          "renderer-neutral producer rejected the authored UV-transform scene");
  RenderAssetRegistry registry(producer.registry_id());
  Require(registry.Apply(*produced.production.asset_delta).ok(),
          "authored package catalog could not be reconstructed");

  bool found_non_identity_scale = false;
  const ValidationResult visit = registry.VisitRecords(
      [&found_non_identity_scale](const RenderAssetRecord &record) {
        if (!record.live() || record.asset.kind != RenderAssetKind::MATERIAL) {
          return ValidationResult::Success();
        }
        const MaterialDescriptor &material =
            std::get<MaterialDescriptor>(*record.payload);
        const TextureBinding *bindings[] = {
            &material.base_color_texture, &material.metallic_roughness_texture,
            &material.normal_texture,     &material.emissive_texture,
            &material.specular_texture,
        };
        for (const TextureBinding *binding : bindings) {
          found_non_identity_scale = found_non_identity_scale ||
                                     (binding->texture.valid() &&
                                      binding->scale != Float2{1.0F, 1.0F});
        }
        return ValidationResult::Success();
      });
  Require(visit.ok() && found_non_identity_scale,
          "fixture stopped exercising authored non-identity UV scale");
  const ValidationResult admission = ValidateOgreNextN1AssetCatalog(
      registry, false, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1);
  Require(admission.code == ValidationCode::UNSUPPORTED_FEATURE &&
              admission.field == "assets.material.texture_transform",
          "current RT4/V1 UV-transform limitation was hidden or changed");
}

void TestStorageAndDigestFailuresPublishNoSource() {
  const NativeVisualShowcaseSceneSourceLoadResult missing =
      LoadNativeVisualShowcaseSceneSource(
          "this/path/must/not/exist/native-showcase.rornative");
  Require(!missing && missing.source == nullptr,
          "missing package path published a scene source");

  std::vector<std::uint8_t> bytes = ReadFixtureBytes();
  bytes.back() ^= 1U;
  const std::uintptr_t nonce = reinterpret_cast<std::uintptr_t>(bytes.data());
  const std::filesystem::path tampered =
      std::filesystem::temp_directory_path() /
      ("ror-native-showcase-tampered-" + std::to_string(nonce) + ".rornative");
  {
    std::ofstream stream(tampered, std::ios::binary | std::ios::trunc);
    Require(stream.good(), "tampered package fixture could not be created");
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    Require(stream.good(), "tampered package fixture could not be written");
  }
  const NativeVisualShowcaseSceneSourceLoadResult rejected =
      LoadNativeVisualShowcaseSceneSource(tampered.string());
  std::error_code remove_error;
  std::filesystem::remove(tampered, remove_error);
  Require(!rejected && rejected.source == nullptr && !remove_error,
          "digest-mismatched package published a source or leaked its fixture");
}

void TestCapturesNeverReopenPackageStorage() {
  const std::vector<std::uint8_t> bytes = ReadFixtureBytes();
  const std::uintptr_t nonce =
      reinterpret_cast<std::uintptr_t>(bytes.data()) ^ 0x524F524E4753544FULL;
  const std::filesystem::path one_shot =
      std::filesystem::temp_directory_path() /
      ("ror-native-showcase-one-shot-" + std::to_string(nonce) + ".rornative");
  {
    std::ofstream stream(one_shot, std::ios::binary | std::ios::trunc);
    Require(stream.good(), "one-shot package fixture could not be created");
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    Require(stream.good(), "one-shot package fixture could not be written");
  }
  NativeVisualShowcaseSceneSourceLoadResult loaded =
      LoadNativeVisualShowcaseSceneSource(one_shot.string());
  Require(loaded.ok() && loaded.source->package_load_count() == 1U,
          "one-shot package did not load exactly once");
  std::error_code remove_error;
  std::filesystem::remove(one_shot, remove_error);
  Require(!remove_error, "one-shot package could not be removed after load");

  GraphicsSceneFrameInput first;
  Require(loaded.source->CaptureJoinedGraphicsFrame(first).ok(),
          "capture reopened removed package storage");
  loaded.source->CommitJoinedGraphicsFrame();
  GraphicsSceneFrameInput second;
  Require(loaded.source->CaptureJoinedGraphicsFrame(second).ok() &&
              second.simulation_tick == 1U &&
              loaded.source->package_load_count() == 1U,
          "subsequent capture reopened or recounted package storage");
  loaded.source->DiscardJoinedGraphicsFrame();
}

} // namespace

int main() {
  TestExactCheckpointLoadsOnceAndRetainsImmutableOwners();
  TestCaptureCommitDiscardIsTransactional();
  TestMovedEvidenceChangesOnlyGateTransform();
  TestSnapshotProducerAcceptsStableAndMovedFrames();
  TestCurrentRt4UvTransformAdmissionRemainsExplicitlyBlocked();
  TestStorageAndDigestFailuresPublishNoSource();
  TestCapturesNeverReopenPackageStorage();
  std::cout << "native visual showcase scene source tests passed\n";
  return EXIT_SUCCESS;
}
