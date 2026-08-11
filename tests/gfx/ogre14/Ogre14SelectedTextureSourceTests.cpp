/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14SelectedTextureSource.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace Ogre {
class Texture {};
}

namespace RoR::Render::Testing {

class Ogre14SelectedTextureSourceResolutionTestAccess final {
public:
  static ValidationResult Mint(
      const Ogre14SelectedTextureSourceReceiptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      std::uintptr_t pointer_token, std::uint64_t handle,
      const std::string &name, std::uint64_t loaded_state,
      const IOgre14SelectedTextureSourceResolver &resolver,
      Ogre14SelectedTextureSourceResolution &resolution,
      IOgre14SelectedTextureSourceFaultInjector *fault = nullptr) {
    return registry.MintLoadedResourceResolution(
        group, generation, pointer_token, handle, name, loaded_state,
        reinterpret_cast<std::uintptr_t>(&resolver), resolution, fault);
  }

  static bool Revalidate(
      const Ogre14SelectedTextureSourceReceiptRegistry &registry,
      const Ogre14SelectedTextureSourceResolution &resolution,
      const IOgre14SelectedTextureSourceResolver &resolver,
      std::uintptr_t pointer_token, std::uint64_t handle,
      const std::string &group, const std::string &name,
      std::uint64_t loaded_state) {
    return registry.RevalidateLoadedResourceResolution(
        resolution, reinterpret_cast<std::uintptr_t>(&resolver),
        pointer_token, handle, group, name, loaded_state);
  }
};

} // namespace RoR::Render::Testing

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireOk(const ValidationResult &result, const char *message) {
  if (!result) {
    throw std::runtime_error(std::string(message) + ": " + result.field +
                             ": " + result.detail);
  }
}

Ogre14SelectedTextureSourceCaptureInput MakeInput(
    const std::vector<std::uint8_t> &bytes, std::uint64_t generation = 1U,
    std::uintptr_t resource_pointer = 0x1000U,
    std::uint64_t resource_handle = 7U,
    std::uint64_t pre_load_state = 0U) {
  Ogre14SelectedTextureSourceCaptureInput input;
  input.effective_resource_group = "CityWorld";
  input.group_generation = generation;
  input.selected_archive_name = "CityWorld.zip";
  input.selected_archive_type = "Zip";
  input.selected_archive_pointer_token = 0x2000U;
  input.file_info_archive_pointer_token = 0x2000U;
  input.file_info_filename = "road.png";
  input.file_info_path = "textures/roads/";
  input.file_info_basename = "road.png";
  input.exact_member_name = "textures/roads/road.png";
  input.file_info_compressed_size =
      static_cast<std::uint64_t>(bytes.size());
  input.file_info_uncompressed_size =
      static_cast<std::uint64_t>(bytes.size());
  input.opened_stream_pointer_token = 0x3000U +
                                      static_cast<std::uintptr_t>(pre_load_state);
  input.opened_stream_name = "textures/roads/road.png";
  input.opened_stream_size = static_cast<std::uint64_t>(bytes.size());
  input.resource_pointer_token = resource_pointer;
  input.resource_handle = resource_handle;
  input.exact_resource_name = "road.png";
  input.resource_state_count_before_load = pre_load_state;
  return input;
}

Ogre14SelectedTextureSourceReceipt BuildReceipt(
    const Ogre14SelectedTextureSourceRegistryConfiguration &configuration,
    const Ogre14SelectedTextureSourceCaptureInput &input,
    const std::vector<std::uint8_t> &bytes) {
  Ogre14SelectedTextureSourceReceipt receipt;
  RequireOk(BuildOgre14SelectedTextureSourceReceipt(
                configuration, input, bytes.data(), bytes.size(), receipt),
            "build selected texture receipt");
  return receipt;
}

class ThrowAt final : public IOgre14SelectedTextureSourceFaultInjector {
public:
  explicit ThrowAt(Ogre14SelectedTextureSourceTransactionStage stage,
                   bool allocation = false)
      : stage_(stage), allocation_(allocation) {}

  void BeforeSelectedTextureSourceStage(
      Ogre14SelectedTextureSourceTransactionStage stage) override {
    if (stage == stage_) {
      if (allocation_) {
        throw std::bad_alloc();
      }
      throw std::runtime_error("injected selected-texture fault");
    }
  }

private:
  Ogre14SelectedTextureSourceTransactionStage stage_;
  bool allocation_ = false;
};

class Resolver final : public IOgre14SelectedTextureSourceResolver {
public:
  const Ogre14SelectedTextureSourceReceiptRegistry *registry = nullptr;
  std::string group = "CityWorld";
  std::uint64_t generation = 1U;
  std::uintptr_t pointer_token = 0x1000U;
  std::uint64_t handle = 7U;
  std::string name = "road.png";
  std::uint64_t loaded_state = 1U;

  ValidationResult ResolveSelectedTextureSource(
      Ogre::Texture &,
      Ogre14SelectedTextureSourceResolution &resolution) const override {
    if (registry == nullptr) {
      return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                       "resolver.registry", "missing");
    }
    return RoR::Render::Testing::
        Ogre14SelectedTextureSourceResolutionTestAccess::Mint(
            *registry, group, generation, pointer_token, handle, name,
            loaded_state, *this, resolution);
  }

  bool RevalidateSelectedTextureSource(
      Ogre::Texture &,
      const Ogre14SelectedTextureSourceResolution &resolution) const
      noexcept override {
    return registry != nullptr &&
           RoR::Render::Testing::
               Ogre14SelectedTextureSourceResolutionTestAccess::Revalidate(
                   *registry, resolution, *this, pointer_token, handle, group,
                   name, loaded_state);
  }
};

void TestExactObservationAndImmutableBytes() {
  const std::vector<std::uint8_t> bytes{'a', 'b', 'c'};
  Ogre14SelectedTextureSourceRegistryConfiguration configuration;
  auto input = MakeInput(bytes);
  Ogre14SelectedTextureSourceReceipt receipt =
      BuildReceipt(configuration, input, bytes);
  const auto *metadata = receipt.metadata();
  Require(metadata != nullptr, "receipt metadata missing");
  Require(metadata->source.source_kind ==
              Ogre14SelectedTextureSourceKind::
                  UNAUTHENTICATED_PACKAGE_ARCHIVE_MEMBER,
          "ordinary source acquired an authenticated trust label");
  Require(metadata->source.selected_archive_name == "CityWorld.zip" &&
              metadata->source.selected_archive_type == "Zip" &&
              metadata->source.selected_archive_pointer_token == 0x2000U,
          "selected archive identity was not retained exactly");
  Require(metadata->source.file_info_filename == "road.png" &&
              metadata->source.file_info_path == "textures/roads/" &&
              metadata->source.file_info_basename == "road.png" &&
              metadata->source.exact_member_name ==
                  "textures/roads/road.png",
          "FileInfo identity was not retained exactly");
  Require(metadata->source.opened_stream_pointer_token == 0x3000U &&
              metadata->source.opened_stream_name ==
                  "textures/roads/road.png" &&
              metadata->source.opened_stream_size == bytes.size(),
          "opened stream identity was not retained exactly");
  Require(metadata->observed_bytes_sha256 ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "observed byte digest is not SHA-256(abc)");
  Require(receipt.ReplacementBytesMatch(bytes.data(), bytes.size()),
          "receipt did not retain exact source bytes");

  input.selected_archive_name = "mutated.zip";
  std::vector<std::uint8_t> mutated = bytes;
  mutated[0U] = 'z';
  Require(receipt.metadata()->source.selected_archive_name == "CityWorld.zip" &&
              receipt.source_bytes()[0U] == 'a',
          "receipt aliased mutable capture input or caller bytes");

  auto invalid = MakeInput(bytes);
  invalid.exact_member_name = "road.png";
  Require(!ValidateOgre14SelectedTextureSourceCaptureInput(invalid),
          "FileInfo path/basename mismatch was accepted");
  invalid = MakeInput(bytes);
  invalid.file_info_archive_pointer_token = 0x9999U;
  Require(!ValidateOgre14SelectedTextureSourceCaptureInput(invalid),
          "FileInfo archive substitution was accepted");
  invalid = MakeInput(bytes);
  invalid.source_kind = static_cast<Ogre14SelectedTextureSourceKind>(99U);
  Require(!ValidateOgre14SelectedTextureSourceCaptureInput(invalid),
          "unknown ordinary source kind was accepted");
}

void TestBuildRollbackAndCaps() {
  const std::vector<std::uint8_t> bytes{'a', 'b', 'c'};
  Ogre14SelectedTextureSourceRegistryConfiguration configuration;
  const auto input = MakeInput(bytes);
  Ogre14SelectedTextureSourceReceipt output =
      BuildReceipt(configuration, input, bytes);
  const auto prior = output;

  for (const auto stage : {
           Ogre14SelectedTextureSourceTransactionStage::
               AFTER_SOURCE_BYTES_COPIED,
           Ogre14SelectedTextureSourceTransactionStage::
               BEFORE_RECEIPT_COMMIT}) {
    ThrowAt fault(stage);
    const ValidationResult result = BuildOgre14SelectedTextureSourceReceipt(
        configuration, input, bytes.data(), bytes.size(), output, &fault);
    Require(!result && output.SharesImmutableStateWith(prior),
            "receipt fault changed prior immutable output");
  }

  configuration.maximum_source_bytes = 2U;
  configuration.maximum_retained_source_bytes = 2U;
  Require(!BuildOgre14SelectedTextureSourceReceipt(
              configuration, input, bytes.data(), bytes.size(), output) &&
              output.SharesImmutableStateWith(prior),
          "source byte cap failure changed prior output");

  configuration = {};
  auto wrong_size = input;
  wrong_size.opened_stream_size = 4U;
  Require(!BuildOgre14SelectedTextureSourceReceipt(
              configuration, wrong_size, bytes.data(), bytes.size(), output),
          "stream/FileInfo size mismatch was accepted");
}

void TestRegistryReloadCollisionsAndCaps() {
  const std::vector<std::uint8_t> bytes{'a', 'b', 'c'};
  const std::vector<std::uint8_t> changed{'x', 'y', 'z'};
  Ogre14SelectedTextureSourceRegistryConfiguration configuration;
  Ogre14SelectedTextureSourceReceiptRegistry registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry(configuration,
                                                            registry),
            "initialize registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                "CityWorld", 1U, registry),
            "activate first group generation");
  const auto receipt = BuildReceipt(configuration, MakeInput(bytes), bytes);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(receipt, registry),
            "commit first receipt");
  Require(registry.size() == 1U &&
              registry.retained_source_bytes() == bytes.size(),
          "first commit accounting is wrong");

  const auto before_retry = registry;
  const auto equal_retry =
      BuildReceipt(configuration, MakeInput(bytes), bytes);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(equal_retry, registry),
            "exact same-state downstream-failure retry");
  Require(registry.SharesImmutableStateWith(before_retry),
          "idempotent same-state retry republished registry");

  const auto same_state_change =
      BuildReceipt(configuration, MakeInput(changed), changed);
  Require(!CommitOgre14SelectedTextureSourceReceipt(same_state_change,
                                                     registry) &&
              registry.SharesImmutableStateWith(before_retry),
          "same pre-load state silently changed selected bytes");

  auto reload_input = MakeInput(changed, 1U, 0x1000U, 7U, 1U);
  const auto reload = BuildReceipt(configuration, reload_input, changed);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(reload, registry),
            "commit strictly newer reload");
  Require(registry.size() == 1U &&
              registry.retained_source_bytes() == changed.size(),
          "reload did not replace exact prior receipt");
  Ogre14SelectedTextureSourceReceipt found;
  RequireOk(registry.FindResource("CityWorld", 1U, 0x1000U, 7U,
                                  "road.png", found),
            "find reloaded receipt");
  Require(found.SharesImmutableStateWith(reload),
          "registry did not retain exact reload receipt state");

  auto stale_input = MakeInput(bytes, 1U, 0x1000U, 7U, 0U);
  const auto stale = BuildReceipt(configuration, stale_input, bytes);
  const auto before_stale = registry;
  Require(!CommitOgre14SelectedTextureSourceReceipt(stale, registry) &&
              registry.SharesImmutableStateWith(before_stale),
          "stale reload resurrected an older source receipt");

  auto pointer_reuse_input = MakeInput(bytes, 1U, 0x1000U, 99U, 2U);
  const auto pointer_reuse =
      BuildReceipt(configuration, pointer_reuse_input, bytes);
  Require(!CommitOgre14SelectedTextureSourceReceipt(pointer_reuse,
                                                     registry),
          "live pointer reuse with another handle was accepted");

  auto handle_collision_input = MakeInput(bytes, 1U, 0x5000U, 7U, 0U);
  const auto handle_collision =
      BuildReceipt(configuration, handle_collision_input, bytes);
  Require(!CommitOgre14SelectedTextureSourceReceipt(handle_collision,
                                                     registry),
          "live handle collision across pointers was accepted");

  Ogre14SelectedTextureSourceRegistryConfiguration bounded = configuration;
  bounded.maximum_live_receipts = 1U;
  Ogre14SelectedTextureSourceReceiptRegistry bounded_registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry(bounded,
                                                           bounded_registry),
            "initialize bounded registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                "CityWorld", 1U, bounded_registry),
            "activate bounded group");
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(receipt,
                                                      bounded_registry),
            "commit bounded first receipt");
  auto second_input = MakeInput(bytes, 1U, 0x6000U, 8U, 0U);
  second_input.exact_resource_name = "second.png";
  const auto second = BuildReceipt(bounded, second_input, bytes);
  const auto before_cap = bounded_registry;
  Require(!CommitOgre14SelectedTextureSourceReceipt(second,
                                                     bounded_registry) &&
              bounded_registry.SharesImmutableStateWith(before_cap),
          "live receipt cap failure changed registry");
}

void TestResolutionFreshnessAndLifecycle() {
  const std::vector<std::uint8_t> bytes{'a', 'b', 'c'};
  Ogre14SelectedTextureSourceRegistryConfiguration configuration;
  Ogre14SelectedTextureSourceReceiptRegistry registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry(configuration,
                                                            registry),
            "initialize resolution registry");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                "CityWorld", 1U, registry),
            "activate resolution group");
  const auto receipt = BuildReceipt(configuration, MakeInput(bytes), bytes);
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(receipt, registry),
            "commit resolution source");

  Resolver resolver;
  resolver.registry = &registry;
  Ogre::Texture texture;
  Ogre14SelectedTextureSourceResolution resolution;
  RequireOk(resolver.ResolveSelectedTextureSource(texture, resolution),
            "mint selected source resolution");
  Require(resolution.initialized() && resolution.MatchesResolver(resolver) &&
              resolution.MatchesLoadedResourceIdentity(
                  0x1000U, 7U, "CityWorld", "road.png", 1U) &&
              resolver.RevalidateSelectedTextureSource(texture, resolution),
          "fresh selected source resolution did not validate");
  Require(resolution.source_receipt() != nullptr &&
              resolution.source_receipt()->SharesImmutableStateWith(receipt),
          "resolution did not retain exact immutable source receipt");
  const auto copied = resolution;
  Require(copied.SharesLoadedResourceAuthorityWith(resolution),
          "resolution copy did not retain exact authority state");

  Resolver other_resolver = resolver;
  Require(!resolution.MatchesResolver(other_resolver),
          "resolution accepted a substituted resolver instance");
  resolver.loaded_state = 2U;
  Require(!resolver.RevalidateSelectedTextureSource(texture, resolution),
          "resolution survived loaded-state substitution");
  resolver.loaded_state = 1U;

  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                "Other", 2U, registry),
            "advance unrelated group");
  Require(!resolver.RevalidateSelectedTextureSource(texture, resolution),
          "resolution survived a newer immutable registry publication");

  RequireOk(RemoveOgre14SelectedTextureSourceResource(
                "CityWorld", 1U, 0x1000U, 7U, "road.png", registry),
            "remove selected texture resource");
  Require(registry.size() == 0U,
          "exact resource removal retained a receipt");
  RequireOk(RemoveOgre14SelectedTextureSourceResource(
                "CityWorld", 1U, 0x1000U, 7U, "road.png", registry),
            "idempotent missing resource removal");

  RequireOk(TeardownOgre14SelectedTextureSourceGroup("CityWorld", 1U,
                                                      registry),
            "teardown selected texture group");
  Require(!TeardownOgre14SelectedTextureSourceGroup("CityWorld", 1U,
                                                     registry),
          "duplicate group teardown was accepted");
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                "CityWorld", 3U, registry),
            "reactivate group with globally newer generation");
  Require(!CommitOgre14SelectedTextureSourceReceipt(receipt, registry),
          "stale receipt resurrected after group generation advance");
}

void TestRegistryTransactionalFaultsAndPoison() {
  const std::vector<std::uint8_t> bytes{'a', 'b', 'c'};
  Ogre14SelectedTextureSourceRegistryConfiguration configuration;
  const auto receipt = BuildReceipt(configuration, MakeInput(bytes), bytes);
  Ogre14SelectedTextureSourceReceiptRegistry registry;
  RequireOk(InitializeOgre14SelectedTextureSourceRegistry(configuration,
                                                            registry),
            "initialize transactional registry");

  {
    const auto prior = registry;
    ThrowAt fault(Ogre14SelectedTextureSourceTransactionStage::
                      BEFORE_GROUP_TRANSITION_COMMIT,
                  true);
    Require(!AdvanceOgre14SelectedTextureSourceGroupGeneration(
                "CityWorld", 1U, registry, &fault) &&
                registry.SharesImmutableStateWith(prior),
            "group advance allocation fault changed prior registry");
  }
  RequireOk(AdvanceOgre14SelectedTextureSourceGroupGeneration(
                "CityWorld", 1U, registry),
            "activate group after rollback");
  {
    const auto prior = registry;
    ThrowAt fault(Ogre14SelectedTextureSourceTransactionStage::
                      BEFORE_REGISTRY_COMMIT);
    Require(!CommitOgre14SelectedTextureSourceReceipt(receipt, registry,
                                                       &fault) &&
                registry.SharesImmutableStateWith(prior),
            "receipt commit fault changed prior registry");
  }
  RequireOk(CommitOgre14SelectedTextureSourceReceipt(receipt, registry),
            "commit after rollback");
  {
    const auto prior = registry;
    ThrowAt fault(Ogre14SelectedTextureSourceTransactionStage::
                      BEFORE_REGISTRY_COMMIT);
    Require(!RemoveOgre14SelectedTextureSourceResource(
                "CityWorld", 1U, 0x1000U, 7U, "road.png", registry,
                &fault) &&
                registry.SharesImmutableStateWith(prior),
            "resource removal fault changed prior registry");
  }
  {
    Resolver resolver;
    resolver.registry = &registry;
    Ogre14SelectedTextureSourceResolution output;
    Ogre::Texture texture;
    RequireOk(resolver.ResolveSelectedTextureSource(texture, output),
              "mint prior resolution");
    const auto prior = output;
    ThrowAt fault(Ogre14SelectedTextureSourceTransactionStage::
                      BEFORE_RESOLUTION_COMMIT);
    Require(!RoR::Render::Testing::
                 Ogre14SelectedTextureSourceResolutionTestAccess::Mint(
                     registry, "CityWorld", 1U, 0x1000U, 7U, "road.png",
                     1U, resolver, output, &fault) &&
                output.SharesLoadedResourceAuthorityWith(prior),
            "resolution fault changed prior opaque output");
  }
  {
    const auto prior = registry;
    ThrowAt fault(Ogre14SelectedTextureSourceTransactionStage::
                      BEFORE_GROUP_TRANSITION_COMMIT);
    Require(!TeardownOgre14SelectedTextureSourceGroup("CityWorld", 1U,
                                                       registry, &fault) &&
                registry.SharesImmutableStateWith(prior),
            "group teardown fault changed prior registry");
  }

  const auto retained_snapshot = registry;
  PoisonOgre14SelectedTextureSourceRegistry(registry);
  Require(!registry.initialized() && retained_snapshot.initialized() &&
              retained_snapshot.size() == 1U,
          "poison did not invalidate only the current registry publication");
  Require(!CommitOgre14SelectedTextureSourceReceipt(receipt, registry),
          "poisoned registry accepted a new receipt");
}

} // namespace

int main() {
  try {
    TestExactObservationAndImmutableBytes();
    TestBuildRollbackAndCaps();
    TestRegistryReloadCollisionsAndCaps();
    TestResolutionFreshnessAndLifecycle();
    TestRegistryTransactionalFaultsAndPoison();
  } catch (const std::exception &error) {
    std::cerr << "Ogre14SelectedTextureSourceTests: " << error.what()
              << '\n';
    return 1;
  }
  std::cout << "Ogre14SelectedTextureSourceTests: all checks passed\n";
  return 0;
}
