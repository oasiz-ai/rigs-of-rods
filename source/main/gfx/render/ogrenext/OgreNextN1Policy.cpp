/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Policy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace RoR::Render {
namespace {

ValidationResult Unsupported(const char *field, const char *detail,
                             std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(ValidationCode::UNSUPPORTED_FEATURE, field,
                                   detail, index);
}

bool IsTextureFree(const MaterialDescriptor &material) noexcept {
  const TextureBinding *bindings[] = {
      &material.base_color_texture,
      &material.metallic_roughness_texture,
      &material.normal_texture,
      &material.occlusion_texture,
      &material.emissive_texture,
  };
  for (const TextureBinding *binding : bindings) {
    if (!IsAbsentRenderAssetReference(binding->texture) ||
        !IsAbsentRenderAssetReference(binding->sampler)) {
      return false;
    }
  }
  return true;
}

bool IsIdentityTextureTransform(const TextureBinding &binding) noexcept {
  return binding.texture_coordinate_set == 0U &&
         binding.scale == Float2{1.0F, 1.0F} && binding.offset == Float2{} &&
         binding.rotation_radians == 0.0F;
}

bool UsesClampToBorder(const SamplerResourceDescriptor &sampler) noexcept {
  return sampler.address_u == SamplerAddressMode::CLAMP_TO_BORDER ||
         sampler.address_v == SamplerAddressMode::CLAMP_TO_BORDER ||
         sampler.address_w == SamplerAddressMode::CLAMP_TO_BORDER;
}

ValidationResult ValidateModernTexturePolicy(
    const TextureResourceDescriptor &texture, std::size_t index) {
  if (texture.type != TextureResourceType::TEXTURE_2D ||
      texture.array_layers != 1U ||
      texture.format != TextureResourceFormat::RGBA8_UNORM) {
    return Unsupported(
        "assets.texture.format",
        "RT4/V1 admits non-array RGBA8 material textures only; this keeps sRGB decode and linear metallic/roughness uploads identical on Metal, D3D11, and Vulkan",
        index);
  }
  return ValidationResult::Success();
}

bool HasOpaqueRgba8Alpha(const TextureResourceDescriptor &texture) noexcept {
  for (const TextureMipLevelDescriptor &mip : texture.mip_levels) {
    for (std::uint32_t row = 0U; row < mip.height; ++row) {
      const auto *source_row = mip.bytes.data() +
                               static_cast<std::size_t>(row) *
                                   mip.row_pitch_bytes;
      for (std::uint32_t column = 0U; column < mip.width; ++column) {
        if (source_row[static_cast<std::size_t>(column) * 4U + 3U] != 255U) {
          return false;
        }
      }
    }
  }
  return true;
}

ValidationResult ValidateModernSamplerPolicy(
    const SamplerResourceDescriptor &sampler, std::size_t index) {
  if (UsesClampToBorder(sampler)) {
    return Unsupported(
        "assets.sampler.address",
        "RT4/V1 rejects clamp-to-border because pinned Ogre maps it to clamp-to-edge on Metal and does not preserve the portable border colour on Vulkan",
        index);
  }
  if (sampler.mip_lod_bias != 0.0F) {
    return Unsupported(
        "assets.sampler.mip_lod_bias",
        "RT4/V1 requires zero mip LOD bias because pinned Ogre does not forward it to Metal samplers",
        index);
  }
  if (sampler.anisotropy_enabled &&
      (sampler.minification_filter != SamplerFilter::LINEAR ||
       sampler.magnification_filter != SamplerFilter::LINEAR ||
       sampler.mip_filter != SamplerFilter::LINEAR ||
       std::floor(sampler.maximum_anisotropy) !=
           sampler.maximum_anisotropy)) {
    return Unsupported(
        "assets.sampler.maximum_anisotropy",
        "RT4/V1 anisotropy requires linear min/mag/mip filters and an integral level so D3D11, Metal, and Vulkan receive the same state",
        index);
  }
  return ValidationResult::Success();
}

bool IsFiniteScaled(const Float3 &value, float scale) noexcept {
  return IsFinite(value.x * scale) && IsFinite(value.y * scale) &&
         IsFinite(value.z * scale);
}

bool IsWithinNativeFloatAccumulation(double magnitude) noexcept {
  const double guarded_maximum = static_cast<double>(std::nextafter(
      (std::numeric_limits<float>::max)(), 0.0F));
  return std::isfinite(magnitude) &&
         magnitude <= guarded_maximum;
}

bool IsTrsRepresentable(const Matrix4x4 &matrix) noexcept {
  if (!HasInvertibleAffineTransform(matrix)) {
    return false;
  }
  const Float3 columns[] = {
      {matrix.elements[0U], matrix.elements[1U], matrix.elements[2U]},
      {matrix.elements[4U], matrix.elements[5U], matrix.elements[6U]},
      {matrix.elements[8U], matrix.elements[9U], matrix.elements[10U]},
  };
  const auto length_squared = [](const Float3 &v) noexcept {
    return v.x * v.x + v.y * v.y + v.z * v.z;
  };
  const auto dot = [](const Float3 &lhs, const Float3 &rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  };
  const float lengths[] = {length_squared(columns[0]),
                           length_squared(columns[1]),
                           length_squared(columns[2])};
  constexpr float kRelativeOrthogonalityTolerance = 1.0e-5F;
  for (std::size_t lhs = 0U; lhs < 3U; ++lhs) {
    for (std::size_t rhs = lhs + 1U; rhs < 3U; ++rhs) {
      const float denominator = std::sqrt(lengths[lhs] * lengths[rhs]);
      if (!IsFinite(denominator) || denominator <= 0.0F ||
          std::fabs(dot(columns[lhs], columns[rhs])) >
              kRelativeOrthogonalityTolerance * denominator) {
        return false;
      }
    }
  }
  return true;
}

ValidationResult ValidateMeshPolicy(const MeshResourceDescriptor &mesh,
                                    std::size_t index,
                                    bool allow_dynamic_meshes,
                                    OgreNextRasterFeatureTier raster_feature_tier) {
  if (mesh.dynamic && !allow_dynamic_meshes) {
    return Unsupported("assets.mesh.dynamic",
                       "N1 accepts immutable static meshes only", index);
  }
  if (mesh.topology != MeshPrimitiveTopology::TRIANGLE_LIST) {
    return Unsupported("assets.mesh.topology",
                       "N1 accepts triangle-list meshes only", index);
  }
  if (mesh.normals.empty()) {
    return Unsupported("assets.mesh.normals",
                       "N1 PBR meshes require authored normals", index);
  }
  if (raster_feature_tier ==
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
    if (mesh.tangents.empty() || mesh.texture_coordinates_0.empty()) {
      return Unsupported(
          "assets.mesh.vertex_streams",
          "RT4/V1 requires authored tangent and UV0 streams for its cross-renderer PBS layout",
          index);
    }
    if (!mesh.velocities.empty() || !mesh.texture_coordinates_1.empty() ||
        !mesh.colors.empty()) {
      return Unsupported(
          "assets.mesh.vertex_streams",
          "RT4/V1 admits position, normal, tangent, and UV0 streams only",
          index);
    }
  } else if (!mesh.tangents.empty() || !mesh.velocities.empty() ||
             !mesh.texture_coordinates_0.empty() ||
             !mesh.texture_coordinates_1.empty() || !mesh.colors.empty()) {
    return Unsupported(
        "assets.mesh.vertex_streams",
        "N1 preserves only position and normal streams; richer streams require the explicit RT4/V1 tier",
        index);
  }
  OgreNextN1NativeMeshBounds native_bounds;
  if (!TryBuildOgreNextN1NativeMeshBounds(mesh.local_bounds, native_bounds)) {
    return Unsupported(
        "assets.mesh.local_bounds",
        "finite portable bounds overflow Ogre's native Aabb or sphere arithmetic",
        index);
  }
  return ValidationResult::Success();
}

ValidationResult ValidateMaterialPolicy(const MaterialDescriptor &material,
                                        std::size_t index,
                                        OgreNextRasterFeatureTier raster_feature_tier) {
  if (material.model != MaterialModel::PBR_METALLIC_ROUGHNESS ||
      material.alpha_mode != MaterialAlphaMode::OPAQUE) {
    return Unsupported(
        "assets.material.model",
        "N1 accepts opaque metallic-roughness PBR materials only", index);
  }
  if (raster_feature_tier == OgreNextRasterFeatureTier::STATIC_PBR_N1 &&
      !IsTextureFree(material)) {
    return Unsupported("assets.material.textures",
                       "N1 materials must be completely texture free", index);
  }
  if (material.base_color_factor.w != 1.0F) {
    return Unsupported("assets.material.base_color_factor",
                       "N1 opaque output requires an alpha factor of one",
                       index);
  }
  if (std::fabs(material.index_of_refraction - 1.5F) > 1.0e-6F) {
    return Unsupported(
        "assets.material.index_of_refraction",
        "the pinned Ogre metallic workflow fixes dielectric F0 at IOR 1.5",
        index);
  }
  if (material.roughness_factor < 1.0e-4F) {
    return Unsupported(
        "assets.material.roughness_factor",
        "N1 rejects near-zero roughness that can produce non-finite PBS shaders",
        index);
  }
  if (!IsFiniteScaled(material.emissive_factor,
                      material.emissive_strength)) {
    return Unsupported(
        "assets.material.emissive",
        "finite emissive inputs overflow Ogre's native PBS color arithmetic",
        index);
  }
  if (raster_feature_tier ==
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
    if (material.normal_texture.texture.valid()) {
      return Unsupported(
          "assets.material.normal_texture",
          "RT4/V1 keeps normal textures fail-closed because pinned PBS reconstructs positive Z from decoded RG instead of preserving the canonical 2*RGB-1 normal",
          index);
    }
    const TextureBinding *supported_bindings[] = {
        &material.base_color_texture,
        &material.metallic_roughness_texture,
        &material.emissive_texture,
    };
    for (const TextureBinding *binding : supported_bindings) {
      if (binding->texture.valid() && !IsIdentityTextureTransform(*binding)) {
        return Unsupported(
            "assets.material.texture_transform",
            "RT4/V1 supports authored UV0 with an identity texture transform only; pinned PBS has no exact general base/emissive transform API",
            index);
      }
    }
    if (material.occlusion_texture.texture.valid()) {
      return Unsupported(
          "assets.material.occlusion_texture",
          "RT4/V1 keeps occlusion fail-closed because pinned HLMS PBS has no ambient-occlusion texture slot",
          index);
    }
  }
  return ValidationResult::Success();
}

} // namespace

bool TryBuildOgreNextN1NativeMeshBounds(
    const Bounds3 &portable,
    OgreNextN1NativeMeshBounds &native) noexcept {
  if (!IsValid(portable)) {
    return false;
  }
  // Halving each operand before addition/subtraction avoids overflowing for
  // ordered finite endpoints whose center or half-size is representable.
  const Float3 center{
      portable.minimum.x * 0.5F + portable.maximum.x * 0.5F,
      portable.minimum.y * 0.5F + portable.maximum.y * 0.5F,
      portable.minimum.z * 0.5F + portable.maximum.z * 0.5F,
  };
  const Float3 half_size{
      portable.maximum.x * 0.5F - portable.minimum.x * 0.5F,
      portable.maximum.y * 0.5F - portable.minimum.y * 0.5F,
      portable.maximum.z * 0.5F - portable.minimum.z * 0.5F,
  };
  if (!IsFinite(center) || !IsNonNegative(half_size)) {
    return false;
  }
  const float radius_squared = half_size.x * half_size.x +
                               half_size.y * half_size.y +
                               half_size.z * half_size.z;
  const float radius = std::sqrt(radius_squared);
  if (!IsFinite(radius_squared) || !IsFinite(radius)) {
    return false;
  }
  native.center = center;
  native.half_size = half_size;
  native.radius = radius;
  return true;
}

bool CanRepresentOgreNextN1WorldBounds(
    const Bounds3 &local_bounds,
    const Matrix4x4 &render_from_object) noexcept {
  if (!IsTrsRepresentable(render_from_object)) {
    return false;
  }
  OgreNextN1NativeMeshBounds bounds;
  if (!TryBuildOgreNextN1NativeMeshBounds(local_bounds, bounds)) {
    return false;
  }

  for (std::size_t row = 0U; row < 3U; ++row) {
    const float matrix_x = render_from_object.elements[row];
    const float matrix_y = render_from_object.elements[4U + row];
    const float matrix_z = render_from_object.elements[8U + row];
    const float translation = render_from_object.elements[12U + row];
    const double center_magnitude =
        std::fabs(static_cast<double>(matrix_x) * bounds.center.x) +
        std::fabs(static_cast<double>(matrix_y) * bounds.center.y) +
        std::fabs(static_cast<double>(matrix_z) * bounds.center.z) +
        std::fabs(static_cast<double>(translation));
    if (!IsWithinNativeFloatAccumulation(center_magnitude)) {
      return false;
    }
    const double half_magnitude =
        std::fabs(static_cast<double>(matrix_x)) * bounds.half_size.x +
        std::fabs(static_cast<double>(matrix_y)) * bounds.half_size.y +
        std::fabs(static_cast<double>(matrix_z)) * bounds.half_size.z;
    if (!IsWithinNativeFloatAccumulation(half_magnitude)) {
      return false;
    }
    // Bound the complete endpoint from the original operands. Ogre's SIMD
    // implementation is free to regroup or fuse the center and half-size
    // arithmetic, so checking one scalar evaluation order is not sufficient.
    if (!IsWithinNativeFloatAccumulation(center_magnitude + half_magnitude)) {
      return false;
    }
  }

  float maximum_scale = 0.0F;
  for (std::size_t column = 0U; column < 3U; ++column) {
    const float x = render_from_object.elements[column * 4U];
    const float y = render_from_object.elements[column * 4U + 1U];
    const float z = render_from_object.elements[column * 4U + 2U];
    const float length_squared = x * x + y * y + z * z;
    const float length = std::sqrt(length_squared);
    if (!IsFinite(length_squared) || !IsFinite(length)) {
      return false;
    }
    maximum_scale = (std::max)(maximum_scale, length);
  }
  return IsWithinNativeFloatAccumulation(
      static_cast<double>(bounds.radius) * maximum_scale);
}

RenderOperationResult
OgreNextN1SubmissionState::Validate(const RenderFrameRequest &request) const {
  if (last_frame_id_ == (std::numeric_limits<std::uint64_t>::max)() ||
      request.frame_id != last_frame_id_ + 1U) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "N1 frame IDs must be contiguous from one after every successful submission");
  }
  if (!request.scene_snapshot) {
    return RenderOperationResult::Failure(RenderOperationCode::INVALID_ARGUMENT,
                                          "N1 scene snapshot is missing");
  }
  const std::uint64_t snapshot_id = request.scene_snapshot->snapshot_id();
  const auto seen = snapshots_.find(snapshot_id);
  if (seen != snapshots_.end()) {
    const std::shared_ptr<const SceneSnapshot> expected = seen->second.lock();
    if (!expected || expected.get() != request.scene_snapshot.get() ||
        expected.owner_before(request.scene_snapshot) ||
        request.scene_snapshot.owner_before(expected)) {
      return RenderOperationResult::Failure(
          RenderOperationCode::RESOURCE_STALE,
          "one N1 snapshot ID identified a different immutable object");
    }
  }
  if (seen == snapshots_.end() && snapshot_id <= last_snapshot_id_) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "new N1 snapshot IDs must exceed every first-seen snapshot ID");
  }
  return RenderOperationResult::Success();
}

void OgreNextN1SubmissionState::Commit(const RenderFrameRequest &request) {
  for (auto iterator = snapshots_.begin(); iterator != snapshots_.end();) {
    if (iterator->second.expired()) {
      iterator = snapshots_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  const std::uint64_t snapshot_id = request.scene_snapshot->snapshot_id();
  if (snapshots_.find(snapshot_id) == snapshots_.end()) {
    snapshots_.emplace(snapshot_id,
                       std::weak_ptr<const SceneSnapshot>(
                           request.scene_snapshot));
    last_snapshot_id_ = snapshot_id;
  }
  last_frame_id_ = request.frame_id;
}

bool OgreNextN1SubmissionState::IsFrameComplete(
    std::uint64_t frame_id) const noexcept {
  return frame_id != 0U && frame_id <= last_frame_id_;
}

std::size_t
OgreNextN1SubmissionState::TrackedSnapshotIdentityCount() const noexcept {
  return snapshots_.size();
}

void OgreNextN1SubmissionState::Reset() noexcept {
  snapshots_.clear();
  last_frame_id_ = 0U;
  last_snapshot_id_ = 0U;
}

bool TryConvertPortableProjectionToOgreClip(
    const Matrix4x4 &portable, Matrix4x4 &converted) noexcept {
  Matrix4x4 candidate = portable;
  for (std::size_t column = 0U; column < 4U; ++column) {
    const std::size_t row_two = column * 4U + 2U;
    const std::size_t row_three = column * 4U + 3U;
    candidate.elements[row_two] =
        2.0F * portable.elements[row_two] - portable.elements[row_three];
  }
  if (!IsFinite(candidate)) {
    return false;
  }
  converted = candidate;
  return true;
}

FrontendCapabilityReport
BuildOgreNextN1CapabilityReport(RasterGraphicsApi raster_api,
                                const char *frontend_version) {
  FrontendCapabilityReport report;
  report.frontend_kind = RendererFrontendKind::OGRE_NEXT;
  report.raster_api = raster_api;
  report.native_api = NativeGraphicsApi::NONE;
  report.frontend_name = "ror-ogre-next-n1";
  report.frontend_version = frontend_version != nullptr ? frontend_version : "unknown";
  report.maximum_texture_dimension_2d =
      kOgreNextN1ConservativeMaximumTextureDimension;
  report.maximum_views = 1U;
  report.maximum_frames_in_flight = 1U;
  report.supported_outputs = FrameOutputMask::COLOR;
  report.raster_ready = raster_api == RasterGraphicsApi::METAL ||
                        raster_api == RasterGraphicsApi::DIRECT3D11 ||
                        raster_api == RasterGraphicsApi::VULKAN;
  report.supports_hdr_output = true;
  return report;
}

ValidationResult ValidateOgreNextN1Initialization(
    const FrontendInitializationRequest &request,
    const FrontendCapabilityReport &capabilities) {
  ValidationResult validation = ValidateFrontendCapabilityReport(capabilities);
  if (!validation) {
    return validation;
  }
  validation = ValidateFrontendInitializationRequest(request);
  if (!validation) {
    return validation;
  }
  if (!request.headless) {
    return Unsupported("headless",
                       "N1 is an offscreen frontend and cannot present");
  }
  if (request.maximum_frames_in_flight != 1U) {
    return Unsupported("maximum_frames_in_flight",
                       "N1 completes exactly one synchronous frame at a time");
  }
  // The pre-device report is deliberately conservative, but it is not an
  // artificial allocation ceiling. The concrete frontend creates only its
  // 64x64 hidden bootstrap here, queries the real device limit, and validates
  // this requested offscreen extent before creating a frame target.
  return ValidationResult::Success();
}

ValidationResult
ValidateOgreNextN1AssetCatalog(const RenderAssetRegistry &registry,
                               bool allow_dynamic_meshes,
                               OgreNextRasterFeatureTier raster_feature_tier) {
  if (registry.registry_id() == 0U || registry.sequence() == 0U) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "asset_registry",
        "N1 requires a synchronized nonzero asset catalog");
  }
  if (!IsKnownOgreNextRasterFeatureTier(raster_feature_tier)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "raster_feature_tier",
                                     "unknown Ogre-Next raster feature tier");
  }
  std::size_t index = 0U;
  return registry.VisitRecords([&](const RenderAssetRecord &record) {
    const std::size_t record_index = index++;
    if (!record.live()) {
      return ValidationResult::Success();
    }
    if (const auto *mesh =
            std::get_if<MeshResourceDescriptor>(record.payload.get())) {
      const ValidationResult validation =
          ValidateMeshPolicy(*mesh, record_index, allow_dynamic_meshes,
                             raster_feature_tier);
      if (!validation) {
        return validation;
      }
      return ValidationResult::Success();
    }
    if (const auto *material =
            std::get_if<MaterialDescriptor>(record.payload.get())) {
      const ValidationResult validation =
          ValidateMaterialPolicy(*material, record_index,
                                 raster_feature_tier);
      if (!validation) {
        return validation;
      }
      if (raster_feature_tier ==
          OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
        const TextureBinding *supported_bindings[] = {
            &material->base_color_texture,
            &material->metallic_roughness_texture,
            &material->emissive_texture,
        };
        for (const TextureBinding *binding : supported_bindings) {
          if (!binding->texture.valid()) {
            continue;
          }
          const TextureResourceDescriptor *texture =
              registry.ResolveTexture(binding->texture);
          const SamplerResourceDescriptor *sampler =
              registry.ResolveSampler(binding->sampler);
          if (texture == nullptr || sampler == nullptr) {
            return ValidationResult::Failure(
                ValidationCode::MISSING_REFERENCE,
                "assets.material.texture_binding",
                "RT4/V1 material dependency could not be resolved",
                record_index);
          }
          ValidationResult binding_validation =
              ValidateModernTexturePolicy(*texture, record_index);
          if (!binding_validation) {
            return binding_validation;
          }
          if (binding == &material->base_color_texture &&
              !HasOpaqueRgba8Alpha(*texture)) {
            return Unsupported(
                "assets.material.base_color_texture.alpha",
                "RT4/V1 opaque materials require alpha 255 in every authored base-color texel and mip",
                record_index);
          }
          binding_validation =
              ValidateModernSamplerPolicy(*sampler, record_index);
          if (!binding_validation) {
            return binding_validation;
          }
        }
      }
      return ValidationResult::Success();
    }
    if (raster_feature_tier ==
        OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
      if (std::holds_alternative<TextureResourceDescriptor>(*record.payload) ||
          std::holds_alternative<SamplerResourceDescriptor>(*record.payload)) {
        // Asset registries may be shared across frontends. Constrain only the
        // texture/sampler pairs actually referenced by an admitted material.
        return ValidationResult::Success();
      }
    }
    return Unsupported("assets.kind",
                       "N1 catalog accepts only live meshes and PBR materials unless RT4/V1 is explicitly selected",
                       record_index);
  });
}

ValidationResult ValidateOgreNextN1Scene(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    bool allow_dynamic_meshes,
    OgreNextRasterFeatureTier raster_feature_tier) {
  if (!IsKnownOgreNextRasterFeatureTier(raster_feature_tier)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "raster_feature_tier",
                                     "unknown Ogre-Next raster feature tier");
  }
  ValidationResult validation = ValidateSceneSnapshotAssets(snapshot, registry);
  if (!validation) {
    return validation;
  }
  if (!IsAbsentRenderAssetReference(snapshot.environment().environment_texture) ||
      !IsAbsentRenderAssetReference(snapshot.environment().environment_sampler)) {
    return Unsupported("environment.texture",
                       "N1 supports constant ambient radiance only");
  }
  if (snapshot.environment().exposure_compensation_ev != 0.0F) {
    return Unsupported(
        "environment.exposure_compensation_ev",
        "N1 does not apply scene-level exposure compensation");
  }
  if (!allow_dynamic_meshes && !snapshot.dynamic_mesh_updates().empty()) {
    return Unsupported("dynamic_mesh_updates",
                       "N1 does not support deformable geometry");
  }
  if (!snapshot.particle_events().empty()) {
    return Unsupported("particle_events", "N1 does not support particles");
  }
  if (!snapshot.reflection_probes().empty()) {
    return Unsupported(
        "reflection_probes",
        "this Ogre-Next checkpoint has not published its native PCC capture adapter and will not silently ignore authored probes");
  }
  if (raster_feature_tier ==
          OgreNextRasterFeatureTier::STATIC_PBR_N1 &&
      !snapshot.lights().empty()) {
    return Unsupported(
        "lights",
        "N1 has no calibrated physical-light adapter; use constant environment radiance");
  }
  if (raster_feature_tier ==
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
    if (snapshot.lights().size() >
        kOgreNextRt4MaximumDirectionalLights) {
      return Unsupported(
          "lights",
          "RT4/V1 admits at most one calibrated directional light");
    }
    for (std::size_t index = 0U; index < snapshot.lights().size(); ++index) {
      const LightDescriptor &light = snapshot.lights()[index];
      if (light.type != LightType::DIRECTIONAL) {
        return Unsupported(
            "lights.type",
            "RT4/V1 admits a directional light only; local-light attenuation is not calibrated yet",
            index);
      }
      if (light.shadow_flags != 0U) {
        return Unsupported(
            "lights.shadow_flags",
            "RT4/V1 directional shadows require a reviewed shadow-node contract",
            index);
      }
      const float native_power =
          light.intensity * kOgreNextRt4LuxToNativePowerScale;
      if (!IsFinite(native_power) ||
          !IsFiniteScaled(light.color_linear, native_power)) {
        return Unsupported(
            "lights.photometry",
            "finite directional lux and color overflow RT4/V1 native light arithmetic",
            index);
      }
    }
  }
  if (!IsFiniteScaled(snapshot.environment().ambient_radiance,
                      snapshot.environment().environment_intensity)) {
    return Unsupported(
        "environment.ambient_radiance",
        "finite ambient inputs overflow Ogre's native environment color arithmetic");
  }
  for (std::size_t index = 0U; index < snapshot.mesh_instances().size();
       ++index) {
    const MeshInstanceDescriptor &instance = snapshot.mesh_instances()[index];
    if (!allow_dynamic_meshes && instance.deformation_revision != 1U) {
      return Unsupported("mesh_instances.deformation_revision",
                         "N1 renders base static geometry only", index);
    }
    if (!IsTrsRepresentable(instance.render_from_object)) {
      return Unsupported(
          "mesh_instances.render_from_object",
          "N1 scene nodes cannot represent affine shear", index);
    }
    if (!CanRepresentOgreNextN1WorldBounds(instance.local_bounds,
                                           instance.render_from_object)) {
      return Unsupported(
          "mesh_instances.world_bounds",
          "finite local bounds and TRS overflow Ogre's world-bound arithmetic",
          index);
    }
    if (LinearDeterminant(instance.render_from_object) < 0.0F) {
      return Unsupported(
          "mesh_instances.render_from_object",
          "N1 rejects mirrored TRS because Ogre's signed parent scale can manufacture a negative world radius",
          index);
    }
  }
  return ValidationResult::Success();
}

ValidationResult ValidateOgreNextN1Frame(
    const RenderFrameRequest &request,
    const FrontendCapabilityReport &capabilities,
    const RenderAssetRegistry &registry,
    OgreNextRasterFeatureTier raster_feature_tier) {
  ValidationResult validation =
      ValidateRenderFrameRequestAgainstCapabilities(request, capabilities);
  if (!validation) {
    return validation;
  }
  if (request.present) {
    return Unsupported("present", "N1 produces offscreen readbacks only");
  }
  if (request.requested_outputs != FrameOutputMask::COLOR ||
      request.views.size() != 1U) {
    return Unsupported("requested_outputs",
                       "N1 renders exactly one colour view");
  }
  const CameraViewRequest &view = request.views.front();
  if (view.exposure != 1.0F) {
    return Unsupported(
        "views.exposure",
        "N1 exposes raw fixed-exposure PBS colour; exposure must be one");
  }
  if (view.temporal_jitter_pixels != Float2{}) {
    return Unsupported("views.temporal_jitter_pixels",
                       "N1 does not apply temporal jitter");
  }
  Matrix4x4 converted_projection;
  if (!TryConvertPortableProjectionToOgreClip(view.clip_from_view,
                                               converted_projection) ||
      !TryConvertPortableProjectionToOgreClip(view.previous_clip_from_view,
                                               converted_projection)) {
    return Unsupported(
        "views.clip_from_view",
        "finite portable projection overflows Ogre's clip-depth conversion");
  }
  if (request.scene_snapshot->asset_registry_id() != registry.registry_id() ||
      request.scene_snapshot->asset_sequence() != registry.sequence()) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "scene_snapshot.asset_sequence",
        "scene requires a different synchronized asset catalog");
  }
  return ValidateOgreNextN1Scene(
      *request.scene_snapshot, registry,
      capabilities.supports_dynamic_mesh_updates, raster_feature_tier);
}

RenderOperationResult
OgreNextN1OperationFromValidation(const ValidationResult &validation) {
  if (validation.ok()) {
    return RenderOperationResult::Success();
  }
  RenderOperationCode code = RenderOperationCode::INVALID_ARGUMENT;
  if (validation.code == ValidationCode::UNSUPPORTED_FEATURE ||
      validation.code == ValidationCode::UNSUPPORTED_VERSION) {
    code = RenderOperationCode::UNSUPPORTED;
  } else if (validation.code == ValidationCode::MISSING_REFERENCE ||
             validation.code == ValidationCode::REVISION_MISMATCH ||
             validation.code == ValidationCode::SEQUENCE_MISMATCH) {
    code = RenderOperationCode::RESOURCE_STALE;
  }
  std::ostringstream detail;
  detail << validation.field << ": " << validation.detail;
  if (validation.element_index != ValidationResult::kNoElement) {
    detail << " (element " << validation.element_index << ')';
  }
  return RenderOperationResult::Failure(code, detail.str());
}

} // namespace RoR::Render
