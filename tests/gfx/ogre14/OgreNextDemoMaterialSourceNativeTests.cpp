/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
#include "gfx/ogre14/Ogre14ManagedMaterialSourceAdapter.h"
#include "gfx/ogre14/Ogre14SelectedTextureSource.h"
#include "gfx/ogre14/detail/OgreNextDemoMaterialSource.h"

#include <OgreHardwarePixelBuffer.h>
#include <OgreLogManager.h>
#include <OgreMaterial.h>
#include <OgrePass.h>
#include <OgreRoot.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace RoR::Render::Testing {

class Ogre14SelectedTextureSourceResolutionTestAccess final {
public:
  static ValidationResult
  Mint(const Ogre14SelectedTextureSourceReceiptRegistry &registry,
       Ogre::Texture &texture, std::uint64_t generation,
       const IOgre14SelectedTextureSourceResolver &resolver,
       Ogre14SelectedTextureSourceResolution &resolution) {
    return registry.MintLoadedResourceResolution(
        texture.getGroup(), generation,
        reinterpret_cast<std::uintptr_t>(&texture),
        static_cast<std::uint64_t>(texture.getHandle()), texture.getName(),
        static_cast<std::uint64_t>(texture.getStateCount()),
        reinterpret_cast<std::uintptr_t>(&resolver), resolution);
  }

  static bool
  Revalidate(const Ogre14SelectedTextureSourceReceiptRegistry &registry,
             Ogre::Texture &texture,
             const Ogre14SelectedTextureSourceResolution &resolution,
             const IOgre14SelectedTextureSourceResolver &resolver) noexcept {
    return registry.RevalidateLoadedResourceResolution(
        resolution, reinterpret_cast<std::uintptr_t>(&resolver),
        reinterpret_cast<std::uintptr_t>(&texture),
        static_cast<std::uint64_t>(texture.getHandle()), texture.getGroup(),
        texture.getName(), static_cast<std::uint64_t>(texture.getStateCount()));
  }
};

class Ogre14AuthenticatedTextureResolutionTestAccess final {
public:
  static ValidationResult
  MintAuthority(const Ogre14AuthenticatedTextureReceiptRegistry &registry,
                const IOgre14AuthenticatedTextureResolver &resolver,
                Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) {
    return registry.MintResolverAuthoritySnapshot(
        reinterpret_cast<std::uintptr_t>(&resolver), snapshot);
  }
};

} // namespace RoR::Render::Testing

namespace {

using namespace RoR::Gfx::Detail;
using namespace RoR::Render;

constexpr char kGroup[] = "MaterialSourceNative";
constexpr char kTextureName[] = "road.png";
constexpr char kMaterialName[] = "RoadMaterial";
constexpr char kSectionKey[] = "static/road/section-0";
constexpr char kAlexisGroup[] = "{bundle USER:/mods/AlexisSaber.zip}";
constexpr char kAlexisMaterialName[] =
    "SaberChassis (AlexisSaber.truck [Instance ID 17])";
constexpr char kAlexisDiffuseName[] = "AlexisSaberChassis.png";
constexpr char kAlexisSpecularName[] = "AlexisSaberChassisSpec.png";

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void RequireOk(const ValidationResult &result, const char *message) {
  if (!result) {
    std::cerr << "FAIL: " << message << ": " << result.field << ": "
              << result.detail << '\n';
    std::exit(EXIT_FAILURE);
  }
}

int Base64Value(char value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A';
  }
  if (value >= 'a' && value <= 'z') {
    return value - 'a' + 26;
  }
  if (value >= '0' && value <= '9') {
    return value - '0' + 52;
  }
  if (value == '+') {
    return 62;
  }
  if (value == '/') {
    return 63;
  }
  return -1;
}

std::vector<std::uint8_t> OpaqueRgbPng() {
  static constexpr char encoded[] =
      "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAACXBIWXMAAAABAAAAAQBP"
      "JcTWAAAAEElEQVR4nGMQNA4BIgYIBQAPogJhhCJXLQAAAABJRU5ErkJggg==";
  std::vector<std::uint8_t> result;
  std::uint32_t accumulator = 0U;
  std::uint32_t bits = 0U;
  for (const char *cursor = encoded; *cursor != '\0' && *cursor != '=';
       ++cursor) {
    const int value = Base64Value(*cursor);
    Require(value >= 0, "PNG fixture contains invalid base64");
    accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
    bits += 6U;
    if (bits >= 8U) {
      bits -= 8U;
      result.push_back(
          static_cast<std::uint8_t>((accumulator >> bits) & 0xffU));
    }
  }
  return result;
}

class TestPixelBuffer final : public Ogre::HardwarePixelBuffer {
public:
  explicit TestPixelBuffer(std::shared_ptr<std::size_t> readback_calls)
      : Ogre::HardwarePixelBuffer(2U, 2U, 1U, Ogre::PF_BYTE_RGBA,
                                  Ogre::HBU_CPU_ONLY, false),
        readback_calls_(std::move(readback_calls)) {}

  void blitFromMemory(const Ogre::PixelBox &, const Ogre::Box &) override {}

  void blitToMemory(const Ogre::Box &, const Ogre::PixelBox &) override {
    ++*readback_calls_;
    throw std::runtime_error("native GPU texture readback is forbidden");
  }

protected:
  Ogre::PixelBox lockImpl(const Ogre::Box &, LockOptions) override {
    ++*readback_calls_;
    throw std::runtime_error("native GPU texture lock is forbidden");
  }

  void unlockImpl() override {}

private:
  std::shared_ptr<std::size_t> readback_calls_;
};

class TestTexture final : public Ogre::Texture {
public:
  static std::size_t destruction_count;

  TestTexture(std::string name, Ogre::ResourceHandle handle, std::string group,
              std::shared_ptr<std::size_t> readback_calls,
              Ogre::TextureType texture_type = Ogre::TEX_TYPE_2D)
      : Ogre::Texture(nullptr, std::move(name), handle, std::move(group), false,
                      nullptr) {
    mWidth = 2U;
    mHeight = 2U;
    mDepth = 1U;
    mSrcWidth = 2U;
    mSrcHeight = 2U;
    mSrcDepth = 1U;
    mTextureType = texture_type;
    mNumRequestedMipmaps = 0U;
    mNumMipmaps = 0U;
    mFormat = Ogre::PF_BYTE_RGBA;
    mSrcFormat = Ogre::PF_BYTE_RGBA;
    mUsage = Ogre::TU_STATIC;
    mSurfaceList.push_back(
        std::make_shared<TestPixelBuffer>(std::move(readback_calls)));
  }

  ~TestTexture() override { ++destruction_count; }

  void MutateMipState(std::uint32_t additional_mips, bool hardware_generated) {
    mNumRequestedMipmaps = additional_mips;
    mNumMipmaps = additional_mips;
    mMipmapsHardwareGenerated = hardware_generated;
  }

  void MutateUsage(int usage) { mUsage = usage; }

  void MutateSourceState(std::uint32_t width, std::uint32_t height,
                         std::uint32_t depth, Ogre::PixelFormat format) {
    mSrcWidth = width;
    mSrcHeight = height;
    mSrcDepth = depth;
    mSrcFormat = format;
  }

  void MutateOutputState(std::uint32_t width, std::uint32_t height,
                         std::uint32_t depth, Ogre::PixelFormat format) {
    mWidth = width;
    mHeight = height;
    mDepth = depth;
    mFormat = format;
  }

protected:
  void prepareImpl() override {}
  void loadImpl() override {}
  void createInternalResourcesImpl() override {}
  void freeInternalResourcesImpl() override {}
};

std::size_t TestTexture::destruction_count = 0U;

class OrdinaryTrustResolver final : public IOgre14AuthenticatedTextureResolver {
public:
  bool
  RequiresAuthenticatedTextureSource(Ogre::Texture &) const noexcept override {
    return false;
  }

  ValidationResult ResolveAuthenticatedTexture(
      Ogre::Texture &, Ogre14AuthenticatedTextureResolution &) const override {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "native_test.authenticated.resolve",
        "ordinary fixture must not resolve authenticated authority");
  }

  bool RevalidateAuthenticatedTexture(
      Ogre::Texture &,
      const Ogre14AuthenticatedTextureResolution &) const noexcept override {
    return false;
  }
};

class EmptyAuthorityProvider final
    : public IOgre14AuthenticatedTextureAuthorityProvider {
public:
  const Ogre14AuthenticatedTextureReceiptRegistry *registry = nullptr;
  const IOgre14AuthenticatedTextureResolver *resolver = nullptr;
  mutable std::size_t capture_calls = 0U;

  ValidationResult CaptureAuthenticatedTextureAuthoritySnapshot(
      Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) const override {
    ++capture_calls;
    if (registry == nullptr || resolver == nullptr) {
      return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                       "native_test.authenticated.provider",
                                       "missing empty authority fixture");
    }
    return RoR::Render::Testing::
        Ogre14AuthenticatedTextureResolutionTestAccess::MintAuthority(
            *registry, *resolver, snapshot);
  }
};

class SelectedResolver final : public IOgre14SelectedTextureSourceResolver {
public:
  const Ogre14SelectedTextureSourceReceiptRegistry *registry = nullptr;
  std::uint64_t generation = 1U;
  mutable std::size_t resolve_calls = 0U;
  mutable std::size_t revalidate_calls = 0U;

  ValidationResult ResolveSelectedTextureSource(
      Ogre::Texture &texture,
      Ogre14SelectedTextureSourceResolution &resolution) const override {
    ++resolve_calls;
    if (registry == nullptr) {
      return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                       "selected_texture_registry",
                                       "native test registry is absent");
    }
    return RoR::Render::Testing::
        Ogre14SelectedTextureSourceResolutionTestAccess::Mint(
            *registry, texture, generation, *this, resolution);
  }

  bool
  RevalidateSelectedTextureSource(Ogre::Texture &texture,
                                  const Ogre14SelectedTextureSourceResolution
                                      &resolution) const noexcept override {
    ++revalidate_calls;
    return registry != nullptr &&
           RoR::Render::Testing::
               Ogre14SelectedTextureSourceResolutionTestAccess::Revalidate(
                   *registry, texture, resolution, *this);
  }
};

struct NativeMaterial final {
  explicit NativeMaterial(const Ogre::TexturePtr &texture,
                          Ogre::ResourceHandle handle = 101U)
      : NativeMaterial(texture, handle, kMaterialName, kGroup) {}

  NativeMaterial(const Ogre::TexturePtr &texture, Ogre::ResourceHandle handle,
                 std::string name, std::string group)
      : material(std::make_shared<Ogre::Material>(
            nullptr, std::move(name), handle, std::move(group))) {
    Ogre::Technique *const technique = material->createTechnique();
    pass = technique->createPass();
    unit = pass->createTextureUnitState();
    sampler = std::make_shared<Ogre::Sampler>();
    sampler->setFiltering(Ogre::FO_LINEAR, Ogre::FO_LINEAR, Ogre::FO_POINT);
    sampler->setAddressingMode(Ogre::TAM_WRAP);
    sampler->setMipmapBias(0.0F);
    sampler->setAnisotropy(1U);
    sampler->setCompareEnabled(false);
    sampler->setCompareFunction(Ogre::CMPF_ALWAYS_PASS);
    sampler->setBorderColour(Ogre::ColourValue::Black);
    unit->setSampler(sampler);
    unit->setTexture(texture);
  }

  Ogre::MaterialPtr material;
  Ogre::Pass *pass = nullptr;
  Ogre::TextureUnitState *unit = nullptr;
  Ogre::SamplerPtr sampler;
};

struct AlexisNativeMaterial final {
  AlexisNativeMaterial(const Ogre::TexturePtr &diffuse_texture,
                       const Ogre::TexturePtr &specular_texture,
                       const Ogre::TexturePtr &environment_texture)
      : base(diffuse_texture, 111U, kAlexisMaterialName, kAlexisGroup) {
    Ogre::Technique *const technique = base.material->getTechnique(0U);
    base.pass->setName("BaseRender");
    base.unit->setName("Diffuse_Map");

    specular_pass = technique->createPass();
    specular_pass->setName("SpecularMapping1");
    specular_pass->setSceneBlending(Ogre::SBF_ONE, Ogre::SBF_ONE);
    specular_unit = specular_pass->createTextureUnitState();
    specular_unit->setName("SpecularMapping1_Tex");
    specular_unit->setColourOperationEx(
        Ogre::LBX_SOURCE2, Ogre::LBS_TEXTURE, Ogre::LBS_TEXTURE);
    specular_unit->setAlphaOperation(
        Ogre::LBX_SOURCE1, Ogre::LBS_TEXTURE, Ogre::LBS_TEXTURE);
    specular_sampler = std::make_shared<Ogre::Sampler>();
    specular_sampler->setFiltering(Ogre::FO_ANISOTROPIC,
                                   Ogre::FO_ANISOTROPIC,
                                   Ogre::FO_LINEAR);
    specular_sampler->setAddressingMode(Ogre::TAM_CLAMP);
    specular_sampler->setMipmapBias(0.0F);
    specular_sampler->setAnisotropy(8U);
    specular_sampler->setCompareEnabled(false);
    specular_sampler->setCompareFunction(Ogre::CMPF_ALWAYS_PASS);
    specular_sampler->setBorderColour(Ogre::ColourValue::Black);
    specular_unit->setSampler(specular_sampler);
    specular_unit->setTexture(specular_texture);

    Ogre::TextureUnitState *const environment =
        specular_pass->createTextureUnitState();
    environment->setName("envmap");
    environment->setTexture(environment_texture);
    environment->setEnvironmentMap(
        true, Ogre::TextureUnitState::ENV_REFLECTION);
  }

  NativeMaterial base;
  Ogre::Pass *specular_pass = nullptr;
  Ogre::TextureUnitState *specular_unit = nullptr;
  Ogre::SamplerPtr specular_sampler;
};

Ogre14SelectedTextureSourceReceipt
BuildReceipt(Ogre::Texture &texture, std::uint64_t generation,
             std::uint64_t state_before_load, std::uintptr_t stream_token,
             const std::vector<std::uint8_t> &bytes) {
  Ogre14SelectedTextureSourceCaptureInput input;
  input.effective_resource_group = texture.getGroup();
  input.group_generation = generation;
  input.selected_archive_name = "MaterialSourceNative.zip";
  input.selected_archive_type = "Zip";
  input.selected_archive_pointer_token = 0x2000U + generation;
  input.file_info_archive_pointer_token = input.selected_archive_pointer_token;
  input.file_info_filename = texture.getName();
  input.file_info_basename = texture.getName();
  input.exact_member_name = texture.getName();
  input.file_info_compressed_size = bytes.size();
  input.file_info_uncompressed_size = bytes.size();
  input.opened_stream_pointer_token = stream_token;
  input.opened_stream_name = texture.getName();
  input.opened_stream_size = bytes.size();
  input.resource_pointer_token = reinterpret_cast<std::uintptr_t>(&texture);
  input.resource_handle = static_cast<std::uint64_t>(texture.getHandle());
  input.exact_resource_name = texture.getName();
  input.resource_state_count_before_load = state_before_load;
  Ogre14SelectedTextureSourceReceipt receipt;
  RequireOk(BuildOgre14SelectedTextureSourceReceipt(
                Ogre14SelectedTextureSourceRegistryConfiguration{}, input,
                bytes.data(), bytes.size(), receipt),
            "build ordinary selected-source receipt");
  return receipt;
}

ManagedMaterialTextureBindingInput ManagedTextureBinding(
    ManagedMaterialTextureSlot slot,
    const ManagedMaterialTextureSourceReceipt &receipt) {
  const ManagedMaterialTextureSourceIdentity *const identity =
      receipt.identity();
  Require(identity != nullptr, "managed source receipt identity is absent");
  ManagedMaterialTextureBindingInput binding;
  binding.slot = slot;
  binding.configured = true;
  binding.declared_texture_name = identity->exact_resource_name;
  binding.resolved_texture_name = identity->exact_resource_name;
  binding.effective_texture_name = identity->exact_resource_name;
  binding.requested_resource_group = identity->effective_resource_group;
  binding.effective_resource_group = identity->effective_resource_group;
  binding.source_receipt = receipt;
  return binding;
}

ManagedMaterialDeclaration BuildManagedSpecularDeclaration(
    const ManagedMaterialTextureSourceReceipt &diffuse,
    const ManagedMaterialTextureSourceReceipt &specular,
    std::string exact_material_name = kAlexisMaterialName,
    ManagedMaterialSemanticType semantic_type =
        ManagedMaterialSemanticType::MESH_STANDARD,
    const ManagedMaterialTextureSourceReceipt *damaged = nullptr) {
  ManagedMaterialDeclarationInput input;
  input.actor_generation = 9U;
  input.definition_generation = 3U;
  input.exact_material_name = std::move(exact_material_name);
  input.declared_type = semantic_type;
  input.resolved_type = semantic_type;
  input.textures[0U] =
      ManagedTextureBinding(ManagedMaterialTextureSlot::DIFFUSE, diffuse);
  input.textures[1U] =
      ManagedTextureBinding(ManagedMaterialTextureSlot::SPECULAR, specular);
  input.textures[2U] =
      damaged != nullptr
          ? ManagedTextureBinding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE,
                                  *damaged)
          : ManagedMaterialTextureBindingInput{};
  input.textures[2U].slot = ManagedMaterialTextureSlot::DAMAGED_DIFFUSE;
  ManagedMaterialDeclaration declaration;
  RequireOk(BuildManagedMaterialDeclaration(
                ManagedMaterialDeclarationRegistryConfiguration{}, input,
                declaration),
            "build native managed specular declaration");
  return declaration;
}

ManagedMaterialDeclaration BuildManagedDiffuseDeclaration(
    const ManagedMaterialTextureSourceReceipt &diffuse,
    std::string exact_material_name = kMaterialName) {
  ManagedMaterialDeclarationInput input;
  input.actor_generation = 9U;
  input.definition_generation = 3U;
  input.exact_material_name = std::move(exact_material_name);
  input.declared_type = ManagedMaterialSemanticType::MESH_STANDARD;
  input.resolved_type = ManagedMaterialSemanticType::MESH_STANDARD;
  input.textures[0U] =
      ManagedTextureBinding(ManagedMaterialTextureSlot::DIFFUSE, diffuse);
  input.textures[1U].slot = ManagedMaterialTextureSlot::SPECULAR;
  input.textures[2U].slot = ManagedMaterialTextureSlot::DAMAGED_DIFFUSE;
  ManagedMaterialDeclaration declaration;
  RequireOk(BuildManagedMaterialDeclaration(
                ManagedMaterialDeclarationRegistryConfiguration{}, input,
                declaration),
            "build native managed diffuse-only declaration");
  return declaration;
}

Ogre14GraphicsSceneMaterialCaptureInput CaptureInput() {
  Ogre14GraphicsSceneMaterialCaptureInput input;
  input.exact_resource_group = kGroup;
  input.exact_name = kMaterialName;
  input.cull = Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
  return input;
}

Ogre14GraphicsSceneMaterialCaptureInput AlexisCaptureInput() {
  Ogre14GraphicsSceneMaterialCaptureInput input;
  input.exact_resource_group = kAlexisGroup;
  input.exact_name = kAlexisMaterialName;
  input.cull = Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
  return input;
}

bool SameCaptureInput(const Ogre14GraphicsSceneMaterialCaptureInput &left,
                      const Ogre14GraphicsSceneMaterialCaptureInput &right) {
  return left.exact_resource_group == right.exact_resource_group &&
         left.exact_name == right.exact_name &&
         left.pass_count == right.pass_count &&
         left.texture_unit_count == right.texture_unit_count &&
         left.has_vertex_program == right.has_vertex_program &&
         left.has_fragment_program == right.has_fragment_program &&
         left.lighting_enabled == right.lighting_enabled &&
         left.diffuse_linear == right.diffuse_linear &&
         left.ambient_linear == right.ambient_linear &&
         left.specular_linear == right.specular_linear &&
         left.emissive_linear == right.emissive_linear &&
         left.shininess == right.shininess && left.blend == right.blend &&
         left.cull == right.cull && left.alpha_reject == right.alpha_reject &&
         left.alpha_reject_value == right.alpha_reject_value &&
         left.depth_write == right.depth_write;
}

std::vector<GraphicsSceneAssetInput>
BuildPlaceholderAssets(const Ogre14GraphicsSceneMaterialCaptureInput &input) {
  std::uint64_t source_id = 0U;
  RequireOk(DeriveOgre14GraphicsSceneMaterialAssetId(
                input.exact_resource_group, input.exact_name, source_id),
            "derive projected placeholder ID");
  MaterialDescriptor placeholder;
  RequireOk(BuildOgre14GraphicsSceneMaterialFallback(input, placeholder),
            "build projected placeholder");
  GraphicsSceneAssetInput asset;
  asset.source_asset_id = source_id;
  asset.payload =
      std::make_shared<const RenderAssetPayload>(std::move(placeholder));
  return {std::move(asset)};
}

const GraphicsSceneAssetInput *FindProjectedMaterial(
    const std::vector<GraphicsSceneAssetInput> &assets) {
  for (const GraphicsSceneAssetInput &asset : assets) {
    if (asset.payload != nullptr &&
        std::get_if<MaterialDescriptor>(asset.payload.get()) != nullptr) {
      return &asset;
    }
  }
  return nullptr;
}

const TextureResourceDescriptor *FindTextureBySourceId(
    const std::vector<GraphicsSceneAssetInput> &assets,
    std::uint64_t source_id) {
  for (const GraphicsSceneAssetInput &asset : assets) {
    if (asset.source_asset_id == source_id && asset.payload != nullptr) {
      return std::get_if<TextureResourceDescriptor>(asset.payload.get());
    }
  }
  return nullptr;
}

const SamplerResourceDescriptor *FindSamplerBySourceId(
    const std::vector<GraphicsSceneAssetInput> &assets,
    std::uint64_t source_id) {
  for (const GraphicsSceneAssetInput &asset : assets) {
    if (asset.source_asset_id == source_id && asset.payload != nullptr) {
      return std::get_if<SamplerResourceDescriptor>(asset.payload.get());
    }
  }
  return nullptr;
}

bool SameAssetOwners(const std::vector<GraphicsSceneAssetInput> &left,
                     const std::vector<GraphicsSceneAssetInput> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.size(); ++index) {
    if (left[index].source_asset_id != right[index].source_asset_id ||
        left[index].payload != right[index].payload ||
        left[index].material_bindings != right[index].material_bindings) {
      return false;
    }
  }
  return true;
}

void RequireZeroReadback(const OgreNextDemoMaterialSource &source,
                         std::size_t native_readbacks) {
  const OgreNextDemoMaterialSourceCounters current =
      source.CurrentCaptureCounters();
  const OgreNextDemoMaterialSourceCounters lifetime = source.LifetimeCounters();
  Require(native_readbacks == 0U && current.gpu_readbacks == 0U &&
              current.authenticated_gpu_readbacks == 0U &&
              current.unauthenticated_gpu_readbacks == 0U &&
              lifetime.gpu_readbacks == 0U &&
              lifetime.authenticated_gpu_readbacks == 0U &&
              lifetime.unauthenticated_gpu_readbacks == 0U,
          "MaterialSource observed a forbidden native GPU readback");
}

void CaptureAndCommit(OgreNextDemoMaterialSource &source,
                      const NativeMaterial &native,
                      const std::shared_ptr<std::size_t> &readbacks) {
  Require(source.BeginCapture(), "begin successful native material capture");
  Ogre14GraphicsSceneMaterialCaptureInput input = CaptureInput();
  bool projected = false;
  RequireOk(source.TryProject(kSectionKey, native.material, true, true, input,
                              projected),
            "project exact ordinary native material");
  Require(projected, "eligible ordinary native material remained matte");
  std::vector<GraphicsSceneAssetInput> assets = BuildPlaceholderAssets(input);
  RequireOk(source.Apply(assets), "apply projected native material assets");
  Require(assets.size() == 3U && source.UsedProjectionCount() == 1U,
          "projected material did not publish one material/texture/sampler");
  const OgreNextDemoMaterialSourceCounters counters =
      source.CurrentCaptureCounters();
  Require(counters.candidate_sections == 1U &&
              counters.projected_sections == 1U &&
              counters.matte_excluded_sections == 0U,
          "native projected section lost exact section denominator");
  Require(counters.distinct_eligible_texture_keys == 1U &&
              counters.distinct_projected_texture_keys == 1U &&
              counters.distinct_matte_only_texture_keys == 0U,
          "native projected section lost distinct texture partition");
  Require(counters.active_texture_state_observations == 1U,
          "native projected section lost active texture observation");
  Require(counters.active_authored_mip_prefix_levels == 1U &&
              counters.active_generated_mip_tail_levels == 1U &&
              counters.active_normalized_output_mip_levels == 2U &&
              counters.active_normalized_texture_observations == 1U &&
              counters.active_opaque_texture_normalizations == 1U &&
              counters.active_straight_alpha_texture_normalizations == 0U &&
              counters.active_linear_specular_texture_normalizations == 0U,
          "native projected section lost active mip normalization buckets");
  Require(counters.projections == 1U &&
              counters.active_replace_material_projections == 1U &&
              counters.active_straight_source_over_material_projections ==
                  0U &&
              counters.active_legacy_straight_alpha_material_projections ==
                  0U &&
              counters.active_alpha_test_disabled_material_projections == 1U &&
              counters.active_alpha_test_greater_material_projections == 0U &&
              counters.active_metallic_roughness_workflow_projections == 1U &&
              counters.active_specular_workflow_projections == 0U,
          "native projection escaped exact blend/test/workflow partitions");
  Require(counters.lossy_material_normalizations == 1U,
          "native projected section hid lossy material normalization");
  RequireZeroReadback(source, *readbacks);
  source.Commit();
  RequireZeroReadback(source, *readbacks);
}

void TestRetryableOrdinaryAbsencePromotion() {
  const std::vector<std::uint8_t> bytes = OpaqueRgbPng();
  auto readbacks = std::make_shared<std::size_t>(0U);
  Ogre14AuthenticatedTextureReceiptRegistry authenticated_registry;
  RequireOk(InitializeOgre14AuthenticatedTextureReceiptRegistry(
                Ogre14AuthenticatedTextureRegistryConfiguration{},
                authenticated_registry),
            "initialize retry authority");
  OrdinaryTrustResolver trust_resolver;
  EmptyAuthorityProvider authority_provider;
  authority_provider.registry = &authenticated_registry;
  authority_provider.resolver = &trust_resolver;
  Ogre14SelectedTextureSourceReceiptRegistry selected_registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry(
                Ogre14SelectedTextureSourceRegistryConfiguration{},
                selected_registry),
            "initialize retry selected registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                kGroup, 1U, selected_registry),
            "activate retry group");
  SelectedResolver selected_resolver;
  selected_resolver.registry = &selected_registry;
  Ogre::TexturePtr texture =
      std::make_shared<TestTexture>(kTextureName, 61U, kGroup, readbacks);
  texture->load();
  NativeMaterial native(texture, 91U);
  OgreNextDemoMaterialSource source;
  Require(
      source.BindAuthenticatedTextureAuthority(trust_resolver,
                                               authority_provider) &&
          source.BindOrdinarySelectedTextureSourceResolver(selected_resolver),
      "bind retry MaterialSource authorities");

  Require(source.BeginCapture(), "begin honest absence frame");
  Ogre14GraphicsSceneMaterialCaptureInput input = CaptureInput();
  const Ogre14GraphicsSceneMaterialCaptureInput before = input;
  bool projected = true;
  RequireOk(source.TryProject(kSectionKey, native.material, true, true, input,
                              projected),
            "honest ordinary absence must be a matte decision");
  const OgreNextDemoMaterialSourceCounters matte =
      source.CurrentCaptureCounters();
  const std::size_t reason =
      static_cast<std::size_t>(OgreNextDemoTextureProjectionExclusion::
                                   ORDINARY_SELECTED_SOURCE_UNAVAILABLE);
  Require(!projected && SameCaptureInput(input, before) &&
              matte.candidate_sections == 1U &&
              matte.projected_sections == 0U &&
              matte.matte_excluded_sections == 1U &&
              matte.exclusions_by_reason[reason] == 1U,
          "honest absence lost named matte denominator accounting");
  std::vector<GraphicsSceneAssetInput> empty_assets;
  RequireOk(source.Apply(empty_assets), "apply honest absence matte frame");
  source.Commit();

  const Ogre14SelectedTextureSourceReceipt receipt =
      BuildReceipt(*texture, 1U, 0U, 0x2500U, bytes);
  RequireOk(
      CommitOgre14SelectedTextureSourceReceipt(receipt, selected_registry),
      "mount receipt after honest absence");
  CaptureAndCommit(source, native, readbacks);
  Require(source.LifetimeCounters().candidate_sections == 2U &&
              source.LifetimeCounters().projected_sections == 1U &&
              source.LifetimeCounters().matte_excluded_sections == 1U,
          "retryable matte was not recounted and promoted after mount");
}

void RequireFrozenProjectionFailure(OgreNextDemoMaterialSource &source,
                                    const NativeMaterial &native,
                                    const char *message) {
  Require(source.BeginCapture(), "begin expected failing native capture");
  Ogre14GraphicsSceneMaterialCaptureInput input = CaptureInput();
  const Ogre14GraphicsSceneMaterialCaptureInput before = input;
  bool projected = true;
  const ValidationResult result = source.TryProject(
      kSectionKey, native.material, true, true, input, projected);
  if (result || result.code != ValidationCode::REVISION_MISMATCH || projected ||
      !SameCaptureInput(input, before)) {
    std::cerr << "frozen failure diagnostic ok=" << result.ok()
              << " code=" << static_cast<unsigned>(result.code)
              << " field=" << result.field << " detail=" << result.detail
              << " projected=" << projected << '\n';
  }
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              !projected && SameCaptureInput(input, before),
          message);
  source.Discard();
}

void TestManagedSpecularProjectionAndRollback() {
  const std::vector<std::uint8_t> bytes = OpaqueRgbPng();
  auto readbacks = std::make_shared<std::size_t>(0U);
  Ogre14AuthenticatedTextureReceiptRegistry authenticated_registry;
  RequireOk(InitializeOgre14AuthenticatedTextureReceiptRegistry(
                Ogre14AuthenticatedTextureRegistryConfiguration{},
                authenticated_registry),
            "initialize managed-specular authenticated authority");
  OrdinaryTrustResolver trust_resolver;
  EmptyAuthorityProvider authority_provider;
  authority_provider.registry = &authenticated_registry;
  authority_provider.resolver = &trust_resolver;

  Ogre14SelectedTextureSourceReceiptRegistry selected_registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry({},
                                                           selected_registry),
            "initialize managed-specular selected registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                kAlexisGroup, 1U, selected_registry),
            "activate managed-specular group");
  SelectedResolver selected_resolver;
  selected_resolver.registry = &selected_registry;

  Ogre::TexturePtr diffuse_texture =
      std::make_shared<TestTexture>(kAlexisDiffuseName, 81U, kAlexisGroup,
                                    readbacks);
  Ogre::TexturePtr specular_texture = std::make_shared<TestTexture>(
      kAlexisSpecularName, 82U, kAlexisGroup, readbacks);
  Ogre::TexturePtr environment_texture = std::make_shared<TestTexture>(
      "EnvironmentTexture", 83U, kAlexisGroup, readbacks,
      Ogre::TEX_TYPE_CUBE_MAP);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(
                BuildReceipt(*diffuse_texture, 1U, 0U, 0x5100U, bytes),
                selected_registry),
            "commit managed diffuse receipt");
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(
                BuildReceipt(*specular_texture, 1U, 0U, 0x5200U, bytes),
                selected_registry),
            "commit managed specular receipt");
  diffuse_texture->load();
  specular_texture->load();
  environment_texture->load();
  AlexisNativeMaterial native(diffuse_texture, specular_texture,
                              environment_texture);

  Ogre14SelectedTextureSourceResolution diffuse_resolution;
  Ogre14SelectedTextureSourceResolution specular_resolution;
  RequireOk(selected_resolver.ResolveSelectedTextureSource(
                *diffuse_texture, diffuse_resolution),
            "resolve managed diffuse authority");
  RequireOk(selected_resolver.ResolveSelectedTextureSource(
                *specular_texture, specular_resolution),
            "resolve managed specular authority");
  ManagedMaterialTextureSourceReceipt diffuse_receipt;
  ManagedMaterialTextureSourceReceipt specular_receipt;
  Ogre14ManagedMaterialSourceAuthorityBinding diffuse_binding;
  Ogre14ManagedMaterialSourceAuthorityBinding specular_binding;
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildSelected(
                diffuse_texture, trust_resolver, selected_resolver,
                diffuse_resolution, {}, diffuse_receipt, diffuse_binding),
            "adapt managed diffuse selected source");
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildSelected(
                specular_texture, trust_resolver, selected_resolver,
                specular_resolution, {}, specular_receipt, specular_binding),
            "adapt managed linear specular selected source");
  const ManagedMaterialDeclaration declaration =
      BuildManagedSpecularDeclaration(diffuse_receipt, specular_receipt);
  std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
             kManagedMaterialTextureSlotCount>
      source_bindings{};
  source_bindings[0U] = diffuse_binding;
  source_bindings[1U] = specular_binding;
  Ogre14ManagedMaterialDeclarationBinding material_binding;
  RequireOk(Ogre14ManagedMaterialDeclarationBinding::Build(
                native.base.material, declaration, source_bindings,
                trust_resolver, selected_resolver, material_binding),
            "bind managed declaration to exact native material");

  OgreNextDemoMaterialSource source;
  Require(source.BindAuthenticatedTextureAuthority(trust_resolver,
                                                   authority_provider) &&
              source.BindOrdinarySelectedTextureSourceResolver(
                  selected_resolver),
          "bind managed-specular MaterialSource authorities");

  const auto require_managed_semantic_matte =
      [&](const ManagedMaterialDeclaration &hostile_declaration,
          std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
                     kManagedMaterialTextureSlotCount>
              hostile_bindings,
          const char *message) {
        Ogre14ManagedMaterialDeclarationBinding hostile_binding;
        RequireOk(Ogre14ManagedMaterialDeclarationBinding::Build(
                      native.base.material, hostile_declaration,
                      hostile_bindings, trust_resolver, selected_resolver,
                      hostile_binding),
                  "bind hostile managed declaration");
        Require(source.BeginCapture(), "begin hostile managed semantic gate");
        Ogre14GraphicsSceneMaterialCaptureInput hostile_input =
            AlexisCaptureInput();
        bool hostile_projected = true;
        RequireOk(source.TryProject(kSectionKey, native.base.material, true,
                                    true, &hostile_binding, hostile_input,
                                    hostile_projected),
                  "evaluate hostile managed semantic gate");
        const OgreNextDemoMaterialSourceCounters hostile_counters =
            source.CurrentCaptureCounters();
        Require(!hostile_projected &&
                    hostile_counters.matte_excluded_sections == 1U &&
                    hostile_counters.exclusions_by_reason[static_cast<
                        std::size_t>(OgreNextDemoTextureProjectionExclusion::
                                        MANAGED_MATERIAL_SEMANTIC_UNSUPPORTED)] ==
                        1U,
                message);
        RequireZeroReadback(source, *readbacks);
        source.Discard();
      };

  auto damaged_bindings = source_bindings;
  damaged_bindings[2U] = diffuse_binding;
  require_managed_semantic_matte(
      BuildManagedSpecularDeclaration(diffuse_receipt, specular_receipt,
                                      kAlexisMaterialName,
                                      ManagedMaterialSemanticType::FLEXMESH_STANDARD,
                                      &diffuse_receipt),
      damaged_bindings,
      "configured damaged diffuse was silently dropped by managed lowering");
  require_managed_semantic_matte(
      BuildManagedSpecularDeclaration(
          diffuse_receipt, specular_receipt, kAlexisMaterialName,
          ManagedMaterialSemanticType::MESH_TRANSPARENT),
      source_bindings,
      "transparent managed declaration was accepted with opaque native state");

  Require(source.BeginCapture(), "begin managed-specular projection");
  Ogre14GraphicsSceneMaterialCaptureInput input = AlexisCaptureInput();
  bool projected = false;
  RequireOk(source.TryProject(kSectionKey, native.base.material, true, true,
                              &material_binding, input, projected),
            "project managed diffuse/specular declaration");
  Require(projected, "managed authored specular material remained matte");
  std::vector<GraphicsSceneAssetInput> assets = BuildPlaceholderAssets(input);
  RequireOk(source.Apply(assets), "apply managed authored specular projection");
  const GraphicsSceneAssetInput *const material_asset =
      FindProjectedMaterial(assets);
  Require(material_asset != nullptr && assets.size() == 5U,
          "managed projection did not publish material/base/specular and two independent samplers");
  const MaterialDescriptor *const material =
      std::get_if<MaterialDescriptor>(material_asset->payload.get());
  const GraphicsSceneAssetBinding &specular_asset_binding =
      material_asset->material_bindings[static_cast<std::size_t>(
          MaterialTextureSlot::SPECULAR)];
  const TextureResourceDescriptor *const specular_descriptor =
      FindTextureBySourceId(assets,
                            specular_asset_binding.texture_source_asset_id);
  const SamplerResourceDescriptor *const specular_sampler =
      FindSamplerBySourceId(assets,
                            specular_asset_binding.sampler_source_asset_id);
  const GraphicsSceneAssetBinding &base_asset_binding =
      material_asset->material_bindings[static_cast<std::size_t>(
          MaterialTextureSlot::BASE_COLOR)];
  Require(material != nullptr &&
              material->pbr_workflow == MaterialPbrWorkflow::SPECULAR &&
              material->metallic_factor == 0.0F &&
              material->specular_factor == Float3{1.0F, 1.0F, 1.0F} &&
              specular_asset_binding.texture_source_asset_id != 0U &&
              specular_asset_binding.sampler_source_asset_id != 0U &&
              specular_asset_binding.sampler_source_asset_id !=
                  base_asset_binding.sampler_source_asset_id &&
              specular_descriptor != nullptr &&
              specular_descriptor->color_space == TextureColorSpace::LINEAR &&
              specular_descriptor->mip_levels.size() == 2U &&
              specular_sampler != nullptr &&
              specular_sampler->minification_filter == SamplerFilter::LINEAR &&
              specular_sampler->magnification_filter ==
                  SamplerFilter::LINEAR &&
              specular_sampler->mip_filter == SamplerFilter::LINEAR &&
              specular_sampler->address_u ==
                  SamplerAddressMode::CLAMP_TO_EDGE &&
              specular_sampler->anisotropy_enabled &&
              specular_sampler->maximum_anisotropy == 8.0F,
          "managed specular source was collapsed, gamma-relabelled, or given "
          "invented metallic state");
  const OgreNextDemoMaterialSourceCounters counters =
      source.CurrentCaptureCounters();
  Require(counters.authored_specular_source_decodes == 1U &&
              counters.linear_specular_source_normalizations == 1U &&
              counters.authored_specular_mip_prefix_levels == 1U &&
              counters.generated_specular_mip_tail_levels == 1U &&
              counters.normalized_specular_output_mip_levels == 2U &&
              counters.modern_source_normalizations == 2U &&
              counters.authored_mip_prefix_levels == 2U &&
              counters.generated_mip_tail_levels == 2U &&
              counters.normalized_output_mip_levels == 4U &&
              counters.specular_workflow_projections == 1U &&
              counters.projections == 1U &&
              counters.active_specular_workflow_projections == 1U &&
              counters.active_metallic_roughness_workflow_projections == 0U &&
              counters.active_normalized_texture_observations == 2U &&
              counters.active_opaque_texture_normalizations == 1U &&
              counters.active_linear_specular_texture_normalizations == 1U &&
              counters.active_anisotropic_sampler_projections == 1U &&
              counters.anisotropic_sampler_projections == 1U,
          "managed specular projection escaped exact decode/workflow/policy "
          "accounting");
  RequireZeroReadback(source, *readbacks);
  source.Commit();

  const Ogre::ColourValue specular_pass_ambient_before =
      native.specular_pass->getAmbient();
  native.specular_pass->setAmbient(
      Ogre::ColourValue(0.5F, 1.0F, 1.0F, 1.0F));
  Require(source.BeginCapture(),
          "begin managed-specular pass-state mutation gate");
  input = AlexisCaptureInput();
  projected = false;
  const ValidationResult specular_pass_mutation = source.TryProject(
      kSectionKey, native.base.material, true, true, &material_binding, input,
      projected);
  Require(!specular_pass_mutation &&
              specular_pass_mutation.code ==
                  ValidationCode::REVISION_MISMATCH &&
              !projected,
          "noncanonical managed specular-pass lighting escaped frozen state");
  RequireZeroReadback(source, *readbacks);
  source.Discard();
  native.specular_pass->setAmbient(specular_pass_ambient_before);

  Require(source.BeginCapture(),
          "begin managed-specular sampler rollback");
  input = AlexisCaptureInput();
  projected = false;
  RequireOk(source.TryProject(kSectionKey, native.base.material, true, true,
                              &material_binding, input, projected),
            "reuse managed projection before specular sampler mutation");
  Require(projected,
          "managed projection cache did not revalidate specular sampler");
  assets = BuildPlaceholderAssets(input);
  const std::vector<GraphicsSceneAssetInput> sampler_before = assets;
  native.specular_sampler->setAnisotropy(16U);
  const ValidationResult sampler_failed_apply = source.Apply(assets);
  Require(!sampler_failed_apply &&
              sampler_failed_apply.code == ValidationCode::REVISION_MISMATCH &&
              SameAssetOwners(assets, sampler_before),
          "managed specular sampler mutation partially published output");
  native.specular_sampler->setAnisotropy(8U);
  RequireZeroReadback(source, *readbacks);
  source.Discard();

  const Ogre14SelectedTextureSourceReceiptRegistry stable_registry =
      selected_registry;
  Require(source.BeginCapture(),
          "begin managed-specular final-authority rollback");
  input = AlexisCaptureInput();
  projected = false;
  RequireOk(source.TryProject(kSectionKey, native.base.material, true, true,
                              &material_binding, input, projected),
            "reuse managed projection before authority mutation");
  Require(projected, "managed projection cache did not revalidate before use");
  assets = BuildPlaceholderAssets(input);
  const std::vector<GraphicsSceneAssetInput> before = assets;
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(
                BuildReceipt(*specular_texture, 1U, 0U, 0x5201U, bytes),
                selected_registry),
            "mutate managed specular immutable receipt before Apply");
  const ValidationResult failed_apply = source.Apply(assets);
  Require(!failed_apply &&
              failed_apply.code == ValidationCode::REVISION_MISMATCH &&
              SameAssetOwners(assets, before) &&
              !material_binding.Revalidate(trust_resolver, selected_resolver),
          "managed specular authority mutation partially published output");
  RequireZeroReadback(source, *readbacks);
  source.Discard();
  selected_registry = stable_registry;

  Require(source.BeginCapture(), "begin managed-specular cache reuse");
  input = AlexisCaptureInput();
  projected = false;
  RequireOk(source.TryProject(kSectionKey, native.base.material, true, true,
                              &material_binding, input, projected),
            "revalidate managed-specular cache after rollback");
  assets = BuildPlaceholderAssets(input);
  Require(projected, "managed projection did not survive atomic rollback");
  RequireOk(source.Apply(assets), "apply managed-specular cache reuse");
  const OgreNextDemoMaterialSourceCounters reuse =
      source.CurrentCaptureCounters();
  Require(reuse.authored_specular_source_decodes == 0U &&
              reuse.active_specular_workflow_projections == 1U &&
              reuse.active_linear_specular_texture_normalizations == 1U,
          "managed cache reuse re-decoded or lost active specular authority");
  RequireZeroReadback(source, *readbacks);
  source.Commit();
}

void TestAlphaStateAndAnisotropicProjection() {
  const std::vector<std::uint8_t> bytes = OpaqueRgbPng();
  auto readbacks = std::make_shared<std::size_t>(0U);
  Ogre14AuthenticatedTextureReceiptRegistry authenticated_registry;
  RequireOk(InitializeOgre14AuthenticatedTextureReceiptRegistry(
                Ogre14AuthenticatedTextureRegistryConfiguration{},
                authenticated_registry),
            "initialize alpha authenticated authority");
  OrdinaryTrustResolver trust_resolver;
  EmptyAuthorityProvider authority_provider;
  authority_provider.registry = &authenticated_registry;
  authority_provider.resolver = &trust_resolver;
  Ogre14SelectedTextureSourceReceiptRegistry selected_registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry({},
                                                           selected_registry),
            "initialize alpha selected registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                kGroup, 1U, selected_registry),
            "activate alpha selected group");
  SelectedResolver selected_resolver;
  selected_resolver.registry = &selected_registry;
  Ogre::TexturePtr texture =
      std::make_shared<TestTexture>(kTextureName, 83U, kGroup, readbacks);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(
                BuildReceipt(*texture, 1U, 0U, 0x5300U, bytes),
                selected_registry),
            "commit alpha source receipt");
  texture->load();
  NativeMaterial native(texture, 112U);
  native.sampler->setFiltering(Ogre::FO_ANISOTROPIC,
                               Ogre::FO_ANISOTROPIC,
                               Ogre::FO_LINEAR);
  native.sampler->setAnisotropy(8U);
  native.pass->setSeparateSceneBlending(
      Ogre::SBF_SOURCE_ALPHA, Ogre::SBF_ONE_MINUS_SOURCE_ALPHA,
      Ogre::SBF_ONE, Ogre::SBF_ONE_MINUS_SOURCE_ALPHA);
  native.pass->setAlphaRejectSettings(Ogre::CMPF_GREATER, 2U);
  native.pass->setDepthWriteEnabled(false);

  OgreNextDemoMaterialSource source;
  Require(source.BindAuthenticatedTextureAuthority(trust_resolver,
                                                   authority_provider) &&
              source.BindOrdinarySelectedTextureSourceResolver(
                  selected_resolver),
          "bind alpha MaterialSource authorities");
  Require(source.BeginCapture(), "begin true source-over alpha capture");
  Ogre14GraphicsSceneMaterialCaptureInput input = CaptureInput();
  bool projected = false;
  RequireOk(source.TryProject(kSectionKey, native.material, true, true, input,
                              projected),
            "project true source-over plus GREATER alpha state");
  Require(projected, "exact alpha state remained matte");
  std::vector<GraphicsSceneAssetInput> assets = BuildPlaceholderAssets(input);
  RequireOk(source.Apply(assets), "apply true source-over alpha projection");
  const GraphicsSceneAssetInput *material_asset = FindProjectedMaterial(assets);
  const MaterialDescriptor *material =
      material_asset != nullptr
          ? std::get_if<MaterialDescriptor>(material_asset->payload.get())
          : nullptr;
  const OgreNextDemoMaterialSourceCounters true_alpha =
      source.CurrentCaptureCounters();
  Require(material != nullptr &&
              material->blend_mode ==
                  MaterialBlendMode::STRAIGHT_SOURCE_OVER &&
              material->alpha_test_mode == MaterialAlphaTestMode::GREATER &&
              material->alpha_cutoff == 2.0F / 255.0F &&
              !material->depth_write &&
              true_alpha.projections == 1U &&
              true_alpha.active_straight_source_over_material_projections ==
                  1U &&
              true_alpha.active_alpha_test_greater_material_projections == 1U &&
              true_alpha.active_anisotropic_sampler_projections == 1U &&
              true_alpha.active_straight_alpha_texture_normalizations == 1U &&
              true_alpha.straight_source_over_material_projections == 1U &&
              true_alpha.alpha_test_material_projections == 1U &&
              true_alpha.anisotropic_sampler_projections == 1U,
          "true source-over/GREATER/depth/anisotropy state was collapsed or "
          "lost exact accounting");
  RequireZeroReadback(source, *readbacks);
  source.Commit();

  Require(source.BeginCapture(), "begin alpha Apply rollback capture");
  input = CaptureInput();
  projected = false;
  RequireOk(source.TryProject(kSectionKey, native.material, true, true, input,
                              projected),
            "reuse alpha projection before comparator mutation");
  assets = BuildPlaceholderAssets(input);
  const std::vector<GraphicsSceneAssetInput> before = assets;
  native.pass->setAlphaRejectSettings(Ogre::CMPF_GREATER_EQUAL, 2U);
  const ValidationResult failed_apply = source.Apply(assets);
  Require(!failed_apply &&
              failed_apply.code == ValidationCode::REVISION_MISMATCH &&
              SameAssetOwners(assets, before),
          "alpha comparator mutation partially published projected assets");
  native.pass->setAlphaRejectSettings(Ogre::CMPF_GREATER, 2U);
  source.Discard();

  source.Reset();
  native.pass->setSeparateSceneBlending(
      Ogre::SBF_SOURCE_ALPHA, Ogre::SBF_ONE_MINUS_SOURCE_ALPHA,
      Ogre::SBF_SOURCE_ALPHA, Ogre::SBF_ONE_MINUS_SOURCE_ALPHA);
  Require(source.BeginCapture(), "begin legacy straight-alpha capture");
  input = CaptureInput();
  projected = false;
  RequireOk(source.TryProject(kSectionKey, native.material, true, true, input,
                              projected),
            "project exact legacy squared-alpha tuple");
  assets = BuildPlaceholderAssets(input);
  Require(projected, "legacy squared-alpha tuple remained matte");
  RequireOk(source.Apply(assets), "apply legacy squared-alpha projection");
  material_asset = FindProjectedMaterial(assets);
  material = material_asset != nullptr
                 ? std::get_if<MaterialDescriptor>(
                       material_asset->payload.get())
                 : nullptr;
  const OgreNextDemoMaterialSourceCounters legacy =
      source.CurrentCaptureCounters();
  Require(material != nullptr &&
              material->blend_mode ==
                  MaterialBlendMode::LEGACY_STRAIGHT_ALPHA &&
              material->alpha_test_mode == MaterialAlphaTestMode::GREATER &&
              !material->depth_write &&
              legacy.active_legacy_straight_alpha_material_projections == 1U &&
              legacy.active_straight_source_over_material_projections == 0U,
          "legacy squared-alpha tuple was relabelled as true source-over");
  RequireZeroReadback(source, *readbacks);
  source.Commit();

  source.Reset();
  native.pass->setSeparateSceneBlending(
      Ogre::SBF_SOURCE_ALPHA, Ogre::SBF_ONE_MINUS_SOURCE_ALPHA,
      Ogre::SBF_ONE, Ogre::SBF_ONE_MINUS_SOURCE_ALPHA);
  native.pass->setAlphaRejectSettings(Ogre::CMPF_ALWAYS_PASS, 0U);
  Ogre::ColourValue nonunit_alpha = native.pass->getDiffuse();
  nonunit_alpha.a = 0.5F;
  native.pass->setDiffuse(nonunit_alpha);
  Require(source.BeginCapture(),
          "begin nonunit factor-alpha blend exclusion capture");
  input = CaptureInput();
  projected = true;
  RequireOk(source.TryProject(kSectionKey, native.material, true, true, input,
                              projected),
            "classify nonunit factor-alpha blended pass");
  const OgreNextDemoMaterialSourceCounters nonunit_alpha_counters =
      source.CurrentCaptureCounters();
  Require(!projected && nonunit_alpha_counters.candidate_sections == 1U &&
              nonunit_alpha_counters.matte_excluded_sections == 1U &&
              nonunit_alpha_counters.exclusions_by_reason[static_cast<
                  std::size_t>(OgreNextDemoTextureProjectionExclusion::
                                  MATERIAL_STATE_UNSUPPORTED)] == 1U,
          "double-attenuating nonunit factor-alpha blend escaped source "
          "classification");
  source.Discard();
}

void TestNativeMaterialSourceLifecycle() {
  TestTexture::destruction_count = 0U;
  const std::vector<std::uint8_t> bytes = OpaqueRgbPng();
  auto readbacks = std::make_shared<std::size_t>(0U);

  Ogre14AuthenticatedTextureReceiptRegistry authenticated_registry;
  RequireOk(InitializeOgre14AuthenticatedTextureReceiptRegistry(
                Ogre14AuthenticatedTextureRegistryConfiguration{},
                authenticated_registry),
            "initialize empty authenticated authority");
  OrdinaryTrustResolver trust_resolver;
  EmptyAuthorityProvider authority_provider;
  authority_provider.registry = &authenticated_registry;
  authority_provider.resolver = &trust_resolver;

  Ogre14SelectedTextureSourceReceiptRegistry selected_registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry(
                Ogre14SelectedTextureSourceRegistryConfiguration{},
                selected_registry),
            "initialize selected-source registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                kGroup, 1U, selected_registry),
            "activate initial selected-source group");
  SelectedResolver selected_resolver;
  selected_resolver.registry = &selected_registry;

  Ogre::TexturePtr texture =
      std::make_shared<TestTexture>(kTextureName, 71U, kGroup, readbacks);
  const Ogre14SelectedTextureSourceReceipt initial_receipt =
      BuildReceipt(*texture, 1U, 0U, 0x3000U, bytes);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(initial_receipt,
                                                     selected_registry),
            "commit initial selected-source receipt");
  texture->load();
  Require(texture->isLoaded() && texture->getStateCount() == 1U,
          "initial synthetic texture load state changed");
  auto native = std::make_unique<NativeMaterial>(texture);

  OgreNextDemoMaterialSource source;
  Require(
      source.BindAuthenticatedTextureAuthority(trust_resolver,
                                               authority_provider) &&
          source.BindOrdinarySelectedTextureSourceResolver(selected_resolver),
      "bind native MaterialSource authorities");

  // Frame N establishes one used ordinary projection.
  CaptureAndCommit(source, *native, readbacks);
  Require(source.LifetimeCounters().ordinary_observed_source_decodes == 1U &&
              authority_provider.capture_calls == 0U,
          "ordinary frame probed authenticated authority or lost accounting");

  // A diffuse-only managed declaration is output-equivalent to opaque v2.
  // Adopt its authority in the pending COW cache without changing the exact
  // material asset ID/name, then prove the committed cache no longer permits
  // an unbound caller to bypass that declaration.
  {
  Require(source.BeginCapture(), "begin opaque-v2 identity baseline");
  Ogre14GraphicsSceneMaterialCaptureInput v2_input = CaptureInput();
  bool v2_projected = false;
  RequireOk(source.TryProject(kSectionKey, native->material, true, true,
                              v2_input, v2_projected),
            "project opaque-v2 identity baseline");
  std::vector<GraphicsSceneAssetInput> v2_assets =
      BuildPlaceholderAssets(v2_input);
  Require(v2_projected && v2_input.exact_name.rfind("OpaqueTUS0/", 0U) == 0U,
          "opaque baseline lost its v2 material name");
  RequireOk(source.Apply(v2_assets), "apply opaque-v2 identity baseline");
  const GraphicsSceneAssetInput *const v2_material =
      FindProjectedMaterial(v2_assets);
  Require(v2_material != nullptr, "opaque-v2 material asset is absent");
  const std::uint64_t v2_material_id = v2_material->source_asset_id;
  const std::string v2_material_name = v2_input.exact_name;
  source.Commit();

  Ogre14SelectedTextureSourceResolution managed_resolution;
  RequireOk(selected_resolver.ResolveSelectedTextureSource(
                *texture, managed_resolution),
            "resolve diffuse-only managed source authority");
  ManagedMaterialTextureSourceReceipt managed_diffuse_receipt;
  Ogre14ManagedMaterialSourceAuthorityBinding managed_diffuse_authority;
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildSelected(
                texture, trust_resolver, selected_resolver,
                managed_resolution, {}, managed_diffuse_receipt,
                managed_diffuse_authority),
            "adapt diffuse-only managed source");
  const ManagedMaterialDeclaration managed_diffuse_declaration =
      BuildManagedDiffuseDeclaration(managed_diffuse_receipt);
  std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
             kManagedMaterialTextureSlotCount>
      managed_diffuse_bindings{};
  managed_diffuse_bindings[0U] = managed_diffuse_authority;
  Ogre14ManagedMaterialDeclarationBinding managed_diffuse_binding;
  RequireOk(Ogre14ManagedMaterialDeclarationBinding::Build(
                native->material, managed_diffuse_declaration,
                managed_diffuse_bindings, trust_resolver, selected_resolver,
                managed_diffuse_binding),
            "bind diffuse-only managed declaration");

  Require(source.BeginCapture(),
          "begin discarded opaque-v2 managed cache promotion");
  Ogre14GraphicsSceneMaterialCaptureInput discarded_promotion_input =
      CaptureInput();
  bool discarded_promotion_projected = false;
  RequireOk(source.TryProject(kSectionKey, native->material, true, true,
                              &managed_diffuse_binding,
                              discarded_promotion_input,
                              discarded_promotion_projected),
            "stage discarded opaque-v2 managed cache promotion");
  Require(discarded_promotion_projected,
          "discarded managed promotion fixture remained matte");
  source.Discard();

  Require(source.BeginCapture(),
          "begin unbound reuse after discarded managed promotion");
  Ogre14GraphicsSceneMaterialCaptureInput unbound_after_discard_input =
      CaptureInput();
  bool unbound_after_discard_projected = false;
  RequireOk(source.TryProject(kSectionKey, native->material, true, true,
                              unbound_after_discard_input,
                              unbound_after_discard_projected),
            "reuse committed unbound cache after discarded promotion");
  Require(unbound_after_discard_projected &&
              unbound_after_discard_input.exact_name == v2_material_name,
          "discarded managed promotion mutated the committed opaque-v2 cache");
  source.Discard();

  Require(source.BeginCapture(), "begin opaque-v2 managed cache promotion");
  Ogre14GraphicsSceneMaterialCaptureInput promoted_input = CaptureInput();
  bool promoted = false;
  RequireOk(source.TryProject(kSectionKey, native->material, true, true,
                              &managed_diffuse_binding, promoted_input,
                              promoted),
            "promote opaque-v2 cache to managed authority");
  std::vector<GraphicsSceneAssetInput> promoted_assets =
      BuildPlaceholderAssets(promoted_input);
  RequireOk(source.Apply(promoted_assets),
            "apply managed opaque-v2 cache promotion");
  const GraphicsSceneAssetInput *const promoted_material =
      FindProjectedMaterial(promoted_assets);
  Require(promoted && promoted_material != nullptr &&
              promoted_material->source_asset_id == v2_material_id &&
              promoted_input.exact_name == v2_material_name,
          "diffuse-only managed authority churned opaque-v2 asset identity");
  source.Commit();

  RequireFrozenProjectionFailure(
      source, *native,
      "committed managed cache permitted an unbound authority bypass");
  source.Reset();
  CaptureAndCommit(source, *native, readbacks);
  }

  // A same-state receipt retry is a different immutable publication. It must
  // invalidate the frozen projection without changing its input or cache.
  const Ogre14SelectedTextureSourceReceiptRegistry initial_registry =
      selected_registry;
  const Ogre14SelectedTextureSourceReceipt replaced_receipt =
      BuildReceipt(*texture, 1U, 0U, 0x3001U, bytes);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(replaced_receipt,
                                                     selected_registry),
            "commit receipt-observation mutation");
  RequireFrozenProjectionFailure(
      source, *native,
      "receipt mutation was flattened or changed capture input");
  selected_registry = initial_registry;
  CaptureAndCommit(source, *native, readbacks);

  // A real native reload and its strictly newer receipt remain a map-cache
  // revision change. Reset is the only admission point for that generation.
  texture->reload();
  Require(texture->isLoaded() && texture->getStateCount() == 2U,
          "synthetic texture reload did not advance native state");
  const Ogre14SelectedTextureSourceReceipt reload_receipt =
      BuildReceipt(*texture, 1U, 1U, 0x3002U, bytes);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(reload_receipt,
                                                     selected_registry),
            "commit strict reload receipt");
  RequireFrozenProjectionFailure(
      source, *native,
      "native reload reused a stale frozen material projection");
  source.Reset();
  CaptureAndCommit(source, *native, readbacks);

  // The same Sampler allocation with changed accepted filter state must be a
  // semantic revision, not a pointer-identity cache hit.
  native->sampler->setFiltering(Ogre::FO_POINT, Ogre::FO_LINEAR,
                                Ogre::FO_POINT);
  RequireFrozenProjectionFailure(
      source, *native,
      "in-place sampler mutation escaped the exact sampler fingerprint");
  native->sampler->setFiltering(Ogre::FO_LINEAR, Ogre::FO_LINEAR,
                                Ogre::FO_POINT);
  CaptureAndCommit(source, *native, readbacks);

  // Legacy gamma/mip/usage/source/output state is accepted as provenance, not
  // output authority. Every reachable bit remains frozen and a mutation is
  // terminal for the committed cache rather than silently changing
  // publication. OGRE 14's _getTexturePtr() reapplies its private requested
  // scalar gamma before returning, so scalar-gamma fingerprint mutations are
  // covered by the exact pure-policy seam; hardware-gamma is stable in this
  // native lifecycle seam.
  auto test_texture = std::static_pointer_cast<TestTexture>(texture);
  Require(test_texture->getGamma() == 1.0F && native->unit->getGamma() == 1.0F,
          "synthetic gamma baseline changed before mutation");
  native->unit->setHardwareGammaEnabled(true);
  RequireFrozenProjectionFailure(
      source, *native, "hardware-gamma mutation escaped provenance key");
  native->unit->setHardwareGammaEnabled(false);
  test_texture->MutateMipState(1U, true);
  RequireFrozenProjectionFailure(
      source, *native, "native mip-count/hardware generation mutation escaped");
  test_texture->MutateMipState(0U, false);
  test_texture->MutateUsage(Ogre::TU_STATIC | Ogre::TU_AUTOMIPMAP);
  RequireFrozenProjectionFailure(
      source, *native, "TU_AUTOMIPMAP usage mutation escaped provenance key");
  test_texture->MutateUsage(Ogre::TU_STATIC);
  test_texture->MutateSourceState(4U, 2U, 1U, Ogre::PF_BYTE_RGBA);
  RequireFrozenProjectionFailure(
      source, *native, "native source dimension mutation escaped provenance");
  test_texture->MutateSourceState(2U, 2U, 1U, Ogre::PF_BYTE_RGBA);
  test_texture->MutateOutputState(2U, 2U, 1U, Ogre::PF_A8R8G8B8);
  RequireFrozenProjectionFailure(
      source, *native, "native output format mutation escaped provenance");
  test_texture->MutateOutputState(2U, 2U, 1U, Ogre::PF_BYTE_RGBA);
  const Ogre::ColourValue ambient_before = native->pass->getAmbient();
  native->pass->setAmbient(0.25F, 0.5F, 0.75F);
  RequireFrozenProjectionFailure(
      source, *native,
      "lossy-normalized ambient mutation escaped material fingerprint");
  native->pass->setAmbient(ambient_before);
  const Ogre::ColourValue specular_before = native->pass->getSpecular();
  native->pass->setSpecular(Ogre::ColourValue(0.75F, 0.5F, 0.25F, 1.0F));
  RequireFrozenProjectionFailure(
      source, *native,
      "lossy-normalized specular mutation escaped material fingerprint");
  native->pass->setSpecular(specular_before);
  const Ogre::ColourValue emissive_before = native->pass->getSelfIllumination();
  Ogre::ColourValue emissive_alpha_mutation = emissive_before;
  emissive_alpha_mutation.a = 0.5F;
  native->pass->setSelfIllumination(emissive_alpha_mutation);
  RequireFrozenProjectionFailure(
      source, *native,
      "discarded emissive alpha mutation escaped material fingerprint");
  native->pass->setSelfIllumination(emissive_before);

  // Mutation after TryProject must fail Apply atomically as well; this reaches
  // the genuine final-publication native owner revalidation seam.
  Require(source.BeginCapture(), "begin native-state Apply rollback capture");
  Ogre14GraphicsSceneMaterialCaptureInput state_rollback_input = CaptureInput();
  bool state_rollback_projected = false;
  RequireOk(source.TryProject(kSectionKey, native->material, true, true,
                              state_rollback_input, state_rollback_projected),
            "project before native-state Apply mutation");
  Require(state_rollback_projected,
          "native-state rollback fixture did not project");
  std::vector<GraphicsSceneAssetInput> state_rollback_assets =
      BuildPlaceholderAssets(state_rollback_input);
  const std::vector<GraphicsSceneAssetInput> state_rollback_before =
      state_rollback_assets;
  native->unit->setHardwareGammaEnabled(true);
  const ValidationResult state_rollback_result =
      source.Apply(state_rollback_assets);
  Require(!state_rollback_result &&
              state_rollback_result.code == ValidationCode::REVISION_MISMATCH &&
              SameAssetOwners(state_rollback_assets, state_rollback_before),
          "native-state Apply mutation partially published projected assets");
  native->unit->setHardwareGammaEnabled(false);
  source.Discard();
  CaptureAndCommit(source, *native, readbacks);

  // The final publication guard repeats the complete admitted TUS0 semantic
  // predicate, not only pointer, sampler, and texture provenance checks.
  Require(source.BeginCapture(), "begin TUS semantic Apply rollback capture");
  Ogre14GraphicsSceneMaterialCaptureInput tus_rollback_input = CaptureInput();
  bool tus_rollback_projected = false;
  RequireOk(source.TryProject(kSectionKey, native->material, true, true,
                              tus_rollback_input, tus_rollback_projected),
            "project before TUS semantic Apply mutation");
  Require(tus_rollback_projected,
          "TUS semantic rollback fixture did not project");
  std::vector<GraphicsSceneAssetInput> tus_rollback_assets =
      BuildPlaceholderAssets(tus_rollback_input);
  const std::vector<GraphicsSceneAssetInput> tus_rollback_before =
      tus_rollback_assets;
  native->unit->setTextureCoordSet(1U);
  const ValidationResult tus_rollback_result =
      source.Apply(tus_rollback_assets);
  Require(!tus_rollback_result &&
              tus_rollback_result.code == ValidationCode::REVISION_MISMATCH &&
              SameAssetOwners(tus_rollback_assets, tus_rollback_before),
          "TUS semantic Apply mutation partially published projected assets");
  native->unit->setTextureCoordSet(0U);
  source.Discard();
  CaptureAndCommit(source, *native, readbacks);

  // Availability classification is part of the same final TUS0 contract.
  // Changing only the content mode leaves the live texture pointer intact,
  // so pointer identity alone cannot authorize publication.
  Require(source.BeginCapture(),
          "begin TUS content-mode Apply rollback capture");
  Ogre14GraphicsSceneMaterialCaptureInput content_rollback_input =
      CaptureInput();
  bool content_rollback_projected = false;
  RequireOk(source.TryProject(kSectionKey, native->material, true, true,
                              content_rollback_input,
                              content_rollback_projected),
            "project before TUS content-mode Apply mutation");
  Require(content_rollback_projected,
          "TUS content-mode rollback fixture did not project");
  std::vector<GraphicsSceneAssetInput> content_rollback_assets =
      BuildPlaceholderAssets(content_rollback_input);
  const std::vector<GraphicsSceneAssetInput> content_rollback_before =
      content_rollback_assets;
  native->unit->setContentType(Ogre::TextureUnitState::CONTENT_SHADOW);
  const ValidationResult content_rollback_result =
      source.Apply(content_rollback_assets);
  Require(!content_rollback_result &&
              content_rollback_result.code ==
                  ValidationCode::REVISION_MISMATCH &&
              SameAssetOwners(content_rollback_assets, content_rollback_before),
          "TUS content-mode Apply mutation partially published assets");
  native->unit->setContentType(Ogre::TextureUnitState::CONTENT_NAMED);
  source.Discard();
  CaptureAndCommit(source, *native, readbacks);

  // OGRE14 state is a regression-floor observation, never output authority:
  // a fresh generation with automipmaps and hardware-generated legacy mips
  // still normalizes the source PNG into the modern full chain.
  source.Reset();
  native->unit->setHardwareGammaEnabled(false);
  test_texture->MutateMipState(1U, true);
  test_texture->MutateUsage(Ogre::TU_STATIC | Ogre::TU_AUTOMIPMAP);
  CaptureAndCommit(source, *native, readbacks);
  const OgreNextDemoMaterialSourceCounters modern_floor =
      source.LifetimeCounters();
  Require(modern_floor.legacy_hardware_gamma_off_observations != 0U &&
              modern_floor.legacy_automipmap_observations != 0U &&
              modern_floor.legacy_native_additional_mip_levels != 0U &&
              modern_floor.authored_mip_prefix_levels != 0U &&
              modern_floor.generated_mip_tail_levels != 0U &&
              modern_floor.normalized_output_mip_levels != 0U,
          "modern normalization excluded or hid legacy gamma/mip provenance");
  test_texture->MutateMipState(0U, false);
  test_texture->MutateUsage(Ogre::TU_STATIC);
  source.Reset();
  CaptureAndCommit(source, *native, readbacks);

  // Teardown after TryProject invalidates Apply's final batch. Candidate
  // assets and the committed cache both remain intact after Discard.
  const Ogre14SelectedTextureSourceReceiptRegistry reload_registry =
      selected_registry;
  Require(source.BeginCapture(), "begin Apply rollback capture");
  Ogre14GraphicsSceneMaterialCaptureInput rollback_input = CaptureInput();
  bool rollback_projected = false;
  RequireOk(source.TryProject(kSectionKey, native->material, true, true,
                              rollback_input, rollback_projected),
            "project before selected-source teardown");
  Require(rollback_projected, "rollback fixture did not select projection");
  std::vector<GraphicsSceneAssetInput> rollback_assets =
      BuildPlaceholderAssets(rollback_input);
  const std::vector<GraphicsSceneAssetInput> rollback_before = rollback_assets;
  RequireOk(
      TeardownOgre14SelectedTextureSourceGroup(kGroup, 1U, selected_registry),
      "teardown selected-source group before Apply");
  const ValidationResult rollback_result = source.Apply(rollback_assets);
  Require(!rollback_result &&
              rollback_result.code == ValidationCode::REVISION_MISMATCH &&
              rollback_result.field ==
                  "ogre_next_demo.material.publication.ordinary."
                  "batch_revalidation" &&
              SameAssetOwners(rollback_assets, rollback_before),
          "failed Apply partially published assets after unmount");
  RequireZeroReadback(source, *readbacks);
  source.Discard();
  selected_registry = reload_registry;
  CaptureAndCommit(source, *native, readbacks);

  // N+1 has no reachable section after unmount. The anti-tombstone owners are
  // inert: Apply publishes them without touching selected-source authority.
  RequireOk(
      TeardownOgre14SelectedTextureSourceGroup(kGroup, 1U, selected_registry),
      "final selected-source unmount");
  const std::size_t resolve_calls_before_unused =
      selected_resolver.resolve_calls;
  const std::size_t revalidate_calls_before_unused =
      selected_resolver.revalidate_calls;
  Require(source.BeginCapture(), "begin unused post-unmount frame");
  std::vector<GraphicsSceneAssetInput> unused_assets;
  RequireOk(source.Apply(unused_assets), "apply unused post-unmount frame");
  Require(unused_assets.size() == 3U &&
              selected_resolver.resolve_calls == resolve_calls_before_unused &&
              selected_resolver.revalidate_calls ==
                  revalidate_calls_before_unused,
          "unreachable cache owner probed selected-source authority");
  RequireZeroReadback(source, *readbacks);
  source.Commit();

  // Destroy every old native owner while the cache retains only integer
  // observation tokens. A same-name generation-2 remount is rejected without
  // dereferencing the dead Material/Pass/TUS/Sampler addresses.
  test_texture.reset();
  native.reset();
  texture.reset();
  Require(TestTexture::destruction_count == 1U,
          "old native Texture owner survived cache-only unmount");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                kGroup, 2U, selected_registry),
            "activate same-name remount generation");
  selected_resolver.generation = 2U;
  Ogre::TexturePtr replacement_texture =
      std::make_shared<TestTexture>(kTextureName, 72U, kGroup, readbacks);
  const Ogre14SelectedTextureSourceReceipt remount_receipt =
      BuildReceipt(*replacement_texture, 2U, 0U, 0x4000U, bytes);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(remount_receipt,
                                                     selected_registry),
            "commit same-name remount receipt");
  replacement_texture->load();
  auto replacement_native =
      std::make_unique<NativeMaterial>(replacement_texture, 102U);
  RequireFrozenProjectionFailure(
      source, *replacement_native,
      "same-name remount reused or dereferenced stale native cache owners");

  // Failed reachability did not corrupt the committed anti-tombstone catalog.
  const std::size_t resolve_calls_before_inert_retry =
      selected_resolver.resolve_calls;
  Require(source.BeginCapture(), "begin inert retry after remount rejection");
  std::vector<GraphicsSceneAssetInput> inert_retry_assets;
  RequireOk(source.Apply(inert_retry_assets),
            "apply inert retry after remount rejection");
  Require(inert_retry_assets.size() == 3U &&
              selected_resolver.resolve_calls ==
                  resolve_calls_before_inert_retry,
          "remount rejection corrupted or revalidated unreachable cache");
  source.Commit();

  source.Reset();
  CaptureAndCommit(source, *replacement_native, readbacks);
  Require(*readbacks == 0U && authority_provider.capture_calls == 0U,
          "native lifecycle used GPU readback or authenticated fallback");
}

} // namespace

int main() {
  Ogre::LogManager log_manager;
  log_manager.createLog("MaterialSourceNativeTests", true, false, true);
  Ogre::Root root("", "", "");
  TestRetryableOrdinaryAbsencePromotion();
  TestManagedSpecularProjectionAndRollback();
  TestAlphaStateAndAnisotropicProjection();
  TestNativeMaterialSourceLifecycle();
  std::cout << "OgreNext demo MaterialSource native lifecycle tests passed\n";
  return EXIT_SUCCESS;
}
