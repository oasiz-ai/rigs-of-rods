/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h"

// The focused test intentionally has no platform RenderSystem. Expose only
// Technique's compile result so a synthetic readable Texture can exercise the
// native capture edge without loading Metal, D3D11, or GL plugins.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include <OgreTechnique.h>
#undef private
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <OgreBuildSettings.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreLogManager.h>
#include <OgreMaterial.h>
#include <OgrePixelFormat.h>
#include <OgrePass.h>
#include <OgreRoot.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace RoR::Render::Testing {

class Ogre14AuthenticatedTextureResolutionTestAccess final {
public:
  static ValidationResult Mint(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      Ogre::Texture &texture,
      const IOgre14AuthenticatedTextureResolver &resolver,
      Ogre14AuthenticatedTextureResolution &resolution) {
    return registry.MintLoadedResourceResolution(
        group, generation, reinterpret_cast<std::uintptr_t>(&texture),
        static_cast<std::uint64_t>(texture.getHandle()), texture.getName(),
        static_cast<std::uint64_t>(texture.getStateCount()),
        reinterpret_cast<std::uintptr_t>(&resolver), resolution);
  }

  static bool Revalidate(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      const Ogre14AuthenticatedTextureResolution &resolution,
      const IOgre14AuthenticatedTextureResolver &resolver,
      Ogre::Texture &texture) noexcept {
    return registry.RevalidateLoadedResourceResolution(
        resolution, reinterpret_cast<std::uintptr_t>(&resolver),
        reinterpret_cast<std::uintptr_t>(&texture),
        static_cast<std::uint64_t>(texture.getHandle()), texture.getGroup(),
        texture.getName(),
        static_cast<std::uint64_t>(texture.getStateCount()));
  }
};

} // namespace RoR::Render::Testing

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

class TestPixelBuffer final : public Ogre::HardwarePixelBuffer {
public:
  explicit TestPixelBuffer(std::array<std::uint8_t, 4U> bytes)
      : Ogre::HardwarePixelBuffer(1U, 1U, 1U, Ogre::PF_BYTE_RGBA,
                                  Ogre::HBU_CPU_ONLY, false),
        bytes_(bytes) {}

  void blitFromMemory(const Ogre::PixelBox &source,
                      const Ogre::Box &destination) override {
    Require(destination.left == 0U && destination.top == 0U &&
                destination.front == 0U && destination.getWidth() == 1U &&
                destination.getHeight() == 1U &&
                destination.getDepth() == 1U,
            "test pixel destination extent changed");
    Ogre::PixelBox target(1U, 1U, 1U, Ogre::PF_BYTE_RGBA, bytes_.data());
    Ogre::PixelUtil::bulkPixelConversion(source, target);
  }

  void blitToMemory(const Ogre::Box &source,
                    const Ogre::PixelBox &destination) override {
    Require(source.left == 0U && source.top == 0U && source.front == 0U &&
                source.getWidth() == 1U && source.getHeight() == 1U &&
                source.getDepth() == 1U,
            "test pixel source extent changed");
    Ogre::PixelBox pixels(1U, 1U, 1U, Ogre::PF_BYTE_RGBA, bytes_.data());
    Ogre::PixelUtil::bulkPixelConversion(pixels, destination);
  }

protected:
  Ogre::PixelBox lockImpl(const Ogre::Box &, LockOptions) override {
    return Ogre::PixelBox(1U, 1U, 1U, Ogre::PF_BYTE_RGBA, bytes_.data());
  }

  void unlockImpl() override {}

private:
  std::array<std::uint8_t, 4U> bytes_;
};

class TestTexture final : public Ogre::Texture {
public:
  TestTexture(std::string name, Ogre::ResourceHandle handle,
              std::string group,
              std::array<std::uint8_t, 4U> bytes = {1U, 2U, 3U, 255U})
      : Ogre::Texture(nullptr, std::move(name), handle, std::move(group),
                      false, nullptr) {
    mWidth = 1U;
    mHeight = 1U;
    mDepth = 1U;
    mSrcWidth = 1U;
    mSrcHeight = 1U;
    mSrcDepth = 1U;
    mTextureType = Ogre::TEX_TYPE_2D;
    mNumRequestedMipmaps = 0U;
    mNumMipmaps = 0U;
    mFormat = Ogre::PF_BYTE_RGBA;
    mSrcFormat = Ogre::PF_BYTE_RGBA;
    mUsage = Ogre::HBU_GPU_TO_CPU;
    mHwGamma = true;
    mSurfaceList.push_back(
        std::make_shared<TestPixelBuffer>(std::move(bytes)));
  }

protected:
  void prepareImpl() override {}
  void loadImpl() override {}
  void createInternalResourcesImpl() override {}
  void freeInternalResourcesImpl() override {}
};

class RegistryResolver final
    : public RoR::Render::IOgre14AuthenticatedTextureResolver {
public:
  enum class ResolveBehavior { RETURN_RESOLUTION, THROW_BAD_ALLOC, THROW_OTHER };

  const RoR::Render::Ogre14AuthenticatedTextureReceiptRegistry *registry =
      nullptr;
  RoR::Render::Ogre14AuthenticatedTextureResolution resolution;
  ResolveBehavior behavior = ResolveBehavior::RETURN_RESOLUTION;
  mutable std::size_t resolve_calls = 0U;
  mutable std::size_t revalidate_calls = 0U;

  RoR::Render::ValidationResult ResolveAuthenticatedTexture(
      Ogre::Texture &,
      RoR::Render::Ogre14AuthenticatedTextureResolution &output) const
      override {
    ++resolve_calls;
    if (behavior == ResolveBehavior::THROW_BAD_ALLOC) {
      throw std::bad_alloc();
    }
    if (behavior == ResolveBehavior::THROW_OTHER) {
      throw 17;
    }
    output = resolution;
    return RoR::Render::ValidationResult::Success();
  }

  bool RevalidateAuthenticatedTexture(
      Ogre::Texture &texture,
      const RoR::Render::Ogre14AuthenticatedTextureResolution &candidate)
      const noexcept override {
    ++revalidate_calls;
    return registry != nullptr &&
           RoR::Render::Testing::
               Ogre14AuthenticatedTextureResolutionTestAccess::Revalidate(
                   *registry, candidate, *this, texture);
  }
};

struct CanonicalMaterial {
  explicit CanonicalMaterial(std::string name)
      : material(nullptr, std::move(name), 1U, "NativeTests") {
    technique = material.createTechnique();
    pass = technique->createPass();
    pass->setLightingEnabled(false);
    pass->setAmbient(Ogre::ColourValue::Black);
  }

  void LoadForCapture(bool auto_manage_texture_units = true) {
    material.compile(auto_manage_texture_units);
    technique->mIsSupported = true;
    material.load();
  }

  Ogre::TextureUnitState *AttachTexture(const Ogre::TexturePtr &texture) {
    Ogre::TextureUnitState *unit = pass->createTextureUnitState();
    auto sampler = std::make_shared<Ogre::Sampler>();
    sampler->setCompareEnabled(false);
    sampler->setCompareFunction(Ogre::CMPF_ALWAYS_PASS);
    unit->setSampler(std::move(sampler));
    unit->setTexture(texture);
    return unit;
  }

  Ogre::Material material;
  Ogre::Technique *technique = nullptr;
  Ogre::Pass *pass = nullptr;
};

RoR::Render::Ogre14AuthenticatedTextureReceipt BuildTextureReceipt(
    Ogre::Texture &texture, std::uint64_t pre_load_state_count,
    std::uint64_t generation = 1U) {
  using namespace RoR::Render;
  Ogre14AuthenticatedTextureCaptureInput input;
  input.effective_resource_group = texture.getGroup();
  input.group_generation = generation;
  input.archive_identity = "/content/NativeTests.zip";
  input.archive_name = "ror-authenticated-native-tests";
  input.archive_type = "EmbeddedZip";
  input.archive_sha256 = std::string(64U, 'a');
  input.archive_pointer_token = 0x900U;
  input.exact_member_name = texture.getName();
  input.binding.resource_pointer_token =
      reinterpret_cast<std::uintptr_t>(&texture);
  input.binding.resource_handle =
      static_cast<std::uint64_t>(texture.getHandle());
  input.binding.resource_state_count = pre_load_state_count;
  input.binding.exact_resource_name = texture.getName();
  const std::array<std::uint8_t, 4U> source_bytes = {1U, 2U, 3U, 4U};
  Ogre14AuthenticatedTextureReceipt receipt;
  Require(BuildOgre14AuthenticatedTextureReceipt(
              Ogre14AuthenticatedTextureRegistryConfiguration{}, input,
              source_bytes.data(), source_bytes.size(), receipt)
              .ok(),
          "native texture source receipt did not build");
  return receipt;
}

RoR::Render::Ogre14AuthenticatedTextureReceiptRegistry
BuildTextureRegistry(
    const RoR::Render::Ogre14AuthenticatedTextureReceipt &receipt,
    std::uint64_t generation = 1U) {
  using namespace RoR::Render;
  Ogre14AuthenticatedTextureReceiptRegistry registry;
  Require(InitializeOgre14AuthenticatedTextureReceiptRegistry(
              Ogre14AuthenticatedTextureRegistryConfiguration{}, registry)
                  .ok() &&
              AdvanceOgre14AuthenticatedTextureGroupGeneration(
                  receipt.metadata()->source.effective_resource_group,
                  generation, registry)
                  .ok() &&
              CommitOgre14AuthenticatedTextureReceipt(receipt, registry)
                  .ok(),
          "native texture registry did not initialize");
  return registry;
}

void RequireRejectedWithoutMutation(
    const CanonicalMaterial &fixture,
    const RoR::Render::Ogre14LegacyNativeMaterialDeclaration &declaration,
    const char *field, const char *message) {
  RoR::Render::Ogre14LegacyNativeMaterialCapture capture;
  capture.material.key.exact_name = "sentinel";
  const auto sentinel_audit =
      std::make_shared<const RoR::Render::Ogre14LegacyMaterialPipelineAudit>();
  capture.exact_native_material_audit = sentinel_audit;
  const RoR::Render::ValidationResult result =
      RoR::Render::CaptureOgre14LegacyNativeMaterial(fixture.material,
                                                     declaration, capture);
  Require(!result && result.field == field &&
              capture.material.key.exact_name == "sentinel" &&
              capture.exact_native_material_audit.get() == sentinel_audit.get(),
          message);
}

void TestNativeStateValidationIsSemanticAndTransactional() {
  using namespace RoR::Render;
  Ogre14LegacyNativeMaterialDeclaration declaration;

  CanonicalMaterial canonical("Canonical");
  canonical.LoadForCapture();
  Ogre14LegacyNativeMaterialCapture capture;
  Require(CaptureOgre14LegacyNativeMaterial(canonical.material, declaration,
                                            capture)
                  .ok() &&
              capture.material.key.exact_name == "Canonical" &&
              capture.material.source_revision != 0U &&
              capture.exact_native_material_audit != nullptr &&
              capture.native_material_audit_receipt.Authenticates(
                  capture.exact_native_material_audit),
          "canonical loaded native material was not captured exactly");
  Ogre14LegacyMaterialPipelineAudit derived_audit;
  Require(
      DeriveOgre14LegacyMaterialPipelineAudit(capture.material, derived_audit)
              .ok() &&
          EquivalentOgre14LegacyMaterialPipelineAudit(
              derived_audit, *capture.exact_native_material_audit),
      "native extractor did not mint the exact independently derived audit");
  Ogre14LegacyNativeMaterialCapture repeated_capture;
  Require(CaptureOgre14LegacyNativeMaterial(canonical.material, declaration,
                                            repeated_capture)
                  .ok() &&
              repeated_capture.native_material_audit_receipt.Authenticates(
                  repeated_capture.exact_native_material_audit) &&
              EquivalentOgre14LegacyMaterialPipelineAudit(
                  *capture.exact_native_material_audit,
                  *repeated_capture.exact_native_material_audit) &&
              (capture.exact_native_material_audit.owner_before(
                   repeated_capture.exact_native_material_audit) ||
               repeated_capture.exact_native_material_audit.owner_before(
                   capture.exact_native_material_audit)),
          "separate native captures reused one audit control block");

  CanonicalMaterial fog("Fog");
  fog.pass->setFog(true, Ogre::FOG_LINEAR);
  fog.LoadForCapture();
  RequireRejectedWithoutMutation(fog, declaration,
                                 "material.pipeline.scene_overrides",
                                 "native fog override was silently dropped");

  CanonicalMaterial sorting("Sorting");
  sorting.pass->setTransparentSortingEnabled(false);
  sorting.LoadForCapture();
  RequireRejectedWithoutMutation(
      sorting, declaration, "material.pipeline.transparent_sorting",
      "native transparent sorting policy was silently dropped");

  CanonicalMaterial lighting("Lighting");
  lighting.pass->setIteratePerLight(true);
  lighting.LoadForCapture();
  RequireRejectedWithoutMutation(
      lighting, declaration, "material.pipeline.lighting_controls",
      "native per-light iteration was silently dropped");

  CanonicalMaterial vertex_color("VertexColor");
  vertex_color.pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
  vertex_color.LoadForCapture();
  RequireRejectedWithoutMutation(
      vertex_color, declaration, "material.pipeline.vertex_colour_tracking",
      "native vertex-colour tracking was silently dropped");

  CanonicalMaterial shadows("Shadows");
  shadows.material.setReceiveShadows(false);
  shadows.LoadForCapture();
  RequireRejectedWithoutMutation(shadows, declaration, "material.shadow_policy",
                                 "native material shadow policy was dropped");

  CanonicalMaterial hardware_rules("HardwareRules");
  hardware_rules.technique->addGPUVendorRule(Ogre::Technique::GPUVendorRule(
      Ogre::GPU_NVIDIA, Ogre::Technique::INCLUDE));
  hardware_rules.LoadForCapture();
  RequireRejectedWithoutMutation(
      hardware_rules, declaration, "material.technique_hardware_rules",
      "native hardware-specific technique rule was silently dropped");

  CanonicalMaterial texcoord("Texcoord");
  Ogre::TextureUnitState *texcoord_unit =
      texcoord.pass->createTextureUnitState();
  texcoord_unit->setTextureCoordSet(2U);
  texcoord.LoadForCapture(false);
  RequireRejectedWithoutMutation(
      texcoord, declaration, "material.texture_unit.texture_coordinate_set",
      "native texture-coordinate range was narrowed before validation");

  CanonicalMaterial unordered_access("UnorderedAccess");
  Ogre::TextureUnitState *unordered_access_unit =
      unordered_access.pass->createTextureUnitState();
  unordered_access_unit->setUnorderedAccessMipLevel(0);
  unordered_access.LoadForCapture(false);
  RequireRejectedWithoutMutation(
      unordered_access, declaration, "material.texture_unit.unordered_access",
      "native unordered-access texture state was silently dropped");

  if constexpr ((std::numeric_limits<std::size_t>::max)() >
                (std::numeric_limits<std::uint32_t>::max)()) {
    CanonicalMaterial iteration_range("IterationRange");
    iteration_range.pass->setPassIterationCount(
        static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()) +
        1U);
    iteration_range.LoadForCapture();
    RequireRejectedWithoutMutation(
        iteration_range, declaration, "material.pipeline.pass_iteration_count",
        "native pass iteration count saturated instead of failing closed");
  }

  Ogre14LegacyNativeMaterialDeclaration invalid_configuration = declaration;
  invalid_configuration.translator_configuration
      .maximum_decoded_bytes_per_asset = 0U;
  RequireRejectedWithoutMutation(
      canonical, invalid_configuration, "configuration.limits",
      "invalid native capture budget reached or mutated material output");
}

void TestAuthenticatedTextureCaptureAuthorityAndRollback() {
  using namespace RoR::Render;
  Ogre14LegacyNativeMaterialDeclaration declaration;
  const Ogre::TexturePtr texture = std::make_shared<TestTexture>(
      "Authenticated.dds", 71U, "NativeTests");
  const Ogre14AuthenticatedTextureReceipt committed_receipt =
      BuildTextureReceipt(*texture, 0U);
  texture->load();
  Require(texture->isLoaded() && texture->getStateCount() == 1U,
          "test texture did not model one successful OGRE load");
  Ogre14AuthenticatedTextureReceiptRegistry registry =
      BuildTextureRegistry(committed_receipt);
  RegistryResolver resolver;
  resolver.registry = &registry;
  Require(Testing::Ogre14AuthenticatedTextureResolutionTestAccess::Mint(
              registry, "NativeTests", 1U, *texture, resolver,
              resolver.resolution)
                  .ok(),
          "registry did not mint the loaded native texture resolution");

  CanonicalMaterial textured("AuthenticatedMaterial");
  textured.AttachTexture(texture);
  textured.LoadForCapture(false);
  Ogre14LegacyNativeMaterialCapture capture;
  ValidationResult first_capture = CaptureOgre14LegacyNativeMaterial(
      textured.material, declaration, resolver, capture);
  Require(first_capture.ok() &&
              capture.version == kOgre14LegacyNativeAssetExtractorVersion &&
              capture.textures.size() == 1U &&
              capture.authenticated_texture_resolutions.size() == 1U &&
              capture.authenticated_texture_resolutions.front()
                      .source_receipt() != nullptr &&
              capture.authenticated_texture_resolutions.front()
                  .source_receipt()
                  ->SharesImmutableStateWith(committed_receipt) &&
              capture.authenticated_texture_resolutions.front()
                  .MatchesResolver(resolver) &&
              resolver.resolve_calls == 1U &&
              resolver.revalidate_calls >= 1U,
          "authenticated native capture lost exact source authority");

  Ogre14LegacyNativeMaterialCapture copied_success;
  RegistryResolver copied_resolver;
  copied_resolver.registry = &registry;
  copied_resolver.resolution = resolver.resolution;
  // The proof is deliberately bound to `resolver`, so a different resolver
  // returning the copied proof must not inherit its authority.
  ValidationResult result = CaptureOgre14LegacyNativeMaterial(
      textured.material, declaration, copied_resolver, copied_success);
  Require(!result && result.field == "texture_resolution.resolve_identity" &&
              copied_success.material.key.exact_name.empty(),
          "resolver substitution published a copied authentic proof");

  Ogre14LegacyNativeMaterialCapture legacy_capture;
  Require(CaptureOgre14LegacyNativeMaterial(
              textured.material, declaration, legacy_capture)
                  .ok() &&
              legacy_capture.textures.size() == 1U &&
              legacy_capture.authenticated_texture_resolutions.empty(),
          "legacy compatibility capture was confused with authenticated source capture");

  CanonicalMaterial untextured("AuthenticatedUntextured");
  untextured.LoadForCapture();
  const std::size_t resolve_calls_before_untextured = resolver.resolve_calls;
  const std::size_t revalidate_calls_before_untextured =
      resolver.revalidate_calls;
  Ogre14LegacyNativeMaterialCapture untextured_capture;
  Require(CaptureOgre14LegacyNativeMaterial(
              untextured.material, declaration, resolver,
              untextured_capture)
                  .ok() &&
              untextured_capture.textures.empty() &&
              untextured_capture.authenticated_texture_resolutions.empty() &&
              resolver.resolve_calls == resolve_calls_before_untextured &&
              resolver.revalidate_calls ==
                  revalidate_calls_before_untextured,
          "untextured authenticated capture invoked or invented texture authority");

  const auto sentinel_audit =
      std::make_shared<const Ogre14LegacyMaterialPipelineAudit>();
  Ogre14LegacyNativeMaterialCapture sentinel;
  sentinel.material.key.exact_name = "sentinel";
  sentinel.exact_native_material_audit = sentinel_audit;
  sentinel.authenticated_texture_resolutions.push_back(resolver.resolution);
  const auto SentinelOutputOwnersUnchanged = [&]() {
    return sentinel.material.key.exact_name == "sentinel" &&
           sentinel.exact_native_material_audit.get() ==
               sentinel_audit.get() &&
           sentinel.authenticated_texture_resolutions.size() == 1U &&
           sentinel.authenticated_texture_resolutions.front()
                   .source_receipt() != nullptr &&
           sentinel.authenticated_texture_resolutions.front()
               .source_receipt()
               ->SharesImmutableStateWith(committed_receipt) &&
           sentinel.authenticated_texture_resolutions.front()
               .MatchesResolver(resolver);
  };
  resolver.behavior = RegistryResolver::ResolveBehavior::THROW_BAD_ALLOC;
  result = CaptureOgre14LegacyNativeMaterial(textured.material, declaration,
                                             resolver, sentinel);
  Require(!result && result.field == "native.allocation" &&
              SentinelOutputOwnersUnchanged(),
          "resolver bad_alloc changed native capture output ownership");
  resolver.behavior = RegistryResolver::ResolveBehavior::THROW_OTHER;
  result = CaptureOgre14LegacyNativeMaterial(textured.material, declaration,
                                             resolver, sentinel);
  Require(!result && result.field == "native.exception" &&
              SentinelOutputOwnersUnchanged(),
          "unexpected resolver exception changed native capture output ownership");
  resolver.behavior = RegistryResolver::ResolveBehavior::RETURN_RESOLUTION;

  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration(
              "Other", 2U, registry)
                  .ok(),
          "unrelated registry publication fixture failed");
  result = CaptureOgre14LegacyNativeMaterial(textured.material, declaration,
                                             resolver, sentinel);
  Require(!result && result.field ==
                         "texture_resolution.final_revalidation" &&
              SentinelOutputOwnersUnchanged(),
          "stale whole-registry snapshot published after unrelated mutation");
  Require(Testing::Ogre14AuthenticatedTextureResolutionTestAccess::Mint(
              registry, "NativeTests", 1U, *texture, resolver,
              resolver.resolution)
                  .ok(),
          "fresh resolve after unrelated publication failed");
  Ogre14LegacyNativeMaterialCapture refreshed;
  Require(CaptureOgre14LegacyNativeMaterial(
              textured.material, declaration, resolver, refreshed)
                  .ok(),
          "fresh authenticated capture after strict invalidation failed");

  texture->reload();
  Require(texture->isLoaded() && texture->getStateCount() == 2U,
          "test texture reload did not advance state exactly once");
  result = CaptureOgre14LegacyNativeMaterial(textured.material, declaration,
                                             resolver, sentinel);
  Require(!result && result.field == "texture_resolution.resolve_identity" &&
              SentinelOutputOwnersUnchanged(),
          "stale reload state published with an old source resolution");
  const Ogre14AuthenticatedTextureReceipt reload_receipt =
      BuildTextureReceipt(*texture, 1U);
  Require(CommitOgre14AuthenticatedTextureReceipt(reload_receipt, registry)
                  .ok() &&
              Testing::Ogre14AuthenticatedTextureResolutionTestAccess::Mint(
                  registry, "NativeTests", 1U, *texture, resolver,
                  resolver.resolution)
                  .ok(),
          "reload receipt and fresh loaded resolution did not commit");
  Ogre14LegacyNativeMaterialCapture reload_capture;
  Require(CaptureOgre14LegacyNativeMaterial(
              textured.material, declaration, resolver, reload_capture)
                  .ok() &&
              reload_capture.authenticated_texture_resolutions.front()
                  .source_receipt()
                  ->SharesImmutableStateWith(reload_receipt),
          "exact reloaded source owner was not preserved");

  Require(TeardownOgre14AuthenticatedTextureGroup(
              "NativeTests", 1U, registry)
                  .ok(),
          "native texture teardown fixture failed");
  result = CaptureOgre14LegacyNativeMaterial(textured.material, declaration,
                                             resolver, sentinel);
  Require(!result && result.field ==
                         "texture_resolution.final_revalidation" &&
              SentinelOutputOwnersUnchanged(),
          "group teardown did not invalidate final native publication");
}

} // namespace

int main() {
  static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                    OGRE_VERSION_PATCH == 2,
                "native extractor test requires pinned OGRE 14.5.2");
  using CaptureFunction = RoR::Render::ValidationResult (*)(
      const Ogre::Material &,
      const RoR::Render::Ogre14LegacyNativeMaterialDeclaration &,
      RoR::Render::Ogre14LegacyNativeMaterialCapture &);
  static_assert(
      std::is_same<
          decltype(static_cast<CaptureFunction>(
              &RoR::Render::CaptureOgre14LegacyNativeMaterial)),
          CaptureFunction>::value,
      "native capture ABI changed without a version migration");
  using AuthenticatedCaptureFunction = RoR::Render::ValidationResult (*)(
      const Ogre::Material &,
      const RoR::Render::Ogre14LegacyNativeMaterialDeclaration &,
      const RoR::Render::IOgre14AuthenticatedTextureResolver &,
      RoR::Render::Ogre14LegacyNativeMaterialCapture &);
  static_assert(
      std::is_same<
          decltype(static_cast<AuthenticatedCaptureFunction>(
              &RoR::Render::CaptureOgre14LegacyNativeMaterial)),
          AuthenticatedCaptureFunction>::value,
      "authenticated native capture ABI changed without a version migration");
  Ogre::LogManager log_manager;
  log_manager.createLog("NativeExtractorTests", true, false, true);
  Ogre::Root root("", "", "");
  TestNativeStateValidationIsSemanticAndTransactional();
  TestAuthenticatedTextureCaptureAuthorityAndRollback();
  std::cout << "OGRE 14 native legacy asset extractor tests passed\n";
  return EXIT_SUCCESS;
}
