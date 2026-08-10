/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/detail/OgreNextDemoPrivatePolicy.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Gfx::Detail;
using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

TextureMipLevelDescriptor MakeMip(
    std::uint32_t width, std::uint32_t height,
    std::vector<std::uint8_t> bytes) {
  TextureMipLevelDescriptor mip;
  mip.width = width;
  mip.height = height;
  mip.row_pitch_bytes = static_cast<std::uint64_t>(width) * 4U;
  mip.layer_pitch_bytes = mip.row_pitch_bytes * height;
  mip.bytes = std::move(bytes);
  return mip;
}

TextureResourceDescriptor NativeBaseLevel() {
  TextureResourceDescriptor texture;
  texture.debug_name = "OgreNextDemo/TestComposite";
  texture.type = TextureResourceType::TEXTURE_2D;
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 4U;
  texture.height = 4U;
  texture.array_layers = 1U;

  std::vector<std::uint8_t> base(4U * 4U * 4U);
  for (std::size_t texel = 0U; texel < 16U; ++texel) {
    base[texel * 4U + 0U] = static_cast<std::uint8_t>(10U + texel);
    base[texel * 4U + 1U] = static_cast<std::uint8_t>(30U + texel);
    base[texel * 4U + 2U] = static_cast<std::uint8_t>(50U + texel);
    base[texel * 4U + 3U] = static_cast<std::uint8_t>(texel);
  }
  texture.mip_levels.push_back(MakeMip(4U, 4U, std::move(base)));
  return texture;
}

void CheckFullMipOpaqueLowering() {
  TextureResourceDescriptor texture = NativeBaseLevel();
  const std::vector<std::uint8_t> base_before =
      texture.mip_levels[0U].bytes;

  const ValidationResult result =
      CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(result.ok(), "valid native base level was rejected");
  Require(texture.mip_levels.size() == 3U &&
              texture.mip_levels.back().width == 1U &&
              texture.mip_levels.back().height == 1U,
          "native base was not completed through 1x1");
  for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
    const auto &bytes = texture.mip_levels[level].bytes;
    for (std::size_t alpha = 3U; alpha < bytes.size(); alpha += 4U) {
      Require(bytes[alpha] == 255U,
              "one output mip retained non-opaque alpha");
    }
  }
  for (std::size_t offset = 0U; offset < base_before.size(); ++offset) {
    if (offset % 4U != 3U) {
      Require(texture.mip_levels[0U].bytes[offset] == base_before[offset],
              "base native RGB byte changed");
    }
  }
  const std::array<std::uint8_t, 16U> expected_second{{
      13U, 33U, 53U, 255U, 15U, 35U, 55U, 255U,
      21U, 41U, 61U, 255U, 23U, 43U, 63U, 255U}};
  Require(std::equal(expected_second.begin(), expected_second.end(),
                     texture.mip_levels[1U].bytes.begin()),
          "generated 4x4-to-2x2 display-domain box result changed");
  const std::array<std::uint8_t, 4U> expected{{18U, 38U, 58U, 255U}};
  Require(std::equal(expected.begin(), expected.end(),
                     texture.mip_levels[2U].bytes.begin()),
          "generated display-domain 2x2 box result changed");
}

void CheckMalformedMipRollback() {
  TextureResourceDescriptor texture = NativeBaseLevel();
  texture.mip_levels[0U].row_pitch_bytes = 9U;
  const TextureResourceDescriptor before = texture;
  const ValidationResult result =
      CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(!result.ok() &&
              result.code == ValidationCode::SIZE_MISMATCH,
          "malformed native base level was accepted");
  Require(texture.mip_levels.size() == before.mip_levels.size() &&
              texture.mip_levels[0U].bytes == before.mip_levels[0U].bytes &&
              texture.mip_levels[0U].row_pitch_bytes == 9U,
          "failed mip validation partially changed the candidate");

  texture = NativeBaseLevel();
  texture.mip_levels.push_back(MakeMip(
      2U, 2U, std::vector<std::uint8_t>(2U * 2U * 4U, 7U)));
  const TextureResourceDescriptor extra_before = texture;
  const ValidationResult extra =
      CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(!extra.ok() && texture.mip_levels.size() == 2U &&
              texture.mip_levels[0U].bytes ==
                  extra_before.mip_levels[0U].bytes &&
              texture.mip_levels[1U].bytes ==
                  extra_before.mip_levels[1U].bytes,
          "native nonzero mip was read or partially rewritten");
}

void CheckSamplingRejectionsAndMutation() {
  OgreNextDemoSamplingObservation canonical;
  canonical.exact_native_state = "stable-native-state";
  Require(ValidateOgreNextDemoSampling(canonical).ok(),
          "canonical sampling was rejected");

  const auto reject = [&canonical](bool OgreNextDemoSamplingObservation::*field,
                                   const char *expected_field) {
    OgreNextDemoSamplingObservation mutation = canonical;
    mutation.*field = false;
    const ValidationResult result = ValidateOgreNextDemoSampling(mutation);
    Require(!result.ok() && result.field == expected_field,
            "sampling mutation did not hit its exact rejection field");
  };
  reject(&OgreNextDemoSamplingObservation::ordinary_texture,
         "ogre_next_demo.terrain.sampling.ordinary");
  reject(&OgreNextDemoSamplingObservation::uv0_identity,
         "ogre_next_demo.terrain.sampling.uv");
  reject(&OgreNextDemoSamplingObservation::sampler_identity,
         "ogre_next_demo.terrain.sampling.sampler");
  reject(&OgreNextDemoSamplingObservation::gamma_disabled,
         "ogre_next_demo.terrain.sampling.gamma");
  reject(&OgreNextDemoSamplingObservation::fog_disabled,
         "ogre_next_demo.terrain.sampling.fog");

  Require(RevalidateOgreNextDemoSampling(canonical, canonical).ok(),
          "identical before/after sampling state was rejected");
  OgreNextDemoSamplingObservation after = canonical;
  after.exact_native_state.push_back('!');
  const ValidationResult mutated =
      RevalidateOgreNextDemoSampling(canonical, after);
  Require(!mutated.ok() &&
              mutated.code == ValidationCode::REVISION_MISMATCH &&
              mutated.field ==
                  "ogre_next_demo.terrain.readback.revalidation",
          "before/after native state mutation was accepted");
}

void CheckIdentityCollisionAndRollback() {
  std::string mesh_key("demo/mesh");
  mesh_key.push_back('\0');
  mesh_key.append("page/0/0");
  std::string texture_key("demo/texture");
  texture_key.push_back('\0');
  texture_key.append("page/0/0");
  std::uint64_t mesh_id = 0U;
  std::uint64_t texture_id = 0U;
  Require(DeriveOgreNextDemoSourceId("demo/mesh", "page/0/0", mesh_id).ok() &&
              DeriveOgreNextDemoSourceId("demo/texture", "page/0/0",
                                         texture_id).ok() &&
              mesh_id != texture_id,
          "domain separation did not produce distinct stable IDs");

  OgreNextDemoIdentityRegistry committed;
  Require(committed.Register(mesh_key, mesh_id).ok(),
          "canonical identity registration failed");
  OgreNextDemoIdentityRegistry pending = committed;
  Require(pending.Register(texture_key, texture_id).ok(),
          "pending identity registration failed");
  pending = committed; // discard candidate
  Require(committed.size() == 1U && pending.size() == 1U &&
              !committed.Contains(texture_key, texture_id),
          "discarded identity leaked into committed state");

  const ValidationResult id_collision =
      committed.Register("different-key", mesh_id);
  Require(!id_collision.ok() &&
              id_collision.code == ValidationCode::DUPLICATE_IDENTIFIER,
          "distinct exact keys sharing one ID were accepted");
  const ValidationResult key_mutation =
      committed.Register(mesh_key, texture_id);
  Require(!key_mutation.ok() &&
              key_mutation.code == ValidationCode::REVISION_MISMATCH,
          "one exact key changing ID was accepted");
}

void CheckMatteFallbackPolicy() {
  Require(!OgreNextDemoRequiresMatte(0U, false),
          "factor-only material was unnecessarily matted");
  Require(OgreNextDemoRequiresMatte(1U, false) &&
              OgreNextDemoRequiresMatte(0U, true),
          "textured or programmed material bypassed the matte fallback");
  Require(OgreNextDemoDropsDynamicBlendColors(true) &&
              !OgreNextDemoDropsDynamicBlendColors(false),
          "FlexBody blend-color drop policy changed");
  Require(OgreNextDemoOmitsInvisibleCab("invisible", 0.0F, false) &&
              !OgreNextDemoOmitsInvisibleCab("invisible", 1.0F, false) &&
              !OgreNextDemoOmitsInvisibleCab("invisible", 0.0F, true) &&
              !OgreNextDemoOmitsInvisibleCab("Invisible", 0.0F, false),
          "authored invisible cab omission is no longer exact");
  Require(OgreNextDemoOmitsNonUniformSpeedBump(
              "topeQr.mesh", {1.0F, 0.5F, 0.5F}) &&
              !OgreNextDemoOmitsNonUniformSpeedBump(
                  "topeQr.mesh", {1.0F, 1.0F, 1.0F}) &&
              !OgreNextDemoOmitsNonUniformSpeedBump(
                  "other.mesh", {1.0F, 0.5F, 0.5F}),
          "CityWorld speed-bump omission broadened beyond its exact identity");
}

void CheckMatteMeshNormalization() {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "demo matte triangle";
  mesh.dynamic = true;
  mesh.local_bounds.minimum = {-1.0F, 0.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {{-1.0F, 0.0F, 0.0F},
                    {1.0F, 0.0F, 0.0F},
                    {0.0F, 1.0F, 0.0F}};
  mesh.normals = {
      {0.0F, 0.0F, 2.0F},
      {0.0F, 0.0F, 0.0F},
      {(std::numeric_limits<float>::quiet_NaN)(), 0.0F, 0.0F}};
  mesh.velocities.assign(3U, {2.0F, 3.0F, 4.0F});
  mesh.texture_coordinates_1.assign(3U, {0.25F, 0.75F});
  mesh.colors.assign(3U, {0.2F, 0.4F, 0.6F, 0.8F});
  mesh.indices = {0U, 1U, 2U};

  Require(NormalizeOgreNextDemoMatteMesh(mesh).ok(),
          "valid matte mesh normalization failed");
  Require(mesh.texture_coordinates_0.size() == 3U &&
              std::all_of(mesh.texture_coordinates_0.begin(),
                          mesh.texture_coordinates_0.end(),
                          [](const Float2 &uv) {
                            return uv.x == 0.0F && uv.y == 0.0F;
                          }),
          "missing matte UV0 was not deterministically synthesized");
  Require(mesh.normals.size() == 3U &&
              mesh.normals[0U] == Float3{0.0F, 0.0F, 1.0F} &&
              mesh.normals[1U] == Float3{0.0F, 1.0F, 0.0F} &&
              mesh.normals[2U] == Float3{0.0F, 1.0F, 0.0F},
          "matte normals were not normalized/fallback-sanitized");
  Require(mesh.tangents.size() == 3U &&
              mesh.tangents[0U] == Float4{1.0F, 0.0F, 0.0F, 1.0F} &&
              mesh.tangents[1U] == Float4{-1.0F, 0.0F, 0.0F, 1.0F} &&
              mesh.tangents[2U] == Float4{-1.0F, 0.0F, 0.0F, 1.0F},
          "matte tangent basis was not rebuilt from sanitized normals");
  Require(mesh.velocities.empty() &&
              mesh.texture_coordinates_1.empty() && mesh.colors.empty() &&
              ValidateMeshResourceDescriptor(mesh).ok(),
          "matte normalization retained an unsupported RT4 stream");

  std::vector<Float3> dynamic_normals{
      {0.0F, 3.0F, 0.0F},
      {(std::numeric_limits<float>::infinity)(), 1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F}};
  std::vector<Float4> dynamic_tangents{{9.0F, 9.0F, 9.0F, 9.0F}};
  Require(BuildOgreNextDemoMatteTangents(
              3U, dynamic_normals, dynamic_tangents)
              .ok() &&
              dynamic_normals ==
                  std::vector<Float3>(3U, {0.0F, 1.0F, 0.0F}) &&
              dynamic_tangents ==
                  std::vector<Float4>(3U, {-1.0F, 0.0F, 0.0F, 1.0F}),
          "joined dynamic normals/tangents were not sanitized deterministically");

  std::vector<Float3> missing_normals;
  std::vector<Float4> missing_tangents{{7.0F, 7.0F, 7.0F, 7.0F}};
  Require(BuildOgreNextDemoMatteTangents(
              2U, missing_normals, missing_tangents)
              .ok() &&
              missing_normals ==
                  std::vector<Float3>(2U, {0.0F, 1.0F, 0.0F}) &&
              missing_tangents ==
                  std::vector<Float4>(2U, {-1.0F, 0.0F, 0.0F, 1.0F}),
          "absent demo normal stream did not receive the fixed fallback");

  std::vector<Float3> wrong_size_normals{{0.0F, 2.0F, 0.0F}};
  std::vector<Float4> wrong_size_tangents{{9.0F, 9.0F, 9.0F, 9.0F}};
  const std::vector<Float3> normals_before = wrong_size_normals;
  const std::vector<Float4> tangents_before = wrong_size_tangents;
  const ValidationResult invalid = BuildOgreNextDemoMatteTangents(
      2U, wrong_size_normals, wrong_size_tangents);
  Require(!invalid.ok() && wrong_size_normals == normals_before &&
              wrong_size_tangents == tangents_before,
          "failed dynamic normal sanitization changed either output stream");

  MeshResourceDescriptor rollback = mesh;
  rollback.normals[0U] = {0.0F, 4.0F, 0.0F};
  rollback.texture_coordinates_0.pop_back();
  const std::vector<Float3> rollback_normals = rollback.normals;
  const std::vector<Float4> rollback_tangents = rollback.tangents;
  const ValidationResult rollback_result =
      NormalizeOgreNextDemoMatteMesh(rollback);
  Require(!rollback_result.ok() &&
              rollback.normals == rollback_normals &&
              rollback.tangents == rollback_tangents &&
              rollback.texture_coordinates_0.size() == 2U,
          "post-validation failure partially committed sanitized mesh streams");
}

} // namespace

int main() {
  CheckFullMipOpaqueLowering();
  CheckMalformedMipRollback();
  CheckSamplingRejectionsAndMutation();
  CheckIdentityCollisionAndRollback();
  CheckMatteFallbackPolicy();
  CheckMatteMeshNormalization();
  std::cout << "OgreNext demo private policy tests passed\n";
  return EXIT_SUCCESS;
}
