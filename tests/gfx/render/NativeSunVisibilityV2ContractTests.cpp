#include "NativeSunVisibilityV2Contract.h"
#include "OgreNextSunVisibilityV2Interop.h"
#include "OgreNextSunVisibilityV2InteropState.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::Render::NativeSunVisibilityV2Capabilities Capabilities() {
  using namespace RoR::Render;
  NativeSunVisibilityV2Capabilities value;
  value.backend = NativeDirectionalShadowBackend::METAL;
  value.supports_raytracing = true;
  value.apple_family_9 = true;
  value.same_ogre_device = true;
  value.same_ogre_queue = true;
  value.same_ogre_timeline = true;
  value.two_level_acceleration_structures = true;
  value.r16_float_visibility = true;
  value.separate_rgba16_base_and_sun_direct = true;
  value.rgba16_float_lit_composite = true;
  value.directional_self_hit_bias = true;
  return value;
}

std::vector<RoR::Render::NativeSunVisibilityV2InstanceSelection>
SmokeSelection() {
  using namespace RoR::Render;
  return {
      {1U, 101U, NATIVE_SUN_VISIBILITY_V2_RECEIVER |
                       NATIVE_SUN_VISIBILITY_V2_OPAQUE |
                       NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE},
      {2U, 102U, NATIVE_SUN_VISIBILITY_V2_RECEIVER |
                       NATIVE_SUN_VISIBILITY_V2_OPAQUE |
                       NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE},
      {3U, 103U, NATIVE_SUN_VISIBILITY_V2_RECEIVER |
                       NATIVE_SUN_VISIBILITY_V2_CASTER |
                       NATIVE_SUN_VISIBILITY_V2_OPAQUE |
                       NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE},
      {4U, 104U, NATIVE_SUN_VISIBILITY_V2_ALPHA_LAYER |
                       NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE},
      {5U, 105U, NATIVE_SUN_VISIBILITY_V2_RT_INERT |
                       NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE},
  };
}

RoR::Render::NativeSunVisibilityV2FrameContract CompleteFrame() {
  using namespace RoR::Render;
  NativeSunVisibilityV2FrameContract frame;
  frame.capabilities = Capabilities();
  frame.frame_id = 1U;
  frame.snapshot_id = 2U;
  frame.view_id = 3U;
  NativeSunVisibilityV2ScenePlan plan;
  Require(static_cast<bool>(
              TryBuildNativeSunVisibilityV2ScenePlan(SmokeSelection(), plan)),
          "frame scene-plan setup failed");
  frame.scene_plan_digest = plan.scene_plan_digest;
  frame.width = 8U;
  frame.height = 4U;
  frame.selected_instance_count = 5U;
  frame.admitted_instance_count = 3U;
  frame.receiver_count = 3U;
  frame.caster_count = 1U;
  frame.excluded_instance_count = 2U;
  frame.excluded_alpha_layer_count = 1U;
  frame.excluded_rt_inert_count = 1U;
  frame.raster_visible_receiver_count = 3U;
  frame.raster_visible_caster_count = 1U;
  frame.raster_visible_caster_receiver_count = 1U;
  frame.unique_mesh_count = 3U;
  frame.blas_build_count = 3U;
  frame.tlas_build_count = 1U;
  frame.tlas_instance_count = 3U;
  frame.blas_resident_bytes = 4096U;
  frame.tlas_resident_bytes = 2048U;
  frame.acceleration_structure_scratch_peak_bytes = 1024U;
  frame.primary_ray_count = 32U;
  frame.secondary_sun_visibility_ray_count = 32U;
  frame.visible_visibility_texel_count = 24U;
  frame.occluded_visibility_texel_count = 8U;
  frame.visibility_texel_count = 32U;
  frame.composite_pixel_count = 32U;
  frame.opaque_alpha_pixel_count = 32U;
  frame.acceleration_structure_encode_nanoseconds = 1U;
  frame.ray_composite_encode_nanoseconds = 1U;
  frame.gpu_execution_nanoseconds = 1U;
  frame.minimum_ray_distance_meters = 0.001F;
  frame.self_hit_origin_bias_multiplier = 2.0F;
  frame.shader_lock_verified = true;
  frame.base_hdr_preserved_under_occlusion = true;
  frame.sun_direct_only_visibility_modulation = true;
  frame.output_opaque_alpha = true;
  frame.submission_completed = true;
  frame.acceptance_samples_validated = true;
  frame.acceptance_caster_instance_id = 3U;
  frame.acceptance_caster_transform_revision = 7U;
  frame.result.stage = NativeSunVisibilityV2Stage::COMPLETE;
  frame.result.frame_id = frame.frame_id;
  frame.result.snapshot_id = frame.snapshot_id;
  const NativeDirectionalShadowRgba16Pixel base{
      {0x3400U, 0x3800U, 0x3a00U, 0x3c00U}};
  const NativeDirectionalShadowRgba16Pixel direct{
      {0x3400U, 0x3400U, 0x3400U, 0x0000U}};
  Require(static_cast<bool>(TryBuildNativeSunVisibilityV2SampleOracle(
              NativeDirectionalShadowVisibility::VISIBLE, base, direct,
              frame.acceptance_samples[0U])),
          "visible oracle setup failed");
  Require(static_cast<bool>(TryBuildNativeSunVisibilityV2SampleOracle(
              NativeDirectionalShadowVisibility::OCCLUDED, base, direct,
              frame.acceptance_samples[1U])),
          "occluded oracle setup failed");
  Require(static_cast<bool>(TryBuildNativeSunVisibilityV2SampleOracle(
              NativeDirectionalShadowVisibility::VISIBLE, base, direct,
              frame.acceptance_samples[2U])),
          "visible caster oracle setup failed");
  frame.acceptance_samples[0U].primary_hit_instance_id = 1U;
  frame.acceptance_samples[0U].primary_hit_is_receiver = true;
  frame.acceptance_samples[1U].primary_hit_instance_id = 1U;
  frame.acceptance_samples[1U].secondary_blocker_instance_id = 3U;
  frame.acceptance_samples[1U].primary_hit_is_receiver = true;
  frame.acceptance_samples[2U].primary_hit_instance_id = 3U;
  frame.acceptance_samples[2U].primary_hit_is_receiver = true;
  frame.acceptance_samples[2U].primary_hit_is_caster = true;
  return frame;
}

RoR::Render::NativeObjectToken ImageToken(std::uint64_t value) {
  using namespace RoR::Render;
  NativeObjectToken token;
  token.api = NativeGraphicsApi::METAL;
  token.kind = NativeObjectKind::IMAGE;
  token.context_id = 77U;
  token.value = value;
  token.generation = 1U;
  return token;
}

RoR::Render::NativeObjectToken TimelineToken(std::uint64_t value) {
  using namespace RoR::Render;
  NativeObjectToken token = ImageToken(value);
  token.kind = NativeObjectKind::TIMELINE_SYNC;
  return token;
}

RoR::Render::NativeContextExport MetalContext() {
  using namespace RoR::Render;
  NativeContextExport context;
  context.native_api = NativeGraphicsApi::METAL;
  context.context_id = 77U;
  context.device.api = NativeGraphicsApi::METAL;
  context.device.kind = NativeObjectKind::DEVICE;
  context.device.context_id = context.context_id;
  context.device.value = 1U;
  context.device.generation = 1U;
  context.graphics_queue.api = NativeGraphicsApi::METAL;
  context.graphics_queue.kind = NativeObjectKind::QUEUE;
  context.graphics_queue.context_id = context.context_id;
  context.graphics_queue.value = 2U;
  context.graphics_queue.generation = 1U;
  return context;
}

std::shared_ptr<const RoR::Render::SceneSnapshot> Snapshot() {
  using namespace RoR::Render;
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 2U;
  descriptor.asset_registry_id = 1U;
  descriptor.asset_sequence = 1U;
  const SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "V2 interop snapshot setup failed");
  return created.snapshot;
}

RoR::Render::OgreNextSunVisibilityV2ImageBinding Binding(
    RoR::Render::OgreNextSunVisibilityV2ImageRole role,
    RoR::Render::OgreNextSunVisibilityV2ImageFormat format,
    std::uint64_t value) {
  using namespace RoR::Render;
  OgreNextSunVisibilityV2ImageBinding binding;
  binding.role = role;
  binding.format = format;
  binding.usage =
      NativeImageUsage::COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE;
  binding.image = ImageToken(value);
  return binding;
}

class TestPresentationContinuation final
    : public RoR::Render::OgreNextSunVisibilityV2PresentationContinuation {
public:
  RoR::Render::NativeSunVisibilityV2Result ContinueFromLitHdr(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      std::uint64_t view_id,
      std::uintptr_t ogre_lit_hdr_texture) override {
    ++call_count;
    observed_view_id = view_id;
    observed_lit_hdr_texture = ogre_lit_hdr_texture;
    RoR::Render::NativeSunVisibilityV2Result result;
    result.code = result_code;
    result.stage = RoR::Render::NativeSunVisibilityV2Stage::PRESENT_CONTINUATION;
    result.frame_id = frame_id;
    result.snapshot_id = snapshot_id;
    result.detail = result_detail;
    return result;
  }

  RoR::Render::NativeSunVisibilityV2Code result_code =
      RoR::Render::NativeSunVisibilityV2Code::OK;
  std::string result_detail = "ok";
  std::uint32_t call_count = 0U;
  std::uint64_t observed_view_id = 0U;
  std::uintptr_t observed_lit_hdr_texture = 0U;
};

RoR::Render::OgreNextSunVisibilityV2FrameImageBinding RawBinding(
    const RoR::Render::OgreNextSunVisibilityV2ImageSetRequest &request,
    TestPresentationContinuation &continuation) {
  using namespace RoR::Render;
  OgreNextSunVisibilityV2FrameImageBinding binding;
  binding.frame_id = request.frame_id;
  binding.snapshot_id = request.snapshot_id;
  binding.view_id = request.view_id;
  binding.scene_snapshot = request.scene_snapshot;
  binding.view = request.view;
  binding.width = request.width;
  binding.height = request.height;
  binding.ogre_base_hdr_texture = 101U;
  binding.ogre_sun_direct_hdr_texture = 102U;
  binding.ogre_visibility_texture = 103U;
  binding.ogre_lit_hdr_texture = 104U;
  binding.presentation_continuation = &continuation;
  return binding;
}

} // namespace

int main() {
  using namespace RoR::Render;

  OgreNextSunVisibilityV2ImageSetRequest image_request;
  image_request.frame_id = 1U;
  image_request.snapshot_id = 2U;
  image_request.view_id = 3U;
  image_request.scene_snapshot = Snapshot();
  image_request.width = 8U;
  image_request.height = 4U;
  image_request.view.view_id = image_request.view_id;
  image_request.view.width = image_request.width;
  image_request.view.height = image_request.height;
  OgreNextSunVisibilityV2ImageSetExport image_set;
  image_set.export_id = 4U;
  image_set.frame_id = image_request.frame_id;
  image_set.snapshot_id = image_request.snapshot_id;
  image_set.view_id = image_request.view_id;
  image_set.scene_snapshot = image_request.scene_snapshot;
  image_set.view = image_request.view;
  image_set.width = image_request.width;
  image_set.height = image_request.height;
  image_set.base_hdr = Binding(
      OgreNextSunVisibilityV2ImageRole::BASE_HDR_RGBA16,
      OgreNextSunVisibilityV2ImageFormat::RGBA16_FLOAT, 10U);
  image_set.sun_direct_hdr = Binding(
      OgreNextSunVisibilityV2ImageRole::SUN_DIRECT_HDR_RGBA16,
      OgreNextSunVisibilityV2ImageFormat::RGBA16_FLOAT, 11U);
  image_set.visibility = Binding(
      OgreNextSunVisibilityV2ImageRole::VISIBILITY_R16,
      OgreNextSunVisibilityV2ImageFormat::R16_FLOAT, 12U);
  image_set.lit_hdr = Binding(
      OgreNextSunVisibilityV2ImageRole::LIT_HDR_RGBA16,
      OgreNextSunVisibilityV2ImageFormat::RGBA16_FLOAT, 13U);
  Require(static_cast<bool>(ValidateOgreNextSunVisibilityV2ImageSetExport(
              image_request, image_set, MetalContext())),
          "valid four-role Metal image set was rejected");
  NativeFrameSynchronization synchronization;
  synchronization.frame_id = image_set.frame_id;
  synchronization.snapshot_id = image_set.snapshot_id;
  synchronization.interop_queue = MetalContext().graphics_queue;
  synchronization.interop_queue_family = kInvalidNativeQueueFamily;
  synchronization.frontend_release_state =
      NativeGeometryBufferState::READ_ONLY_ACCELERATION_STRUCTURE_BUILD;
  synchronization.external_return_state =
      synchronization.frontend_release_state;
  synchronization.frontend_image_release_state =
      NativeImageState::GENERAL_READ_WRITE;
  synchronization.external_image_return_state =
      NativeImageState::GENERAL_READ_WRITE;
  synchronization.frontend_complete_timeline = TimelineToken(20U);
  synchronization.frontend_complete_value = 1U;
  synchronization.external_complete_timeline = TimelineToken(20U);
  synchronization.external_complete_value = 2U;
  Require(static_cast<bool>(ValidateOgreNextSunVisibilityV2FrameTransaction(
              image_request, image_set, MetalContext(), synchronization,
              true)),
          "valid same-Ogre-queue V2 image transaction was rejected");
  synchronization.snapshot_id = 99U;
  Require(!ValidateOgreNextSunVisibilityV2FrameTransaction(
              image_request, image_set, MetalContext(), synchronization,
              true),
          "cross-snapshot V2 image/timeline transaction was accepted");
  image_set.visibility.image = image_set.lit_hdr.image;
  ++image_set.visibility.image.generation;
  Require(!ValidateOgreNextSunVisibilityV2ImageSetExport(
              image_request, image_set, MetalContext()),
          "aliased V2 image roles were accepted after generation spoofing");

  image_set.visibility = Binding(
      OgreNextSunVisibilityV2ImageRole::VISIBILITY_R16,
      OgreNextSunVisibilityV2ImageFormat::R16_FLOAT, 12U);
  synchronization.snapshot_id = image_request.snapshot_id;
  TestPresentationContinuation continuation;
  const OgreNextSunVisibilityV2FrameImageBinding raw_binding =
      RawBinding(image_request, continuation);
  OgreNextSunVisibilityV2InteropState image_state;
  Require(image_state.Initialize(MetalContext()).code ==
              NativeSunVisibilityV2Code::OK,
          "V2 image-state initialization failed");
  Require(image_state.PreparePublish(raw_binding, image_set).code ==
              NativeSunVisibilityV2Code::OK &&
              image_state.CanCommitPrepared(image_request.frame_id,
                                             image_request.snapshot_id) &&
              !image_state.CanCommitPrepared(image_request.frame_id + 1U,
                                              image_request.snapshot_id),
          "V2 prepared publication did not preserve exact lineage");
  OgreNextSunVisibilityV2ImageSetExport leased_images;
  Require(image_state.Acquire(image_request, leased_images).code ==
              NativeSunVisibilityV2Code::RESOURCE_STALE,
          "uncommitted V2 image publication was acquired");
  image_state.CommitPrepared();
  Require(image_state.Acquire(image_request, leased_images).code ==
              NativeSunVisibilityV2Code::OK &&
              image_state.HasOutstandingLease() &&
              leased_images.export_id != image_set.export_id,
          "atomic V2 image set was not acquired as one new lease");
  OgreNextSunVisibilityV2ImageSetExport spoofed_lease = leased_images;
  ++spoofed_lease.visibility.image.generation;
  Require(image_state.ValidateLease(spoofed_lease).code ==
              NativeSunVisibilityV2Code::RESOURCE_STALE,
          "V2 image lease accepted spoofed token generation");
  Require(image_state.ContinuePresentation(leased_images, synchronization)
                  .code == NativeSunVisibilityV2Code::RESOURCE_STALE,
          "V2 LitHdr continuation ran before the external frame");
  image_state.ObserveExternalFrameBegun(synchronization);
  image_state.Release(leased_images.export_id);
  Require(image_state.HasOutstandingLease(),
          "begun V2 image lease released before rollback or continuation");
  Require(image_state.ContinuePresentation(leased_images, synchronization)
                  .code == NativeSunVisibilityV2Code::RESOURCE_STALE,
          "V2 LitHdr continuation ran before EndExternalFrame");
  image_state.ObserveExternalFrameEnded(synchronization);
  const NativeSunVisibilityV2Result continued =
      image_state.ContinuePresentation(leased_images, synchronization);
  Require(continued.code == NativeSunVisibilityV2Code::OK &&
              continued.stage ==
                  NativeSunVisibilityV2Stage::PRESENT_CONTINUATION &&
              continuation.call_count == 1U &&
              continuation.observed_view_id == image_request.view_id &&
              continuation.observed_lit_hdr_texture ==
                  raw_binding.ogre_lit_hdr_texture,
          "post-external V2 LitHdr continuation lost its exact texture");
  Require(image_state.ContinuePresentation(leased_images, synchronization)
                  .code == NativeSunVisibilityV2Code::RESOURCE_STALE &&
              continuation.call_count == 1U,
          "V2 LitHdr continuation executed more than once");
  image_state.Release(leased_images.export_id);
  Require(!image_state.HasOutstandingLease(),
          "completed V2 image lease was not released");

  Require(image_state.Acquire(image_request, leased_images).code ==
              NativeSunVisibilityV2Code::OK,
          "V2 failed-continuation lease setup failed");
  image_state.ObserveExternalFrameBegun(synchronization);
  image_state.ObserveExternalFrameEnded(synchronization);
  continuation.result_code = NativeSunVisibilityV2Code::BACKEND_FAILURE;
  continuation.result_detail = "injected-continuation-failure";
  const NativeSunVisibilityV2Result failed_continuation =
      image_state.ContinuePresentation(leased_images, synchronization);
  Require(failed_continuation.code == continuation.result_code &&
              failed_continuation.stage ==
                  NativeSunVisibilityV2Stage::PRESENT_CONTINUATION &&
              failed_continuation.detail == continuation.result_detail &&
              continuation.call_count == 2U,
          "V2 failed continuation did not preserve its exact result");
  Require(image_state.ContinuePresentation(leased_images, synchronization)
                  .code == NativeSunVisibilityV2Code::RESOURCE_STALE &&
              continuation.call_count == 2U,
          "V2 failed continuation was invoked more than once");
  image_state.Release(leased_images.export_id);
  Require(!image_state.HasOutstandingLease(),
          "failed V2 continuation lease was not released");
  continuation.result_code = NativeSunVisibilityV2Code::OK;
  continuation.result_detail = "ok";

  Require(image_state.Acquire(image_request, leased_images).code ==
              NativeSunVisibilityV2Code::OK,
          "V2 rollback lease setup failed");
  NativeSunVisibilityV2Result image_failure;
  image_failure.code = NativeSunVisibilityV2Code::RESOURCE_STALE;
  image_failure.stage = NativeSunVisibilityV2Stage::IMAGE_EXPORT;
  image_failure.frame_id = image_request.frame_id;
  image_failure.snapshot_id = image_request.snapshot_id;
  image_failure.detail = "lit-hdr-generation-stale";
  const NativeSunVisibilityV2Result image_rollback =
      image_state.AbortBeforeSubmission(leased_images, synchronization,
                                        image_failure);
  Require(image_rollback.code == image_failure.code &&
              image_rollback.stage == image_failure.stage &&
              image_rollback.detail == image_failure.detail,
          "V2 image rollback lost the exact result stage/detail");
  image_state.Release(leased_images.export_id);
  Require(!image_state.HasOutstandingLease(),
          "rolled-back V2 image lease was not released");
  image_state.Reset();

  const std::vector<NativeSunVisibilityV2InstanceSelection> selection =
      SmokeSelection();
  NativeSunVisibilityV2ScenePlan plan;
  Require(static_cast<bool>(
              TryBuildNativeSunVisibilityV2ScenePlan(selection, plan)),
          "valid road/gate/exclusion plan was rejected");
  Require(plan.admitted_instances.size() == 3U &&
              plan.receiver_count == 3U && plan.caster_count == 1U &&
              plan.excluded_alpha_layer_count == 1U &&
              plan.excluded_decal_count == 0U &&
              plan.excluded_rt_inert_count == 1U &&
              plan.raster_visible_receiver_count == 3U &&
              plan.raster_visible_caster_count == 1U &&
              plan.unique_mesh_ids.size() == 3U &&
              plan.scene_plan_digest != 0U,
          "scene plan did not exclude alpha/decal/RT-inert instances exactly");
  auto digest_mutation = selection;
  digest_mutation[4U].mesh_id = 106U;
  NativeSunVisibilityV2ScenePlan changed_plan;
  Require(static_cast<bool>(TryBuildNativeSunVisibilityV2ScenePlan(
              digest_mutation, changed_plan)) &&
              changed_plan.scene_plan_digest != plan.scene_plan_digest,
          "scene-plan digest did not bind an excluded selection entry");

  NativeSunVisibilityV2ScenePlan sentinel = plan;
  auto mutation = selection;
  mutation[3U].flags |= NATIVE_SUN_VISIBILITY_V2_OPAQUE;
  Require(!TryBuildNativeSunVisibilityV2ScenePlan(mutation, sentinel) &&
              sentinel.admitted_instances.size() ==
                  plan.admitted_instances.size() &&
              sentinel.admitted_instances.front().instance_id ==
                  plan.admitted_instances.front().instance_id &&
              sentinel.admitted_instances.back().instance_id ==
                  plan.admitted_instances.back().instance_id,
          "ambiguous opaque alpha layer mutated a valid plan");
  mutation = selection;
  mutation[1U].instance_id = 1U;
  Require(!TryBuildNativeSunVisibilityV2ScenePlan(mutation, sentinel),
          "duplicate instance identifiers were accepted");
  mutation = selection;
  mutation[0U].flags = NATIVE_SUN_VISIBILITY_V2_RECEIVER;
  Require(!TryBuildNativeSunVisibilityV2ScenePlan(mutation, sentinel),
          "receiver without explicit opacity was accepted");
  mutation = selection;
  mutation[2U].flags = NATIVE_SUN_VISIBILITY_V2_CASTER |
                       NATIVE_SUN_VISIBILITY_V2_OPAQUE |
                       NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE;
  Require(!TryBuildNativeSunVisibilityV2ScenePlan(mutation, sentinel),
          "raster-visible caster-only gate was admitted without surface identity");

  NativeSunVisibilityV2FrameContract frame = CompleteFrame();
  Require(static_cast<bool>(
              ValidateNativeSunVisibilityV2FrameContract(frame)),
          "complete V2 frame contract was rejected");
  Require(frame.acceptance_samples[1U].lit_hdr_rgba16.channels ==
              frame.acceptance_samples[1U].base_hdr_rgba16.channels,
          "occlusion did not preserve BaseHdr including ambient/sky/emissive");
  Require(frame.acceptance_samples[0U].lit_hdr_rgba16.channels !=
              frame.acceptance_samples[0U].base_hdr_rgba16.channels,
          "visible sun direct did not affect LitHdr");
  Require(IsCanonicalNativeSunVisibilityV2R16(
              kNativeDirectionalShadowOccludedR16) &&
              IsCanonicalNativeSunVisibilityV2R16(
                  kNativeDirectionalShadowVisibleR16) &&
              !IsCanonicalNativeSunVisibilityV2R16(0x8000U),
          "negative-zero visibility escaped the bit-exact R16 contract");

  frame.production_gpu_content_readbacks = 1U;
  Require(!ValidateNativeSunVisibilityV2FrameContract(frame),
          "production content readback was admitted");
  frame = CompleteFrame();
  frame.capabilities.apple_family_9 = false;
  Require(!ValidateNativeSunVisibilityV2FrameContract(frame),
          "missing Apple family 9 gate was admitted");
  frame = CompleteFrame();
  frame.secondary_sun_visibility_ray_count = 33U;
  Require(!ValidateNativeSunVisibilityV2FrameContract(frame),
          "impossible secondary-ray count was admitted");
  frame = CompleteFrame();
  frame.blas_build_count = 0U;
  frame.blas_cache_hit_count = 3U;
  frame.tlas_build_count = 0U;
  frame.tlas_refit_count = 1U;
  Require(static_cast<bool>(ValidateNativeSunVisibilityV2FrameContract(frame)),
          "persistent BLAS cache hits and TLAS refit were rejected");
  frame.blas_cache_hit_count = 2U;
  Require(!ValidateNativeSunVisibilityV2FrameContract(frame),
          "unique mesh without a build, refit, or cache hit was accepted");
  frame = CompleteFrame();
  frame.blas_build_count = 0U;
  frame.blas_cache_hit_count = 3U;
  frame.tlas_build_count = 0U;
  frame.tlas_cache_hit_count = 1U;
  Require(static_cast<bool>(ValidateNativeSunVisibilityV2FrameContract(frame)),
          "unchanged-frame persistent BLAS/TLAS cache hits were rejected");
  frame.tlas_cache_hit_count = 0U;
  Require(!ValidateNativeSunVisibilityV2FrameContract(frame),
          "unchanged frame without TLAS build, refit, or cache hit was accepted");
  frame = CompleteFrame();
  frame.acceptance_samples[1U].lit_hdr_rgba16.channels[0U] = 0U;
  Require(!ValidateNativeSunVisibilityV2FrameContract(frame),
          "V1-style whole-raster shadowing was admitted by V2");
  frame = CompleteFrame();
  frame.acceptance_samples[2U].primary_hit_is_caster = false;
  Require(!ValidateNativeSunVisibilityV2FrameContract(frame),
          "visible gate pixel was allowed to name a road surface behind it");
  frame = CompleteFrame();
  frame.acceptance_samples_validated = false;
  frame.acceptance_samples = {};
  Require(static_cast<bool>(ValidateNativeSunVisibilityV2FrameContract(frame)),
          "zero-readback production contract was rejected");

  const NativeSunVisibilityV2FrameContract first_frame = CompleteFrame();
  frame = first_frame;
  Require(static_cast<bool>(
              ValidateNativeSunVisibilityV2FirstFrameSmokeContract(frame)),
          "exact project-owned first-frame smoke was rejected");
  frame.frame_id = 2U;
  frame.snapshot_id = 4U;
  frame.result.frame_id = frame.frame_id;
  frame.result.snapshot_id = frame.snapshot_id;
  frame.acceptance_caster_transform_revision = 8U;
  frame.blas_build_count = 0U;
  frame.blas_cache_hit_count = 3U;
  frame.tlas_build_count = 0U;
  frame.tlas_refit_count = 1U;
  Require(static_cast<bool>(
              ValidateNativeSunVisibilityV2MovedCasterSmokeContract(
                  first_frame, frame)),
          "moved-gate cache reuse and TLAS refit smoke was rejected");

  NativeSunVisibilityV2FrameContract drift = frame;
  drift.excluded_alpha_layer_count = 0U;
  drift.excluded_decal_count = 2U;
  drift.excluded_rt_inert_count = 0U;
  Require(!ValidateNativeSunVisibilityV2MovedCasterSmokeContract(first_frame,
                                                                  drift),
          "moved-gate smoke accepted two decals instead of exact exclusions");
  drift = frame;
  drift.raster_visible_receiver_count = 1U;
  Require(!ValidateNativeSunVisibilityV2MovedCasterSmokeContract(first_frame,
                                                                  drift),
          "moved-gate smoke accepted incomplete raster-visible receivers");
  drift = frame;
  drift.acceptance_caster_instance_id = 999U;
  drift.acceptance_samples[1U].secondary_blocker_instance_id = 999U;
  drift.acceptance_samples[2U].primary_hit_instance_id = 999U;
  Require(!ValidateNativeSunVisibilityV2MovedCasterSmokeContract(first_frame,
                                                                  drift),
          "moved-gate smoke accepted blocker/sample identity 999");
  drift = frame;
  ++drift.scene_plan_digest;
  Require(!ValidateNativeSunVisibilityV2MovedCasterSmokeContract(first_frame,
                                                                  drift),
          "moved-gate smoke accepted scene-plan drift");
  drift = frame;
  drift.acceptance_caster_transform_revision =
      first_frame.acceptance_caster_transform_revision;
  Require(!ValidateNativeSunVisibilityV2MovedCasterSmokeContract(first_frame,
                                                                  drift),
          "moved-gate smoke accepted an unchanged caster transform revision");

  NativeSunVisibilityV2Result timeout;
  timeout.code = NativeSunVisibilityV2Code::TIMEOUT;
  timeout.stage = NativeSunVisibilityV2Stage::EXTERNAL_COMPLETION;
  timeout.frame_id = 44U;
  timeout.snapshot_id = 55U;
  timeout.detail = "metal-command-timeout";
  Require(ValidateNativeSunVisibilityV2Result(timeout),
          "timeout lost its exact failure stage");
  timeout.stage = NativeSunVisibilityV2Stage::COMPLETE;
  Require(!ValidateNativeSunVisibilityV2Result(timeout),
          "failed result forged successful completion stage");
  timeout.stage = static_cast<NativeSunVisibilityV2Stage>(255U);
  Require(!ValidateNativeSunVisibilityV2Result(timeout),
          "unknown failure stage was accepted");
  timeout.stage = NativeSunVisibilityV2Stage::EXTERNAL_COMPLETION;
  timeout.detail = "Driver said: timeout";
  Require(!ValidateNativeSunVisibilityV2Result(timeout),
          "unbounded driver prose was accepted as a stable detail token");

  NativeSunVisibilityV2LifecycleTracker lifecycle;
  Require(lifecycle.Initialize(), "lifecycle initialization failed");
  Require(lifecycle.BeginFrame(10U, 20U, 96U, 64U),
          "initial extent was rejected");
  NativeSunVisibilityV2Result pre_submit;
  pre_submit.code = NativeSunVisibilityV2Code::RESOURCE_STALE;
  pre_submit.stage = NativeSunVisibilityV2Stage::IMAGE_EXPORT;
  pre_submit.frame_id = 10U;
  pre_submit.snapshot_id = 20U;
  pre_submit.detail = "lit-hdr-generation-stale";
  Require(lifecycle.RollbackBeforeSubmission(pre_submit) &&
              lifecycle.state() == NativeSunVisibilityV2LifecycleState::READY &&
              lifecycle.rollback_count() == 1U &&
              lifecycle.last_result().stage ==
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT &&
              lifecycle.last_result().detail == "lit-hdr-generation-stale",
          "pre-submit rollback lost its exact stage/detail");
  Require(lifecycle.BeginFrame(11U, 21U, 128U, 72U) &&
              lifecycle.MarkSubmitted() && lifecycle.Complete() &&
              lifecycle.width() == 128U && lifecycle.height() == 72U,
          "completed resize did not commit its new extent");
  Require(!lifecycle.BeginFrame(11U, 23U, 128U, 72U),
          "completed V2 frame identity was reused");
  Require(lifecycle.BeginFrame(12U, 22U, 160U, 90U) &&
              lifecycle.MarkSubmitted(),
          "timeout setup failed");
  NativeSunVisibilityV2Result submitted_timeout;
  submitted_timeout.code = NativeSunVisibilityV2Code::TIMEOUT;
  submitted_timeout.stage = NativeSunVisibilityV2Stage::EXTERNAL_COMPLETION;
  submitted_timeout.frame_id = 12U;
  submitted_timeout.snapshot_id = 22U;
  submitted_timeout.detail = "metal-command-timeout";
  Require(lifecycle.ObserveSubmittedFault(submitted_timeout) &&
              !lifecycle.Shutdown(false) && lifecycle.width() == 128U &&
              lifecycle.height() == 72U,
          "timeout did not retain the last complete extent and live work");
  Require(lifecycle.Shutdown(true) &&
              lifecycle.last_result().code ==
                  NativeSunVisibilityV2Code::TIMEOUT,
          "bounded timeout recovery lost its exact result");

  NativeSunVisibilityV2LifecycleTracker device_loss;
  Require(device_loss.Initialize() &&
              device_loss.BeginFrame(30U, 40U, 96U, 64U) &&
              device_loss.MarkSubmitted(),
          "device-loss setup failed");
  NativeSunVisibilityV2Result lost;
  lost.code = NativeSunVisibilityV2Code::DEVICE_LOST;
  lost.stage = NativeSunVisibilityV2Stage::EXTERNAL_COMPLETION;
  lost.frame_id = 30U;
  lost.snapshot_id = 40U;
  lost.detail = "metal-command-buffer-device-removed";
  Require(device_loss.ObserveSubmittedFault(lost) &&
              device_loss.state() ==
                  NativeSunVisibilityV2LifecycleState::FAULTED &&
              device_loss.Shutdown(true),
          "device loss did not follow the submitted-fault teardown path");

  return EXIT_SUCCESS;
}
