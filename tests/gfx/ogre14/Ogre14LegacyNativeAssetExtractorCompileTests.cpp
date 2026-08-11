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

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iomanip>
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

  bool RequiresAuthenticatedTextureSource(
      Ogre::Texture &) const noexcept override {
    return true;
  }

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

class NativeDeclarationDigestFaultInjector final
    : public RoR::Render::
          IOgre14LegacyNativeMaterialDeclarationDigestFaultInjector {
public:
  enum class Behavior { THROW_BAD_ALLOC, THROW_OTHER };

  explicit NativeDeclarationDigestFaultInjector(Behavior behavior)
      : behavior_(behavior) {}

  void BeforeNativeMaterialDeclarationDigestStage(
      RoR::Render::Ogre14LegacyNativeMaterialDeclarationDigestStage stage)
      override {
    if (stage != RoR::Render::
                     Ogre14LegacyNativeMaterialDeclarationDigestStage::
                         BEFORE_DIGEST_COMMIT) {
      return;
    }
    if (behavior_ == Behavior::THROW_BAD_ALLOC) {
      throw std::bad_alloc();
    }
    throw 17;
  }

private:
  Behavior behavior_;
};

class NativeDeclarationMutationFaultInjector final
    : public RoR::Render::
          IOgre14LegacyNativeMaterialDeclarationDigestFaultInjector {
public:
  explicit NativeDeclarationMutationFaultInjector(Ogre::Pass &pass)
      : pass_(pass) {}

  void BeforeNativeMaterialDeclarationDigestStage(
      RoR::Render::Ogre14LegacyNativeMaterialDeclarationDigestStage stage)
      override {
    if (!mutated_ &&
        stage == RoR::Render::
                     Ogre14LegacyNativeMaterialDeclarationDigestStage::
                         BEFORE_FRESHNESS_REVALIDATION) {
      pass_.setDiffuse(Ogre::ColourValue(0.25F, 0.5F, 0.75F, 1.0F));
      mutated_ = true;
    }
  }

private:
  Ogre::Pass &pass_;
  bool mutated_ = false;
};

struct CanonicalMaterial {
  explicit CanonicalMaterial(std::string name,
                             std::string group = "NativeTests")
      : material(nullptr, std::move(name), 1U, std::move(group)) {
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
  Require(RoR::Render::Testing::
              Ogre14LegacyNativeMaterialAuditTestAccess::SealOpaqueSentinel(
                  capture)
              .ok(),
          "native rejection sentinel could not be sealed");
  const auto sentinel_audit = capture.exact_native_material_audit;
  const auto sentinel_receipt = capture.native_material_audit_receipt;
  const std::uint32_t sentinel_declaration_version =
      capture.native_material_declaration_serialization_version;
  const auto sentinel_declaration_sha256 =
      capture.native_material_declaration_sha256;
  const RoR::Render::ValidationResult result =
      RoR::Render::CaptureOgre14LegacyNativeMaterial(fixture.material,
                                                     declaration, capture);
  Require(!result && result.field == field &&
              capture.material.key.exact_name == "sentinel" &&
              capture.exact_native_material_audit.get() ==
                  sentinel_audit.get() &&
              capture.native_material_declaration_serialization_version ==
                  sentinel_declaration_version &&
              capture.native_material_declaration_sha256 ==
                  sentinel_declaration_sha256 &&
              capture.native_material_audit_receipt
                  .SharesNativeDeclarationAuthorityWith(sentinel_receipt) &&
              capture.native_material_audit_receipt.Authenticates(capture),
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
              capture.native_material_audit_receipt.Authenticates(capture),
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
                  repeated_capture) &&
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

  CanonicalMaterial stale_inactive_light("StaleInactiveLight");
  stale_inactive_light.pass->setIteratePerLight(
      false, false, Ogre::Light::LT_DIRECTIONAL);
  stale_inactive_light.LoadForCapture();
  RequireRejectedWithoutMutation(
      stale_inactive_light, declaration,
      "material.pipeline.lighting_controls",
      "inactive stale light-type state was authenticated as canonical");

  CanonicalMaterial unknown_inactive_light("UnknownInactiveLight");
  unknown_inactive_light.pass->setIteratePerLight(
      false, false, static_cast<Ogre::Light::LightTypes>(99));
  unknown_inactive_light.LoadForCapture();
  RequireRejectedWithoutMutation(
      unknown_inactive_light, declaration,
      "material.pipeline.lighting_controls",
      "unknown inactive light-type enum was authenticated");

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

  CanonicalMaterial unresolved_caster("UnresolvedCaster");
  unresolved_caster.technique->setShadowCasterMaterial(
      Ogre::String("MissingNativeCasterMaterial"));
  unresolved_caster.LoadForCapture();
  Require(!unresolved_caster.technique->getShadowCasterMaterial() &&
              unresolved_caster.technique->getShadowCasterMaterialName() ==
                  "MissingNativeCasterMaterial",
          "unresolved caster declaration fixture did not retain its exact name");
  RequireRejectedWithoutMutation(
      unresolved_caster, declaration, "material.shadow_materials",
      "unresolved shadow-caster declaration was confused with absent state");

  CanonicalMaterial unresolved_receiver("UnresolvedReceiver");
  unresolved_receiver.technique->setShadowReceiverMaterial(
      Ogre::String("MissingNativeReceiverMaterial"));
  unresolved_receiver.LoadForCapture();
  Require(!unresolved_receiver.technique->getShadowReceiverMaterial() &&
              unresolved_receiver.technique->getShadowReceiverMaterialName() ==
                  "MissingNativeReceiverMaterial",
          "unresolved receiver declaration fixture did not retain its exact name");
  RequireRejectedWithoutMutation(
      unresolved_receiver, declaration, "material.shadow_materials",
      "unresolved shadow-receiver declaration was confused with absent state");

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

  const Ogre::TexturePtr null_sampler_texture = std::make_shared<TestTexture>(
      "NullSampler.dds", 80U, "NativeTests");
  null_sampler_texture->load();
  CanonicalMaterial null_sampler("NullSampler");
  Ogre::TextureUnitState *null_sampler_unit =
      null_sampler.AttachTexture(null_sampler_texture);
  null_sampler.LoadForCapture(false);
  null_sampler_unit->mSampler.reset();
  RequireRejectedWithoutMutation(
      null_sampler, declaration, "material.texture_unit.sampler",
      "null native sampler crashed or escaped fail-closed validation");

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

void TestNativeDeclarationDigestIsCanonicalOpaqueAndTransactional() {
  using namespace RoR::Render;
  Ogre14LegacyNativeMaterialDeclaration declaration;

  CanonicalMaterial canonical("DigestCanonical");
  canonical.LoadForCapture();
  Ogre14LegacyNativeMaterialCapture capture;
  Require(CaptureOgre14LegacyNativeMaterial(canonical.material, declaration,
                                            capture)
                  .ok() &&
              capture.native_material_declaration_serialization_version ==
                  kOgre14LegacyNativeMaterialDeclarationSerializationVersion &&
              std::any_of(
                  capture.native_material_declaration_sha256.begin(),
                  capture.native_material_declaration_sha256.end(),
                  [](std::uint8_t byte) { return byte != 0U; }) &&
              capture.native_material_audit_receipt.Authenticates(capture),
          "native declaration digest was not minted with exact authority");
  const Ogre14LegacyNativeMaterialDeclarationSha256 expected_digest{{
      0xD0U, 0x3FU, 0x59U, 0x76U, 0x23U, 0xEFU, 0xB9U, 0x11U,
      0x55U, 0xC2U, 0x17U, 0xF2U, 0xE7U, 0x19U, 0x35U, 0x70U,
      0x40U, 0x78U, 0x0AU, 0x18U, 0xB5U, 0x80U, 0x01U, 0xFAU,
      0x32U, 0x6FU, 0x4CU, 0xD3U, 0x59U, 0x07U, 0xF3U, 0xDFU,
  }};
  if (capture.native_material_declaration_sha256 != expected_digest) {
    std::cerr << "actual native declaration SHA-256: ";
    for (const std::uint8_t byte :
         capture.native_material_declaration_sha256) {
      std::cerr << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned int>(byte);
    }
    std::cerr << std::dec << '\n';
  }
  Require(capture.native_material_declaration_sha256 == expected_digest,
          "canonical declaration serialization or SHA-256 known answer drifted");

  Ogre14LegacyNativeMaterialCapture altered_digest_capture = capture;
  altered_digest_capture.native_material_declaration_sha256.front() ^= 0x80U;
  Ogre14LegacyNativeMaterialCapture altered_version_capture = capture;
  ++altered_version_capture
        .native_material_declaration_serialization_version;
  Ogre14LegacyNativeMaterialCapture identical_digest_copy = capture;
  identical_digest_copy.native_material_declaration_sha256 =
      Ogre14LegacyNativeMaterialDeclarationSha256(
          capture.native_material_declaration_sha256);
  Require(!capture.native_material_audit_receipt.Authenticates(
              altered_digest_capture) &&
              !capture.native_material_audit_receipt.Authenticates(
                  altered_version_capture) &&
              capture.native_material_audit_receipt.Authenticates(
                  identical_digest_copy),
          "declaration digest value semantics or alteration checks drifted");
  const auto reboxed_audit =
      std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
          *capture.exact_native_material_audit);
  Ogre14LegacyNativeMaterialCapture reboxed_capture = capture;
  reboxed_capture.exact_native_material_audit = reboxed_audit;
  Require(!capture.native_material_audit_receipt.Authenticates(reboxed_capture),
          "reboxed native audit value retained declaration authority");

  Ogre14LegacyNativeMaterialCaptureSha256 projection_sentinel{};
  projection_sentinel.fill(0xC3U);
  const Ogre14LegacyNativeMaterialCaptureSha256 unchanged_projection =
      projection_sentinel;
  Ogre14LegacyNativeMaterialCapture nonfinite_public_capture = capture;
  nonfinite_public_capture.material.diffuse_linear.x =
      (std::numeric_limits<float>::quiet_NaN)();
  ValidationResult projection_result =
      ComputeOgre14LegacyNativeMaterialCaptureSha256(
          nonfinite_public_capture, projection_sentinel);
  Require(!projection_result &&
              projection_result.field == "native_capture_projection.float" &&
              projection_sentinel == unchanged_projection &&
              !capture.native_material_audit_receipt.Authenticates(
                  nonfinite_public_capture),
          "non-finite public capture changed or bypassed projection authority");
  Ogre14LegacyNativeMaterialCapture oversized_public_capture = capture;
  oversized_public_capture.material.key.exact_resource_group.assign(
      512U * 1024U, 'g');
  projection_result = ComputeOgre14LegacyNativeMaterialCaptureSha256(
      oversized_public_capture, projection_sentinel);
  Require(!projection_result &&
              projection_result.field ==
                  "native_capture_projection.canonical_bytes" &&
              projection_sentinel == unchanged_projection &&
              !capture.native_material_audit_receipt.Authenticates(
                  oversized_public_capture),
          "oversized public capture changed or bypassed projection authority");

  Ogre14LegacyNativeMaterialCapture repeated;
  Require(CaptureOgre14LegacyNativeMaterial(canonical.material, declaration,
                                            repeated)
                  .ok() &&
              repeated.native_material_declaration_sha256 ==
                  capture.native_material_declaration_sha256 &&
              !capture.native_material_audit_receipt
                   .SharesNativeDeclarationAuthorityWith(
                       repeated.native_material_audit_receipt) &&
              !capture.native_material_audit_receipt.Authenticates(repeated),
          "fresh native capture reused declaration control-block authority");

  CanonicalMaterial renamed("DigestRenamed");
  renamed.LoadForCapture();
  Ogre14LegacyNativeMaterialCapture renamed_capture;
  Require(CaptureOgre14LegacyNativeMaterial(renamed.material, declaration,
                                            renamed_capture)
                  .ok() &&
              renamed_capture.native_material_declaration_sha256 !=
                  capture.native_material_declaration_sha256,
          "exact material group/name did not affect the native digest");

  CanonicalMaterial regrouped("DigestCanonical", "DigestForeignGroup");
  regrouped.LoadForCapture();
  Ogre14LegacyNativeMaterialCapture regrouped_capture;
  Require(CaptureOgre14LegacyNativeMaterial(regrouped.material, declaration,
                                            regrouped_capture)
                  .ok() &&
              regrouped_capture.native_material_declaration_sha256 !=
                  capture.native_material_declaration_sha256,
          "exact material resource group did not affect the native digest");

  CanonicalMaterial altered_pass("DigestCanonical");
  altered_pass.pass->setCullingMode(Ogre::CULL_ANTICLOCKWISE);
  altered_pass.LoadForCapture();
  Ogre14LegacyNativeMaterialCapture altered_pass_capture;
  Require(CaptureOgre14LegacyNativeMaterial(
              altered_pass.material, declaration, altered_pass_capture)
                  .ok() &&
              altered_pass_capture.native_material_declaration_sha256 !=
                  capture.native_material_declaration_sha256,
          "altered accepted pass state did not affect the native digest");

  const Ogre::TexturePtr texture = std::make_shared<TestTexture>(
      "DigestTexture.dds", 81U, "NativeTests");
  texture->load();
  CanonicalMaterial textured("DigestTextured");
  Ogre::TextureUnitState *base_unit = textured.AttachTexture(texture);
  base_unit->setName("base-color");
  textured.LoadForCapture(false);
  Ogre14LegacyNativeMaterialCapture textured_capture;
  Require(CaptureOgre14LegacyNativeMaterial(textured.material, declaration,
                                            textured_capture)
                  .ok(),
          "canonical textured declaration digest fixture failed");

  CanonicalMaterial renamed_unit("DigestTextured");
  Ogre::TextureUnitState *renamed_native_unit =
      renamed_unit.AttachTexture(texture);
  renamed_native_unit->setName("renamed-base-color");
  renamed_unit.LoadForCapture(false);
  Ogre14LegacyNativeMaterialCapture renamed_unit_capture;
  Require(CaptureOgre14LegacyNativeMaterial(
              renamed_unit.material, declaration, renamed_unit_capture)
                  .ok() &&
              renamed_unit_capture.native_material_declaration_sha256 !=
                  textured_capture.native_material_declaration_sha256,
          "exact texture-unit name/order did not affect the native digest");

  const Ogre::TexturePtr other_group_texture = std::make_shared<TestTexture>(
      "DigestTexture.dds", 82U, "NativeForeignGroup");
  other_group_texture->load();
  CanonicalMaterial altered_texture_key("DigestTextured");
  Ogre::TextureUnitState *altered_key_unit =
      altered_texture_key.AttachTexture(other_group_texture);
  altered_key_unit->setName("base-color");
  altered_texture_key.LoadForCapture(false);
  Ogre14LegacyNativeMaterialCapture altered_texture_key_capture;
  Require(CaptureOgre14LegacyNativeMaterial(
              altered_texture_key.material, declaration,
              altered_texture_key_capture)
                  .ok() &&
              altered_texture_key_capture.native_material_declaration_sha256 !=
                  textured_capture.native_material_declaration_sha256,
          "exact texture resource group did not affect the native digest");

  const Ogre::TexturePtr other_named_texture = std::make_shared<TestTexture>(
      "DigestTextureOther.dds", 83U, "NativeTests");
  other_named_texture->load();
  CanonicalMaterial altered_texture_name("DigestTextured");
  Ogre::TextureUnitState *altered_name_unit =
      altered_texture_name.AttachTexture(other_named_texture);
  altered_name_unit->setName("base-color");
  altered_texture_name.LoadForCapture(false);
  Ogre14LegacyNativeMaterialCapture altered_texture_name_capture;
  Require(CaptureOgre14LegacyNativeMaterial(
              altered_texture_name.material, declaration,
              altered_texture_name_capture)
                  .ok() &&
              altered_texture_name_capture
                      .native_material_declaration_sha256 !=
                  textured_capture.native_material_declaration_sha256,
          "exact texture resource name did not affect the native digest");

  CanonicalMaterial altered_sampler("DigestTextured");
  Ogre::TextureUnitState *sampler_unit = altered_sampler.AttachTexture(texture);
  sampler_unit->setName("base-color");
  auto changed_sampler = std::make_shared<Ogre::Sampler>();
  changed_sampler->setFiltering(Ogre::FO_POINT, Ogre::FO_LINEAR,
                                Ogre::FO_NONE);
  changed_sampler->setCompareEnabled(false);
  changed_sampler->setCompareFunction(Ogre::CMPF_ALWAYS_PASS);
  sampler_unit->setSampler(std::move(changed_sampler));
  altered_sampler.LoadForCapture(false);
  Ogre14LegacyNativeMaterialCapture altered_sampler_capture;
  Require(CaptureOgre14LegacyNativeMaterial(
              altered_sampler.material, declaration, altered_sampler_capture)
                  .ok() &&
              altered_sampler_capture.native_material_declaration_sha256 !=
                  textured_capture.native_material_declaration_sha256,
          "altered sampler state did not affect the native digest");

  CanonicalMaterial altered_combine("DigestTextured");
  Ogre::TextureUnitState *combine_unit = altered_combine.AttachTexture(texture);
  combine_unit->setName("base-color");
  combine_unit->setColourOperationEx(
      Ogre::LBX_ADD, Ogre::LBS_TEXTURE, Ogre::LBS_CURRENT,
      Ogre::ColourValue(0.25F, 0.5F, 0.75F, 1.0F),
      Ogre::ColourValue(1.0F, 0.75F, 0.5F, 0.25F), 0.375F);
  altered_combine.LoadForCapture(false);
  RequireRejectedWithoutMutation(
      altered_combine, declaration, "material.texture_unit",
      "altered unsupported combine semantics were partially hashed");

  CanonicalMaterial nonfinite_state("DigestNonFinite");
  nonfinite_state.pass->setDepthBias(
      (std::numeric_limits<float>::quiet_NaN)(), 0.0F);
  nonfinite_state.LoadForCapture();
  RequireRejectedWithoutMutation(
      nonfinite_state, declaration, "material.pipeline.depth_bias",
      "non-finite native structure was silently omitted from the digest");

  CanonicalMaterial oversized_name("DigestOversizedUnit");
  Ogre::TextureUnitState *oversized_unit =
      oversized_name.AttachTexture(texture);
  oversized_unit->setName(std::string(
      kOgre14LegacyNativeMaterialDeclarationMaximumCanonicalBytes, 'u'));
  oversized_name.LoadForCapture(false);
  RequireRejectedWithoutMutation(
      oversized_name, declaration,
      "native_material_declaration.canonical_bytes",
      "oversized native declaration bypassed the canonical byte cap");

  CanonicalMaterial multiple_units("DigestMultipleUnits");
  multiple_units.AttachTexture(texture)->setName("unit-zero");
  multiple_units.AttachTexture(texture)->setName("unit-one");
  multiple_units.LoadForCapture(false);
  RequireRejectedWithoutMutation(
      multiple_units, declaration, "material.texture_units",
      "unsupported texture-unit ordering was partially hashed");

  CanonicalMaterial multiple_passes("DigestMultiplePasses");
  multiple_passes.technique->createPass();
  multiple_passes.LoadForCapture();
  RequireRejectedWithoutMutation(
      multiple_passes, declaration, "material.pass_structure",
      "unsupported pass ordering was partially hashed");

  CanonicalMaterial multiple_techniques("DigestMultipleTechniques");
  multiple_techniques.material.createTechnique()->createPass();
  multiple_techniques.LoadForCapture();
  RequireRejectedWithoutMutation(
      multiple_techniques, declaration, "material.pass_structure",
      "unsupported technique ordering was partially hashed");

  CanonicalMaterial environment("DigestEnvironment");
  Ogre::TextureUnitState *environment_unit = environment.AttachTexture(texture);
  environment_unit->setName("environment");
  environment_unit->setEnvironmentMap(true, Ogre::TextureUnitState::ENV_PLANAR);
  environment.LoadForCapture(false);
  RequireRejectedWithoutMutation(
      environment, declaration, "material.texture_unit",
      "unsupported environment augmentation was partially hashed");

  Ogre14LegacyNativeMaterialCapture sentinel;
  sentinel.material.key.exact_name = "sentinel";
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealOpaqueSentinel(sentinel)
              .ok(),
          "native digest exception sentinel could not be sealed");
  const auto sentinel_owner = sentinel.exact_native_material_audit;
  const auto sentinel_receipt = sentinel.native_material_audit_receipt;
  const std::uint32_t sentinel_version =
      sentinel.native_material_declaration_serialization_version;
  const auto sentinel_digest = sentinel.native_material_declaration_sha256;
  const auto SentinelUnchanged = [&]() {
    return sentinel.material.key.exact_name == "sentinel" &&
           sentinel.exact_native_material_audit.get() == sentinel_owner.get() &&
           sentinel.native_material_declaration_serialization_version ==
               sentinel_version &&
           sentinel.native_material_declaration_sha256 == sentinel_digest &&
           sentinel.native_material_audit_receipt
               .SharesNativeDeclarationAuthorityWith(sentinel_receipt) &&
           sentinel.native_material_audit_receipt.Authenticates(sentinel);
  };
  NativeDeclarationDigestFaultInjector bad_alloc_injector(
      NativeDeclarationDigestFaultInjector::Behavior::THROW_BAD_ALLOC);
  Testing::
      SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting(
          &bad_alloc_injector);
  ValidationResult result = CaptureOgre14LegacyNativeMaterial(
      canonical.material, declaration, sentinel);
  Testing::
      SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting(
          nullptr);
  Require(!result && result.field == "native.allocation" &&
              SentinelUnchanged(),
          "digest bad_alloc changed deep native capture ownership");
  NativeDeclarationDigestFaultInjector unexpected_injector(
      NativeDeclarationDigestFaultInjector::Behavior::THROW_OTHER);
  Testing::
      SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting(
          &unexpected_injector);
  result = CaptureOgre14LegacyNativeMaterial(canonical.material, declaration,
                                             sentinel);
  Testing::
      SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting(
          nullptr);
  Require(!result && result.field == "native.exception" && SentinelUnchanged(),
          "unexpected digest exception changed deep native capture ownership");

  CanonicalMaterial mutating("DigestMutating");
  mutating.LoadForCapture();
  NativeDeclarationMutationFaultInjector mutation_injector(*mutating.pass);
  Testing::
      SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting(
          &mutation_injector);
  result = CaptureOgre14LegacyNativeMaterial(mutating.material, declaration,
                                             sentinel);
  Testing::
      SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting(
          nullptr);
  Require(!result &&
              result.field == "native_material_declaration.freshness" &&
              SentinelUnchanged(),
          "mid-capture native mutation published a stale or hybrid digest");
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

  Ogre14LegacyNativeMaterialCapture sentinel;
  sentinel.material.key.exact_name = "sentinel";
  sentinel.authenticated_texture_resolutions.push_back(resolver.resolution);
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealOpaqueSentinel(sentinel)
              .ok(),
          "authenticated native sentinel could not be sealed");
  const auto sentinel_audit = sentinel.exact_native_material_audit;
  const auto sentinel_receipt = sentinel.native_material_audit_receipt;
  const std::uint32_t sentinel_declaration_version =
      sentinel.native_material_declaration_serialization_version;
  const auto sentinel_declaration_sha256 =
      sentinel.native_material_declaration_sha256;
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
               .MatchesResolver(resolver) &&
           sentinel.native_material_declaration_serialization_version ==
               sentinel_declaration_version &&
           sentinel.native_material_declaration_sha256 ==
               sentinel_declaration_sha256 &&
           sentinel.native_material_audit_receipt
               .SharesNativeDeclarationAuthorityWith(sentinel_receipt) &&
           sentinel.native_material_audit_receipt.Authenticates(sentinel);
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
  TestNativeDeclarationDigestIsCanonicalOpaqueAndTransactional();
  TestAuthenticatedTextureCaptureAuthorityAndRollback();
  std::cout << "OGRE 14 native legacy asset extractor tests passed\n";
  return EXIT_SUCCESS;
}
