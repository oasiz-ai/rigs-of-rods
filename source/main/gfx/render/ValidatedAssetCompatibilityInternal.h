/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Internal relationship checks for registry-validated assets.

#pragma once

#include "RenderValidation.h"

namespace RoR::Render {

struct DynamicMeshUpdateDescriptor;
struct MaterialDescriptor;
struct MeshInstanceDescriptor;
struct MeshResourceDescriptor;
class RenderAssetRegistry;
struct SamplerResourceDescriptor;
struct SceneSnapshotDescriptor;
struct TextureResourceDescriptor;

ValidationResult
ValidateMaterialMeshCompatibility(const MaterialDescriptor &material,
                                  const MeshResourceDescriptor &mesh);
ValidationResult ValidateDynamicMeshUpdateCompatibility(
    const MeshResourceDescriptor &mesh,
    const DynamicMeshUpdateDescriptor &update);
ValidationResult ValidateMeshInstanceCompatibility(
    const MeshResourceDescriptor &mesh, const MeshInstanceDescriptor &instance,
    const DynamicMeshUpdateDescriptor *deformation_update);
ValidationResult ValidateEnvironmentTextureCompatibility(
    const TextureResourceDescriptor &texture,
    const SamplerResourceDescriptor &sampler);
ValidationResult ValidateSceneSnapshotAssets(
    const SceneSnapshotDescriptor &descriptor,
    const RenderAssetRegistry &registry);

class ValidatedAssetCompatibilityAccess final {
private:
  constexpr ValidatedAssetCompatibilityAccess() noexcept = default;
  ~ValidatedAssetCompatibilityAccess() = default;
  ValidatedAssetCompatibilityAccess(
      const ValidatedAssetCompatibilityAccess &) = delete;
  ValidatedAssetCompatibilityAccess &
  operator=(const ValidatedAssetCompatibilityAccess &) = delete;

  friend ValidationResult
  ValidateMaterialMeshCompatibility(const MaterialDescriptor &material,
                                    const MeshResourceDescriptor &mesh);
  friend ValidationResult ValidateDynamicMeshUpdateCompatibility(
      const MeshResourceDescriptor &mesh,
      const DynamicMeshUpdateDescriptor &update);
  friend ValidationResult ValidateMeshInstanceCompatibility(
      const MeshResourceDescriptor &mesh,
      const MeshInstanceDescriptor &instance,
      const DynamicMeshUpdateDescriptor *deformation_update);
  friend ValidationResult ValidateEnvironmentTextureCompatibility(
      const TextureResourceDescriptor &texture,
      const SamplerResourceDescriptor &sampler);
  friend ValidationResult ValidateSceneSnapshotAssets(
      const SceneSnapshotDescriptor &descriptor,
      const RenderAssetRegistry &registry);
};

namespace Detail {

/// These helpers deliberately omit standalone descriptor validation. Callers
/// must supply immutable descriptors returned by a RenderAssetRegistry after
/// a successful Apply(), and a snapshot payload that already passed
/// ValidateSceneSnapshotDescriptor(). Only the full public validators and the
/// registry-resolving snapshot validator can construct the required access
/// token.
[[nodiscard]] ValidationResult
ValidateMaterialMeshCompatibilityFromValidatedAssets(
    const ValidatedAssetCompatibilityAccess &access,
    const MaterialDescriptor &material, const MeshResourceDescriptor &mesh);

[[nodiscard]] ValidationResult
ValidateDynamicMeshUpdateCompatibilityFromValidatedMesh(
    const ValidatedAssetCompatibilityAccess &access,
    const MeshResourceDescriptor &mesh,
    const DynamicMeshUpdateDescriptor &update);

[[nodiscard]] ValidationResult
ValidateMeshInstanceCompatibilityFromValidatedMesh(
    const ValidatedAssetCompatibilityAccess &access,
    const MeshResourceDescriptor &mesh, const MeshInstanceDescriptor &instance,
    const DynamicMeshUpdateDescriptor *deformation_update);

[[nodiscard]] ValidationResult
ValidateEnvironmentTextureCompatibilityFromValidatedAssets(
    const ValidatedAssetCompatibilityAccess &access,
    const TextureResourceDescriptor &texture,
    const SamplerResourceDescriptor &sampler);

} // namespace Detail
} // namespace RoR::Render
