/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Policy.h"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>

namespace {

using namespace RoR::Render;

static_assert(kOgreNextN1OgreLayerVisibilityMask == UINT32_C(0xC0000000),
              "N1 must reserve Ogre layer bits");
static_assert(kOgreNextN1AuthoredVisibilityMask == UINT32_C(0x3FFFFFFF),
              "N1 authored visibility must match Ogre's assignable range");
static_assert(kOgreNextRt4PccVisibilityMask == UINT32_C(0x30000000),
              "RT4 must reserve both PCC bits");
static_assert(kOgreNextRt4InternalVisibilityMask == UINT32_C(0xF0000000),
              "RT4 must reserve PCC and Ogre layer bits");
static_assert(kOgreNextRt4AuthoredVisibilityMask == UINT32_C(0x0FFFFFFF),
              "RT4 authored visibility must match Ogre's public mask range");

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "Ogre-Next N1 policy test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RenderAssetId Id(std::uint64_t low) {
  return RenderAssetId::FromWords(0x4E315F4153534554ULL, low);
}

RenderAssetReference Ref(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, Id(low), 1U);
}

MeshResourceDescriptor MakeMesh() {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "N1 triangle";
  mesh.local_bounds.minimum = {-1.0F, -1.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {
      {-1.0F, -1.0F, 0.0F},
      {1.0F, -1.0F, 0.0F},
      {0.0F, 1.0F, 0.0F},
  };
  mesh.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

MeshResourceDescriptor MakeModernMesh() {
  MeshResourceDescriptor mesh = MakeMesh();
  mesh.debug_name = "RT4/V1 textured triangle";
  mesh.tangents.assign(3U, Float4{1.0F, 0.0F, 0.0F, 1.0F});
  mesh.texture_coordinates_0 = {
      {0.0F, 1.0F},
      {1.0F, 1.0F},
      {0.5F, 0.0F},
  };
  return mesh;
}

MaterialDescriptor MakeMaterial() {
  MaterialDescriptor material;
  material.debug_name = "N1 blue metal";
  material.base_color_factor = {0.08F, 0.35F, 0.9F, 1.0F};
  material.metallic_factor = 0.25F;
  material.roughness_factor = 0.3F;
  return material;
}

RenderAssetDelta MakeCatalogDelta(std::uint64_t registry_id,
                                  MeshResourceDescriptor mesh = MakeMesh(),
                                  MaterialDescriptor material = MakeMaterial()) {
  RenderAssetDelta delta;
  delta.registry_id = registry_id;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  RenderAssetMutation mesh_mutation;
  mesh_mutation.asset = Ref(RenderAssetKind::MESH, 1U);
  mesh_mutation.payload = std::move(mesh);
  delta.mutations.push_back(std::move(mesh_mutation));
  RenderAssetMutation material_mutation;
  material_mutation.asset = Ref(RenderAssetKind::MATERIAL, 2U);
  material_mutation.payload = std::move(material);
  delta.mutations.push_back(std::move(material_mutation));
  return delta;
}

TextureResourceDescriptor MakeRgba8Texture(TextureColorSpace color_space,
                                           std::uint8_t red,
                                           std::uint8_t green,
                                           std::uint8_t blue) {
  TextureResourceDescriptor texture;
  texture.debug_name = "RT4/V1 one-pixel texture";
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = color_space;
  texture.width = 1U;
  texture.height = 1U;
  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.layer_pitch_bytes = 4U;
  mip.bytes = {red, green, blue, 255U};
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

RenderAssetDelta MakeModernCatalogDelta(std::uint64_t registry_id) {
  MaterialDescriptor material = MakeMaterial();
  material.debug_name = "RT4/V1 complete textured PBS";
  material.base_color_texture.texture = Ref(RenderAssetKind::TEXTURE, 3U);
  material.base_color_texture.sampler = Ref(RenderAssetKind::SAMPLER, 7U);
  material.metallic_roughness_texture.texture =
      Ref(RenderAssetKind::TEXTURE, 4U);
  material.metallic_roughness_texture.sampler =
      Ref(RenderAssetKind::SAMPLER, 7U);
  material.normal_texture.texture = Ref(RenderAssetKind::TEXTURE, 5U);
  material.normal_texture.sampler = Ref(RenderAssetKind::SAMPLER, 7U);
  material.emissive_texture.texture = Ref(RenderAssetKind::TEXTURE, 6U);
  material.emissive_texture.sampler = Ref(RenderAssetKind::SAMPLER, 7U);

  RenderAssetDelta delta =
      MakeCatalogDelta(registry_id, MakeModernMesh(), material);
  const TextureResourceDescriptor textures[] = {
      MakeRgba8Texture(TextureColorSpace::SRGB, 180U, 80U, 30U),
      MakeRgba8Texture(TextureColorSpace::LINEAR, 255U, 96U, 210U),
      // (180, 128) reconstructs +Z ~= 0.911282; B=244 decodes to
      // 0.913725, within the exact half-UNORM-step admission tolerance.
      MakeRgba8Texture(TextureColorSpace::LINEAR, 180U, 128U, 244U),
      MakeRgba8Texture(TextureColorSpace::SRGB, 20U, 10U, 5U),
  };
  for (std::size_t index = 0U; index < 4U; ++index) {
    RenderAssetMutation texture;
    texture.asset = Ref(RenderAssetKind::TEXTURE, 3U + index);
    texture.payload = textures[index];
    delta.mutations.push_back(std::move(texture));
  }
  SamplerResourceDescriptor sampler;
  sampler.debug_name = "RT4/V1 trilinear repeat";
  RenderAssetMutation sampler_mutation;
  sampler_mutation.asset = Ref(RenderAssetKind::SAMPLER, 7U);
  sampler_mutation.payload = sampler;
  delta.mutations.push_back(std::move(sampler_mutation));
  return delta;
}

RenderAssetDelta MakeSpecularCatalogDelta(std::uint64_t registry_id,
                                          float specular_anisotropy = 8.0F,
                                          float index_of_refraction = 1.52F) {
  RenderAssetDelta delta = MakeModernCatalogDelta(registry_id);
  MaterialDescriptor &material =
      std::get<MaterialDescriptor>(delta.mutations[1U].payload);
  material.pbr_workflow = MaterialPbrWorkflow::SPECULAR;
  material.index_of_refraction = index_of_refraction;
  material.metallic_factor = 0.0F;
  material.metallic_roughness_texture = {};
  material.specular_factor = {0.72F, 0.65F, 0.51F};
  material.specular_texture.texture = Ref(RenderAssetKind::TEXTURE, 8U);
  material.specular_texture.sampler = Ref(RenderAssetKind::SAMPLER, 9U);

  RenderAssetMutation texture_mutation;
  texture_mutation.asset = Ref(RenderAssetKind::TEXTURE, 8U);
  texture_mutation.payload =
      MakeRgba8Texture(TextureColorSpace::LINEAR, 96U, 128U, 224U);
  delta.mutations.push_back(std::move(texture_mutation));

  SamplerResourceDescriptor specular_sampler;
  specular_sampler.debug_name = "RT4/V1 authored specular anisotropic sampler";
  specular_sampler.anisotropy_enabled = true;
  specular_sampler.maximum_anisotropy = specular_anisotropy;
  RenderAssetMutation sampler_mutation;
  sampler_mutation.asset = Ref(RenderAssetKind::SAMPLER, 9U);
  sampler_mutation.payload = std::move(specular_sampler);
  delta.mutations.push_back(std::move(sampler_mutation));
  return delta;
}

RenderAssetDelta
MakeDisplayDomainUnlitCatalogDelta(std::uint64_t registry_id) {
  MaterialDescriptor material;
  material.debug_name = "RT4/V1 display-domain Unlit";
  material.model = MaterialModel::UNLIT;
  material.base_color_transfer =
      BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE;
  material.base_color_texture.texture = Ref(RenderAssetKind::TEXTURE, 3U);
  material.base_color_texture.sampler = Ref(RenderAssetKind::SAMPLER, 4U);

  RenderAssetDelta delta =
      MakeCatalogDelta(registry_id, MakeModernMesh(), material);
  TextureResourceDescriptor texture;
  texture.debug_name = "RT4/V1 complete display-domain texture";
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 2U;
  texture.height = 2U;
  TextureMipLevelDescriptor base;
  base.width = 2U;
  base.height = 2U;
  base.row_pitch_bytes = 8U;
  base.layer_pitch_bytes = 16U;
  base.bytes = {0U,   32U,  64U,  255U, 255U, 96U,  64U,  255U,
                0U,   160U, 192U, 255U, 255U, 224U, 192U, 255U};
  texture.mip_levels.push_back(std::move(base));
  TextureMipLevelDescriptor last;
  last.width = 1U;
  last.height = 1U;
  last.row_pitch_bytes = 4U;
  last.layer_pitch_bytes = 4U;
  last.bytes = {250U, 7U, 201U, 255U};
  texture.mip_levels.push_back(std::move(last));
  RenderAssetMutation texture_mutation;
  texture_mutation.asset = Ref(RenderAssetKind::TEXTURE, 3U);
  texture_mutation.payload = std::move(texture);
  delta.mutations.push_back(std::move(texture_mutation));

  SamplerResourceDescriptor sampler;
  sampler.debug_name = "RT4/V1 exact display-domain sampler";
  sampler.mip_filter = SamplerFilter::NEAREST;
  sampler.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_w = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.maximum_lod = 1.0F;
  RenderAssetMutation sampler_mutation;
  sampler_mutation.asset = Ref(RenderAssetKind::SAMPLER, 4U);
  sampler_mutation.payload = std::move(sampler);
  delta.mutations.push_back(std::move(sampler_mutation));
  return delta;
}

std::shared_ptr<const SceneSnapshot> MakeDisplayDomainUnlitScene(
    std::uint64_t registry_id, std::uint32_t flags) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 1U;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = 1U;
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = Ref(RenderAssetKind::MESH, 1U);
  instance.material = Ref(RenderAssetKind::MATERIAL, 2U);
  instance.local_bounds = MakeModernMesh().local_bounds;
  instance.flags = flags;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  Require(result.ok(), "display-domain Unlit scene fixture is invalid");
  return result.snapshot;
}

Matrix4x4 Projection(float near_plane = 0.1F, float far_plane = 20.0F) {
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  projection.elements[10U] = far_plane / (near_plane - far_plane);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      near_plane * far_plane / (near_plane - far_plane);
  return projection;
}

std::shared_ptr<const SceneSnapshot>
MakeScene(std::uint64_t registry_id, Matrix4x4 transform = Matrix4x4{},
          std::uint64_t snapshot_id = 1U,
          Float3 ambient_radiance = {0.03F, 0.03F, 0.03F},
          float environment_intensity = 1.0F,
          Bounds3 local_bounds = MakeMesh().local_bounds,
          float exposure_compensation_ev = 0.0F,
          std::uint32_t instance_visibility_mask =
              (std::numeric_limits<std::uint32_t>::max)()) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = 1U;
  descriptor.environment.ambient_radiance = ambient_radiance;
  descriptor.environment.environment_intensity = environment_intensity;
  descriptor.environment.exposure_compensation_ev = exposure_compensation_ev;
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = Ref(RenderAssetKind::MESH, 1U);
  instance.material = Ref(RenderAssetKind::MATERIAL, 2U);
  instance.render_from_object = transform;
  instance.previous_render_from_object = transform;
  instance.local_bounds = local_bounds;
  instance.visibility_mask = instance_visibility_mask;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  Require(result.ok(), "test scene contract is invalid");
  return result.snapshot;
}

std::shared_ptr<const SceneSnapshot>
MakeReflectionScene(
    std::uint64_t registry_id,
    std::uint32_t instance_visibility =
        (std::numeric_limits<std::uint32_t>::max)(),
    std::uint32_t probe_visibility =
        (std::numeric_limits<std::uint32_t>::max)()) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 4U;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = 1U;
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = Ref(RenderAssetKind::MESH, 1U);
  instance.material = Ref(RenderAssetKind::MATERIAL, 2U);
  instance.local_bounds = MakeMesh().local_bounds;
  instance.visibility_mask = instance_visibility;
  descriptor.mesh_instances.push_back(instance);
  ReflectionProbeRuntimeDescriptor probe;
  probe.probe_id = 1U;
  probe.visibility_mask = probe_visibility;
  descriptor.reflection_probes.push_back(probe);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  Require(result.ok(), "reflection-probe policy fixture is invalid");
  return result.snapshot;
}

RenderFrameRequest MakeFrame(std::shared_ptr<const SceneSnapshot> scene);

float ProjectDepth(const Matrix4x4 &projection, float view_z) {
  const float clip_z = projection.elements[10U] * view_z +
                       projection.elements[14U];
  const float clip_w = projection.elements[11U] * view_z +
                       projection.elements[15U];
  return clip_z / clip_w;
}

void TestProjectionDepthConversion() {
  constexpr float kNear = 0.1F;
  constexpr float kFar = 20.0F;
  const Matrix4x4 portable = Projection(kNear, kFar);
  Matrix4x4 converted;
  Require(TryConvertPortableProjectionToOgreClip(portable, converted),
          "finite perspective projection conversion failed");
  Require(std::fabs(ProjectDepth(portable, -kNear)) < 1.0e-5F &&
              std::fabs(ProjectDepth(portable, -kFar) - 1.0F) < 1.0e-5F,
          "portable projection fixture is not [0,1]");
  Require(std::fabs(ProjectDepth(converted, -kNear) + 1.0F) < 1.0e-5F &&
              std::fabs(ProjectDepth(converted, -kFar) - 1.0F) < 1.0e-5F,
          "portable projection did not convert near/far to Ogre [-1,1]");
  Require(converted.elements[0U] == portable.elements[0U] &&
              converted.elements[5U] == portable.elements[5U] &&
              converted.elements[11U] == portable.elements[11U],
          "depth conversion changed non-depth projection rows");

  const float minimum_normal = (std::numeric_limits<float>::min)();
  const float hostile_near = minimum_normal * 0.5F;
  const float hostile_far = minimum_normal;
  Matrix4x4 hostile;
  hostile.elements.fill(0.0F);
  hostile.elements[0U] = 1.0F;
  hostile.elements[5U] = 1.0F;
  hostile.elements[10U] = 1.0F / (hostile_near - hostile_far);
  hostile.elements[14U] = hostile_near * hostile.elements[10U];
  hostile.elements[15U] = 1.0F;
  Require(IsCanonicalProjection(hostile, hostile_near, hostile_far),
          "hostile finite orthographic projection fixture is not canonical");
  Matrix4x4 untouched;
  const Matrix4x4 original_untouched = untouched;
  Require(!TryConvertPortableProjectionToOgreClip(hostile, untouched) &&
              untouched == original_untouched,
          "projection conversion manufactured infinity or mutated output");
}

void TestLifetimeSubmissionState() {
  constexpr std::uint64_t kRegistryId = 70U;
  auto first_scene = MakeScene(kRegistryId);
  const std::weak_ptr<const SceneSnapshot> first_scene_lifetime = first_scene;
  OgreNextN1SubmissionState state;
  RenderFrameRequest request = MakeFrame(first_scene);
  Require(state.Validate(request).ok(), "first submission identity was rejected");
  Require(state.PrepareCommit(request).ok() &&
              state.CanCommitPrepared(request),
          "first submission could not be prepared atomically");
  state.AbortPrepared();
  Require(!state.IsFrameComplete(1U) &&
              state.TrackedSnapshotIdentityCount() == 0U,
          "aborted submission leaked completion or snapshot identity state");
  Require(state.PrepareCommit(request).ok() &&
              state.CanCommitPrepared(request),
          "aborted submission could not be retried with the same identity");
  state.CommitPrepared(request);
  Require(state.IsFrameComplete(1U), "committed synchronous frame is incomplete");

  request.frame_id = 2U;
  Require(state.Validate(request).ok(), "latest snapshot replay was rejected");
  state.Commit(request);

  RenderFrameRequest aliased = MakeFrame(MakeScene(kRegistryId));
  aliased.frame_id = 3U;
  Require(state.Validate(aliased).code == RenderOperationCode::RESOURCE_STALE,
          "same snapshot ID with a different owner escaped identity checks");

  const auto alias_target = MakeScene(kRegistryId);
  RenderFrameRequest aliased_owner = MakeFrame(
      std::shared_ptr<const SceneSnapshot>(first_scene, alias_target.get()));
  aliased_owner.frame_id = 3U;
  Require(state.Validate(aliased_owner).code ==
              RenderOperationCode::RESOURCE_STALE,
          "aliasing owner with a different snapshot pointee escaped identity checks");
  aliased_owner.scene_snapshot.reset();

  RenderFrameRequest newer =
      MakeFrame(MakeScene(kRegistryId, Matrix4x4{}, 2U));
  newer.frame_id = 3U;
  Require(state.Validate(newer).ok(), "new monotonic snapshot was rejected");
  state.Commit(newer);
  request.frame_id = 4U;
  Require(state.Validate(request).ok(),
          "exact older snapshot replay was rejected");
  Require(state.PrepareCommit(request).ok() &&
              state.CanCommitPrepared(request),
          "exact older snapshot replay failed prepared-commit validation");
  state.CommitPrepared(request);

  aliased.frame_id = 5U;
  Require(state.Validate(aliased).code == RenderOperationCode::RESOURCE_STALE,
          "older snapshot ID alias escaped lifetime identity checks");

  newer.frame_id = 5U;
  Require(state.Validate(newer).ok(), "contiguous frame ID was rejected");
  state.Commit(newer);
  newer.frame_id = 7U;
  Require(state.Validate(newer).code == RenderOperationCode::INVALID_ARGUMENT,
          "sparse frame ID escaped bounded N1 completion policy");
  Require(state.IsFrameComplete(1U) && state.IsFrameComplete(5U),
          "successful frame fell out of lifetime completion history");
  Require(!state.IsFrameComplete(6U),
          "never-submitted frame appeared in completion history");
  first_scene.reset();
  request.scene_snapshot.reset();
  Require(first_scene_lifetime.expired(),
          "submission history retained a completed snapshot payload");
  RenderFrameRequest expired_alias = MakeFrame(MakeScene(kRegistryId));
  expired_alias.frame_id = 6U;
  Require(state.Validate(expired_alias).code ==
              RenderOperationCode::RESOURCE_STALE,
          "expired snapshot owner identity allowed an ID alias");
  for (std::uint64_t snapshot_id = 3U; snapshot_id <= 4096U; ++snapshot_id) {
    auto transient = MakeScene(kRegistryId, Matrix4x4{}, snapshot_id);
    RenderFrameRequest transient_request = MakeFrame(transient);
    transient_request.frame_id = snapshot_id + 3U;
    Require(state.Validate(transient_request).ok(),
            "sustained unique-snapshot fixture was rejected");
    state.Commit(transient_request);
  }
  Require(state.TrackedSnapshotIdentityCount() <= 2U,
          "expired snapshot identity metadata grew linearly");
  state.Reset();
  request = MakeFrame(MakeScene(kRegistryId));
  Require(state.Validate(request).ok(),
          "shutdown-style reset did not clear submission identities");
}

RenderFrameRequest MakeFrame(std::shared_ptr<const SceneSnapshot> scene) {
  RenderFrameRequest request;
  request.frame_id = 1U;
  request.scene_snapshot = std::move(scene);
  request.present = false;
  CameraViewRequest view;
  view.view_id = 1U;
  view.width = 192U;
  view.height = 128U;
  view.near_plane = 0.1F;
  view.far_plane = 20.0F;
  view.clip_from_view = Projection();
  view.previous_clip_from_view = Projection();
  request.views.push_back(view);
  return request;
}

void TestCapabilitiesFailClosed() {
  const FrontendCapabilityReport metal = BuildOgreNextN1CapabilityReport(
      RasterGraphicsApi::METAL, "test");
  Require(ValidateFrontendCapabilityReport(metal).ok(),
          "Metal capability report is internally inconsistent");
  Require(metal.frontend_kind == RendererFrontendKind::OGRE_NEXT &&
              metal.supported_outputs == FrameOutputMask::COLOR &&
              metal.maximum_views == 1U && metal.supports_hdr_output &&
              metal.supports_dynamic_mesh_updates &&
              metal.supports_continuous_particles &&
              metal.maximum_texture_dimension_2d ==
                  kOgreNextN1ConservativeMaximumTextureDimension,
          "N1 did not report its admitted raster surface");
  Require(metal.native_api == NativeGraphicsApi::NONE &&
              !metal.supports_compute && !metal.supports_async_compute &&
              !metal.supports_particle_events &&
              !metal.supports_native_interop &&
              !metal.supports_native_ray_tracing_api &&
              !metal.native_ray_tracing_probe_passed &&
              !metal.native_ray_tracing_geometry_interop_ready,
          "an unproved N1 capability defaulted on");
  const FrontendCapabilityReport d3d = BuildOgreNextN1CapabilityReport(
      RasterGraphicsApi::DIRECT3D11, "test");
  Require(d3d.raster_api == RasterGraphicsApi::DIRECT3D11 &&
              d3d.native_api == NativeGraphicsApi::NONE,
          "D3D11 raster incorrectly implied D3D12/DXR interop");
}

void TestHdrFeatureCombinationPolicy() {
  constexpr std::uint64_t kRegistryId = 73U;
  RenderAssetRegistry registry(kRegistryId);
  Require(registry.Apply(MakeCatalogDelta(kRegistryId)).ok(),
          "HDR feature-combination catalog setup failed");
  const FrontendCapabilityReport capabilities =
      BuildOgreNextN1CapabilityReport(RasterGraphicsApi::METAL, "test");
  RenderFrameRequest request = MakeFrame(MakeReflectionScene(kRegistryId));

  // This pure admission check runs without constructing a frontend or native
  // device. HDR+PSSM remains fail-closed unless the caller identifies the
  // exact reviewed single-evaluation topology explicitly.
  const ValidationResult hdr_pssm = ValidateOgreNextN1Frame(
      request, capabilities, registry,
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
      OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, true);
  Require(hdr_pssm.code == ValidationCode::UNSUPPORTED_FEATURE &&
              hdr_pssm.field == "hdr_scene_topology",
          "HDR+PSSM escaped pre-device feature-combination admission");

  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::DISABLED, true)
              .ok(),
          "reflection probes were incorrectly excluded from the HDR path");
}

void TestNativeDirectionalShadowScenePolicy() {
  constexpr std::uint64_t kRegistryId = 174U;
  constexpr OgreNextRasterFeatureTier kModern =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  RenderAssetRegistry registry(kRegistryId);
  Require(registry.Apply(MakeModernCatalogDelta(kRegistryId)).ok(),
          "native directional-shadow catalog setup failed");

  const auto make_scene = [&](std::uint32_t receiver_flags,
                              std::uint32_t occluder_flags) {
    SceneSnapshotDescriptor descriptor;
    descriptor.snapshot_id = 1U;
    descriptor.asset_registry_id = kRegistryId;
    descriptor.asset_sequence = 1U;
    LightDescriptor light;
    light.light_id = 1U;
    light.type = LightType::DIRECTIONAL;
    light.intensity = 1024.0F;
    light.direction = {0.0F, 0.0F, -1.0F};
    light.previous_direction = light.direction;
    light.shadow_flags = LIGHT_SHADOW_DEFAULT_FLAGS;
    descriptor.lights.push_back(light);
    MeshInstanceDescriptor receiver;
    receiver.instance_id = 1U;
    receiver.mesh = Ref(RenderAssetKind::MESH, 1U);
    receiver.material = Ref(RenderAssetKind::MATERIAL, 2U);
    receiver.local_bounds = MakeModernMesh().local_bounds;
    receiver.flags = receiver_flags;
    descriptor.mesh_instances.push_back(receiver);
    MeshInstanceDescriptor occluder = receiver;
    occluder.instance_id = 2U;
    occluder.flags = occluder_flags;
    descriptor.mesh_instances.push_back(occluder);
    SceneSnapshotCreateResult created =
        CreateSceneSnapshot(std::move(descriptor));
    Require(created.ok(),
            "native directional-shadow policy fixture is invalid");
    return created.snapshot;
  };

  const auto scene = make_scene(MESH_INSTANCE_RECEIVES_SHADOW,
                                MESH_INSTANCE_CASTS_SHADOW);
  Require(ValidateOgreNextN1Scene(*scene, registry, false, kModern).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "shadow light escaped the default PSSM-disabled policy");
  Require(ValidateOgreNextN1Scene(
              *scene, registry, false, kModern,
              OgreNextDirectionalShadowMode::DISABLED, false, true)
              .ok(),
          "the explicit RT4 native directional-shadow scene was rejected");
  Require(ValidateOgreNextN1Scene(
              *scene, registry, false, kModern,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, false, true)
              .field == "directional_shadow_mode",
          "native directional shadows and PSSM were admitted together");
  Require(ValidateOgreNextN1Scene(
              *scene, registry, false, kModern,
              OgreNextDirectionalShadowMode::DISABLED, true, true)
              .field == "hdr_scene_topology",
          "native directional shadows and persistent HDR were admitted together");

  const auto ambiguous = make_scene(MESH_INSTANCE_DEFAULT_FLAGS,
                                    MESH_INSTANCE_CASTS_SHADOW);
  Require(ValidateOgreNextN1Scene(
              *ambiguous, registry, false, kModern,
              OgreNextDirectionalShadowMode::DISABLED, false, true)
              .field == "mesh_instances.flags",
          "an instance that both casts and receives escaped native role admission");

  RenderFrameRequest request = MakeFrame(scene);
  request.color_format = PixelFormat::RGBA16_FLOAT;
  const FrontendCapabilityReport capabilities =
      BuildOgreNextN1CapabilityReport(RasterGraphicsApi::METAL, "test");
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry, kModern,
              OgreNextDirectionalShadowMode::DISABLED, false, true)
              .ok(),
          "native directional-shadow frame incorrectly entered the PSSM planner");

  request.color_format = PixelFormat::RGBA16_FLOAT;
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry, kModern,
              OgreNextDirectionalShadowMode::DISABLED, true,
              false, true, true,
              OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2)
              .ok(),
          "reviewed HDR sun-visibility V2 frame entered the PSSM planner");
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry, kModern,
              OgreNextDirectionalShadowMode::DISABLED, true,
              false, true, true,
              OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1)
              .field == "hdr_scene_topology",
          "sun-visibility V2 escaped its exact directional-split topology");
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry, kModern,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, true,
              false, true, true,
              OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2)
              .field == "hdr_scene_topology",
          "sun-visibility V2 and PSSM were admitted together");
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry, kModern,
              OgreNextDirectionalShadowMode::DISABLED, true,
              true, true, true,
              OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2)
              .field == "hdr_scene_topology",
          "sun-visibility V2 and native N4 were admitted together");

  request.views.front().near_plane = kOgreNextPssmNearMeters;
  request.views.front().far_plane = kOgreNextPssmFarMeters;
  request.views.front().clip_from_view =
      Projection(kOgreNextPssmNearMeters, kOgreNextPssmFarMeters);
  request.views.front().previous_clip_from_view =
      request.views.front().clip_from_view;
  request.color_format = PixelFormat::RGBA8_SRGB;
  const ValidationResult reviewed_hdr_pssm = ValidateOgreNextN1Frame(
      request, capabilities, registry, kModern,
      OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, true,
      false, false, false,
      OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1);
  if (!reviewed_hdr_pssm) {
    std::cerr << "Ogre-Next N1 policy test failed: reviewed one-scene "
                 "HDR+PSSM compositor topology was rejected ("
              << reviewed_hdr_pssm.field << ": "
              << reviewed_hdr_pssm.detail << ")\n";
    std::exit(EXIT_FAILURE);
  }
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry, kModern,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, true,
              false, false, false,
              OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2)
              .field == "hdr_scene_topology",
          "wrong HDR topology escaped exact HDR+PSSM admission");
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry, kModern,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, true,
              true, false, false,
              OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1)
              .field == "hdr_scene_topology",
          "native directional shadows escaped exact HDR+PSSM admission");
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::STATIC_PBR_N1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, true,
              false, false, false,
              OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "non-RT4 tier escaped exact HDR+PSSM admission");
}

void TestInitializationPolicy() {
  const FrontendCapabilityReport capabilities =
      BuildOgreNextN1CapabilityReport(RasterGraphicsApi::METAL, "test");
  FrontendInitializationRequest request;
  request.initial_width = 192U;
  request.initial_height = 128U;
  request.maximum_frames_in_flight = 1U;
  request.headless = true;
  Require(ValidateOgreNextN1Initialization(request, capabilities).ok(),
          "valid headless initialization was rejected");
  request.headless = false;
  Require(ValidateOgreNextN1Initialization(request, capabilities).code ==
              ValidationCode::MISSING_REFERENCE,
          "windowless presentation escaped the base validator");
  request.headless = true;
  request.maximum_frames_in_flight = 2U;
  Require(ValidateOgreNextN1Initialization(request, capabilities).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "N1 accepted multiple frames in flight");
  request.maximum_frames_in_flight = 1U;
  request.initial_width = 5120U;
  request.initial_height = 2880U;
  Require(ValidateOgreNextN1Initialization(request, capabilities).ok(),
          "conservative pre-device report became an artificial 5K cap");
}

void TestAssetPolicy() {
  constexpr std::uint64_t kRegistryId = 71U;
  RenderAssetRegistry registry(kRegistryId);
  Require(registry.Apply(MakeCatalogDelta(kRegistryId)).ok(),
          "valid N1 catalog could not be constructed");
  Require(ValidateOgreNextN1AssetCatalog(registry).ok(),
          "valid N1 catalog was rejected");

  OgreNextN1NativeMeshBounds native_bounds;
  Require(TryBuildOgreNextN1NativeMeshBounds(MakeMesh().local_bounds,
                                             native_bounds) &&
              native_bounds.center == Float3{} &&
              native_bounds.half_size == Float3{1.0F, 1.0F, 0.0F} &&
              std::fabs(native_bounds.radius - std::sqrt(2.0F)) < 1.0e-6F,
          "valid mesh bounds did not produce finite Ogre bounds");

  MeshResourceDescriptor overflowing_bounds = MakeMesh();
  constexpr float kLargeCoordinate = 1.0e20F;
  overflowing_bounds.local_bounds.minimum =
      {-kLargeCoordinate, -kLargeCoordinate, -kLargeCoordinate};
  overflowing_bounds.local_bounds.maximum =
      {kLargeCoordinate, kLargeCoordinate, kLargeCoordinate};
  overflowing_bounds.positions = {
      {-kLargeCoordinate, -kLargeCoordinate, -kLargeCoordinate},
      {kLargeCoordinate, -kLargeCoordinate, -kLargeCoordinate},
      {0.0F, kLargeCoordinate, kLargeCoordinate},
  };
  Require(ValidateMeshResourceDescriptor(overflowing_bounds).ok(),
          "finite hostile mesh fixture is not portable-contract valid");
  RenderAssetRegistry overflowing_bounds_registry(kRegistryId);
  Require(overflowing_bounds_registry
              .Apply(MakeCatalogDelta(kRegistryId, overflowing_bounds,
                                      MakeMaterial()))
              .ok(),
          "finite hostile mesh fixture is not registry valid");
  Require(ValidateOgreNextN1AssetCatalog(overflowing_bounds_registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "finite mesh values that overflow Ogre bounds escaped admission");

  MeshResourceDescriptor dynamic_mesh = MakeMesh();
  dynamic_mesh.dynamic = true;
  RenderAssetRegistry dynamic_registry(kRegistryId);
  Require(dynamic_registry
              .Apply(MakeCatalogDelta(kRegistryId, dynamic_mesh,
                                      MakeMaterial()))
              .ok(),
          "dynamic policy fixture is not contract valid");
  Require(ValidateOgreNextN1AssetCatalog(dynamic_registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "explicit static-only policy admitted dynamic geometry");
  Require(ValidateOgreNextN1AssetCatalog(dynamic_registry, true).ok(),
          "admitted full-frame dynamic geometry was rejected");

  MaterialDescriptor overflowing_emissive = MakeMaterial();
  overflowing_emissive.emissive_factor =
      {(std::numeric_limits<float>::max)(), 0.0F, 0.0F};
  overflowing_emissive.emissive_strength = 2.0F;
  Require(ValidateMaterialDescriptor(overflowing_emissive).ok(),
          "finite hostile emissive fixture is not portable-contract valid");
  RenderAssetRegistry overflowing_emissive_registry(kRegistryId);
  Require(overflowing_emissive_registry
              .Apply(MakeCatalogDelta(kRegistryId, MakeMesh(),
                                      overflowing_emissive))
              .ok(),
          "finite hostile emissive fixture is not registry valid");
  Require(ValidateOgreNextN1AssetCatalog(overflowing_emissive_registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "finite emissive values that overflow Ogre PBS escaped admission");

  MaterialDescriptor textured = MakeMaterial();
  textured.base_color_texture.texture = Ref(RenderAssetKind::TEXTURE, 3U);
  textured.base_color_texture.sampler = Ref(RenderAssetKind::SAMPLER, 4U);
  Require(!ValidateMaterialDescriptor(textured).ok() ||
              textured.base_color_texture.texture.valid(),
          "texture fixture was not populated");
  RenderAssetDelta textured_delta = MakeCatalogDelta(kRegistryId);
  std::get<MaterialDescriptor>(textured_delta.mutations[1U].payload) = textured;
  TextureResourceDescriptor texture;
  texture.width = 1U;
  texture.height = 1U;
  texture.color_space = TextureColorSpace::SRGB;
  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.layer_pitch_bytes = 4U;
  mip.bytes = {255U, 255U, 255U, 255U};
  texture.mip_levels.push_back(mip);
  RenderAssetMutation texture_mutation;
  texture_mutation.asset = Ref(RenderAssetKind::TEXTURE, 3U);
  texture_mutation.payload = texture;
  SamplerResourceDescriptor sampler;
  RenderAssetMutation sampler_mutation;
  sampler_mutation.asset = Ref(RenderAssetKind::SAMPLER, 4U);
  sampler_mutation.payload = sampler;
  textured_delta.mutations.push_back(std::move(texture_mutation));
  textured_delta.mutations.push_back(std::move(sampler_mutation));
  RenderAssetRegistry textured_registry(kRegistryId);
  Require(textured_registry.Apply(textured_delta).ok(),
          "textured policy fixture is not registry valid");
  Require(ValidateOgreNextN1AssetCatalog(textured_registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "texture resources escaped N1 admission");
}

void TestModernPbrAssetPolicy() {
  constexpr std::uint64_t kRegistryId = 73U;
  constexpr OgreNextRasterFeatureTier kModern =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  RenderAssetRegistry registry(kRegistryId);
  Require(registry.Apply(MakeModernCatalogDelta(kRegistryId)).ok(),
          "valid RT4/V1 catalog could not be constructed");
  Require(ValidateOgreNextN1AssetCatalog(registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "default N1 silently enabled the opt-in textured tier");
  Require(ValidateOgreNextN1AssetCatalog(registry, false, kModern).ok(),
          "valid RT4/V1 texture, sampler, tangent, and UV0 catalog was rejected");

  const auto set_shared_affine = [](MaterialDescriptor &material,
                                    Float2 scale, Float2 offset) {
    TextureBinding *bindings[] = {
        &material.base_color_texture,
        &material.metallic_roughness_texture,
        &material.normal_texture,
        &material.emissive_texture,
        &material.specular_texture,
    };
    for (TextureBinding *binding : bindings) {
      if (binding->texture.valid()) {
        binding->scale = scale;
        binding->offset = offset;
      }
    }
  };
  RenderAssetDelta affine_delta =
      MakeModernCatalogDelta(kRegistryId + 200U);
  MaterialDescriptor &affine_material =
      std::get<MaterialDescriptor>(affine_delta.mutations[1U].payload);
  set_shared_affine(affine_material, {2.0F, 4.0F}, {0.125F, -0.25F});
  OgreNextN1PbsUv0AffineTransform affine;
  Require(BuildOgreNextN1PbsUv0AffineTransform(affine_material, affine).ok() &&
              affine.scale == Float2{2.0F, 4.0F} &&
              affine.offset == Float2{0.125F, -0.25F} &&
              affine.portable_texture_binding_count == 4U &&
              affine.native_texture_slot_count == 5U && affine.transformed,
          "exact shared UV0 affine profile did not lower deterministically");
  RenderAssetRegistry affine_registry(kRegistryId + 200U);
  Require(affine_registry.Apply(affine_delta).ok() &&
              ValidateOgreNextN1AssetCatalog(affine_registry, false, kModern)
                  .ok(),
          "finite positive shared UV0 scale/offset was rejected");

  RenderAssetDelta mismatched_affine =
      MakeModernCatalogDelta(kRegistryId + 201U);
  std::get<MaterialDescriptor>(mismatched_affine.mutations[1U].payload)
      .base_color_texture.scale = {2.0F, 4.0F};
  RenderAssetRegistry mismatched_affine_registry(kRegistryId + 201U);
  Require(mismatched_affine_registry.Apply(mismatched_affine).ok(),
          "cross-slot affine mismatch fixture is not contract valid");
  const ValidationResult mismatched_affine_result =
      ValidateOgreNextN1AssetCatalog(mismatched_affine_registry, false,
                                     kModern);
  Require(mismatched_affine_result.code ==
              ValidationCode::UNSUPPORTED_FEATURE &&
              mismatched_affine_result.field ==
                  "assets.material.texture_transform",
          "different transforms across bound PBS slots escaped admission");

  RenderAssetDelta negative_affine =
      MakeModernCatalogDelta(kRegistryId + 202U);
  MaterialDescriptor &negative_affine_material =
      std::get<MaterialDescriptor>(negative_affine.mutations[1U].payload);
  set_shared_affine(negative_affine_material, {-1.0F, 4.0F}, Float2{});
  RenderAssetRegistry negative_affine_registry(kRegistryId + 202U);
  Require(negative_affine_registry.Apply(negative_affine).ok(),
          "negative affine fixture is not renderer-contract valid");
  const ValidationResult negative_affine_result =
      ValidateOgreNextN1AssetCatalog(negative_affine_registry, false,
                                     kModern);
  Require(negative_affine_result.code ==
              ValidationCode::UNSUPPORTED_FEATURE &&
              negative_affine_result.field ==
                  "assets.material.texture_transform",
          "negative UV scale escaped the positive native affine profile");

  RenderAssetDelta uv1_affine =
      MakeModernCatalogDelta(kRegistryId + 203U);
  MaterialDescriptor &uv1_affine_material =
      std::get<MaterialDescriptor>(uv1_affine.mutations[1U].payload);
  TextureBinding *uv1_bindings[] = {
      &uv1_affine_material.base_color_texture,
      &uv1_affine_material.metallic_roughness_texture,
      &uv1_affine_material.normal_texture,
      &uv1_affine_material.emissive_texture,
  };
  for (TextureBinding *binding : uv1_bindings) {
    binding->texture_coordinate_set = 1U;
  }
  RenderAssetRegistry uv1_affine_registry(kRegistryId + 203U);
  Require(uv1_affine_registry.Apply(uv1_affine).ok(),
          "UV1 affine fixture is not renderer-contract valid");
  const ValidationResult uv1_affine_result =
      ValidateOgreNextN1AssetCatalog(uv1_affine_registry, false, kModern);
  Require(uv1_affine_result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              uv1_affine_result.field ==
                  "assets.material.texture_transform",
          "UV1 escaped the exact native UV0 affine profile");

  RenderAssetDelta nonfinite_affine =
      MakeModernCatalogDelta(kRegistryId + 204U);
  std::get<MaterialDescriptor>(nonfinite_affine.mutations[1U].payload)
      .base_color_texture.offset.x =
      (std::numeric_limits<float>::quiet_NaN)();
  RenderAssetRegistry nonfinite_affine_registry(kRegistryId + 204U);
  const ValidationResult nonfinite_affine_result =
      nonfinite_affine_registry.Apply(nonfinite_affine);
  Require(nonfinite_affine_result.code == ValidationCode::NON_FINITE_VALUE,
          "non-finite UV affine state entered the synchronized registry");

  RenderAssetRegistry specular_registry(kRegistryId + 100U);
  Require(specular_registry
              .Apply(MakeSpecularCatalogDelta(kRegistryId + 100U))
              .ok(),
          "authored specular RT4/V1 fixture is not registry valid");
  Require(ValidateOgreNextN1AssetCatalog(specular_registry, false, kModern)
              .ok(),
          "authored IOR 1.52 linear-specular workflow and texture were rejected");
  Require(ValidateOgreNextN1SamplerDeviceLimits(specular_registry, 8.0F,
                                                 kModern)
              .ok(),
          "exact specular anisotropy equal to the device limit was rejected");
  const ValidationResult over_limit_specular =
      ValidateOgreNextN1SamplerDeviceLimits(specular_registry, 4.0F, kModern);
  Require(over_limit_specular.code == ValidationCode::UNSUPPORTED_FEATURE &&
              over_limit_specular.field ==
                  "assets.sampler.maximum_anisotropy",
          "SPECULAR-slot anisotropy above the device limit escaped admission");

  RenderAssetDelta noncanonical_metallic_ior =
      MakeModernCatalogDelta(kRegistryId + 99U);
  std::get<MaterialDescriptor>(noncanonical_metallic_ior.mutations[1U].payload)
      .index_of_refraction = 1.52F;
  RenderAssetRegistry noncanonical_metallic_ior_registry(kRegistryId + 99U);
  Require(noncanonical_metallic_ior_registry
              .Apply(noncanonical_metallic_ior)
              .ok(),
          "metallic-roughness IOR 1.52 fixture is not registry valid");
  const ValidationResult noncanonical_metallic_ior_result =
      ValidateOgreNextN1AssetCatalog(noncanonical_metallic_ior_registry,
                                     false, kModern);
  Require(noncanonical_metallic_ior_result.code ==
                  ValidationCode::UNSUPPORTED_FEATURE &&
              noncanonical_metallic_ior_result.field ==
                  "assets.material.index_of_refraction",
          "metallic-roughness workflow accepted a noncanonical unused IOR");

  const auto alpha_catalog = [&](std::uint64_t registry_id,
                                 MaterialBlendMode blend,
                                 MaterialAlphaTestMode alpha_test,
                                 float cutoff) {
    RenderAssetDelta delta = MakeModernCatalogDelta(registry_id);
    MaterialDescriptor &material =
        std::get<MaterialDescriptor>(delta.mutations[1U].payload);
    material.blend_mode = blend;
    material.alpha_test_mode = alpha_test;
    material.alpha_cutoff = cutoff;
    material.depth_write = false;
    std::get<TextureResourceDescriptor>(delta.mutations[2U].payload)
        .mip_levels.front()
        .bytes[3U] = 128U;
    return delta;
  };
  RenderAssetRegistry source_over_registry(kRegistryId + 101U);
  Require(source_over_registry
              .Apply(alpha_catalog(kRegistryId + 101U,
                                   MaterialBlendMode::STRAIGHT_SOURCE_OVER,
                                   MaterialAlphaTestMode::DISABLED, 0.5F))
              .ok() &&
              ValidateOgreNextN1AssetCatalog(source_over_registry, false,
                                              kModern)
                  .ok(),
          "exact true source-over RT4/V1 material was rejected");
  RenderAssetRegistry legacy_alpha_registry(kRegistryId + 102U);
  Require(legacy_alpha_registry
              .Apply(alpha_catalog(kRegistryId + 102U,
                                   MaterialBlendMode::LEGACY_STRAIGHT_ALPHA,
                                   MaterialAlphaTestMode::GREATER,
                                   2.0F / 255.0F))
              .ok() &&
              ValidateOgreNextN1AssetCatalog(legacy_alpha_registry, false,
                                              kModern)
                  .ok(),
          "combined legacy-alpha/GREATER/depth-write-off RT4/V1 material was rejected");

  RenderAssetDelta missing_alpha_texture = alpha_catalog(
      kRegistryId + 103U, MaterialBlendMode::STRAIGHT_SOURCE_OVER,
      MaterialAlphaTestMode::DISABLED, 0.5F);
  std::get<MaterialDescriptor>(missing_alpha_texture.mutations[1U].payload)
      .base_color_texture = {};
  RenderAssetRegistry missing_alpha_registry(kRegistryId + 103U);
  Require(missing_alpha_registry.Apply(missing_alpha_texture).ok() &&
              ValidateOgreNextN1AssetCatalog(missing_alpha_registry, false,
                                              kModern)
                      .code == ValidationCode::UNSUPPORTED_FEATURE,
          "alpha blending without authored texture coverage escaped admission");

  RenderAssetDelta modulated_alpha_blend = alpha_catalog(
      kRegistryId + 105U, MaterialBlendMode::STRAIGHT_SOURCE_OVER,
      MaterialAlphaTestMode::DISABLED, 0.5F);
  std::get<MaterialDescriptor>(modulated_alpha_blend.mutations[1U].payload)
      .base_color_factor.w = 0.5F;
  RenderAssetRegistry modulated_alpha_blend_registry(kRegistryId + 105U);
  Require(modulated_alpha_blend_registry.Apply(modulated_alpha_blend).ok(),
          "nonunit alpha-blend factor fixture is not registry valid");
  const ValidationResult modulated_alpha_blend_result =
      ValidateOgreNextN1AssetCatalog(modulated_alpha_blend_registry, false,
                                     kModern);
  Require(modulated_alpha_blend_result.code ==
              ValidationCode::UNSUPPORTED_FEATURE &&
              modulated_alpha_blend_result.field ==
                  "assets.material.base_color_factor",
          "alpha blend with double-attenuating nonunit factor escaped exact "
          "pinned-PBS policy");

  RenderAssetDelta modulated_alpha_test = alpha_catalog(
      kRegistryId + 104U, MaterialBlendMode::REPLACE,
      MaterialAlphaTestMode::GREATER, 2.0F / 255.0F);
  std::get<MaterialDescriptor>(modulated_alpha_test.mutations[1U].payload)
      .base_color_factor.w = 0.5F;
  RenderAssetRegistry modulated_alpha_registry(kRegistryId + 104U);
  Require(modulated_alpha_registry.Apply(modulated_alpha_test).ok() &&
              ValidateOgreNextN1AssetCatalog(modulated_alpha_registry, false,
                                              kModern)
                      .code == ValidationCode::UNSUPPORTED_FEATURE,
          "alpha test with nonunit factor escaped exact pinned-PBS policy");

  const auto make_lit_scene = [&](std::vector<LightDescriptor> lights,
                                  Matrix4x4 transform = Matrix4x4{},
                                  bool analytic_sky = false) {
    SceneSnapshotDescriptor descriptor;
    descriptor.snapshot_id = 1U;
    descriptor.asset_registry_id = kRegistryId;
    descriptor.asset_sequence = 1U;
    MeshInstanceDescriptor instance;
    instance.instance_id = 1U;
    instance.mesh = Ref(RenderAssetKind::MESH, 1U);
    instance.material = Ref(RenderAssetKind::MATERIAL, 2U);
    instance.local_bounds = MakeModernMesh().local_bounds;
    instance.render_from_object = transform;
    instance.previous_render_from_object = transform;
    descriptor.mesh_instances.push_back(instance);
    descriptor.lights = std::move(lights);
    if (analytic_sky) {
      descriptor.environment.analytic_sky.enabled = true;
      descriptor.environment.analytic_sky.sun_light_id =
          descriptor.lights.front().light_id;
      descriptor.environment.analytic_sky.zenith_radiance =
          {0.08F, 0.12F, 0.2F};
      descriptor.environment.analytic_sky.horizon_radiance =
          {0.3F, 0.24F, 0.18F};
      descriptor.environment.analytic_sky.ground_radiance =
          {0.01F, 0.009F, 0.008F};
      descriptor.environment.analytic_sky.sun_disk_radiance =
          {24.0F, 20.0F, 16.0F};
      descriptor.environment.analytic_sky.sun_angular_radius_radians =
          0.00465047F;
    }
    SceneSnapshotCreateResult scene =
        CreateSceneSnapshot(std::move(descriptor));
    Require(scene.ok(), "RT4/V1 light policy fixture is invalid");
    return scene.snapshot;
  };
  LightDescriptor directional;
  directional.light_id = 1U;
  directional.intensity = 1024.0F;
  directional.direction = {0.0F, 0.0F, -1.0F};
  directional.shadow_flags = 0U;
  Require(ValidateOgreNextN1Scene(*make_lit_scene({directional}), registry,
                                  false, kModern)
              .ok(),
          "one calibrated RT4/V1 directional light was rejected");
  const auto analytic_sky_scene =
      make_lit_scene({directional}, Matrix4x4{}, true);
  Require(ValidateOgreNextN1Scene(*analytic_sky_scene, registry, false,
                                  kModern)
              .ok() &&
              ValidateOgreNextN1Scene(*analytic_sky_scene, registry)
                      .field == "environment.analytic_sky",
          "native analytic sky was rejected by RT4/V1 or leaked into texture-free N1");
  Matrix4x4 uniform_scale;
  uniform_scale.elements[0U] = 2.0F;
  uniform_scale.elements[5U] = 2.0F;
  uniform_scale.elements[10U] = 2.0F;
  Require(ValidateOgreNextN1Scene(
              *make_lit_scene({directional}, uniform_scale), registry, false,
              kModern)
              .ok(),
          "uniform RT4/V1 scale was rejected");
  Matrix4x4 non_uniform_scale;
  non_uniform_scale.elements[0U] = 2.0F;
  const ValidationResult non_uniform_scale_validation =
      ValidateOgreNextN1Scene(
          *make_lit_scene({directional}, non_uniform_scale), registry, false,
          kModern);
  Require(non_uniform_scale_validation.code ==
              ValidationCode::UNSUPPORTED_FEATURE &&
              non_uniform_scale_validation.field ==
                  "mesh_instances.render_from_object",
          "non-uniform RT4/V1 scale escaped tangent-frame admission");
  LightDescriptor point = directional;
  point.type = LightType::POINT;
  point.position = {1.0F, 2.0F, 3.0F};
  point.previous_position = point.position;
  point.direction = {0.0F, -1.0F, 0.0F};
  point.previous_direction = point.direction;
  point.range = 10.0F;
  Require(ValidateOgreNextN1Scene(*make_lit_scene({point}), registry, false,
                                  kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "uncalibrated local light escaped RT4/V1 admission");
  LightDescriptor shadowed = directional;
  shadowed.shadow_flags = LIGHT_SHADOW_DEFAULT_FLAGS;
  Require(ValidateOgreNextN1Scene(*make_lit_scene({shadowed}), registry,
                                  false, kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "directional light without a shadow contract escaped RT4/V1 admission");
  LightDescriptor second = directional;
  second.light_id = 2U;
  Require(ValidateOgreNextN1Scene(
              *make_lit_scene({directional, second}), registry, false, kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "multiple directional lights escaped RT4/V1 admission");
  LightDescriptor maximum_photometry = directional;
  Require(NormalizePhotometricColorLinear({0.0F, 0.0F, 1.0F},
                                          maximum_photometry.color_linear),
          "canonical saturated-blue RT4/V1 fixture could not be normalized");
  maximum_photometry.intensity = (std::numeric_limits<float>::max)();
  Require(ValidateOgreNextN1Scene(*make_lit_scene({maximum_photometry}),
                                  registry, false, kModern)
              .ok(),
          "maximum canonical RT4/V1 photometry was rejected");

  RenderAssetDelta transformed_delta = MakeModernCatalogDelta(kRegistryId + 1U);
  std::get<MaterialDescriptor>(transformed_delta.mutations[1U].payload)
      .base_color_texture.rotation_radians = 0.25F;
  RenderAssetRegistry transformed_registry(kRegistryId + 1U);
  Require(transformed_registry.Apply(transformed_delta).ok(),
          "texture-transform fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(transformed_registry, false, kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "unmapped general texture transform escaped RT4/V1 admission");

  RenderAssetDelta occlusion_delta = MakeModernCatalogDelta(kRegistryId + 2U);
  MaterialDescriptor &occluded =
      std::get<MaterialDescriptor>(occlusion_delta.mutations[1U].payload);
  occluded.occlusion_texture.texture = Ref(RenderAssetKind::TEXTURE, 4U);
  occluded.occlusion_texture.sampler = Ref(RenderAssetKind::SAMPLER, 7U);
  RenderAssetRegistry occlusion_registry(kRegistryId + 2U);
  Require(occlusion_registry.Apply(occlusion_delta).ok(),
          "packed ORM occlusion fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(occlusion_registry, false, kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "pinned-PBS ambient-occlusion mismatch escaped RT4/V1 admission");

  RenderAssetDelta negative_normal_delta =
      MakeModernCatalogDelta(kRegistryId + 3U);
  std::get<TextureResourceDescriptor>(
      negative_normal_delta.mutations[4U].payload)
      .mip_levels.front()
      .bytes[2U] = 0U;
  RenderAssetRegistry negative_normal_registry(kRegistryId + 3U);
  Require(negative_normal_registry.Apply(negative_normal_delta).ok(),
          "negative-Z normal fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(negative_normal_registry, false,
                                         kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "negative-Z normal escaped RT4/V1 positive-hemisphere admission");

  RenderAssetDelta inconsistent_normal_delta =
      MakeModernCatalogDelta(kRegistryId + 9U);
  std::get<TextureResourceDescriptor>(
      inconsistent_normal_delta.mutations[4U].payload)
      .mip_levels.front()
      .bytes[2U] = 200U;
  RenderAssetRegistry inconsistent_normal_registry(kRegistryId + 9U);
  Require(inconsistent_normal_registry.Apply(inconsistent_normal_delta).ok(),
          "inconsistent normal fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(inconsistent_normal_registry, false,
                                         kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "normal B inconsistent with pinned RG reconstruction escaped admission");

  RenderAssetDelta scaled_normal_delta =
      MakeModernCatalogDelta(kRegistryId + 10U);
  std::get<MaterialDescriptor>(scaled_normal_delta.mutations[1U].payload)
      .normal_scale = 0.5F;
  RenderAssetRegistry scaled_normal_registry(kRegistryId + 10U);
  Require(scaled_normal_registry.Apply(scaled_normal_delta).ok(),
          "scaled-normal fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(scaled_normal_registry, false,
                                         kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "nonidentity glTF normal scale escaped Ogre lerp-weight admission");

  RenderAssetDelta nonopaque_normal_delta =
      MakeModernCatalogDelta(kRegistryId + 11U);
  std::get<TextureResourceDescriptor>(
      nonopaque_normal_delta.mutations[4U].payload)
      .mip_levels.front()
      .bytes[3U] = 254U;
  RenderAssetRegistry nonopaque_normal_registry(kRegistryId + 11U);
  Require(nonopaque_normal_registry.Apply(nonopaque_normal_delta).ok(),
          "nonopaque normal fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(nonopaque_normal_registry, false,
                                         kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "discarded nonopaque normal alpha escaped canonical admission");

  RenderAssetDelta border_delta = MakeModernCatalogDelta(kRegistryId + 4U);
  std::get<SamplerResourceDescriptor>(border_delta.mutations.back().payload)
      .address_u = SamplerAddressMode::CLAMP_TO_BORDER;
  RenderAssetRegistry border_registry(kRegistryId + 4U);
  Require(border_registry.Apply(border_delta).ok(),
          "border sampler fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(border_registry, false, kModern).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "cross-renderer clamp-to-border mismatch escaped RT4/V1 admission");

  RenderAssetDelta biased_delta = MakeModernCatalogDelta(kRegistryId + 5U);
  std::get<SamplerResourceDescriptor>(biased_delta.mutations.back().payload)
      .mip_lod_bias = 1.0F;
  RenderAssetRegistry biased_registry(kRegistryId + 5U);
  Require(biased_registry.Apply(biased_delta).ok(),
          "LOD-bias sampler fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(biased_registry, false, kModern).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "Metal-omitted mip LOD bias escaped RT4/V1 admission");

  RenderAssetDelta unreferenced_delta =
      MakeModernCatalogDelta(kRegistryId + 6U);
  TextureResourceDescriptor unreferenced_texture =
      MakeRgba8Texture(TextureColorSpace::LINEAR, 128U, 128U, 128U);
  unreferenced_texture.format = TextureResourceFormat::R8_UNORM;
  unreferenced_texture.mip_levels.front().row_pitch_bytes = 1U;
  unreferenced_texture.mip_levels.front().layer_pitch_bytes = 1U;
  unreferenced_texture.mip_levels.front().bytes = {128U};
  RenderAssetMutation unreferenced_texture_mutation;
  unreferenced_texture_mutation.asset =
      Ref(RenderAssetKind::TEXTURE, 10U);
  unreferenced_texture_mutation.payload = std::move(unreferenced_texture);
  SamplerResourceDescriptor unreferenced_sampler;
  unreferenced_sampler.address_u = SamplerAddressMode::CLAMP_TO_BORDER;
  RenderAssetMutation unreferenced_sampler_mutation;
  unreferenced_sampler_mutation.asset = Ref(RenderAssetKind::SAMPLER, 8U);
  unreferenced_sampler_mutation.payload = unreferenced_sampler;
  unreferenced_delta.mutations.push_back(
      std::move(unreferenced_sampler_mutation));
  unreferenced_delta.mutations.push_back(
      std::move(unreferenced_texture_mutation));
  RenderAssetRegistry unreferenced_registry(kRegistryId + 6U);
  Require(unreferenced_registry.Apply(unreferenced_delta).ok(),
          "unreferenced shared-catalog fixture is not structurally valid");
  Require(ValidateOgreNextN1AssetCatalog(unreferenced_registry, false, kModern)
              .ok(),
          "RT4/V1 constrained texture or sampler assets not referenced by a material");

  RenderAssetDelta transparent_base_delta =
      MakeModernCatalogDelta(kRegistryId + 7U);
  std::get<TextureResourceDescriptor>(
      transparent_base_delta.mutations[2U].payload)
      .mip_levels.front()
      .bytes[3U] = 128U;
  RenderAssetRegistry transparent_base_registry(kRegistryId + 7U);
  Require(transparent_base_registry.Apply(transparent_base_delta).ok(),
          "nonopaque base-color fixture is not renderer-contract valid");
  Require(ValidateOgreNextN1AssetCatalog(transparent_base_registry, false,
                                         kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "nonopaque base texture escaped RT4/V1 straight-alpha evidence policy");

  RenderAssetDelta missing_tangent_delta =
      MakeModernCatalogDelta(kRegistryId + 8U);
  std::get<MeshResourceDescriptor>(missing_tangent_delta.mutations[0U].payload)
      .tangents.clear();
  RenderAssetRegistry missing_tangent_registry(kRegistryId + 8U);
  Require(missing_tangent_registry.Apply(missing_tangent_delta).ok(),
          "missing-tangent catalog fixture is not structurally valid");
  Require(ValidateOgreNextN1AssetCatalog(missing_tangent_registry, false, kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "missing authored tangent stream escaped RT4/V1 admission");
}

void TestDisplayDomainUnlitPolicy() {
  constexpr std::uint64_t kRegistryId = 90U;
  constexpr OgreNextRasterFeatureTier kModern =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  const auto validate_catalog = [&](RenderAssetDelta delta) {
    RenderAssetRegistry candidate(delta.registry_id);
    Require(candidate.Apply(delta).ok(),
            "display-domain Unlit mutation is not structurally valid");
    return ValidateOgreNextN1AssetCatalog(candidate, false, kModern);
  };

  RenderAssetRegistry registry(kRegistryId);
  Require(registry.Apply(MakeDisplayDomainUnlitCatalogDelta(kRegistryId)).ok(),
          "display-domain Unlit catalog could not be constructed");
  Require(ValidateOgreNextN1AssetCatalog(registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "legacy N1 silently admitted display-domain Unlit");
  Require(ValidateOgreNextN1AssetCatalog(registry, false, kModern).ok(),
          "exact one-texture display-domain Unlit catalog was rejected");

  RenderAssetDelta before_filter =
      MakeDisplayDomainUnlitCatalogDelta(kRegistryId + 1U);
  std::get<MaterialDescriptor>(before_filter.mutations[1U].payload)
      .base_color_transfer = BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER;
  Require(validate_catalog(std::move(before_filter)).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "generic decode-before-filter Unlit escaped exact admission");

  RenderAssetDelta nonwhite =
      MakeDisplayDomainUnlitCatalogDelta(kRegistryId + 2U);
  std::get<MaterialDescriptor>(nonwhite.mutations[1U].payload)
      .base_color_factor.x = 0.5F;
  Require(validate_catalog(std::move(nonwhite)).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "display-domain Unlit accepted nonwhite vertex-independent modulation");

  RenderAssetDelta incomplete =
      MakeDisplayDomainUnlitCatalogDelta(kRegistryId + 3U);
  std::get<TextureResourceDescriptor>(incomplete.mutations[2U].payload)
      .mip_levels.pop_back();
  Require(validate_catalog(std::move(incomplete)).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "display-domain Unlit accepted an incomplete mip chain");

  RenderAssetDelta trilinear =
      MakeDisplayDomainUnlitCatalogDelta(kRegistryId + 4U);
  std::get<SamplerResourceDescriptor>(trilinear.mutations[3U].payload)
      .mip_filter = SamplerFilter::LINEAR;
  Require(validate_catalog(std::move(trilinear)).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "display-domain Unlit accepted cross-mip interpolation");

  RenderAssetDelta transparent =
      MakeDisplayDomainUnlitCatalogDelta(kRegistryId + 5U);
  std::get<TextureResourceDescriptor>(transparent.mutations[2U].payload)
      .mip_levels.back()
      .bytes[3U] = 254U;
  Require(validate_catalog(std::move(transparent)).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "display-domain Unlit accepted a nonopaque authored mip");

  struct HostileProfileMutation final {
    const char *label;
    std::function<void(RenderAssetDelta &)> mutate;
  };
  const std::vector<HostileProfileMutation> hostile_profile_mutations{
      {"display-domain Unlit accepted alpha testing",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .alpha_test_mode = MaterialAlphaTestMode::GREATER;
       }},
      {"display-domain Unlit accepted double-sided state",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .double_sided = true;
       }},
      {"display-domain Unlit accepted a missing base texture",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .base_color_texture = {};
       }},
      {"display-domain Unlit accepted UV1",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .base_color_texture.texture_coordinate_set = 1U;
       }},
      {"display-domain Unlit accepted base UV scale",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .base_color_texture.scale.x = 0.5F;
       }},
      {"display-domain Unlit accepted base UV offset",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .base_color_texture.offset.x = 0.25F;
       }},
      {"display-domain Unlit accepted base UV rotation",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .base_color_texture.rotation_radians = 0.25F;
       }},
      {"display-domain Unlit accepted a metallic-roughness binding",
       [](RenderAssetDelta &delta) {
         MaterialDescriptor &material =
             std::get<MaterialDescriptor>(delta.mutations[1U].payload);
         material.metallic_roughness_texture = material.base_color_texture;
       }},
      {"display-domain Unlit accepted a normal binding",
       [](RenderAssetDelta &delta) {
         MaterialDescriptor &material =
             std::get<MaterialDescriptor>(delta.mutations[1U].payload);
         material.normal_texture = material.base_color_texture;
       }},
      {"display-domain Unlit accepted an occlusion binding",
       [](RenderAssetDelta &delta) {
         MaterialDescriptor &material =
             std::get<MaterialDescriptor>(delta.mutations[1U].payload);
         material.occlusion_texture = material.base_color_texture;
       }},
      {"display-domain Unlit accepted an emissive binding",
       [](RenderAssetDelta &delta) {
         MaterialDescriptor &material =
             std::get<MaterialDescriptor>(delta.mutations[1U].payload);
         material.emissive_texture = material.base_color_texture;
       }},
      {"display-domain Unlit accepted metallic factor",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .metallic_factor = 0.25F;
       }},
      {"display-domain Unlit accepted roughness factor",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .roughness_factor = 0.75F;
       }},
      {"display-domain Unlit accepted normal scale",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .normal_scale = 0.5F;
       }},
      {"display-domain Unlit accepted occlusion strength",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .occlusion_strength = 0.5F;
       }},
      {"display-domain Unlit accepted emissive factor",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .emissive_factor.x = 0.25F;
       }},
      {"display-domain Unlit accepted emissive strength",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .emissive_strength = 0.5F;
       }},
      {"display-domain Unlit accepted alpha cutoff",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .alpha_cutoff = 0.25F;
       }},
      {"display-domain Unlit accepted noncanonical IOR",
       [](RenderAssetDelta &delta) {
         std::get<MaterialDescriptor>(delta.mutations[1U].payload)
             .index_of_refraction = 1.4F;
       }},
      {"display-domain Unlit accepted an array texture type",
       [](RenderAssetDelta &delta) {
         std::get<TextureResourceDescriptor>(delta.mutations[2U].payload).type =
             TextureResourceType::TEXTURE_2D_ARRAY;
       }},
      {"display-domain Unlit accepted multiple texture layers",
       [](RenderAssetDelta &delta) {
         TextureResourceDescriptor &texture =
             std::get<TextureResourceDescriptor>(delta.mutations[2U].payload);
         texture.type = TextureResourceType::TEXTURE_2D_ARRAY;
         texture.array_layers = 2U;
         for (TextureMipLevelDescriptor &mip : texture.mip_levels) {
           const std::vector<std::uint8_t> layer = mip.bytes;
           mip.bytes.insert(mip.bytes.end(), layer.begin(), layer.end());
         }
       }},
      {"display-domain Unlit accepted a non-RGBA8 format",
       [](RenderAssetDelta &delta) {
         TextureResourceDescriptor &texture =
             std::get<TextureResourceDescriptor>(delta.mutations[2U].payload);
         texture.format = TextureResourceFormat::RGBA16_FLOAT;
         for (TextureMipLevelDescriptor &mip : texture.mip_levels) {
           mip.row_pitch_bytes = static_cast<std::uint64_t>(mip.width) * 8U;
           mip.layer_pitch_bytes =
               mip.row_pitch_bytes * static_cast<std::uint64_t>(mip.height);
           mip.bytes.assign(static_cast<std::size_t>(mip.layer_pitch_bytes),
                            0U);
         }
       }},
      {"display-domain Unlit accepted linear color space",
       [](RenderAssetDelta &delta) {
         std::get<TextureResourceDescriptor>(delta.mutations[2U].payload)
             .color_space = TextureColorSpace::LINEAR;
       }},
      {"display-domain Unlit accepted nearest minification",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .minification_filter = SamplerFilter::NEAREST;
       }},
      {"display-domain Unlit accepted nearest magnification",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .magnification_filter = SamplerFilter::NEAREST;
       }},
      {"display-domain Unlit accepted repeat U addressing",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .address_u = SamplerAddressMode::REPEAT;
       }},
      {"display-domain Unlit accepted repeat V addressing",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .address_v = SamplerAddressMode::REPEAT;
       }},
      {"display-domain Unlit accepted repeat W addressing",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .address_w = SamplerAddressMode::REPEAT;
       }},
      {"display-domain Unlit accepted mip LOD bias",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .mip_lod_bias = 0.25F;
       }},
      {"display-domain Unlit accepted nonzero minimum LOD",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .minimum_lod = 0.25F;
       }},
      {"display-domain Unlit accepted truncated maximum LOD",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .maximum_lod = 0.5F;
       }},
      {"display-domain Unlit accepted anisotropic filtering",
       [](RenderAssetDelta &delta) {
         SamplerResourceDescriptor &sampler =
             std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload);
         sampler.anisotropy_enabled = true;
         sampler.maximum_anisotropy = 2.0F;
       }},
      {"display-domain Unlit accepted latent anisotropy",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .maximum_anisotropy = 2.0F;
       }},
      {"display-domain Unlit accepted comparison sampling",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .compare_enabled = true;
       }},
      {"display-domain Unlit accepted latent compare operation",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .compare_operation = SamplerCompareOperation::LESS;
       }},
      {"display-domain Unlit accepted a noncanonical border color",
       [](RenderAssetDelta &delta) {
         std::get<SamplerResourceDescriptor>(delta.mutations[3U].payload)
             .border_color.x = 1.0F;
       }},
  };
  for (std::size_t index = 0U; index < hostile_profile_mutations.size();
       ++index) {
    RenderAssetDelta hostile = MakeDisplayDomainUnlitCatalogDelta(
        kRegistryId + 10U + static_cast<std::uint64_t>(index));
    hostile_profile_mutations[index].mutate(hostile);
    RenderAssetRegistry candidate(hostile.registry_id);
    const ValidationResult applied = candidate.Apply(hostile);
    if (!applied) {
      // Some exact-profile violations (for example array material textures or
      // comparison sampling) are already rejected by the shared portable
      // material/registry contract. They still prove fail-closed admission;
      // the N1-specific check is exercised for every structurally valid case.
      continue;
    }
    const ValidationResult result =
        ValidateOgreNextN1AssetCatalog(candidate, false, kModern);
    Require(result.code == ValidationCode::UNSUPPORTED_FEATURE,
            hostile_profile_mutations[index].label);
  }

  Require(ValidateOgreNextN1Scene(
              *MakeDisplayDomainUnlitScene(
                  kRegistryId, MESH_INSTANCE_VISIBLE_IN_REFLECTIONS),
              registry, false, kModern)
              .ok(),
          "shadow-free display-domain Unlit scene was rejected");
  Require(ValidateOgreNextN1Scene(
              *MakeDisplayDomainUnlitScene(
                  kRegistryId, MESH_INSTANCE_CASTS_SHADOW),
              registry, false, kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "display-domain Unlit instance was allowed to cast shadows");
  Require(ValidateOgreNextN1Scene(
              *MakeDisplayDomainUnlitScene(
                  kRegistryId, MESH_INSTANCE_RECEIVES_SHADOW),
              registry, false, kModern)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "display-domain Unlit instance was allowed to receive shadows");
}

void TestFrameAndScenePolicy() {
  constexpr std::uint64_t kRegistryId = 72U;
  RenderAssetRegistry registry(kRegistryId);
  Require(registry.Apply(MakeCatalogDelta(kRegistryId)).ok(),
          "frame catalog setup failed");
  const FrontendCapabilityReport capabilities =
      BuildOgreNextN1CapabilityReport(RasterGraphicsApi::VULKAN, "test");
  RenderFrameRequest request = MakeFrame(MakeScene(kRegistryId));
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).ok(),
          "valid static PBR colour frame was rejected");
  request.views.front().visibility_mask = (1U << 29U) | 1U;
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).ok(),
          "static N1 rejected an Ogre-assignable view visibility bit");
  request.views.front().visibility_mask = (1U << 30U) | 1U;
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).field ==
              "views.visibility_mask",
          "static N1 admitted a view mask that aliases Ogre layer state");
  request = MakeFrame(MakeScene(
      kRegistryId, Matrix4x4{}, 1U, {0.03F, 0.03F, 0.03F}, 1.0F,
      MakeMesh().local_bounds, 0.0F, (1U << 28U) | 1U));
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).ok(),
          "static N1 rejected an Ogre-assignable item visibility bit");
  request = MakeFrame(MakeScene(
      kRegistryId, Matrix4x4{}, 1U, {0.03F, 0.03F, 0.03F}, 1.0F,
      MakeMesh().local_bounds, 0.0F, (1U << 31U) | 1U));
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).field ==
              "mesh_instances.visibility_mask",
          "static N1 admitted an item mask that aliases Ogre layer state");

  request = MakeFrame(MakeReflectionScene(kRegistryId));
  const ValidationResult legacy_reflection_validation =
      ValidateOgreNextN1Frame(request, capabilities, registry);
  Require(legacy_reflection_validation.code ==
              ValidationCode::UNSUPPORTED_FEATURE &&
              legacy_reflection_validation.field == "reflection_probes",
          "legacy N1 silently admitted authored reflection probes");
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1)
              .ok(),
          "RT4/V1 rejected its native reflection-probe contract");

  request = MakeFrame(MakeReflectionScene(
      kRegistryId, (1U << 29U) | 1U,
      (std::numeric_limits<std::uint32_t>::max)()));
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1)
              .field == "mesh_instances.visibility_mask",
          "RT4/V1 admitted an authored item mask that aliases PCC proxy state");
  request = MakeFrame(MakeReflectionScene(
      kRegistryId, (1U << 30U) | 1U,
      (std::numeric_limits<std::uint32_t>::max)()));
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1)
              .field == "mesh_instances.visibility_mask",
          "RT4/V1 admitted an authored item mask that aliases Ogre layer state");
  request = MakeFrame(MakeReflectionScene(
      kRegistryId, (std::numeric_limits<std::uint32_t>::max)(),
      (1U << 28U) | 1U));
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1)
              .field == "reflection_probes.visibility_mask",
          "RT4/V1 admitted an authored probe mask that aliases PCC capture state");
  request = MakeFrame(MakeReflectionScene(
      kRegistryId, (std::numeric_limits<std::uint32_t>::max)(),
      (1U << 31U) | 1U));
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1)
              .field == "reflection_probes.visibility_mask",
          "RT4/V1 admitted an authored probe mask that aliases Ogre layer state");
  request = MakeFrame(MakeReflectionScene(kRegistryId));
  request.views.front().visibility_mask = (1U << 29U) | 1U;
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1)
              .field == "views.visibility_mask",
          "RT4/V1 admitted a view mask that aliases PCC proxy state");
  request = MakeFrame(MakeReflectionScene(kRegistryId));
  request.views.front().visibility_mask = (1U << 30U) | 1U;
  Require(ValidateOgreNextN1Frame(
              request, capabilities, registry,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1)
              .field == "views.visibility_mask",
          "RT4/V1 admitted a view mask that aliases Ogre layer state");

  SceneSnapshotDescriptor lit_descriptor;
  lit_descriptor.snapshot_id = 2U;
  lit_descriptor.asset_registry_id = kRegistryId;
  lit_descriptor.asset_sequence = 1U;
  MeshInstanceDescriptor lit_instance;
  lit_instance.instance_id = 1U;
  lit_instance.mesh = Ref(RenderAssetKind::MESH, 1U);
  lit_instance.material = Ref(RenderAssetKind::MATERIAL, 2U);
  lit_instance.local_bounds = MakeMesh().local_bounds;
  lit_descriptor.mesh_instances.push_back(lit_instance);
  LightDescriptor uncalibrated_light;
  uncalibrated_light.light_id = 1U;
  uncalibrated_light.shadow_flags = 0U;
  lit_descriptor.lights.push_back(uncalibrated_light);
  SceneSnapshotCreateResult lit_scene =
      CreateSceneSnapshot(std::move(lit_descriptor));
  Require(lit_scene.ok(), "analytic-light fixture is invalid");
  request = MakeFrame(lit_scene.snapshot);
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "uncalibrated physical light escaped N1 admission");

  request = MakeFrame(MakeScene(kRegistryId));
  request.requested_outputs =
      FrameOutputMask::COLOR | FrameOutputMask::DEPTH;
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "depth output escaped fail-closed capability validation");
  request.requested_outputs = FrameOutputMask::COLOR;
  request.views.front().exposure = 2.0F;
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "unimplemented exposure escaped N1 validation");

  request = MakeFrame(MakeScene(kRegistryId, Matrix4x4{}, 3U,
                                {0.03F, 0.03F, 0.03F}, 1.0F,
                                MakeMesh().local_bounds, 1.0F));
  const ValidationResult scene_exposure_validation =
      ValidateOgreNextN1Frame(request, capabilities, registry);
  Require(scene_exposure_validation.code ==
              ValidationCode::UNSUPPORTED_FEATURE &&
              scene_exposure_validation.field ==
                  "environment.exposure_compensation_ev",
          "unimplemented scene exposure escaped N1 validation");

  request = MakeFrame(MakeScene(kRegistryId));
  FrontendCapabilityReport constrained_capabilities = capabilities;
  constrained_capabilities.maximum_texture_dimension_2d = 128U;
  Require(ValidateOgreNextN1Frame(request, constrained_capabilities, registry)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "device texture limit did not constrain N1 frame extent");

  Matrix4x4 shear;
  shear.elements[4U] = 0.25F;
  request = MakeFrame(MakeScene(kRegistryId, shear));
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "affine shear was silently decomposed");

  Matrix4x4 legacy_non_uniform_scale;
  legacy_non_uniform_scale.elements[0U] = 2.0F;
  request = MakeFrame(MakeScene(kRegistryId, legacy_non_uniform_scale));
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).ok(),
          "texture-free N1 unexpectedly inherited the RT4/V1 scale gate");

  MaterialDescriptor double_sided = MakeMaterial();
  double_sided.double_sided = true;
  RenderAssetRegistry double_sided_registry(kRegistryId + 2U);
  Require(double_sided_registry
              .Apply(MakeCatalogDelta(kRegistryId + 2U, MakeMesh(),
                                      double_sided))
              .ok(),
          "double-sided mirror fixture catalog is invalid");
  Matrix4x4 mirrored;
  mirrored.elements[0U] = -1.0F;
  request = MakeFrame(MakeScene(kRegistryId + 2U, mirrored));
  Require(ValidateOgreNextN1Frame(request, capabilities,
                                  double_sided_registry)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "mirrored TRS escaped Ogre signed-radius admission");

  request = MakeFrame(MakeScene(
      kRegistryId, Matrix4x4{}, 3U,
      {(std::numeric_limits<float>::max)(), 0.0F, 0.0F}, 2.0F));
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "finite ambient values that overflow Ogre color escaped admission");

  MeshResourceDescriptor hostile_mesh = MakeMesh();
  const float maximum = (std::numeric_limits<float>::max)();
  hostile_mesh.local_bounds.minimum = {maximum, maximum, maximum};
  hostile_mesh.local_bounds.maximum = hostile_mesh.local_bounds.minimum;
  hostile_mesh.positions.assign(3U, hostile_mesh.local_bounds.minimum);
  RenderAssetRegistry hostile_registry(kRegistryId + 1U);
  Require(hostile_registry
              .Apply(MakeCatalogDelta(kRegistryId + 1U, hostile_mesh,
                                      MakeMaterial()))
              .ok() &&
              ValidateOgreNextN1AssetCatalog(hostile_registry).ok(),
          "hostile world-bound fixture did not pass isolated asset admission");
  Matrix4x4 hostile_transform;
  hostile_transform.elements[12U] = maximum;
  request = MakeFrame(MakeScene(kRegistryId + 1U, hostile_transform, 1U,
                                {0.03F, 0.03F, 0.03F}, 1.0F,
                                hostile_mesh.local_bounds));
  Require(ValidateOgreNextN1Frame(request, capabilities, hostile_registry)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "finite local bounds plus TRS overflow escaped world-bound admission");

  constexpr float kSqrtHalf = 0.707106769F;
  Matrix4x4 cancellation_transform;
  cancellation_transform.elements[0U] = -kSqrtHalf;
  cancellation_transform.elements[2U] = kSqrtHalf;
  cancellation_transform.elements[8U] = kSqrtHalf;
  cancellation_transform.elements[10U] = kSqrtHalf;
  cancellation_transform.elements[12U] = maximum * 0.55F;
  Bounds3 cancellation_point;
  cancellation_point.minimum = {maximum, 0.0F, maximum * 0.75F};
  cancellation_point.maximum = cancellation_point.minimum;
  Require(!CanRepresentOgreNextN1WorldBounds(cancellation_point,
                                              cancellation_transform),
          "cancellation-heavy TRS escaped native SIMD-order overflow admission");

  const float minimum_normal = (std::numeric_limits<float>::min)();
  const float hostile_near = minimum_normal * 0.5F;
  const float hostile_far = minimum_normal;
  Matrix4x4 hostile_projection;
  hostile_projection.elements.fill(0.0F);
  hostile_projection.elements[0U] = 1.0F;
  hostile_projection.elements[5U] = 1.0F;
  hostile_projection.elements[10U] =
      1.0F / (hostile_near - hostile_far);
  hostile_projection.elements[14U] =
      hostile_near * hostile_projection.elements[10U];
  hostile_projection.elements[15U] = 1.0F;
  request = MakeFrame(MakeScene(kRegistryId));
  request.views.front().near_plane = hostile_near;
  request.views.front().far_plane = hostile_far;
  request.views.front().clip_from_view = hostile_projection;
  request.views.front().previous_clip_from_view = hostile_projection;
  Require(ValidateRenderFrameRequest(request).ok(),
          "hostile finite projection frame fixture is not contract valid");
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "finite projection overflow escaped N1 native admission");
}

void TestAnalyticSkyNativeMeshIsCameraLocalAndTransactional() {
  using namespace RoR::Render;
  SceneEnvironmentDescriptor environment;
  environment.environment_intensity = 2.0F;
  environment.analytic_sky.enabled = true;
  environment.analytic_sky.sun_light_id = 7U;
  environment.analytic_sky.zenith_radiance = {0.1F, 0.2F, 0.3F};
  environment.analytic_sky.horizon_radiance = {0.4F, 0.5F, 0.6F};
  environment.analytic_sky.ground_radiance = {0.01F, 0.02F, 0.03F};
  environment.analytic_sky.sun_disk_radiance = {8.0F, 7.0F, 6.0F};
  environment.analytic_sky.sun_angular_radius_radians = 0.00465047F;
  LightDescriptor sun;
  sun.light_id = 7U;
  sun.type = LightType::DIRECTIONAL;
  sun.direction = {0.0F, -0.8F, -0.6F};

  OgreNextAnalyticSkyNativeMesh mesh;
  ValidationResult result = BuildOgreNextAnalyticSkyNativeMesh(
      environment, sun, 2.0F, mesh);
  constexpr std::size_t kRingVertices =
      kOgreNextAnalyticSkyLongitudeSegments + 1U;
  constexpr std::size_t kHemisphereVertices =
      kOgreNextAnalyticSkyHemisphereRings * kRingVertices + 1U;
  constexpr std::size_t kHemisphereIndices =
      (kOgreNextAnalyticSkyHemisphereRings - 1U) *
          kOgreNextAnalyticSkyLongitudeSegments * 6U +
      kOgreNextAnalyticSkyLongitudeSegments * 3U;
  Require(result.ok() &&
              mesh.background_vertices.size() == kHemisphereVertices * 2U &&
              mesh.background_indices.size() == kHemisphereIndices * 2U &&
              mesh.sun_vertices.size() ==
                  kOgreNextAnalyticSkySunSegments + 2U &&
              mesh.sun_indices.size() ==
                  kOgreNextAnalyticSkySunSegments * 3U,
          "native analytic-sky mesh topology changed");
  const OgreNextAnalyticSkyNativeVertex &upper_horizon =
      mesh.background_vertices.front();
  const OgreNextAnalyticSkyNativeVertex &upper_pole =
      mesh.background_vertices[kHemisphereVertices - 1U];
  const OgreNextAnalyticSkyNativeVertex &lower_horizon =
      mesh.background_vertices[kHemisphereVertices];
  Require(upper_horizon.position == lower_horizon.position &&
              upper_horizon.radiance == Float4{0.8F, 1.0F, 1.2F, 1.0F} &&
              lower_horizon.radiance ==
                  Float4{0.02F, 0.04F, 0.06F, 1.0F} &&
              upper_pole.position == Float3{0.0F, 2.0F, 0.0F} &&
              upper_pole.radiance == Float4{0.2F, 0.4F, 0.6F, 1.0F},
          "native analytic-sky horizon discontinuity or linear radiance changed");
  Require(mesh.sun_vertices.front().position == Float3{0.0F, 1.6F, 1.2F} &&
              mesh.sun_vertices.front().radiance ==
                  Float4{16.0F, 14.0F, 12.0F, 1.0F},
          "native analytic sun is not centred on the negated emitted-light direction");
  const Float3 edge = mesh.sun_vertices[1U].position;
  const float edge_length =
      std::sqrt(edge.x * edge.x + edge.y * edge.y + edge.z * edge.z);
  const float centre_dot_edge =
      (0.8F * edge.y + 0.6F * edge.z) / edge_length;
  Require(std::fabs(edge_length - 2.0F) <= 1.0e-5F &&
              std::fabs(centre_dot_edge -
                        std::cos(environment.analytic_sky
                                     .sun_angular_radius_radians)) <=
                  1.0e-5F,
          "native analytic sun cap changed angular radius");

  const std::size_t accepted_background_size = mesh.background_vertices.size();
  const std::size_t accepted_sun_index_size = mesh.sun_indices.size();
  const Float3 accepted_first_position =
      mesh.background_vertices.front().position;
  sun.light_id = 8U;
  result = BuildOgreNextAnalyticSkyNativeMesh(environment, sun, 2.0F, mesh);
  Require(!result && result.field ==
                         "environment.analytic_sky.sun_light_id" &&
              mesh.background_vertices.size() == accepted_background_size &&
              mesh.sun_indices.size() == accepted_sun_index_size &&
              mesh.background_vertices.front().position ==
                  accepted_first_position,
          "mismatched sky/light identity changed the published native mesh");
}

} // namespace

int main() {
  TestProjectionDepthConversion();
  TestLifetimeSubmissionState();
  TestCapabilitiesFailClosed();
  TestHdrFeatureCombinationPolicy();
  TestNativeDirectionalShadowScenePolicy();
  TestInitializationPolicy();
  TestAssetPolicy();
  TestModernPbrAssetPolicy();
  TestDisplayDomainUnlitPolicy();
  TestAnalyticSkyNativeMeshIsCameraLocalAndTransactional();
  TestFrameAndScenePolicy();
  std::cout << "Ogre-Next N1 fail-closed policy tests passed\n";
  return EXIT_SUCCESS;
}
