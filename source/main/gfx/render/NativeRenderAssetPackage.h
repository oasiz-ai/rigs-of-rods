/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief OGRE-free decoder for deterministic forward-native static assets.

#pragma once

#include "GraphicsSceneSnapshotProducer.h"
#include "RenderPayloadDigest.h"
#include "RenderValidation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kNativeRenderAssetPackageVersion = 1U;
constexpr std::uint32_t kNativeRenderAssetPackageTransmissionVersion = 2U;
constexpr std::size_t kNativeRenderAssetPackageHeaderBytes = 80U;
constexpr std::size_t kMaximumNativeRenderAssetPackageBytes =
    256U * 1024U * 1024U;

/// Complete immutable renderer-neutral result of one .rornative decode.
///
/// Material payload references are canonically absent. Their source-side
/// texture/sampler identities live in GraphicsSceneAssetInput bindings so the
/// GraphicsSceneSnapshotProducer remains the only owner of RenderAssetIds and
/// revisions. The returned owner is const and no field aliases package bytes.
struct NativeRenderAssetPackage {
  std::uint32_t version = kNativeRenderAssetPackageVersion;
  RenderPayloadDigest package_sha256{};
  RenderPayloadDigest body_sha256{};
  std::string package_id;
  std::string origin_class;
  RenderPayloadDigest compiler_sha256{};
  RenderPayloadDigest generator_sha256{};
  RenderPayloadDigest glb_sha256{};
  RenderPayloadDigest composition_sha256{};
  RenderPayloadDigest source_manifest_sha256{};
  std::string provenance_manifest_json;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneStaticMeshInput> static_meshes;
};

struct NativeRenderAssetPackageDecodeResult {
  ValidationResult validation;
  std::shared_ptr<const NativeRenderAssetPackage> package;

  [[nodiscard]] bool ok() const noexcept {
    return validation.ok() && package != nullptr;
  }
};

/// Decodes one complete package transactionally. Hostile null, truncated,
/// oversized, digest-mismatched, non-canonical, or semantically inconsistent
/// input returns a failure with no partially published package. The expected
/// full-package digest is mandatory trust input from a checked report/ledger;
/// the self-declared body digest alone cannot authenticate provenance hashes.
[[nodiscard]] NativeRenderAssetPackageDecodeResult
DecodeNativeRenderAssetPackage(const std::uint8_t *bytes,
                               std::size_t byte_count,
                               const RenderPayloadDigest &expected_package_sha256) noexcept;

} // namespace RoR::Render
