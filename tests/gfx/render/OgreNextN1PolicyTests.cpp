/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Policy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using namespace RoR::Render;

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
          float environment_intensity = 1.0F) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = 1U;
  descriptor.environment.ambient_radiance = ambient_radiance;
  descriptor.environment.environment_intensity = environment_intensity;
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = Ref(RenderAssetKind::MESH, 1U);
  instance.material = Ref(RenderAssetKind::MATERIAL, 2U);
  instance.render_from_object = transform;
  instance.previous_render_from_object = transform;
  instance.local_bounds = MakeMesh().local_bounds;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  Require(result.ok(), "test scene contract is invalid");
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
  const Matrix4x4 converted =
      ConvertPortableProjectionToOgreClip(portable);
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
}

void TestLifetimeSubmissionState() {
  constexpr std::uint64_t kRegistryId = 70U;
  const auto first_scene = MakeScene(kRegistryId);
  OgreNextN1SubmissionState state;
  RenderFrameRequest request = MakeFrame(first_scene);
  Require(state.Validate(request).ok(), "first submission identity was rejected");
  state.Commit(request);
  Require(state.IsFrameComplete(1U), "committed synchronous frame is incomplete");

  request.frame_id = 2U;
  Require(state.Validate(request).ok(), "latest snapshot replay was rejected");
  state.Commit(request);

  RenderFrameRequest aliased = MakeFrame(MakeScene(kRegistryId));
  aliased.frame_id = 3U;
  Require(state.Validate(aliased).code == RenderOperationCode::RESOURCE_STALE,
          "same snapshot ID with a different owner escaped identity checks");

  RenderFrameRequest newer =
      MakeFrame(MakeScene(kRegistryId, Matrix4x4{}, 2U));
  newer.frame_id = 3U;
  Require(state.Validate(newer).ok(), "new monotonic snapshot was rejected");
  state.Commit(newer);
  request.frame_id = 4U;
  Require(state.Validate(request).ok(),
          "exact older snapshot replay was rejected");
  state.Commit(request);

  aliased.frame_id = 5U;
  Require(state.Validate(aliased).code == RenderOperationCode::RESOURCE_STALE,
          "older snapshot ID alias escaped lifetime identity checks");

  for (std::uint64_t frame_id = 5U; frame_id <= 130U; ++frame_id) {
    newer.frame_id = frame_id;
    Require(state.Validate(newer).ok(), "lifetime history fixture was rejected");
    state.Commit(newer);
  }
  newer.frame_id = 132U;
  Require(state.Validate(newer).ok(), "sparse frame ID was rejected");
  state.Commit(newer);
  Require(state.IsFrameComplete(1U) && state.IsFrameComplete(130U) &&
              state.IsFrameComplete(132U),
          "successful frame fell out of lifetime completion history");
  Require(!state.IsFrameComplete(131U),
          "never-submitted frame appeared in completion history");
  state.Reset();
  request.frame_id = 1U;
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
              metal.maximum_texture_dimension_2d ==
                  kOgreNextN1ConservativeMaximumTextureDimension,
          "N1 did not report its admitted raster surface");
  Require(metal.native_api == NativeGraphicsApi::NONE &&
              !metal.supports_compute && !metal.supports_async_compute &&
              !metal.supports_dynamic_mesh_updates &&
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
          "dynamic geometry escaped N1 admission");

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
  uncalibrated_light.casts_shadows = false;
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

  request = MakeFrame(MakeScene(
      kRegistryId, Matrix4x4{}, 3U,
      {(std::numeric_limits<float>::max)(), 0.0F, 0.0F}, 2.0F));
  Require(ValidateOgreNextN1Frame(request, capabilities, registry).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "finite ambient values that overflow Ogre color escaped admission");
}

} // namespace

int main() {
  TestProjectionDepthConversion();
  TestLifetimeSubmissionState();
  TestCapabilitiesFailClosed();
  TestInitializationPolicy();
  TestAssetPolicy();
  TestFrameAndScenePolicy();
  std::cout << "Ogre-Next N1 fail-closed policy tests passed\n";
  return EXIT_SUCCESS;
}
