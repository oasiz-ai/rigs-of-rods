/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "ManagedMaterialDeclaration.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

[[noreturn]] void Fail(const std::string &message) {
  std::cerr << "ManagedMaterialDeclarationTests: " << message << '\n';
  std::exit(1);
}

void Require(bool condition, const std::string &message) {
  if (!condition) {
    Fail(message);
  }
}

void RequireOk(const ValidationResult &result, const std::string &context) {
  if (!result) {
    Fail(context + ": " + result.field + ": " + result.detail);
  }
}

std::vector<std::uint8_t> Bytes(std::uint8_t seed,
                                std::size_t count = 32U) {
  std::vector<std::uint8_t> bytes(count);
  for (std::size_t index = 0U; index < count; ++index) {
    bytes[index] = static_cast<std::uint8_t>(seed + index * 13U);
  }
  return bytes;
}

std::string HexDigest(const RenderPayloadDigest &digest) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string text;
  text.reserve(digest.size() * 2U);
  for (const std::uint8_t byte : digest) {
    text.push_back(kHex[(byte >> 4U) & 0xFU]);
    text.push_back(kHex[byte & 0xFU]);
  }
  return text;
}

ManagedMaterialTextureSourceIdentity SourceIdentity(
    ManagedMaterialSourceTrust trust, const std::string &group,
    std::uint64_t group_generation, const std::string &resource,
    const std::vector<std::uint8_t> &bytes,
    const std::string &member_suffix = "") {
  ManagedMaterialTextureSourceIdentity identity;
  identity.trust = trust;
  identity.group_generation = group_generation;
  identity.effective_resource_group = group;
  identity.exact_resource_name = resource;
  identity.byte_count = static_cast<std::uint64_t>(bytes.size());
  identity.source_sha256 =
      ComputeRenderPayloadDigest(bytes.data(), bytes.size());
  switch (trust) {
  case ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES:
  case ManagedMaterialSourceTrust::OBSERVED_SELECTED_SOURCE:
    identity.archive_name = "ordinary-fixture.zip";
    identity.archive_type = "Zip";
    identity.exact_member_name = "textures/" + resource + member_suffix;
    break;
  case ManagedMaterialSourceTrust::AUTHENTICATED_ARCHIVE_MEMBER:
    identity.archive_identity = "fixture-auth-archive";
    identity.archive_name = "authenticated-fixture.zip";
    identity.archive_type = "EmbeddedZip";
    identity.archive_sha256 = HexDigest(ComputeRenderPayloadDigest(
        reinterpret_cast<const std::uint8_t *>("archive"), 7U));
    identity.exact_member_name = "textures/" + resource + member_suffix;
    break;
  case ManagedMaterialSourceTrust::AUTHENTICATED_GENERATED_FALLBACK:
    identity.archive_sha256 = HexDigest(ComputeRenderPayloadDigest(
        reinterpret_cast<const std::uint8_t *>("authority"), 9U));
    identity.generated_rule = "fixture-generated-source-v1";
    identity.generated_rule_version = 1U;
    break;
  }
  return identity;
}

ManagedMaterialTextureSourceReceipt MakeSource(
    ManagedMaterialSourceTrust trust, const std::string &group,
    std::uint64_t group_generation, const std::string &resource,
    std::uint8_t seed, const std::string &member_suffix = "") {
  const std::vector<std::uint8_t> bytes = Bytes(seed);
  const ManagedMaterialTextureSourceIdentity identity = SourceIdentity(
      trust, group, group_generation, resource, bytes, member_suffix);
  ManagedMaterialTextureSourceReceipt receipt;
  RequireOk(BuildManagedMaterialTextureSourceReceipt(
                ManagedMaterialDeclarationRegistryConfiguration{}, identity,
                bytes.data(), bytes.size(), receipt),
            "build source receipt");
  return receipt;
}

ManagedMaterialTextureBindingInput Binding(
    ManagedMaterialTextureSlot slot, const std::string &declared_name,
    const std::string &resolved_name, const std::string &group,
    const ManagedMaterialTextureSourceReceipt &receipt) {
  ManagedMaterialTextureBindingInput binding;
  binding.slot = slot;
  binding.configured = true;
  binding.declared_texture_name = declared_name;
  binding.resolved_texture_name = resolved_name;
  binding.effective_texture_name = resolved_name;
  binding.requested_resource_group = group;
  binding.effective_resource_group = group;
  binding.source_receipt = receipt;
  return binding;
}

ManagedMaterialTextureBindingInput EmptyBinding(
    ManagedMaterialTextureSlot slot) {
  ManagedMaterialTextureBindingInput binding;
  binding.slot = slot;
  return binding;
}

ManagedMaterialDeclarationInput DeclarationInput(
    std::uint64_t actor_generation, std::uint64_t definition_generation,
    const std::string &name, ManagedMaterialSemanticType declared_type,
    ManagedMaterialSemanticType resolved_type,
    const ManagedMaterialTextureBindingInput &diffuse,
    const ManagedMaterialTextureBindingInput &specular =
        EmptyBinding(ManagedMaterialTextureSlot::SPECULAR),
    const ManagedMaterialTextureBindingInput &damaged =
        EmptyBinding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE),
    bool double_sided = false, bool removed = false) {
  ManagedMaterialDeclarationInput input;
  input.actor_generation = actor_generation;
  input.definition_generation = definition_generation;
  input.exact_material_name = name;
  input.declared_type = declared_type;
  input.resolved_type = resolved_type;
  input.type_overridden_by_tuneup = declared_type != resolved_type;
  input.double_sided = double_sided;
  input.removed_by_tuneup = removed;
  input.textures = {diffuse, specular, damaged};
  return input;
}

ManagedMaterialDeclaration BuildDeclaration(
    const ManagedMaterialDeclarationInput &input) {
  ManagedMaterialDeclaration declaration;
  RequireOk(BuildManagedMaterialDeclaration(
                ManagedMaterialDeclarationRegistryConfiguration{}, input,
                declaration),
            "build declaration " + input.exact_material_name);
  return declaration;
}

class ThrowAt final : public IManagedMaterialDeclarationFaultInjector {
public:
  ManagedMaterialDeclarationTransactionStage stage =
      ManagedMaterialDeclarationTransactionStage::BEFORE_REGISTRY_COMMIT;

  void BeforeManagedMaterialDeclarationStage(
      ManagedMaterialDeclarationTransactionStage observed) override {
    if (observed == stage) {
      throw std::runtime_error("injected managed-material failure");
    }
  }
};

void TestAllTypesAndTuneupResolution() {
  const auto ordinary = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "vehicle", 1U, "body.png", 11U);
  const auto authenticated = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "addon", 2U,
      "body-modern.png", 21U);
  const auto generated = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "addon",
      2U, "specular-generated.dds", 31U);
  const auto damaged = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "vehicle", 1U, "body-damaged.png", 41U);

  const std::array<ManagedMaterialSemanticType, 4U> types{{
      ManagedMaterialSemanticType::FLEXMESH_STANDARD,
      ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT,
      ManagedMaterialSemanticType::MESH_STANDARD,
      ManagedMaterialSemanticType::MESH_TRANSPARENT,
  }};
  for (std::size_t index = 0U; index < types.size(); ++index) {
    const std::string name = "type-fixture-" + std::to_string(index);
    const bool flexmesh =
        types[index] == ManagedMaterialSemanticType::FLEXMESH_STANDARD ||
        types[index] == ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT;
    const auto input = DeclarationInput(
        7U, index + 1U, name, types[index], types[index],
        Binding(ManagedMaterialTextureSlot::DIFFUSE, "body.png", "body.png",
                "vehicle", ordinary),
        Binding(ManagedMaterialTextureSlot::SPECULAR,
                "specular-generated.dds", "specular-generated.dds", "addon",
                generated),
        flexmesh
            ? Binding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE,
                      "body-damaged.png", "body-damaged.png", "vehicle",
                      damaged)
            : EmptyBinding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE),
        (index % 2U) != 0U);
    const ManagedMaterialDeclaration declaration = BuildDeclaration(input);
    const auto *metadata = declaration.metadata();
    Require(metadata != nullptr && metadata->declared_type == types[index] &&
                metadata->resolved_type == types[index] &&
                metadata->double_sided == ((index % 2U) != 0U) &&
                declaration.source_receipt(
                    ManagedMaterialTextureSlot::DIFFUSE) != nullptr &&
                declaration.source_receipt(
                    ManagedMaterialTextureSlot::SPECULAR) != nullptr &&
                (declaration.source_receipt(
                     ManagedMaterialTextureSlot::DAMAGED_DIFFUSE) != nullptr) ==
                    flexmesh,
            "one exact managed-material type did not round trip");
  }

  const ManagedMaterialDeclaration overridden = BuildDeclaration(
      DeclarationInput(
          8U, 1U, "tuneup-replacement",
          ManagedMaterialSemanticType::FLEXMESH_STANDARD,
          ManagedMaterialSemanticType::MESH_TRANSPARENT,
          Binding(ManagedMaterialTextureSlot::DIFFUSE, "legacy-body.png",
                  "body-modern.png", "addon", authenticated),
          EmptyBinding(ManagedMaterialTextureSlot::SPECULAR),
          EmptyBinding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE), true));
  const auto *metadata = overridden.metadata();
  Require(metadata != nullptr && metadata->type_overridden_by_tuneup &&
              metadata->textures[0U].declared_texture_name ==
                  "legacy-body.png" &&
              metadata->textures[0U].resolved_texture_name ==
                  "body-modern.png" &&
              metadata->textures[0U].requested_resource_group == "addon" &&
              metadata->textures[0U].effective_resource_group == "addon" &&
              overridden.source_receipt(
                  ManagedMaterialTextureSlot::DIFFUSE)
                      ->identity()
                      ->trust == ManagedMaterialSourceTrust::
                                     CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
          "post-Tuneup type/media/group authority was not preserved");

  ManagedMaterialDeclaration invalid_mesh_damage;
  const ValidationResult invalid_mesh_damage_result =
      BuildManagedMaterialDeclaration(
          ManagedMaterialDeclarationRegistryConfiguration{},
          DeclarationInput(
              9U, 1U, "invalid-mesh-damage",
              ManagedMaterialSemanticType::FLEXMESH_STANDARD,
              ManagedMaterialSemanticType::MESH_STANDARD,
              Binding(ManagedMaterialTextureSlot::DIFFUSE, "body.png",
                      "body.png", "vehicle", ordinary),
              EmptyBinding(ManagedMaterialTextureSlot::SPECULAR),
              Binding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE,
                      "body-damaged.png", "body-damaged.png", "vehicle",
                      damaged)),
          invalid_mesh_damage);
  Require(!invalid_mesh_damage_result &&
              invalid_mesh_damage_result.code ==
                  ValidationCode::INVALID_ASSET_REFERENCE &&
              !invalid_mesh_damage.initialized(),
          "resolved mesh material accepted a flexmesh-only damaged slot");
}

void TestRemovedMaterialAndMissingSources() {
  ManagedMaterialTextureBindingInput removed_diffuse;
  removed_diffuse.slot = ManagedMaterialTextureSlot::DIFFUSE;
  removed_diffuse.configured = true;
  removed_diffuse.declared_texture_name = "legacy.png";
  removed_diffuse.resolved_texture_name = "removed-modern.png";
  removed_diffuse.requested_resource_group = "removed-addon";
  removed_diffuse.effective_texture_name.clear();
  removed_diffuse.effective_resource_group.clear();
  const ManagedMaterialDeclaration removed = BuildDeclaration(
      DeclarationInput(
          12U, 1U, "removed-by-tuneup",
          ManagedMaterialSemanticType::MESH_STANDARD,
          ManagedMaterialSemanticType::MESH_STANDARD, removed_diffuse,
          EmptyBinding(ManagedMaterialTextureSlot::SPECULAR),
          EmptyBinding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE), false,
          true));
  Require(removed.metadata() != nullptr &&
              removed.metadata()->removed_by_tuneup &&
              removed.metadata()->textures[0U].configured &&
              !removed.metadata()->textures[0U].source_receipt_present &&
              removed.source_receipt(
                  ManagedMaterialTextureSlot::DIFFUSE) == nullptr,
          "Tuneup removal did not retain names without claiming authority");

  ManagedMaterialDeclaration sentinel = removed;
  ManagedMaterialTextureBindingInput missing_diffuse = removed_diffuse;
  missing_diffuse.effective_texture_name =
      missing_diffuse.resolved_texture_name;
  missing_diffuse.effective_resource_group =
      missing_diffuse.requested_resource_group;
  auto missing = DeclarationInput(
      12U, 2U, "missing-source", ManagedMaterialSemanticType::MESH_STANDARD,
      ManagedMaterialSemanticType::MESH_STANDARD, missing_diffuse);
  const ValidationResult missing_result = BuildManagedMaterialDeclaration(
      ManagedMaterialDeclarationRegistryConfiguration{}, missing, sentinel);
  Require(!missing_result &&
              missing_result.code == ValidationCode::MISSING_REFERENCE &&
              sentinel.SharesImmutableStateWith(removed),
          "missing required source did not fail transactionally");

  const std::vector<std::uint8_t> bytes = Bytes(91U);
  auto identity = SourceIdentity(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "vehicle", 4U, "missing.png", bytes);
  ManagedMaterialTextureSourceReceipt receipt;
  const ValidationResult no_bytes = BuildManagedMaterialTextureSourceReceipt(
      ManagedMaterialDeclarationRegistryConfiguration{}, identity, nullptr, 0U,
      receipt);
  Require(!no_bytes && !receipt.initialized(),
          "missing source bytes minted a receipt");

  auto forged_authenticated = identity;
  forged_authenticated.trust =
      ManagedMaterialSourceTrust::AUTHENTICATED_ARCHIVE_MEMBER;
  forged_authenticated.archive_identity = "caller-forgery";
  forged_authenticated.archive_sha256 = HexDigest(
      ComputeRenderPayloadDigest(bytes.data(), bytes.size()));
  ManagedMaterialTextureSourceReceipt forged_output;
  const ValidationResult forged = BuildManagedMaterialTextureSourceReceipt(
      ManagedMaterialDeclarationRegistryConfiguration{}, forged_authenticated,
      bytes.data(), bytes.size(), forged_output);
  Require(!forged && forged.code == ValidationCode::UNSUPPORTED_FEATURE &&
              !forged_output.initialized(),
          "public neutral builder forged authenticated source trust");

  auto forged_observed = identity;
  forged_observed.trust = ManagedMaterialSourceTrust::OBSERVED_SELECTED_SOURCE;
  ManagedMaterialTextureSourceReceipt forged_observed_output;
  const ValidationResult observed = BuildManagedMaterialTextureSourceReceipt(
      ManagedMaterialDeclarationRegistryConfiguration{}, forged_observed,
      bytes.data(), bytes.size(), forged_observed_output);
  Require(!observed && observed.code == ValidationCode::UNSUPPORTED_FEATURE &&
              !forged_observed_output.initialized(),
          "public neutral builder forged observed selected-source trust");
}

void TestRegistryOrderSharingDuplicateAndCollision() {
  ManagedMaterialDeclarationRegistry registry;
  RequireOk(InitializeManagedMaterialDeclarationRegistry(
                ManagedMaterialDeclarationRegistryConfiguration{}, 50U,
                registry),
            "initialize registry");
  const auto shared = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "shared-group", 10U, "shared.png", 13U);

  const ManagedMaterialDeclaration zulu = BuildDeclaration(DeclarationInput(
      50U, 1U, "zulu", ManagedMaterialSemanticType::MESH_STANDARD,
      ManagedMaterialSemanticType::MESH_STANDARD,
      Binding(ManagedMaterialTextureSlot::DIFFUSE, "shared.png", "shared.png",
              "shared-group", shared)));
  const ManagedMaterialDeclaration alpha = BuildDeclaration(DeclarationInput(
      50U, 2U, "alpha", ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT,
      ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT,
      Binding(ManagedMaterialTextureSlot::DIFFUSE, "shared.png", "shared.png",
              "shared-group", shared),
      EmptyBinding(ManagedMaterialTextureSlot::SPECULAR),
      EmptyBinding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE), true));
  RequireOk(CommitManagedMaterialDeclaration(zulu, registry), "commit zulu");
  RequireOk(CommitManagedMaterialDeclaration(alpha, registry), "commit alpha");
  Require(registry.size() == 2U &&
              registry.retained_source_bytes() == shared.source_size(),
          "shared immutable source bytes were double-counted");

  ManagedMaterialDeclarationSnapshot snapshot;
  RequireOk(CaptureManagedMaterialDeclarationSnapshot(registry, snapshot),
            "capture ordered snapshot");
  Require(snapshot.size() == 2U && snapshot.at(0U)->metadata()->exact_material_name ==
                                      "alpha" &&
              snapshot.at(1U)->metadata()->exact_material_name == "zulu" &&
              snapshot.Find("zulu") != nullptr &&
              IsManagedMaterialDeclarationSnapshotCurrent(registry, snapshot),
          "snapshot order or current authority is nondeterministic");

  const ManagedMaterialDeclaration duplicate = BuildDeclaration(
      DeclarationInput(
          50U, 3U, "alpha", ManagedMaterialSemanticType::MESH_STANDARD,
          ManagedMaterialSemanticType::MESH_STANDARD,
          Binding(ManagedMaterialTextureSlot::DIFFUSE, "shared.png",
                  "shared.png", "shared-group", shared)));
  const ManagedMaterialDeclarationRegistry before_duplicate = registry;
  const ValidationResult duplicate_result =
      CommitManagedMaterialDeclaration(duplicate, registry);
  Require(!duplicate_result &&
              duplicate_result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              registry.SharesImmutableStateWith(before_duplicate),
          "duplicate managed material changed registry state");

  const std::vector<std::uint8_t> collision_bytes = Bytes(99U);
  auto collision_identity = *shared.identity();
  collision_identity.source_sha256 = ComputeRenderPayloadDigest(
      collision_bytes.data(), collision_bytes.size());
  collision_identity.byte_count = collision_bytes.size();
  ManagedMaterialTextureSourceReceipt collision_source;
  RequireOk(BuildManagedMaterialTextureSourceReceipt(
                ManagedMaterialDeclarationRegistryConfiguration{},
                collision_identity, collision_bytes.data(),
                collision_bytes.size(), collision_source),
            "build colliding source");
  const ManagedMaterialDeclaration collision = BuildDeclaration(
      DeclarationInput(
          50U, 3U, "collision", ManagedMaterialSemanticType::MESH_STANDARD,
          ManagedMaterialSemanticType::MESH_STANDARD,
          Binding(ManagedMaterialTextureSlot::DIFFUSE, "shared.png",
                  "shared.png", "shared-group", collision_source)));
  const ValidationResult collision_result =
      CommitManagedMaterialDeclaration(collision, registry);
  Require(!collision_result &&
              collision_result.code == ValidationCode::REVISION_MISMATCH &&
              registry.SharesImmutableStateWith(before_duplicate),
          "same-generation source identity collision was published");
}

void TestTransactionsReloadAndRevocation() {
  const auto source = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "auth", 20U,
      "body.png", 4U);
  const ManagedMaterialDeclaration first = BuildDeclaration(DeclarationInput(
      100U, 1U, "body", ManagedMaterialSemanticType::FLEXMESH_STANDARD,
      ManagedMaterialSemanticType::FLEXMESH_STANDARD,
      Binding(ManagedMaterialTextureSlot::DIFFUSE, "body.png", "body.png",
              "auth", source)));

  ManagedMaterialDeclarationRegistry registry;
  RequireOk(InitializeManagedMaterialDeclarationRegistry(
                ManagedMaterialDeclarationRegistryConfiguration{}, 100U,
                registry),
            "initialize transaction registry");
  ThrowAt fault;
  const ManagedMaterialDeclarationRegistry empty_owner = registry;
  const ValidationResult failed_commit =
      CommitManagedMaterialDeclaration(first, registry, &fault);
  Require(!failed_commit && registry.SharesImmutableStateWith(empty_owner) &&
              registry.size() == 0U,
          "faulted declaration commit partially published");
  RequireOk(CommitManagedMaterialDeclaration(first, registry),
            "retry declaration commit");

  ManagedMaterialDeclarationSnapshot current;
  RequireOk(CaptureManagedMaterialDeclarationSnapshot(registry, current),
            "capture pre-reload snapshot");
  ManagedMaterialDeclarationSnapshot sentinel = current;
  fault.stage =
      ManagedMaterialDeclarationTransactionStage::BEFORE_SNAPSHOT_COMMIT;
  const ValidationResult failed_snapshot =
      CaptureManagedMaterialDeclarationSnapshot(registry, sentinel, &fault);
  Require(!failed_snapshot && sentinel.SharesImmutableStateWith(current),
          "faulted snapshot capture changed output");

  fault.stage = ManagedMaterialDeclarationTransactionStage::
      BEFORE_GENERATION_TRANSITION_COMMIT;
  const ManagedMaterialDeclarationRegistry pre_reset = registry;
  const ValidationResult failed_reset =
      ResetManagedMaterialDeclarationRegistry(100U, 101U, registry, &fault);
  Require(!failed_reset && registry.SharesImmutableStateWith(pre_reset) &&
              IsManagedMaterialDeclarationSnapshotCurrent(registry, current),
          "faulted reload changed live actor authority");
  RequireOk(ResetManagedMaterialDeclarationRegistry(100U, 101U, registry),
            "reload registry");
  Require(!IsManagedMaterialDeclarationSnapshotCurrent(registry, current) &&
              registry.actor_generation() == 101U && registry.size() == 0U,
          "reload left prior snapshot authoritative");

  const auto reloaded_source = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "auth", 21U,
      "body.png", 5U, "-reload");
  const ManagedMaterialDeclaration reloaded = BuildDeclaration(
      DeclarationInput(
          101U, 1U, "body", ManagedMaterialSemanticType::FLEXMESH_STANDARD,
          ManagedMaterialSemanticType::FLEXMESH_STANDARD,
          Binding(ManagedMaterialTextureSlot::DIFFUSE, "body.png", "body.png",
                  "auth", reloaded_source)));
  RequireOk(CommitManagedMaterialDeclaration(reloaded, registry),
            "commit reloaded declaration");
  ManagedMaterialDeclarationSnapshot reloaded_snapshot;
  RequireOk(CaptureManagedMaterialDeclarationSnapshot(registry,
                                                       reloaded_snapshot),
            "capture reloaded snapshot");
  Require(reloaded_snapshot.Find("body")
                  ->source_receipt(ManagedMaterialTextureSlot::DIFFUSE)
                  ->identity()
                  ->source_sha256 !=
              current.Find("body")
                  ->source_receipt(ManagedMaterialTextureSlot::DIFFUSE)
                  ->identity()
                  ->source_sha256,
          "source reload reused stale byte identity");

  Require(RevokeManagedMaterialDeclarationRegistry(101U, registry),
          "revoke actor source authority");
  Require(!registry.active() && registry.size() == 0U &&
              !IsManagedMaterialDeclarationSnapshotCurrent(
                  registry, reloaded_snapshot) &&
              reloaded_snapshot.Find("body") != nullptr,
          "revocation did not close live authority while retaining in-flight ownership");
}

void TestSourceAndDeclarationFaultRollback() {
  const std::vector<std::uint8_t> bytes = Bytes(61U);
  const auto identity = SourceIdentity(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "ordinary", 30U, "fault.png", bytes);
  const auto sentinel = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "ordinary", 30U, "sentinel.png", 60U);
  ManagedMaterialTextureSourceReceipt output = sentinel;
  ThrowAt fault;
  fault.stage =
      ManagedMaterialDeclarationTransactionStage::AFTER_SOURCE_BYTES_COPIED;
  ValidationResult result = BuildManagedMaterialTextureSourceReceipt(
      ManagedMaterialDeclarationRegistryConfiguration{}, identity,
      bytes.data(), bytes.size(), output, &fault);
  Require(!result && output.SharesImmutableStateWith(sentinel),
          "source copy fault changed receipt output");
  fault.stage = ManagedMaterialDeclarationTransactionStage::
      BEFORE_SOURCE_RECEIPT_COMMIT;
  result = BuildManagedMaterialTextureSourceReceipt(
      ManagedMaterialDeclarationRegistryConfiguration{}, identity,
      bytes.data(), bytes.size(), output, &fault);
  Require(!result && output.SharesImmutableStateWith(sentinel),
          "source publication fault changed receipt output");

  ManagedMaterialTextureSourceReceipt source;
  RequireOk(BuildManagedMaterialTextureSourceReceipt(
                ManagedMaterialDeclarationRegistryConfiguration{}, identity,
                bytes.data(), bytes.size(), source),
            "build source after fault");
  const ManagedMaterialDeclarationInput input = DeclarationInput(
      200U, 1U, "faulted-declaration",
      ManagedMaterialSemanticType::MESH_TRANSPARENT,
      ManagedMaterialSemanticType::MESH_TRANSPARENT,
      Binding(ManagedMaterialTextureSlot::DIFFUSE, "fault.png", "fault.png",
              "ordinary", source));
  const ManagedMaterialDeclaration declaration_sentinel = BuildDeclaration(
      DeclarationInput(
          201U, 1U, "sentinel", ManagedMaterialSemanticType::MESH_STANDARD,
          ManagedMaterialSemanticType::MESH_STANDARD,
          Binding(ManagedMaterialTextureSlot::DIFFUSE, "sentinel.png",
                  "sentinel.png", "ordinary", sentinel)));
  ManagedMaterialDeclaration declaration_output = declaration_sentinel;
  fault.stage =
      ManagedMaterialDeclarationTransactionStage::BEFORE_DECLARATION_COMMIT;
  result = BuildManagedMaterialDeclaration(
      ManagedMaterialDeclarationRegistryConfiguration{}, input,
      declaration_output, &fault);
  Require(!result && declaration_output.SharesImmutableStateWith(
                         declaration_sentinel),
          "declaration fault changed output");
}

void TestExactEightMaterialFixture() {
  ManagedMaterialDeclarationRegistry registry;
  RequireOk(InitializeManagedMaterialDeclarationRegistry(
                ManagedMaterialDeclarationRegistryConfiguration{}, 800U,
                registry),
            "initialize eight-material fixture");
  const auto ordinary = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "fixture-vehicle", 70U, "shared-base.png", 71U);
  const auto authenticated = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "fixture-addon", 71U, "shared-specular.png", 72U);
  const auto damaged = MakeSource(
      ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES,
      "fixture-vehicle", 70U, "shared-damaged.png", 73U);
  const std::array<ManagedMaterialSemanticType, 4U> types{{
      ManagedMaterialSemanticType::FLEXMESH_STANDARD,
      ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT,
      ManagedMaterialSemanticType::MESH_STANDARD,
      ManagedMaterialSemanticType::MESH_TRANSPARENT,
  }};
  const std::array<const char *, 8U> names{{
      "fixture-panel-a", "fixture-panel-b", "fixture-panel-c",
      "fixture-panel-d", "fixture-trim-a",  "fixture-trim-b",
      "fixture-glass-a", "fixture-glass-b",
  }};
  for (std::size_t index = 0U; index < names.size(); ++index) {
    const ManagedMaterialSemanticType type = types[index % types.size()];
    const bool flexmesh =
        type == ManagedMaterialSemanticType::FLEXMESH_STANDARD ||
        type == ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT;
    const bool authored_specular = index != 5U;
    const auto input = DeclarationInput(
        800U, index + 1U, names[index], type, type,
        Binding(ManagedMaterialTextureSlot::DIFFUSE, "shared-base.png",
                "shared-base.png", "fixture-vehicle", ordinary),
        authored_specular
            ? Binding(ManagedMaterialTextureSlot::SPECULAR,
                      "shared-specular.png", "shared-specular.png",
                      "fixture-addon", authenticated)
            : EmptyBinding(ManagedMaterialTextureSlot::SPECULAR),
        flexmesh
            ? Binding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE,
                      "shared-damaged.png", "shared-damaged.png",
                      "fixture-vehicle", damaged)
            : EmptyBinding(ManagedMaterialTextureSlot::DAMAGED_DIFFUSE),
        (index % 3U) == 0U);
    RequireOk(CommitManagedMaterialDeclaration(BuildDeclaration(input),
                                               registry),
              "commit eight-material fixture entry");
  }
  ManagedMaterialDeclarationSnapshot snapshot;
  RequireOk(CaptureManagedMaterialDeclarationSnapshot(registry, snapshot),
            "capture eight-material fixture");
  Require(snapshot.size() == 8U &&
              snapshot.retained_source_bytes() ==
                  ordinary.source_size() + authenticated.source_size() +
                      damaged.source_size(),
          "exact eight-material fixture or shared-source accounting drifted");
  std::size_t specular_count = 0U;
  std::size_t damaged_count = 0U;
  std::size_t transparent_count = 0U;
  for (std::size_t index = 0U; index < snapshot.size(); ++index) {
    const auto *metadata = snapshot.at(index)->metadata();
    Require(metadata != nullptr &&
                metadata->exact_material_name.find("fixture-") == 0U &&
                metadata->exact_material_name.find("alexis") ==
                    std::string::npos,
            "fixture depends on a private asset name");
    specular_count += metadata->textures[static_cast<std::size_t>(
                          ManagedMaterialTextureSlot::SPECULAR)]
                          .configured
                          ? 1U
                          : 0U;
    damaged_count += metadata->textures[static_cast<std::size_t>(
                         ManagedMaterialTextureSlot::DAMAGED_DIFFUSE)]
                         .configured
                         ? 1U
                         : 0U;
    transparent_count +=
        metadata->resolved_type ==
                    ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT ||
                metadata->resolved_type ==
                    ManagedMaterialSemanticType::MESH_TRANSPARENT
            ? 1U
            : 0U;
  }
  Require(specular_count == 7U && damaged_count == 4U &&
              transparent_count == 4U,
          "eight-material authored slot/type shape drifted");
}

} // namespace

int main() {
  static_assert(std::is_nothrow_copy_constructible_v<
                    ManagedMaterialTextureSourceReceipt> &&
                    std::is_nothrow_copy_assignable_v<
                        ManagedMaterialTextureSourceReceipt> &&
                    std::is_nothrow_copy_constructible_v<
                        ManagedMaterialDeclaration> &&
                    std::is_nothrow_copy_assignable_v<
                        ManagedMaterialDeclaration> &&
                    std::is_nothrow_copy_constructible_v<
                        ManagedMaterialDeclarationSnapshot> &&
                    std::is_nothrow_copy_assignable_v<
                        ManagedMaterialDeclarationSnapshot> &&
                    noexcept(RevokeManagedMaterialDeclarationRegistry(
                        1U, std::declval<ManagedMaterialDeclarationRegistry &>())),
                "immutable managed-material ownership must remain noexcept-copyable");
  TestAllTypesAndTuneupResolution();
  TestRemovedMaterialAndMissingSources();
  TestRegistryOrderSharingDuplicateAndCollision();
  TestTransactionsReloadAndRevocation();
  TestSourceAndDeclarationFaultRollback();
  TestExactEightMaterialFixture();
  return 0;
}
