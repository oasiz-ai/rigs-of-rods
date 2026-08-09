/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace RoR::Render::Testing {

class Ogre14AuthenticatedTextureResolutionTestAccess final {
public:
  static ValidationResult Mint(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      std::uintptr_t pointer_token, std::uint64_t handle,
      const std::string &name, std::uint64_t loaded_state_count,
      const IOgre14AuthenticatedTextureResolver &resolver,
      Ogre14AuthenticatedTextureResolution &resolution,
      IOgre14AuthenticatedTextureFaultInjector *fault_injector = nullptr) {
    return registry.MintLoadedResourceResolution(
        group, generation, pointer_token, handle, name, loaded_state_count,
        reinterpret_cast<std::uintptr_t>(&resolver), resolution,
        fault_injector);
  }

  static bool Revalidate(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      const Ogre14AuthenticatedTextureResolution &resolution,
      const IOgre14AuthenticatedTextureResolver &resolver,
      std::uintptr_t pointer_token, std::uint64_t handle,
      const std::string &group, const std::string &name,
      std::uint64_t loaded_state_count) noexcept {
    return registry.RevalidateLoadedResourceResolution(
        resolution, reinterpret_cast<std::uintptr_t>(&resolver),
        pointer_token, handle, group, name, loaded_state_count);
  }
};

} // namespace RoR::Render::Testing

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void Write32(std::vector<std::uint8_t> &bytes, std::size_t offset,
             std::uint32_t value) {
  bytes.at(offset) = static_cast<std::uint8_t>(value & 0xffU);
  bytes.at(offset + 1U) =
      static_cast<std::uint8_t>((value >> 8U) & 0xffU);
  bytes.at(offset + 2U) =
      static_cast<std::uint8_t>((value >> 16U) & 0xffU);
  bytes.at(offset + 3U) =
      static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::vector<std::uint8_t> MakeDds(bool dx10 = false) {
  std::vector<std::uint8_t> bytes(dx10 ? 152U : 132U, 0U);
  bytes[0U] = 'D';
  bytes[1U] = 'D';
  bytes[2U] = 'S';
  bytes[3U] = ' ';
  Write32(bytes, 4U, 124U);
  Write32(bytes, 8U, 0x0002100fU);
  Write32(bytes, 12U, 32U);
  Write32(bytes, 16U, 64U);
  Write32(bytes, 20U, 256U);
  Write32(bytes, 24U, 2U);
  Write32(bytes, 28U, 4U);
  Write32(bytes, 32U, 0x12345678U);
  Write32(bytes, 76U, 32U);
  Write32(bytes, 80U, 0x41U);
  Write32(bytes, 84U, dx10 ? 0x30315844U : 0U);
  Write32(bytes, 88U, 32U);
  Write32(bytes, 92U, 0x00ff0000U);
  Write32(bytes, 96U, 0x0000ff00U);
  Write32(bytes, 100U, 0x000000ffU);
  Write32(bytes, 104U, 0xff000000U);
  Write32(bytes, 108U, 0x00401008U);
  Write32(bytes, 112U, 0x0000fe00U);
  Write32(bytes, 116U, 7U);
  Write32(bytes, 120U, 8U);
  Write32(bytes, 124U, 0x87654321U);
  if (dx10) {
    Write32(bytes, 128U, 28U);
    Write32(bytes, 132U, 3U);
    Write32(bytes, 136U, 4U);
    Write32(bytes, 140U, 6U);
    Write32(bytes, 144U, 1U);
  }
  bytes.back() = 0x5aU;
  return bytes;
}

Ogre14AuthenticatedTextureCaptureInput MakeArchiveInput(
    std::uintptr_t pointer_token = 0x100U, std::uint64_t handle = 7U,
    std::uint64_t state_count = 0U, std::uint64_t generation = 1U,
    std::string group = "CityWorld", std::string member = "NeoQ/Wall.DDS") {
  Ogre14AuthenticatedTextureCaptureInput input;
  input.effective_resource_group = std::move(group);
  input.group_generation = generation;
  input.archive_identity = "/content/CityWorld.zip";
  input.archive_name = "ror-authenticated-embedded-zip-v1-a-1";
  input.archive_type = "EmbeddedZip";
  input.archive_sha256 = std::string(64U, 'a');
  input.archive_pointer_token = 0x900U;
  input.exact_member_name = member;
  input.binding.resource_pointer_token = pointer_token;
  input.binding.resource_handle = handle;
  input.binding.resource_state_count = state_count;
  input.binding.exact_resource_name = std::move(member);
  return input;
}

Ogre14AuthenticatedTextureCaptureInput MakeGeneratedInput(
    std::uintptr_t pointer_token = 0x200U, std::uint64_t handle = 8U,
    std::uint64_t generation = 1U) {
  Ogre14AuthenticatedTextureCaptureInput input;
  input.source_kind =
      Ogre14AuthenticatedTextureSourceKind::VERSIONED_GENERATED_FALLBACK;
  input.effective_resource_group = "CityWorld";
  input.group_generation = generation;
  input.archive_sha256 = std::string(64U, 'b');
  input.exact_member_name = "ror_generated_missing.dds";
  input.generated_fallback_rule = kOgre14GeneratedTextureFallbackRule;
  input.generated_fallback_rule_version =
      kOgre14GeneratedTextureFallbackRuleVersion;
  input.binding.resource_pointer_token = pointer_token;
  input.binding.resource_handle = handle;
  input.binding.exact_resource_name = input.exact_member_name;
  return input;
}

Ogre14AuthenticatedTextureReceipt BuildReceipt(
    const Ogre14AuthenticatedTextureCaptureInput &input,
    const std::vector<std::uint8_t> &bytes,
    const Ogre14AuthenticatedTextureRegistryConfiguration &configuration = {}) {
  Ogre14AuthenticatedTextureReceipt receipt;
  Require(BuildOgre14AuthenticatedTextureReceipt(
              configuration, input, bytes.data(), bytes.size(), receipt)
              .ok(),
          "valid authenticated texture receipt did not build");
  return receipt;
}

Ogre14AuthenticatedTextureReceiptRegistry MakeRegistry(
    const Ogre14AuthenticatedTextureRegistryConfiguration &configuration = {}) {
  Ogre14AuthenticatedTextureReceiptRegistry registry;
  Require(InitializeOgre14AuthenticatedTextureReceiptRegistry(configuration,
                                                               registry)
              .ok(),
          "valid authenticated texture registry did not initialize");
  return registry;
}

class ThrowingFault final : public IOgre14AuthenticatedTextureFaultInjector {
public:
  Ogre14AuthenticatedTextureTransactionStage stage =
      Ogre14AuthenticatedTextureTransactionStage::AFTER_SOURCE_BYTES_COPIED;
  bool bad_allocation = true;

  void BeforeAuthenticatedTextureStage(
      Ogre14AuthenticatedTextureTransactionStage current) override {
    if (current != stage) {
      return;
    }
    if (bad_allocation) {
      throw std::bad_alloc();
    }
    throw 17;
  }
};

class ThrowingArchiveMountFault final
    : public IOgre14AuthenticatedArchiveMountFaultInjector {
public:
  Ogre14AuthenticatedArchiveMountStage stage =
      Ogre14AuthenticatedArchiveMountStage::
          AFTER_EMBEDDED_ZIP_REGISTRATION;
  bool bad_allocation = true;

  void BeforeAuthenticatedArchiveMountStage(
      Ogre14AuthenticatedArchiveMountStage current) override {
    if (current != stage) {
      return;
    }
    if (bad_allocation) {
      throw std::bad_alloc();
    }
    throw 23;
  }
};

class DummyTextureResolver final
    : public IOgre14AuthenticatedTextureResolver {
public:
  ValidationResult ResolveAuthenticatedTexture(
      Ogre::Texture &,
      Ogre14AuthenticatedTextureResolution &) const override {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "dummy.resolve",
        "pure receipt test does not resolve an OGRE texture");
  }

  bool RevalidateAuthenticatedTexture(
      Ogre::Texture &,
      const Ogre14AuthenticatedTextureResolution &) const noexcept override {
    return false;
  }
};

void TestExactBytesHashAndArchiveIdentity() {
  const std::vector<std::uint8_t> original = {'a', 'b', 'c'};
  std::vector<std::uint8_t> caller_bytes = original;
  const auto input = MakeArchiveInput();
  Ogre14AuthenticatedTextureReceipt receipt;
  Require(BuildOgre14AuthenticatedTextureReceipt(
              Ogre14AuthenticatedTextureRegistryConfiguration{}, input,
              caller_bytes.data(), caller_bytes.size(), receipt)
              .ok(),
          "archive texture receipt did not build");
  caller_bytes[0U] = 'z';
  const auto *metadata = receipt.metadata();
  Require(receipt.initialized() && metadata != nullptr &&
              metadata->version ==
                  kOgre14AuthenticatedTextureReceiptVersion &&
              metadata->source.archive_identity ==
                  "/content/CityWorld.zip" &&
              metadata->source.archive_name ==
                  "ror-authenticated-embedded-zip-v1-a-1" &&
              metadata->source.archive_type == "EmbeddedZip" &&
              metadata->source.archive_sha256 == std::string(64U, 'a') &&
              metadata->source.archive_pointer_token == 0x900U &&
              metadata->source.exact_member_name == "NeoQ/Wall.DDS" &&
              metadata->source.binding.resource_pointer_token == 0x100U &&
              metadata->source.binding.resource_handle == 7U &&
              metadata->source.binding.resource_state_count == 0U &&
              metadata->byte_count == 3U &&
              metadata->bytes_sha256 ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" &&
              metadata->dds.kind == Ogre14SourceDdsHeaderKind::NOT_DDS &&
              receipt.ReplacementBytesMatch(original.data(), original.size()) &&
              !receipt.ReplacementBytesMatch(caller_bytes.data(),
                                             caller_bytes.size()),
          "receipt did not retain exact immutable source bytes and identity");
  Require(!IsLowercaseOgre14Sha256(std::string(64U, 'A')) &&
              !IsLowercaseOgre14Sha256("abc") &&
              IsLowercaseOgre14Sha256(metadata->bytes_sha256),
          "lowercase SHA-256 validation was not exact");
}

void TestDdsHeaderFactsAndGeneratedRule() {
  const auto legacy_bytes = MakeDds(false);
  const auto legacy = BuildReceipt(MakeArchiveInput(), legacy_bytes);
  const auto &facts = legacy.metadata()->dds;
  Require(facts.kind == Ogre14SourceDdsHeaderKind::LEGACY &&
              facts.header_size == 124U && facts.flags == 0x0002100fU &&
              facts.height == 32U && facts.width == 64U &&
              facts.pitch_or_linear_size == 256U && facts.depth == 2U &&
              facts.mip_map_count == 4U &&
              facts.reserved1[0U] == 0x12345678U &&
              facts.pixel_format_size == 32U &&
              facts.pixel_format_flags == 0x41U &&
              facts.rgb_bit_count == 32U &&
              facts.red_mask == 0x00ff0000U &&
              facts.green_mask == 0x0000ff00U &&
              facts.blue_mask == 0x000000ffU &&
              facts.alpha_mask == 0xff000000U &&
              facts.caps == 0x00401008U && facts.caps2 == 0x0000fe00U &&
              facts.caps3 == 7U && facts.caps4 == 8U &&
              facts.reserved2 == 0x87654321U,
          "legacy DDS header facts were not captured exactly");

  const auto dx10 = BuildReceipt(MakeGeneratedInput(), MakeDds(true));
  const auto &dx10_facts = dx10.metadata()->dds;
  Require(dx10_facts.kind == Ogre14SourceDdsHeaderKind::DX10 &&
              dx10_facts.four_cc == 0x30315844U &&
              dx10_facts.dxgi_format == 28U &&
              dx10_facts.resource_dimension == 3U &&
              dx10_facts.misc_flag == 4U && dx10_facts.array_size == 6U &&
              dx10_facts.misc_flags2 == 1U &&
              dx10.metadata()->source.generated_fallback_rule ==
                  kOgre14GeneratedTextureFallbackRule &&
              dx10.metadata()->source.generated_fallback_rule_version ==
                  kOgre14GeneratedTextureFallbackRuleVersion &&
              dx10.metadata()->source.archive_identity.empty(),
          "DX10 or generated-fallback provenance was incomplete");

  std::vector<std::uint8_t> truncated = {'D', 'D', 'S', ' ', 1U};
  Ogre14AuthenticatedTextureReceipt sentinel = legacy;
  const ValidationResult result = BuildOgre14AuthenticatedTextureReceipt(
      Ogre14AuthenticatedTextureRegistryConfiguration{}, MakeArchiveInput(),
      truncated.data(), truncated.size(), sentinel);
  Require(!result && result.code == ValidationCode::SIZE_MISMATCH &&
              sentinel.SharesImmutableStateWith(legacy),
          "truncated DDS did not fail without mutating receipt output");
}

void TestArchiveMemberSelectionCollisionAndCaps() {
  const Ogre14AuthenticatedTextureArchiveMemberObservation collisions[] = {
      {"Textures/Wall.dds", true, true, true},
      {"Textures/wall.dds", false, true, true},
  };
  std::string selected = "sentinel";
  ValidationResult result = SelectOgre14AuthenticatedTextureArchiveMember(
      false, true, collisions, 2U, selected);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              selected == "sentinel",
          "case-folded full-path collision was accepted or mutated output");

  result = SelectOgre14AuthenticatedTextureArchiveMember(
      true, false, collisions, 2U, selected);
  Require(result.ok() && selected == "Textures/Wall.dds",
          "case-sensitive exact full-path member was not selected");

  const Ogre14AuthenticatedTextureArchiveMemberObservation zip_basename[] = {
      {"Textures/Wall.dds", false, false, true},
  };
  selected = "sentinel";
  result = SelectOgre14AuthenticatedTextureArchiveMember(
      false, true, zip_basename, 1U, selected);
  Require(result.ok() && selected == "Textures/Wall.dds",
          "unique non-strict Zip basename fallback lost its exact path/case");

  const Ogre14AuthenticatedTextureArchiveMemberObservation duplicate_exact[] = {
      {"Textures/Wall.dds", true, true, false},
      {"Textures/Wall.dds", true, true, false},
  };
  selected = "sentinel";
  result = SelectOgre14AuthenticatedTextureArchiveMember(
      true, false, duplicate_exact, 2U, selected);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              selected == "sentinel",
          "duplicate exact archive member was accepted or mutated output");

  result = SelectOgre14AuthenticatedTextureArchiveMember(
      true, true, zip_basename, 1U, selected);
  Require(!result && result.code == ValidationCode::INVALID_ENUM,
          "invalid case-sensitive Zip fallback mode was accepted");

  result = SelectOgre14AuthenticatedTextureArchiveMember(
      false, true, zip_basename,
      kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates + 1U,
      selected);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE,
          "archive member candidate cap+1 was accepted");
}

void TestInputAndConfigurationValidation() {
  const auto bytes = std::vector<std::uint8_t>{1U};
  Ogre14AuthenticatedTextureReceipt output;
  auto input = MakeArchiveInput();
  auto configuration = Ogre14AuthenticatedTextureRegistryConfiguration{};

  configuration.version += 1U;
  Require(!BuildOgre14AuthenticatedTextureReceipt(
               configuration, input, bytes.data(), bytes.size(), output),
          "unknown registry configuration version was accepted");
  configuration = {};
  configuration.maximum_live_receipts = 0U;
  Ogre14AuthenticatedTextureReceiptRegistry zero_cap_registry;
  Require(!InitializeOgre14AuthenticatedTextureReceiptRegistry(configuration,
                                                                zero_cap_registry),
          "zero receipt cap was accepted");
  configuration = {};
  configuration.maximum_group_records =
      kOgre14AuthenticatedTextureMaximumGroupRecords + 1U;
  Require(!InitializeOgre14AuthenticatedTextureReceiptRegistry(
               configuration, zero_cap_registry),
          "group-record cap above hard maximum was accepted");
  configuration = {};
  configuration.maximum_source_bytes =
      kOgre14AuthenticatedTextureMaximumSourceBytes + 1U;
  Ogre14AuthenticatedTextureReceiptRegistry invalid_registry;
  Require(!InitializeOgre14AuthenticatedTextureReceiptRegistry(
               configuration, invalid_registry),
          "source byte cap above hard maximum was accepted");

  auto RequireInvalid = [&bytes, &output](
                            Ogre14AuthenticatedTextureCaptureInput candidate,
                            const char *message) {
    const ValidationResult result = BuildOgre14AuthenticatedTextureReceipt(
        Ogre14AuthenticatedTextureRegistryConfiguration{}, candidate,
        bytes.data(), bytes.size(), output);
    Require(!result, message);
  };
  input = MakeArchiveInput();
  input.version += 1U;
  RequireInvalid(input, "unknown capture version was accepted");
  input = MakeArchiveInput();
  input.effective_resource_group.clear();
  RequireInvalid(input, "empty resource group was accepted");
  input = MakeArchiveInput();
  input.group_generation = 0U;
  RequireInvalid(input, "zero group generation was accepted");
  input = MakeArchiveInput();
  input.archive_sha256[0U] = 'A';
  RequireInvalid(input, "uppercase archive SHA-256 was accepted");
  input = MakeArchiveInput();
  input.archive_pointer_token = 0U;
  RequireInvalid(input, "zero archive pointer token was accepted");
  input = MakeArchiveInput();
  input.binding.resource_pointer_token = 0U;
  RequireInvalid(input, "zero resource pointer token was accepted");
  input = MakeArchiveInput();
  input.binding.kind =
      Ogre14AuthenticatedTextureBindingKind::PRE_RESOURCE_TOKEN;
  input.binding.resource_pointer_token = 0U;
  input.binding.resource_handle = 0U;
  input.binding.resource_state_count = 0U;
  input.binding.pre_resource_token = 99U;
  Require(BuildOgre14AuthenticatedTextureReceipt(
              Ogre14AuthenticatedTextureRegistryConfiguration{}, input,
              bytes.data(), bytes.size(), output)
              .ok(),
          "exact pre-resource token was rejected");
  input.binding.resource_handle = 1U;
  RequireInvalid(input, "pre-resource token with a resource handle was accepted");
  input = MakeGeneratedInput();
  input.archive_pointer_token = 0x900U;
  RequireInvalid(input, "generated fallback archive pointer was accepted");
  input = MakeGeneratedInput();
  input.generated_fallback_rule = "guessed-rule";
  RequireInvalid(input, "unknown generated fallback rule was accepted");
  input = MakeGeneratedInput();
  input.generated_fallback_rule_version += 1U;
  RequireInvalid(input, "unknown generated fallback rule version was accepted");

  configuration = {};
  configuration.maximum_source_bytes = 1U;
  const std::vector<std::uint8_t> two_bytes = {1U, 2U};
  Require(!BuildOgre14AuthenticatedTextureReceipt(
               configuration, MakeArchiveInput(), two_bytes.data(),
               two_bytes.size(), output),
          "source byte cap+1 was accepted");
}

void TestRegistryCollisionReloadRemovalAndPointerReuse() {
  Ogre14AuthenticatedTextureReceiptRegistry registry = MakeRegistry();
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("CityWorld", 1U,
                                                            registry)
              .ok(),
          "first group generation did not activate");
  const std::vector<std::uint8_t> bytes = {1U, 2U, 3U};
  const auto first = BuildReceipt(MakeArchiveInput(), bytes);
  Require(CommitOgre14AuthenticatedTextureReceipt(first, registry).ok() &&
              registry.size() == 1U &&
              registry.retained_source_bytes() == bytes.size(),
          "first authenticated texture receipt did not commit");

  Ogre14AuthenticatedTextureReceipt found;
  Require(registry.FindResource("CityWorld", 1U, 0x100U, 7U,
                                "NeoQ/Wall.DDS", found)
                  .ok() &&
              found.SharesImmutableStateWith(first),
          "exact resource binding did not resolve its immutable receipt");
  Ogre14AuthenticatedTextureReceipt unchanged = found;
  Require(!registry.FindResource("CityWorld", 1U, 0x100U, 7U,
                                 "neoq/wall.dds", found) &&
              found.SharesImmutableStateWith(unchanged),
          "case-folded resource lookup matched or mutated output");

  const Ogre14AuthenticatedTextureReceiptRegistry retry_owner = registry;
  const ValidationResult duplicate =
      CommitOgre14AuthenticatedTextureReceipt(first, registry);
  Require(duplicate.ok() &&
              registry.SharesImmutableStateWith(retry_owner) &&
              registry.FindResource("CityWorld", 1U, 0x100U, 7U,
                                    "NeoQ/Wall.DDS", found)
                      .ok() &&
              found.SharesImmutableStateWith(first),
          "exact same-state downstream-failure retry was not idempotent");

  const auto reload = BuildReceipt(MakeArchiveInput(0x100U, 7U, 1U), bytes);
  Require(CommitOgre14AuthenticatedTextureReceipt(reload, registry).ok() &&
              registry.size() == 1U,
          "strictly newer identical resource reload was rejected");
  Require(registry.FindResource("CityWorld", 1U, 0x100U, 7U,
                                "NeoQ/Wall.DDS", found)
                  .ok() &&
              found.metadata()->source.binding.resource_state_count == 1U,
          "reload did not replace the exact pre-load state count");

  const std::vector<std::uint8_t> changed_bytes = {1U, 2U, 4U};
  const auto changed_reload =
      BuildReceipt(MakeArchiveInput(0x100U, 7U, 2U), changed_bytes);
  Require(!CommitOgre14AuthenticatedTextureReceipt(changed_reload, registry),
          "same-generation reload silently changed authenticated bytes");
  auto substituted_archive_input = MakeArchiveInput(0x100U, 7U, 2U);
  substituted_archive_input.archive_pointer_token = 0x901U;
  const auto substituted_archive =
      BuildReceipt(substituted_archive_input, bytes);
  Require(!CommitOgre14AuthenticatedTextureReceipt(
              substituted_archive, registry),
          "archive pointer substitution was accepted during reload");
  const auto pointer_reuse = BuildReceipt(
      MakeArchiveInput(0x100U, 8U, 2U, 1U, "CityWorld", "Other.DDS"), bytes);
  Require(!CommitOgre14AuthenticatedTextureReceipt(pointer_reuse, registry),
          "live pointer reuse with another handle/name was accepted");
  const auto handle_collision = BuildReceipt(
      MakeArchiveInput(0x101U, 7U, 0U, 1U, "CityWorld", "Second.DDS"), bytes);
  Require(!CommitOgre14AuthenticatedTextureReceipt(handle_collision, registry),
          "one live resource handle mapped to multiple pointers");

  const Ogre14AuthenticatedTextureReceiptRegistry owner = registry;
  Require(!RemoveOgre14AuthenticatedTextureResource(
               "CityWorld", 0x100U, 8U, "NeoQ/Wall.DDS", registry) &&
              registry.SharesImmutableStateWith(owner),
          "mismatched resource removal changed the live pointer binding");
  Require(RemoveOgre14AuthenticatedTextureResource(
              "CityWorld", 0x100U, 7U, "NeoQ/Wall.DDS", registry)
              .ok() &&
              registry.size() == 0U,
          "exact resource removal did not erase its receipt");
  Require(RemoveOgre14AuthenticatedTextureResource(
              "CityWorld", 0x100U, 7U, "NeoQ/Wall.DDS", registry)
              .ok(),
          "missing exact removal was not idempotent");
  Require(CommitOgre14AuthenticatedTextureReceipt(pointer_reuse, registry)
              .ok(),
          "pointer reuse remained blocked after exact resource removal");
}

void TestStaleGenerationResetTeardownAndGlobalMonotonicity() {
  Ogre14AuthenticatedTextureReceiptRegistry registry = MakeRegistry();
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("CityWorld", 10U,
                                                            registry)
              .ok(),
          "generation ten did not activate");
  const auto old = BuildReceipt(
      MakeArchiveInput(0x300U, 3U, 0U, 10U), {9U, 8U, 7U});
  Require(CommitOgre14AuthenticatedTextureReceipt(old, registry).ok(),
          "generation ten receipt did not commit");
  Require(!AdvanceOgre14AuthenticatedTextureGroupGeneration("Other", 9U,
                                                             registry),
          "cross-group generation regression was accepted");
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("Other", 11U,
                                                            registry)
              .ok(),
          "globally newer other-group generation was rejected");
  const auto other = BuildReceipt(
      MakeArchiveInput(0x400U, 4U, 0U, 11U, "Other", "Other.DDS"), {4U});
  Require(CommitOgre14AuthenticatedTextureReceipt(other, registry).ok() &&
              registry.size() == 2U,
          "second active group receipt did not commit");

  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("CityWorld", 12U,
                                                            registry)
              .ok() &&
              registry.size() == 1U &&
              registry.maximum_group_generation_seen() == 12U,
          "group reset did not clear only the target generation");
  Require(!CommitOgre14AuthenticatedTextureReceipt(old, registry),
          "stale receipt resurrected after group reset");
  const auto current = BuildReceipt(
      MakeArchiveInput(0x300U, 30U, 0U, 12U), {9U, 8U, 7U});
  Require(CommitOgre14AuthenticatedTextureReceipt(current, registry).ok(),
          "pointer reuse in a new authenticated generation was rejected");
  Require(TeardownOgre14AuthenticatedTextureGroup("CityWorld", 12U, registry)
                  .ok() &&
              registry.size() == 1U,
          "exact group teardown did not clear/deactivate its receipts");
  Require(!CommitOgre14AuthenticatedTextureReceipt(current, registry),
          "receipt committed into a torn-down generation");
  Require(!TeardownOgre14AuthenticatedTextureGroup("CityWorld", 12U,
                                                    registry),
          "duplicate group teardown was accepted");
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("CityWorld", 13U,
                                                            registry)
              .ok(),
          "new generation did not reactivate torn-down group");
}

void TestPreResourceTokensAndCaps() {
  Ogre14AuthenticatedTextureRegistryConfiguration configuration;
  configuration.maximum_live_receipts = 1U;
  configuration.maximum_source_bytes = 4U;
  configuration.maximum_retained_source_bytes = 4U;
  configuration.maximum_total_identity_bytes = 1024U;
  auto registry = MakeRegistry(configuration);
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("CityWorld", 1U,
                                                            registry)
              .ok(),
          "bounded registry group did not activate");
  auto pre_input = MakeArchiveInput();
  pre_input.binding.kind =
      Ogre14AuthenticatedTextureBindingKind::PRE_RESOURCE_TOKEN;
  pre_input.binding.resource_pointer_token = 0U;
  pre_input.binding.resource_handle = 0U;
  pre_input.binding.resource_state_count = 0U;
  pre_input.binding.pre_resource_token = 55U;
  const auto pre = BuildReceipt(pre_input, {1U, 2U}, configuration);
  Require(CommitOgre14AuthenticatedTextureReceipt(pre, registry).ok(),
          "exact pre-resource token did not commit");
  Require(!CommitOgre14AuthenticatedTextureReceipt(pre, registry),
          "duplicate pre-resource token was accepted");
  const auto second = BuildReceipt(MakeArchiveInput(0x501U), {3U},
                                   configuration);
  Require(!CommitOgre14AuthenticatedTextureReceipt(second, registry),
          "receipt count cap+1 was accepted");

  configuration.maximum_live_receipts = 2U;
  configuration.maximum_source_bytes = 2U;
  configuration.maximum_retained_source_bytes = 2U;
  registry = MakeRegistry(configuration);
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("CityWorld", 1U,
                                                            registry)
              .ok() &&
              CommitOgre14AuthenticatedTextureReceipt(pre, registry).ok(),
          "retained-byte cap fixture did not initialize");
  Require(!CommitOgre14AuthenticatedTextureReceipt(second, registry),
          "retained source byte cap+1 was accepted");

  const auto identity_probe =
      BuildReceipt(MakeArchiveInput(), std::vector<std::uint8_t>{1U});
  configuration = {};
  configuration.maximum_total_identity_bytes = identity_probe.identity_size();
  const auto identity_limited = BuildReceipt(
      MakeArchiveInput(), std::vector<std::uint8_t>{1U}, configuration);
  registry = MakeRegistry(configuration);
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("CityWorld", 1U,
                                                            registry)
                  .ok() &&
              !CommitOgre14AuthenticatedTextureReceipt(identity_limited,
                                                        registry),
          "retained identity byte cap+1 was accepted");

  configuration = {};
  configuration.maximum_group_records = 1U;
  registry = MakeRegistry(configuration);
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("First", 1U,
                                                            registry)
              .ok() &&
              !AdvanceOgre14AuthenticatedTextureGroupGeneration(
                  "Second", 2U, registry),
          "group-record cap+1 was accepted");
}

void TestRegistryMintedLoadedTextureResolutionAuthority() {
  using ResolutionAccess =
      RoR::Render::Testing::Ogre14AuthenticatedTextureResolutionTestAccess;
  const std::vector<std::uint8_t> bytes = {4U, 3U, 2U, 1U};
  const Ogre14AuthenticatedTextureReceipt committed_receipt =
      BuildReceipt(MakeArchiveInput(), bytes);
  const Ogre14AuthenticatedTextureReceipt build_only_forgery =
      BuildReceipt(MakeArchiveInput(), bytes);
  Require(!committed_receipt.SharesImmutableStateWith(build_only_forgery),
          "independent Build receipts unexpectedly shared authority");

  Ogre14AuthenticatedTextureReceiptRegistry registry = MakeRegistry();
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration(
              "CityWorld", 1U, registry)
                  .ok() &&
              CommitOgre14AuthenticatedTextureReceipt(committed_receipt,
                                                       registry)
                  .ok(),
          "loaded resolution registry fixture did not initialize");
  DummyTextureResolver resolver;
  DummyTextureResolver substitute_resolver;
  Ogre14AuthenticatedTextureResolution resolution;
  Require(ResolutionAccess::Mint(
              registry, "CityWorld", 1U, 0x100U, 7U,
              "NeoQ/Wall.DDS", 1U, resolver, resolution)
                  .ok() &&
              resolution.initialized() &&
              resolution.version() ==
                  kOgre14AuthenticatedTextureResolutionVersion &&
              resolution.loaded_resource_state_count() == 1U &&
              resolution.source_receipt() != nullptr &&
              resolution.source_receipt()->SharesImmutableStateWith(
                  committed_receipt) &&
              !resolution.source_receipt()->SharesImmutableStateWith(
                  build_only_forgery),
          "registry mint did not select only its exact committed receipt owner");
  Require(resolution.MatchesResolver(resolver) &&
              !resolution.MatchesResolver(substitute_resolver) &&
              resolution.MatchesLoadedResourceIdentity(
                  0x100U, 7U, "CityWorld", "NeoQ/Wall.DDS", 1U) &&
              !resolution.MatchesLoadedResourceIdentity(
                  0x101U, 7U, "CityWorld", "NeoQ/Wall.DDS", 1U) &&
              !resolution.MatchesLoadedResourceIdentity(
                  0x100U, 8U, "CityWorld", "NeoQ/Wall.DDS", 1U) &&
              !resolution.MatchesLoadedResourceIdentity(
                  0x100U, 7U, "Other", "NeoQ/Wall.DDS", 1U) &&
              !resolution.MatchesLoadedResourceIdentity(
                  0x100U, 7U, "CityWorld", "neoq/wall.dds", 1U) &&
              !resolution.MatchesLoadedResourceIdentity(
                  0x100U, 7U, "CityWorld", "NeoQ/Wall.DDS", 2U),
          "loaded resolution did not bind exact resolver/pointer/handle/group/name/state");
  Require(ResolutionAccess::Revalidate(
              registry, resolution, resolver, 0x100U, 7U, "CityWorld",
              "NeoQ/Wall.DDS", 1U) &&
              !ResolutionAccess::Revalidate(
                  registry, resolution, substitute_resolver, 0x100U, 7U,
                  "CityWorld", "NeoQ/Wall.DDS", 1U) &&
              !ResolutionAccess::Revalidate(
                  registry, resolution, resolver, 0x101U, 7U, "CityWorld",
                  "NeoQ/Wall.DDS", 1U) &&
              !ResolutionAccess::Revalidate(
                  registry, resolution, resolver, 0x100U, 8U, "CityWorld",
                  "NeoQ/Wall.DDS", 1U) &&
              !ResolutionAccess::Revalidate(
                  registry, resolution, resolver, 0x100U, 7U, "Other",
                  "NeoQ/Wall.DDS", 1U) &&
              !ResolutionAccess::Revalidate(
                  registry, resolution, resolver, 0x100U, 7U, "CityWorld",
                  "neoq/wall.dds", 1U) &&
              !ResolutionAccess::Revalidate(
                  registry, resolution, resolver, 0x100U, 7U, "CityWorld",
                  "NeoQ/Wall.DDS", 2U),
          "registry authority accepted a resolver/resource substitution or stale state");

  const Ogre14AuthenticatedTextureResolution copied_resolution = resolution;
  Require(copied_resolution.source_receipt() != nullptr &&
              copied_resolution.source_receipt()->SharesImmutableStateWith(
                  committed_receipt) &&
              ResolutionAccess::Revalidate(
                  registry, copied_resolution, resolver, 0x100U, 7U,
                  "CityWorld", "NeoQ/Wall.DDS", 1U),
          "copying an authentic resolution lost its exact control blocks");

  Ogre14AuthenticatedTextureReceiptRegistry equivalent_but_wrong =
      MakeRegistry();
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration(
              "CityWorld", 1U, equivalent_but_wrong)
                  .ok() &&
              CommitOgre14AuthenticatedTextureReceipt(
                  build_only_forgery, equivalent_but_wrong)
                  .ok() &&
              !ResolutionAccess::Revalidate(
                  equivalent_but_wrong, resolution, resolver, 0x100U, 7U,
                  "CityWorld", "NeoQ/Wall.DDS", 1U),
          "equivalent synthetic registry substituted for the minting snapshot");

  ThrowingFault fault;
  fault.stage = Ogre14AuthenticatedTextureTransactionStage::
      BEFORE_RESOLUTION_COMMIT;
  Ogre14AuthenticatedTextureResolution sentinel = resolution;
  ValidationResult result = ResolutionAccess::Mint(
      registry, "CityWorld", 1U, 0x100U, 7U, "NeoQ/Wall.DDS", 1U,
      resolver, sentinel, &fault);
  Require(!result && result.field == "texture_resolution.allocation" &&
              sentinel.source_receipt() != nullptr &&
              sentinel.source_receipt()->SharesImmutableStateWith(
                  committed_receipt),
          "bad_alloc during resolution mint changed output authority");
  fault.bad_allocation = false;
  result = ResolutionAccess::Mint(
      registry, "CityWorld", 1U, 0x100U, 7U, "NeoQ/Wall.DDS", 1U,
      resolver, sentinel, &fault);
  Require(!result && result.field == "texture_resolution.exception" &&
              sentinel.source_receipt() != nullptr &&
              sentinel.source_receipt()->SharesImmutableStateWith(
                  committed_receipt),
          "unexpected resolution-mint exception changed output authority");

  const auto unrelated = BuildReceipt(
      MakeArchiveInput(0x500U, 50U, 0U, 2U, "Other", "Other.dds"),
      {9U});
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration(
              "Other", 2U, registry)
                  .ok() &&
              CommitOgre14AuthenticatedTextureReceipt(unrelated, registry)
                  .ok() &&
              !ResolutionAccess::Revalidate(
                  registry, resolution, resolver, 0x100U, 7U, "CityWorld",
                  "NeoQ/Wall.DDS", 1U),
          "old proof survived an unrelated immutable-registry publication");
  Ogre14AuthenticatedTextureResolution refreshed;
  Require(ResolutionAccess::Mint(
              registry, "CityWorld", 1U, 0x100U, 7U,
              "NeoQ/Wall.DDS", 1U, resolver, refreshed)
                  .ok() &&
              ResolutionAccess::Revalidate(
                  registry, refreshed, resolver, 0x100U, 7U, "CityWorld",
                  "NeoQ/Wall.DDS", 1U),
          "fresh resolve did not recover after unrelated strict invalidation");

  const auto reload_receipt =
      BuildReceipt(MakeArchiveInput(0x100U, 7U, 1U), bytes);
  Require(CommitOgre14AuthenticatedTextureReceipt(reload_receipt, registry)
                  .ok() &&
              !ResolutionAccess::Revalidate(
                  registry, refreshed, resolver, 0x100U, 7U, "CityWorld",
                  "NeoQ/Wall.DDS", 1U),
          "reload did not invalidate the prior source resolution");
  Ogre14AuthenticatedTextureResolution reload_resolution;
  Require(ResolutionAccess::Mint(
              registry, "CityWorld", 1U, 0x100U, 7U,
              "NeoQ/Wall.DDS", 2U, resolver, reload_resolution)
                  .ok() &&
              !ResolutionAccess::Mint(
                  registry, "CityWorld", 1U, 0x100U, 7U,
                  "NeoQ/Wall.DDS", 1U, resolver, sentinel) &&
              ResolutionAccess::Revalidate(
                  registry, reload_resolution, resolver, 0x100U, 7U,
                  "CityWorld", "NeoQ/Wall.DDS", 2U),
          "exact preload+1 reload state contract was not enforced");
  Require(TeardownOgre14AuthenticatedTextureGroup(
              "CityWorld", 1U, registry)
                  .ok() &&
              !ResolutionAccess::Revalidate(
                  registry, reload_resolution, resolver, 0x100U, 7U,
                  "CityWorld", "NeoQ/Wall.DDS", 2U),
          "group teardown did not invalidate its loaded texture proof");
}

void TestTransactionalRollback() {
  const std::vector<std::uint8_t> bytes = {1U, 2U, 3U};
  const auto sentinel = BuildReceipt(MakeArchiveInput(), bytes);
  Ogre14AuthenticatedTextureReceipt output = sentinel;
  ThrowingFault fault;
  ValidationResult result = BuildOgre14AuthenticatedTextureReceipt(
      Ogre14AuthenticatedTextureRegistryConfiguration{}, MakeArchiveInput(),
      bytes.data(), bytes.size(), output, &fault);
  Require(!result && result.field == "texture_receipt.allocation" &&
              output.SharesImmutableStateWith(sentinel),
          "bad_alloc changed immutable receipt output");
  fault.stage =
      Ogre14AuthenticatedTextureTransactionStage::BEFORE_RECEIPT_COMMIT;
  fault.bad_allocation = false;
  result = BuildOgre14AuthenticatedTextureReceipt(
      Ogre14AuthenticatedTextureRegistryConfiguration{}, MakeArchiveInput(),
      bytes.data(), bytes.size(), output, &fault);
  Require(!result && result.field == "texture_receipt.exception" &&
              output.SharesImmutableStateWith(sentinel),
          "unexpected build exception changed immutable receipt output");

  auto registry = MakeRegistry();
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration("CityWorld", 1U,
                                                            registry)
              .ok() &&
              CommitOgre14AuthenticatedTextureReceipt(sentinel, registry)
                  .ok(),
          "rollback registry fixture did not initialize");
  const auto registry_owner = registry;
  const auto second = BuildReceipt(
      MakeArchiveInput(0x999U, 99U, 0U, 1U, "CityWorld", "Second.DDS"),
      bytes);
  fault.stage =
      Ogre14AuthenticatedTextureTransactionStage::BEFORE_REGISTRY_COMMIT;
  fault.bad_allocation = true;
  result = CommitOgre14AuthenticatedTextureReceipt(second, registry, &fault);
  Require(!result && result.field ==
                         "texture_registry.receipt_commit.allocation" &&
              registry.SharesImmutableStateWith(registry_owner) &&
              registry.size() == 1U,
          "bad_alloc changed committed receipt registry");
  fault.bad_allocation = false;
  result = CommitOgre14AuthenticatedTextureReceipt(second, registry, &fault);
  Require(!result && result.field ==
                         "texture_registry.receipt_commit.exception" &&
              registry.SharesImmutableStateWith(registry_owner) &&
              registry.size() == 1U,
          "unexpected receipt commit exception changed registry owner");
  result = RemoveOgre14AuthenticatedTextureResource(
      "CityWorld", 0x100U, 7U, "NeoQ/Wall.DDS", registry, &fault);
  Require(!result && result.field ==
                         "texture_registry.resource_remove.exception" &&
              registry.SharesImmutableStateWith(registry_owner),
          "unexpected removal exception changed registry owner");

  fault.stage = Ogre14AuthenticatedTextureTransactionStage::
      BEFORE_GROUP_TRANSITION_COMMIT;
  fault.bad_allocation = true;
  result = AdvanceOgre14AuthenticatedTextureGroupGeneration(
      "CityWorld", 2U, registry, &fault);
  Require(!result && result.field ==
                         "texture_registry.group_transition.allocation" &&
              registry.SharesImmutableStateWith(registry_owner) &&
              registry.maximum_group_generation_seen() == 1U,
          "failed group transition changed registry generation/owner");
  fault.bad_allocation = false;
  result = AdvanceOgre14AuthenticatedTextureGroupGeneration(
      "CityWorld", 2U, registry, &fault);
  Require(!result && result.field ==
                         "texture_registry.group_transition.exception" &&
              registry.SharesImmutableStateWith(registry_owner) &&
              registry.maximum_group_generation_seen() == 1U,
          "unexpected group transition exception changed registry owner");
  fault.bad_allocation = true;
  result = TeardownOgre14AuthenticatedTextureGroup("CityWorld", 1U, registry,
                                                    &fault);
  Require(!result && result.field ==
                         "texture_registry.group_teardown.allocation" &&
              registry.SharesImmutableStateWith(registry_owner),
          "failed group teardown changed registry owner");
  fault.bad_allocation = false;
  result = TeardownOgre14AuthenticatedTextureGroup("CityWorld", 1U, registry,
                                                    &fault);
  Require(!result && result.field ==
                         "texture_registry.group_teardown.exception" &&
              registry.SharesImmutableStateWith(registry_owner),
          "unexpected group teardown exception changed registry owner");
}

void TestAuthenticatedArchiveMountFaultRollbackAndTeardownModel() {
  struct MountState final {
    std::uint64_t generation = 41U;
    std::uint64_t retained_bytes = 3U;
    std::vector<std::string> archive_names{"already-mounted"};
    std::vector<std::shared_ptr<const std::vector<std::uint8_t>>>
        active_owners{std::make_shared<const std::vector<std::uint8_t>>(
            std::initializer_list<std::uint8_t>{1U, 2U, 3U})};
    std::shared_ptr<const std::vector<std::uint8_t>> pending_owner;
    bool embedded_zip_factory_entry = false;
    bool resource_location_and_manager_entry = false;
    bool rollback_cleanup_succeeds = true;
    bool process_terminal = false;
  };

  const auto exercise_mount = [](
                                  MountState &state,
                                  IOgre14AuthenticatedArchiveMountFaultInjector
                                      *fault_injector,
    bool &caught) {
    if (state.process_terminal) {
      caught = true;
      return;
    }
    auto candidate_archive_names = state.archive_names;
    candidate_archive_names.push_back("new-authenticated-archive");
    const std::uint64_t candidate_generation = state.generation + 1U;
    state.pending_owner =
        std::make_shared<const std::vector<std::uint8_t>>(
            std::initializer_list<std::uint8_t>{4U, 5U, 6U, 7U});
    state.retained_bytes += state.pending_owner->size();
    auto candidate_active_owners = state.active_owners;
    candidate_active_owners.push_back(state.pending_owner);
    try {
      state.embedded_zip_factory_entry = true;
      MaybeInjectOgre14AuthenticatedArchiveMountFault(
          Ogre14AuthenticatedArchiveMountStage::
              AFTER_EMBEDDED_ZIP_REGISTRATION,
          fault_injector);
      state.resource_location_and_manager_entry = true;
      MaybeInjectOgre14AuthenticatedArchiveMountFault(
          Ogre14AuthenticatedArchiveMountStage::
              AFTER_RESOURCE_LOCATION_INSERTION,
          fault_injector);
      const bool pointer_bound_candidate = true;
      (void)pointer_bound_candidate;
      MaybeInjectOgre14AuthenticatedArchiveMountFault(
          Ogre14AuthenticatedArchiveMountStage::
              BEFORE_POINTER_BOUND_STATE_SWAP,
          fault_injector);

      // This models ContentManager's allocation-free publication sequence.
      state.archive_names.swap(candidate_archive_names);
      state.active_owners.swap(candidate_active_owners);
      state.generation = candidate_generation;
      state.pending_owner.reset();
    } catch (...) {
      caught = true;
      if (!state.rollback_cleanup_succeeds) {
        // ContentManager calls std::terminate() here. The model records that
        // terminal boundary so the owner/accounting invariant remains
        // inspectable without terminating the test process.
        state.process_terminal = true;
        return;
      }
      state.resource_location_and_manager_entry = false;
      state.embedded_zip_factory_entry = false;
      state.retained_bytes -= state.pending_owner->size();
      state.pending_owner.reset();
    }
  };

  for (const auto stage : {
           Ogre14AuthenticatedArchiveMountStage::
               AFTER_EMBEDDED_ZIP_REGISTRATION,
           Ogre14AuthenticatedArchiveMountStage::
               AFTER_RESOURCE_LOCATION_INSERTION,
           Ogre14AuthenticatedArchiveMountStage::
               BEFORE_POINTER_BOUND_STATE_SWAP}) {
    for (const bool bad_allocation : {true, false}) {
      MountState state;
      const auto *const prior_owner = state.active_owners.front().get();
      ThrowingArchiveMountFault fault;
      fault.stage = stage;
      fault.bad_allocation = bad_allocation;
      bool caught = false;
      exercise_mount(state, &fault, caught);
      Require(caught && state.generation == 41U &&
                  state.archive_names ==
                      std::vector<std::string>{"already-mounted"} &&
                  state.active_owners.size() == 1U &&
                  state.active_owners.front().get() == prior_owner &&
                  state.retained_bytes == 3U &&
                  !state.pending_owner &&
                  !state.embedded_zip_factory_entry &&
                  !state.resource_location_and_manager_entry,
              "mount fault changed prior generation/maps/retained-byte "
              "accounting or left an OGRE registry entry");
    }
  }

  MountState terminal;
  terminal.rollback_cleanup_succeeds = false;
  ThrowingArchiveMountFault terminal_fault;
  terminal_fault.stage = Ogre14AuthenticatedArchiveMountStage::
      AFTER_RESOURCE_LOCATION_INSERTION;
  terminal_fault.bad_allocation = false;
  bool terminal_caught = false;
  exercise_mount(terminal, &terminal_fault, terminal_caught);
  const auto terminal_pending_owner = terminal.pending_owner;
  Require(terminal_caught && terminal.process_terminal &&
              terminal.generation == 41U &&
              terminal.archive_names ==
                  std::vector<std::string>{"already-mounted"} &&
              terminal.retained_bytes == 7U && terminal.pending_owner &&
              terminal.embedded_zip_factory_entry &&
              terminal.resource_location_and_manager_entry,
          "unrecoverable mount cleanup did not retain exact bytes and enter "
          "the process-terminal boundary");
  terminal_caught = false;
  exercise_mount(terminal, nullptr, terminal_caught);
  Require(terminal_caught && terminal.process_terminal &&
              terminal.pending_owner == terminal_pending_owner &&
              terminal.generation == 41U && terminal.retained_bytes == 7U,
          "process-terminal mount state admitted a recoverable retry");

  MountState successful;
  std::weak_ptr<const std::vector<std::uint8_t>> mounted_owner;
  bool caught = false;
  exercise_mount(successful, nullptr, caught);
  mounted_owner = successful.active_owners.back();
  Require(!caught && successful.generation == 42U &&
              successful.retained_bytes == 7U &&
              successful.archive_names ==
                  std::vector<std::string>{"already-mounted",
                                           "new-authenticated-archive"} &&
              successful.embedded_zip_factory_entry &&
              successful.resource_location_and_manager_entry &&
              !successful.pending_owner,
          "successful authenticated archive mount did not publish atomically");

  // This models the mandatory teardown ordering: remove the ResourceGroup /
  // ArchiveManager location, erase the EmbeddedZip factory entry, update
  // accounting, then release the immutable owner.
  successful.resource_location_and_manager_entry = false;
  successful.embedded_zip_factory_entry = false;
  successful.retained_bytes -= successful.active_owners.back()->size();
  successful.active_owners.pop_back();
  Require(!successful.resource_location_and_manager_entry &&
              !successful.embedded_zip_factory_entry &&
              successful.retained_bytes == 3U && mounted_owner.expired(),
          "successful teardown retained manager/factory state or immutable "
          "archive bytes");
}

} // namespace

int main() {
  static_assert(
      std::is_nothrow_copy_constructible_v<
          Ogre14AuthenticatedTextureReceipt> &&
          std::is_nothrow_copy_assignable_v<
              Ogre14AuthenticatedTextureReceipt> &&
          std::is_nothrow_move_constructible_v<
              Ogre14AuthenticatedTextureReceipt> &&
          std::is_nothrow_move_assignable_v<
              Ogre14AuthenticatedTextureReceipt> &&
          std::is_nothrow_copy_constructible_v<
              Ogre14AuthenticatedTextureReceiptRegistry> &&
          std::is_nothrow_copy_assignable_v<
              Ogre14AuthenticatedTextureReceiptRegistry> &&
          std::is_nothrow_move_constructible_v<
              Ogre14AuthenticatedTextureReceiptRegistry> &&
          std::is_nothrow_move_assignable_v<
              Ogre14AuthenticatedTextureReceiptRegistry> &&
          std::is_nothrow_copy_constructible_v<
              Ogre14AuthenticatedTextureResolution> &&
          std::is_nothrow_copy_assignable_v<
              Ogre14AuthenticatedTextureResolution> &&
          std::is_nothrow_move_constructible_v<
              Ogre14AuthenticatedTextureResolution> &&
          std::is_nothrow_move_assignable_v<
              Ogre14AuthenticatedTextureResolution>,
      "immutable texture receipt, registry, and resolution snapshots must publish noexcept");
  TestExactBytesHashAndArchiveIdentity();
  TestDdsHeaderFactsAndGeneratedRule();
  TestArchiveMemberSelectionCollisionAndCaps();
  TestInputAndConfigurationValidation();
  TestRegistryCollisionReloadRemovalAndPointerReuse();
  TestStaleGenerationResetTeardownAndGlobalMonotonicity();
  TestPreResourceTokensAndCaps();
  TestRegistryMintedLoadedTextureResolutionAuthority();
  TestTransactionalRollback();
  TestAuthenticatedArchiveMountFaultRollbackAndTeardownModel();
  std::cout << "OGRE 14 authenticated texture receipt tests passed\n";
  return EXIT_SUCCESS;
}
