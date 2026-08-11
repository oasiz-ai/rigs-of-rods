/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14ManagedMaterialSourceAdapter.h"

#include <OgreMaterial.h>
#include <OgreRoot.h>
#include <OgreTexture.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace RoR::Render::Testing {

class Ogre14SelectedTextureSourceResolutionTestAccess final {
public:
  static ValidationResult Mint(
      const Ogre14SelectedTextureSourceReceiptRegistry &registry,
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

  static bool Revalidate(
      const Ogre14SelectedTextureSourceReceiptRegistry &registry,
      Ogre::Texture &texture,
      const Ogre14SelectedTextureSourceResolution &resolution,
      const IOgre14SelectedTextureSourceResolver &resolver) noexcept {
    return registry.RevalidateLoadedResourceResolution(
        resolution, reinterpret_cast<std::uintptr_t>(&resolver),
        reinterpret_cast<std::uintptr_t>(&texture),
        static_cast<std::uint64_t>(texture.getHandle()), texture.getGroup(),
        texture.getName(),
        static_cast<std::uint64_t>(texture.getStateCount()));
  }
};

class Ogre14AuthenticatedTextureResolutionTestAccess final {
public:
  static ValidationResult Mint(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      Ogre::Texture &texture, std::uint64_t generation,
      const IOgre14AuthenticatedTextureResolver &resolver,
      Ogre14AuthenticatedTextureResolution &resolution) {
    return registry.MintLoadedResourceResolution(
        texture.getGroup(), generation,
        reinterpret_cast<std::uintptr_t>(&texture),
        static_cast<std::uint64_t>(texture.getHandle()), texture.getName(),
        static_cast<std::uint64_t>(texture.getStateCount()),
        reinterpret_cast<std::uintptr_t>(&resolver), resolution);
  }

  static bool Revalidate(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      Ogre::Texture &texture,
      const Ogre14AuthenticatedTextureResolution &resolution,
      const IOgre14AuthenticatedTextureResolver &resolver) noexcept {
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

using namespace RoR::Render;

constexpr char kGroup[] = "ManagedMaterialAuthorityFixture";

[[noreturn]] void Fail(const std::string &message) {
  std::cerr << "Ogre14ManagedMaterialSourceAdapterTests: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void Require(bool condition, const std::string &message) {
  if (!condition) {
    Fail(message);
  }
}

void RequireOk(const ValidationResult &result, const std::string &message) {
  if (!result) {
    Fail(message + ": " + result.field + ": " + result.detail);
  }
}

class TestTexture final : public Ogre::Texture {
public:
  TestTexture(std::string name, Ogre::ResourceHandle handle)
      : Ogre::Texture(nullptr, std::move(name), handle, kGroup, false,
                      nullptr) {
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
    mUsage = Ogre::TU_STATIC;
  }

protected:
  void prepareImpl() override {}
  void loadImpl() override {}
  void createInternalResourcesImpl() override {}
  void freeInternalResourcesImpl() override {}
};

class Resolver final : public IOgre14AuthenticatedTextureResolver,
                       public IOgre14SelectedTextureSourceResolver {
public:
  const Ogre14AuthenticatedTextureReceiptRegistry *authenticated_registry =
      nullptr;
  const Ogre14SelectedTextureSourceReceiptRegistry *selected_registry =
      nullptr;
  std::uint64_t generation = 1U;
  bool authenticated = false;

  bool RequiresAuthenticatedTextureSource(
      Ogre::Texture &) const noexcept override {
    return authenticated;
  }

  ValidationResult ResolveAuthenticatedTexture(
      Ogre::Texture &texture,
      Ogre14AuthenticatedTextureResolution &resolution) const override {
    if (authenticated_registry == nullptr) {
      return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                       "fixture.auth_registry", "missing");
    }
    return RoR::Render::Testing::
        Ogre14AuthenticatedTextureResolutionTestAccess::Mint(
            *authenticated_registry, texture, generation, *this, resolution);
  }

  bool RevalidateAuthenticatedTexture(
      Ogre::Texture &texture,
      const Ogre14AuthenticatedTextureResolution &resolution) const
      noexcept override {
    return authenticated && authenticated_registry != nullptr &&
           RoR::Render::Testing::
               Ogre14AuthenticatedTextureResolutionTestAccess::Revalidate(
                   *authenticated_registry, texture, resolution, *this);
  }

  ValidationResult ResolveSelectedTextureSource(
      Ogre::Texture &texture,
      Ogre14SelectedTextureSourceResolution &resolution) const override {
    if (selected_registry == nullptr) {
      return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                       "fixture.selected_registry", "missing");
    }
    return RoR::Render::Testing::
        Ogre14SelectedTextureSourceResolutionTestAccess::Mint(
            *selected_registry, texture, generation, *this, resolution);
  }

  bool RevalidateSelectedTextureSource(
      Ogre::Texture &texture,
      const Ogre14SelectedTextureSourceResolution &resolution) const
      noexcept override {
    return !authenticated && selected_registry != nullptr &&
           RoR::Render::Testing::
               Ogre14SelectedTextureSourceResolutionTestAccess::Revalidate(
                   *selected_registry, texture, resolution, *this);
  }
};

Ogre14SelectedTextureSourceReceipt BuildSelectedReceipt(
    Ogre::Texture &texture, std::uint64_t state_before_load,
    const std::vector<std::uint8_t> &bytes) {
  Ogre14SelectedTextureSourceCaptureInput input;
  input.effective_resource_group = texture.getGroup();
  input.group_generation = 1U;
  input.selected_archive_name = "fixture.zip";
  input.selected_archive_type = "Zip";
  input.selected_archive_pointer_token = 0x2100U;
  input.file_info_archive_pointer_token = 0x2100U;
  input.file_info_filename = texture.getName();
  input.file_info_basename = texture.getName();
  input.exact_member_name = texture.getName();
  input.file_info_compressed_size = bytes.size();
  input.file_info_uncompressed_size = bytes.size();
  input.opened_stream_pointer_token = 0x3100U + state_before_load;
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
            "build selected receipt");
  return receipt;
}

Ogre14AuthenticatedTextureReceipt BuildAuthenticatedReceipt(
    Ogre::Texture &texture, std::uint64_t state_before_load,
    const std::vector<std::uint8_t> &bytes, bool generated = false) {
  Ogre14AuthenticatedTextureCaptureInput input;
  input.effective_resource_group = texture.getGroup();
  input.group_generation = 1U;
  input.archive_sha256 = std::string(64U, generated ? 'b' : 'a');
  input.exact_member_name = texture.getName();
  input.binding.resource_pointer_token =
      reinterpret_cast<std::uintptr_t>(&texture);
  input.binding.resource_handle =
      static_cast<std::uint64_t>(texture.getHandle());
  input.binding.resource_state_count = state_before_load;
  input.binding.exact_resource_name = texture.getName();
  if (generated) {
    input.source_kind = Ogre14AuthenticatedTextureSourceKind::
        VERSIONED_GENERATED_FALLBACK;
    input.generated_fallback_rule = kOgre14GeneratedTextureFallbackRule;
    input.generated_fallback_rule_version =
        kOgre14GeneratedTextureFallbackRuleVersion;
  } else {
    input.archive_identity = "fixture-authenticated-archive";
    input.archive_name = "fixture-embedded.zip";
    input.archive_type = "EmbeddedZip";
    input.archive_pointer_token = 0x4100U;
  }
  Ogre14AuthenticatedTextureReceipt receipt;
  RequireOk(BuildOgre14AuthenticatedTextureReceipt(
                Ogre14AuthenticatedTextureRegistryConfiguration{}, input,
                bytes.data(), bytes.size(), receipt),
            "build authenticated receipt");
  return receipt;
}

ManagedMaterialDeclaration BuildDeclaration(
    const std::string &name,
    const ManagedMaterialTextureSourceReceipt &source,
    std::uint64_t definition_generation = 1U) {
  ManagedMaterialTextureBindingInput diffuse;
  diffuse.slot = ManagedMaterialTextureSlot::DIFFUSE;
  diffuse.configured = true;
  diffuse.declared_texture_name = source.identity()->exact_resource_name;
  diffuse.resolved_texture_name = source.identity()->exact_resource_name;
  diffuse.effective_texture_name = source.identity()->exact_resource_name;
  diffuse.requested_resource_group =
      source.identity()->effective_resource_group;
  diffuse.effective_resource_group =
      source.identity()->effective_resource_group;
  diffuse.source_receipt = source;
  ManagedMaterialDeclarationInput input;
  input.actor_generation = 7U;
  input.definition_generation = definition_generation;
  input.exact_material_name = name;
  input.declared_type = ManagedMaterialSemanticType::MESH_STANDARD;
  input.resolved_type = ManagedMaterialSemanticType::MESH_STANDARD;
  input.textures[0U] = diffuse;
  input.textures[1U].slot = ManagedMaterialTextureSlot::SPECULAR;
  input.textures[2U].slot = ManagedMaterialTextureSlot::DAMAGED_DIFFUSE;
  ManagedMaterialDeclaration declaration;
  RequireOk(BuildManagedMaterialDeclaration(
                ManagedMaterialDeclarationRegistryConfiguration{}, input,
                declaration),
            "build neutral declaration");
  return declaration;
}

void TestSelectedAuthorityReuseReloadAndTeardown() {
  const std::vector<std::uint8_t> bytes{'o', 'r', 'd'};
  Ogre14SelectedTextureSourceReceiptRegistry registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry({}, registry),
            "initialize selected registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(kGroup, 1U,
                                                               registry),
            "activate selected group");
  Ogre::TexturePtr texture =
      std::make_shared<TestTexture>("ordinary.png", 71U);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(
                BuildSelectedReceipt(*texture, 0U, bytes), registry),
            "commit selected receipt");
  texture->load();
  Resolver resolver;
  resolver.selected_registry = &registry;
  Ogre14SelectedTextureSourceResolution resolution;
  RequireOk(resolver.ResolveSelectedTextureSource(*texture, resolution),
            "resolve selected texture");

  ManagedMaterialTextureSourceReceipt neutral;
  Ogre14ManagedMaterialSourceAuthorityBinding binding;
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildSelected(
                texture, resolver, resolver, resolution, {}, neutral, binding),
            "adapt selected authority");
  Require(neutral.identity()->trust ==
              ManagedMaterialSourceTrust::OBSERVED_SELECTED_SOURCE &&
              binding.Revalidate(resolver, resolver),
          "selected source lost observed trust or liveness");

  ManagedMaterialTextureSourceReceipt shared;
  Ogre14ManagedMaterialSourceAuthorityBinding shared_binding;
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildSelected(
                texture, resolver, resolver, resolution, {}, shared,
                shared_binding, &neutral),
            "reuse selected neutral receipt");
  Require(shared.SharesImmutableStateWith(neutral),
          "same selected authority recopied immutable bytes");

  ManagedMaterialDeclarationRegistryConfiguration strict;
  strict.maximum_source_bytes = bytes.size() - 1U;
  strict.maximum_retained_source_bytes = bytes.size() - 1U;
  ManagedMaterialTextureSourceReceipt sentinel = neutral;
  Ogre14ManagedMaterialSourceAuthorityBinding binding_sentinel = binding;
  const ValidationResult strict_result =
      Ogre14ManagedMaterialSourceAdapter::BuildSelected(
          texture, resolver, resolver, resolution, strict, sentinel,
          binding_sentinel, &neutral);
  Require(!strict_result && sentinel.SharesImmutableStateWith(neutral) &&
              binding_sentinel.SharesImmutableStateWith(binding),
          "reuse bypassed stricter bounds or changed outputs");

  texture->reload();
  Require(!binding.Revalidate(resolver, resolver),
          "selected reload retained stale authority");
  const std::vector<std::uint8_t> changed{'n', 'e', 'w'};
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(
                BuildSelectedReceipt(*texture, 1U, changed), registry),
            "commit selected reload");
  Ogre14SelectedTextureSourceResolution reload_resolution;
  RequireOk(resolver.ResolveSelectedTextureSource(*texture, reload_resolution),
            "resolve selected reload");
  ManagedMaterialTextureSourceReceipt reloaded;
  Ogre14ManagedMaterialSourceAuthorityBinding reloaded_binding;
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildSelected(
                texture, resolver, resolver, reload_resolution, {}, reloaded,
                reloaded_binding, &neutral),
            "adapt selected reload");
  Require(!reloaded.SharesImmutableStateWith(neutral) &&
              reloaded.identity()->source_sha256 !=
                  neutral.identity()->source_sha256,
          "selected reload reused stale bytes");
  RequireOk(TeardownOgre14SelectedTextureSourceGroup(kGroup, 1U, registry),
            "teardown selected group");
  Require(!reloaded_binding.Revalidate(resolver, resolver),
          "selected binding survived group teardown");
}

void TestAuthenticatedArchiveGeneratedAndMaterialBinding() {
  const std::vector<std::uint8_t> archive_bytes{'a', 'u', 't', 'h'};
  Ogre14AuthenticatedTextureReceiptRegistry registry;
  RequireOk(InitializeOgre14AuthenticatedTextureReceiptRegistry({}, registry),
            "initialize authenticated registry");
  RequireOk(AdvanceOgre14AuthenticatedTextureGroupGeneration(kGroup, 1U,
                                                              registry),
            "activate authenticated group");
  Ogre::TexturePtr texture =
      std::make_shared<TestTexture>("authenticated.png", 81U);
  RequireOk(CommitOgre14AuthenticatedTextureReceipt(
                BuildAuthenticatedReceipt(*texture, 0U, archive_bytes),
                registry),
            "commit authenticated archive receipt");
  texture->load();
  Resolver resolver;
  resolver.authenticated = true;
  resolver.authenticated_registry = &registry;
  Ogre14AuthenticatedTextureResolution resolution;
  RequireOk(resolver.ResolveAuthenticatedTexture(*texture, resolution),
            "resolve authenticated texture");
  ManagedMaterialTextureSourceReceipt neutral;
  Ogre14ManagedMaterialSourceAuthorityBinding source_binding;
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildAuthenticated(
                texture, resolver, resolution, {}, neutral, source_binding),
            "adapt authenticated archive");
  Require(neutral.identity()->trust ==
              ManagedMaterialSourceTrust::AUTHENTICATED_ARCHIVE_MEMBER,
          "authenticated archive trust kind was lost");

  const ManagedMaterialDeclaration declaration =
      BuildDeclaration("body", neutral);
  Ogre::MaterialPtr material =
      std::make_shared<Ogre::Material>(nullptr, "actor/body", 91U, kGroup);
  std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
             kManagedMaterialTextureSlotCount>
      sources{};
  sources[0U] = source_binding;
  Ogre14ManagedMaterialDeclarationBinding material_binding;
  RequireOk(Ogre14ManagedMaterialDeclarationBinding::Build(
                material, declaration, sources, resolver, resolver,
                material_binding),
            "build exact material declaration binding");
  Ogre::MaterialPtr substitute =
      std::make_shared<Ogre::Material>(nullptr, "actor/body", 92U, kGroup);
  Require(material_binding.MatchesExactMaterial(material) &&
              !material_binding.MatchesExactMaterial(substitute) &&
              material_binding.Revalidate(resolver, resolver),
          "runtime material route accepted a substituted MaterialPtr");

  Ogre::TexturePtr generated_texture =
      std::make_shared<TestTexture>("generated.dds", 82U);
  const std::vector<std::uint8_t> generated_bytes{'g', 'e', 'n'};
  RequireOk(CommitOgre14AuthenticatedTextureReceipt(
                BuildAuthenticatedReceipt(*generated_texture, 0U,
                                           generated_bytes, true),
                registry),
            "commit authenticated generated receipt");
  generated_texture->load();
  Ogre14AuthenticatedTextureResolution generated_resolution;
  RequireOk(resolver.ResolveAuthenticatedTexture(*generated_texture,
                                                  generated_resolution),
            "resolve generated texture");
  ManagedMaterialTextureSourceReceipt generated;
  Ogre14ManagedMaterialSourceAuthorityBinding generated_binding;
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildAuthenticated(
                generated_texture, resolver, generated_resolution, {},
                generated, generated_binding),
            "adapt generated authority");
  Require(generated.identity()->trust == ManagedMaterialSourceTrust::
                                             AUTHENTICATED_GENERATED_FALLBACK,
          "generated source collapsed into archive trust");

  std::weak_ptr<Ogre::Texture> retained = texture;
  texture.reset();
  Require(!retained.expired(),
          "source binding did not retain the exact strong TexturePtr");
  RequireOk(TeardownOgre14AuthenticatedTextureGroup(kGroup, 1U, registry),
            "teardown authenticated group");
  Require(!source_binding.Revalidate(resolver, resolver) &&
              !material_binding.Revalidate(resolver, resolver),
          "authenticated teardown left source/material authority live");
}

void TestFrameReachabilityIgnoresOnlyStaleUnreachableBindings() {
  const std::vector<std::uint8_t> bytes{'r', 'e', 'a', 'c', 'h'};
  Ogre14SelectedTextureSourceReceiptRegistry source_registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry({}, source_registry),
            "initialize reachability source registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                kGroup, 1U, source_registry),
            "activate reachability source group");

  Ogre::TexturePtr reachable_texture =
      std::make_shared<TestTexture>("reachable.png", 101U);
  Ogre::TexturePtr unreachable_texture =
      std::make_shared<TestTexture>("unreachable.png", 102U);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(
                BuildSelectedReceipt(*reachable_texture, 0U, bytes),
                source_registry),
            "commit reachable source receipt");
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(
                BuildSelectedReceipt(*unreachable_texture, 0U, bytes),
                source_registry),
            "commit unreachable source receipt");
  reachable_texture->load();
  unreachable_texture->load();

  Resolver resolver;
  resolver.selected_registry = &source_registry;
  Ogre14SelectedTextureSourceResolution reachable_resolution;
  Ogre14SelectedTextureSourceResolution unreachable_resolution;
  RequireOk(resolver.ResolveSelectedTextureSource(
                *reachable_texture, reachable_resolution),
            "resolve reachable source");
  RequireOk(resolver.ResolveSelectedTextureSource(
                *unreachable_texture, unreachable_resolution),
            "resolve unreachable source");

  ManagedMaterialTextureSourceReceipt reachable_source;
  ManagedMaterialTextureSourceReceipt unreachable_source;
  Ogre14ManagedMaterialSourceAuthorityBinding reachable_source_binding;
  Ogre14ManagedMaterialSourceAuthorityBinding unreachable_source_binding;
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildSelected(
                reachable_texture, resolver, resolver, reachable_resolution,
                {}, reachable_source, reachable_source_binding),
            "adapt reachable source");
  RequireOk(Ogre14ManagedMaterialSourceAdapter::BuildSelected(
                unreachable_texture, resolver, resolver,
                unreachable_resolution, {}, unreachable_source,
                unreachable_source_binding),
            "adapt unreachable source");

  const ManagedMaterialDeclaration reachable_declaration =
      BuildDeclaration("actor/reachable", reachable_source);
  const ManagedMaterialDeclaration unreachable_declaration =
      BuildDeclaration("actor/unreachable", unreachable_source, 2U);
  Ogre::MaterialPtr reachable_material = std::make_shared<Ogre::Material>(
      nullptr, "actor/reachable/native", 111U, kGroup);
  Ogre::MaterialPtr unreachable_material = std::make_shared<Ogre::Material>(
      nullptr, "actor/unreachable/native", 112U, kGroup);
  std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
             kManagedMaterialTextureSlotCount>
      reachable_sources{};
  std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
             kManagedMaterialTextureSlotCount>
      unreachable_sources{};
  reachable_sources[0U] = reachable_source_binding;
  unreachable_sources[0U] = unreachable_source_binding;
  Ogre14ManagedMaterialDeclarationBinding reachable_binding;
  Ogre14ManagedMaterialDeclarationBinding unreachable_binding;
  RequireOk(Ogre14ManagedMaterialDeclarationBinding::Build(
                reachable_material, reachable_declaration, reachable_sources,
                resolver, resolver, reachable_binding),
            "build reachable declaration binding");
  RequireOk(Ogre14ManagedMaterialDeclarationBinding::Build(
                unreachable_material, unreachable_declaration,
                unreachable_sources, resolver, resolver,
                unreachable_binding),
            "build unreachable declaration binding");

  ManagedMaterialDeclarationRegistry declaration_registry;
  RequireOk(InitializeManagedMaterialDeclarationRegistry(
                {}, 7U, declaration_registry),
            "initialize reachability declaration registry");
  RequireOk(CommitManagedMaterialDeclaration(reachable_declaration,
                                              declaration_registry),
            "commit reachable declaration");
  RequireOk(CommitManagedMaterialDeclaration(unreachable_declaration,
                                              declaration_registry),
            "commit unreachable declaration");
  ManagedMaterialDeclarationSnapshot snapshot;
  RequireOk(CaptureManagedMaterialDeclarationSnapshot(declaration_registry,
                                                       snapshot),
            "capture reachability declaration snapshot");

  const std::vector<Ogre14ManagedMaterialDeclarationBinding> published{
      reachable_binding, unreachable_binding};
  const std::vector<Ogre14ManagedMaterialDeclarationBinding> reachable_only{
      reachable_binding};
  RequireOk(ValidateOgre14ReachableManagedMaterialBindings(
                snapshot, published, reachable_only, resolver, resolver),
            "validate initial reachable binding");
  Require(reachable_binding.ReferencesExactMaterial(reachable_material) &&
              !reachable_binding.ReferencesExactMaterial(
                  unreachable_material),
          "exact material reference route accepted a substitute");

  unreachable_texture->reload();
  Require(reachable_binding.Revalidate(resolver, resolver) &&
              !unreachable_binding.Revalidate(resolver, resolver),
          "fixture did not isolate stale unreachable authority");
  RequireOk(ValidateOgre14ReachableManagedMaterialBindings(
                snapshot, published, reachable_only, resolver, resolver),
            "stale unreachable binding poisoned reachable frame");

  const std::vector<Ogre14ManagedMaterialDeclarationBinding> both_reachable{
      reachable_binding, unreachable_binding};
  const ValidationResult stale_reachable =
      ValidateOgre14ReachableManagedMaterialBindings(
          snapshot, published, both_reachable, resolver, resolver);
  Require(!stale_reachable &&
              stale_reachable.code == ValidationCode::REVISION_MISMATCH &&
              stale_reachable.field ==
                  "managed_material_ogre14.reachable_source_authority",
          "stale frame-reachable binding escaped fail-closed validation");

  const std::vector<Ogre14ManagedMaterialDeclarationBinding>
      incomplete_publication{reachable_binding};
  const ValidationResult incomplete =
      ValidateOgre14ReachableManagedMaterialBindings(
          snapshot, incomplete_publication, reachable_only, resolver,
          resolver);
  Require(!incomplete && incomplete.code == ValidationCode::SIZE_MISMATCH,
          "reachability weakened the immutable publication-set invariant");
}

} // namespace

int main() {
  // Material construction reads core OGRE manager singletons even though this
  // clean-room fixture never creates a renderer or GPU resource.
  Ogre::Root root("", "", "");
  (void)root;
  TestSelectedAuthorityReuseReloadAndTeardown();
  TestAuthenticatedArchiveGeneratedAndMaterialBinding();
  TestFrameReachabilityIgnoresOnlyStaleUnreachableBindings();
  return EXIT_SUCCESS;
}
