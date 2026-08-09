#include "gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceipt.h"
#include "gfx/ogre14/Ogre14AuthenticatedResourceThreadGate.h"
#include "resources/LegacyMaterialScriptSanitizer.h"

#include <openssl/evp.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace RoR::Render::Testing {

class Ogre14AuthenticatedMaterialScriptTestAccess final {
public:
  static ValidationResult Initialize(
      const Ogre14AuthenticatedMaterialScriptRegistryConfiguration &config,
      Ogre14AuthenticatedMaterialScriptRegistry &registry) {
    return registry.Initialize(config);
  }

  static ValidationResult Advance(
      Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const std::string &group, std::uint64_t generation) {
    return registry.AdvanceGroupGeneration(group, generation);
  }

  static ValidationResult Commit(
      Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      const std::vector<Ogre14AuthenticatedMaterialScriptSourceInput>
          &sources,
      const std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput>
          &materials,
      IOgre14AuthenticatedMaterialScriptCommitFaultInjector *fault_injector =
          nullptr) {
    return registry.CommitWholeGroup(group, generation, sources, materials,
                                     fault_injector);
  }

  static ValidationResult Teardown(
      Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const std::string &group, std::uint64_t generation) {
    return registry.TeardownGroup(group, generation);
  }

  static ValidationResult Mint(
      const Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      std::uintptr_t pointer_token, std::uint64_t handle,
      const std::string &name, const std::string &origin,
      std::uintptr_t resolver_token,
      Ogre14AuthenticatedMaterialScriptResolution &resolution) {
    return registry.MintResolution(group, generation, pointer_token, handle,
                                   name, origin, resolver_token, resolution);
  }

  static bool Revalidate(
      const Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const Ogre14AuthenticatedMaterialScriptResolution &resolution,
      std::uintptr_t resolver_token, std::uintptr_t pointer_token,
      std::uint64_t handle, const std::string &group,
      const std::string &name, const std::string &origin) {
    return registry.RevalidateResolution(resolution, resolver_token,
                                         pointer_token, handle, group, name,
                                         origin);
  }

  static ValidationResult Remove(
      Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const std::string &group, std::uintptr_t pointer_token,
      std::uint64_t handle, const std::string &name,
      const std::string &origin) {
    return registry.RemoveMaterial(group, pointer_token, handle, name,
                                   origin);
  }

  static void Poison(
      Ogre14AuthenticatedMaterialScriptRegistry &registry) noexcept {
    registry.Poison();
  }
};

} // namespace RoR::Render::Testing

namespace {

using namespace RoR;
using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::string Sha256(const std::vector<std::uint8_t> &bytes) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0U;
  Require(EVP_Digest(bytes.data(), bytes.size(), digest.data(), &digest_size,
                     EVP_sha256(), nullptr) == 1 &&
              digest_size == 32U,
          "test SHA-256 failed");
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output(64U, '0');
  for (std::size_t index = 0U; index < 32U; ++index) {
    output[index * 2U] = kHex[digest[index] >> 4U];
    output[index * 2U + 1U] = kHex[digest[index] & 0x0fU];
  }
  return output;
}

TerrainBundleAuthenticatedArchiveSnapshot MakeSnapshot() {
  const std::vector<std::uint8_t> archive = {'a', 'r', 'c', 'h', 'i', 'v', 'e'};
  static std::atomic<std::uint64_t> sequence{0U};
  const auto nonce = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto path = std::filesystem::temp_directory_path() /
                    ("ror-authenticated-material-script-test-" +
                     std::to_string(nonce) + "-" +
                     std::to_string(++sequence) + "-" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(&archive)) +
                     ".archive");
  {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    Require(stream.good(), "could not create synthetic archive");
    stream.write(reinterpret_cast<const char *>(archive.data()),
                 static_cast<std::streamsize>(archive.size()));
    Require(stream.good(), "could not write synthetic archive");
  }
  TerrainBundleAuthenticatedArchiveSnapshot snapshot;
  std::string observed;
  std::string error;
  Require(LoadAndVerifyTerrainBundleArchiveSnapshot(
              path.string(), Sha256(archive), 1024U, snapshot, observed, error),
          "could not authenticate synthetic archive snapshot");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  return snapshot;
}

Ogre14AuthenticatedMaterialScriptSourceInput MakeSource(
    const TerrainBundleAuthenticatedArchiveSnapshot &snapshot,
    std::uint64_t ordinal, std::string compiler_file,
    std::string exact_member, Ogre14MaterialScriptSourceRole role,
    std::uint64_t parse_token = 1U) {
  Ogre14AuthenticatedMaterialScriptSourceInput input;
  const std::string payload = role == Ogre14MaterialScriptSourceRole::ROOT_SCRIPT
                                  ? "material Root {}\n"
                                  : "material Imported {}\n";
  input.original_bytes =
      std::make_shared<const std::vector<std::uint8_t>>(
          payload.begin(), payload.end());
  input.effective_bytes = input.original_bytes;
  auto &metadata = input.metadata;
  metadata.source_role = role;
  metadata.parse_token = parse_token;
  metadata.source_open_ordinal = ordinal;
  metadata.group_generation = 1U;
  metadata.effective_group = "CityWorld";
  metadata.root_script_request = "root.material";
  metadata.compiler_file_identity = std::move(compiler_file);
  metadata.archive_source_identity = snapshot.source_archive_identity();
  metadata.selected_archive_name = "authenticated-cityworld-v1";
  metadata.selected_archive_type = "EmbeddedZip";
  metadata.archive_sha256 = snapshot.archive_sha256();
  metadata.archive_pointer_token = 0x900U;
  metadata.file_info_filename = exact_member;
  const std::size_t slash = exact_member.find_last_of('/');
  metadata.file_info_path = slash == std::string::npos
                                ? std::string()
                                : exact_member.substr(0U, slash + 1U);
  metadata.file_info_basename = slash == std::string::npos
                                    ? exact_member
                                    : exact_member.substr(slash + 1U);
  metadata.exact_member_name = std::move(exact_member);
  metadata.compressed_size = input.original_bytes->size();
  metadata.uncompressed_size = input.original_bytes->size();
  metadata.original_byte_count = input.original_bytes->size();
  metadata.effective_byte_count = input.effective_bytes->size();
  metadata.original_sha256 = Sha256(*input.original_bytes);
  metadata.effective_sha256 = Sha256(*input.effective_bytes);
  metadata.repair_plan_version = kLegacyMaterialScriptRepairPlanVersion;
  Require(ComputeLegacyMaterialScriptNoRepairPlanSha256(
              metadata.archive_sha256, metadata.exact_member_name,
              metadata.original_sha256, metadata.repair_plan_sha256),
          "could not hash NONE repair record");
  input.authenticated_archive_snapshot = snapshot;
  return input;
}

Ogre14AuthenticatedMaterialScriptMaterialInput MakeMaterial(
    std::size_t source_index, std::uint64_t event_ordinal,
    std::uintptr_t pointer_token, std::uint64_t handle, std::string name,
    std::string origin) {
  Ogre14AuthenticatedMaterialScriptMaterialInput input;
  input.source_index = source_index;
  input.binding.event_ordinal = event_ordinal;
  input.binding.material_pointer_token = pointer_token;
  input.binding.material_handle = handle;
  input.binding.exact_material_name = std::move(name);
  input.binding.exact_group = "CityWorld";
  input.binding.exact_origin = std::move(origin);
  return input;
}

Ogre14AuthenticatedMaterialScriptRegistry MakeRegistry() {
  Ogre14AuthenticatedMaterialScriptRegistry registry;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              {}, registry)
              .ok(),
          "registry initialization failed");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
              registry, "CityWorld", 1U)
              .ok(),
          "group generation advance failed");
  return registry;
}

class ThrowingCommitFaultInjector final
    : public IOgre14AuthenticatedMaterialScriptCommitFaultInjector {
public:
  ThrowingCommitFaultInjector(
      Ogre14AuthenticatedMaterialScriptCommitFaultPoint target,
      bool throw_bad_alloc) noexcept
      : target_(target), throw_bad_alloc_(throw_bad_alloc) {}

  void OnOgre14AuthenticatedMaterialScriptCommitFault(
      Ogre14AuthenticatedMaterialScriptCommitFaultPoint point) override {
    if (point != target_) {
      return;
    }
    if (throw_bad_alloc_) {
      throw std::bad_alloc();
    }
    throw 7;
  }

private:
  Ogre14AuthenticatedMaterialScriptCommitFaultPoint target_;
  bool throw_bad_alloc_ = false;
};

void TestImportedClosureAndResolution() {
  const auto snapshot = MakeSnapshot();
  std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> sources;
  sources.push_back(MakeSource(snapshot, 1U, "root.material",
                               "root.material",
                               Ogre14MaterialScriptSourceRole::ROOT_SCRIPT));
  sources.push_back(MakeSource(
      snapshot, 2U, "inc/base.material", "inc/base.material",
      Ogre14MaterialScriptSourceRole::COMPILER_DEPENDENCY));
  std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput> materials;
  materials.push_back(
      MakeMaterial(0U, 1U, 0x100U, 11U, "Root", "root.material"));
  materials.push_back(MakeMaterial(1U, 2U, 0x200U, 12U, "Imported",
                                   "inc/base.material"));

  auto registry = MakeRegistry();
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, sources, materials)
              .ok(),
          "valid whole-group commit failed");
  Require(registry.size() == 2U && registry.source_count() == 2U,
          "registry counts do not preserve the parse closure");

  Ogre14AuthenticatedMaterialScriptResolution root;
  Ogre14AuthenticatedMaterialScriptResolution imported;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
              registry, "CityWorld", 1U, 0x100U, 11U, "Root",
              "root.material", 0x777U, root)
              .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
                  registry, "CityWorld", 1U, 0x200U, 12U, "Imported",
                  "inc/base.material", 0x777U, imported)
                  .ok(),
          "current material resolutions did not mint");
  const auto *root_receipt = root.receipt();
  const auto *imported_receipt = imported.receipt();
  Require(root_receipt != nullptr && imported_receipt != nullptr &&
              root_receipt->source_count() == 2U &&
              imported_receipt->source_count() == 2U &&
              root_receipt->primary_source_index() == 0U &&
              imported_receipt->primary_source_index() == 1U &&
              root_receipt->SharesSourceStateWith(*imported_receipt),
          "material receipts do not share the exact ordered import closure");
  Require(root_receipt->source_metadata_at(0U)->source_open_ordinal == 1U &&
              root_receipt->source_metadata_at(1U)->source_open_ordinal == 2U,
          "parse closure is not ordered by source-open ordinal");
  Require(root_receipt->original_bytes_at(0U) ==
                  sources[0].original_bytes->data() &&
              root_receipt->effective_bytes_at(1U) ==
                  sources[1].effective_bytes->data(),
          "publication did not preserve immutable source byte owners");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Revalidate(
              registry, imported, 0x777U, 0x200U, 12U, "CityWorld",
              "Imported", "inc/base.material") &&
              !Testing::Ogre14AuthenticatedMaterialScriptTestAccess::
                  Revalidate(registry, imported, 0x778U, 0x200U, 12U,
                             "CityWorld", "Imported", "inc/base.material"),
          "resolver authority was not bound exactly");
}

void TestRollbackAndStaleAuthority() {
  const auto snapshot = MakeSnapshot();
  std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> sources;
  sources.push_back(MakeSource(snapshot, 1U, "root.material",
                               "root.material",
                               Ogre14MaterialScriptSourceRole::ROOT_SCRIPT));
  std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput> materials;
  materials.push_back(
      MakeMaterial(0U, 1U, 0x100U, 11U, "Root", "root.material"));
  auto registry = MakeRegistry();
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, sources, materials)
              .ok(),
          "baseline group commit failed");
  const auto sentinel = registry;

  auto forged = sources;
  auto forged_effective = *forged.front().effective_bytes;
  forged_effective.push_back('x');
  forged.front().effective_bytes =
      std::make_shared<const std::vector<std::uint8_t>>(
          std::move(forged_effective));
  Require(!Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
               registry, "CityWorld", 1U, forged, materials)
               .ok() &&
              registry.SharesImmutableStateWith(sentinel),
          "failed group commit changed the registry sentinel");

  Ogre14AuthenticatedMaterialScriptResolution resolution;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
              registry, "CityWorld", 1U, 0x100U, 11U, "Root",
              "root.material", 0x777U, resolution)
              .ok(),
          "baseline resolution failed");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Remove(
              registry, "CityWorld", 0x100U, 11U, "Root", "wrong.origin")
              .code == ValidationCode::REVISION_MISMATCH &&
              registry.SharesImmutableStateWith(sentinel),
          "wrong-origin removal changed the registry");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Remove(
              registry, "CityWorld", 0x999U, 11U, "Root", "root.material")
                  .code == ValidationCode::REVISION_MISMATCH &&
              registry.SharesImmutableStateWith(sentinel),
          "partial-identity removal changed the registry");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Remove(
              registry, "CityWorld", 0x100U, 11U, "Root", "root.material")
              .ok() &&
              registry.size() == 0U &&
              !Testing::Ogre14AuthenticatedMaterialScriptTestAccess::
                  Revalidate(registry, resolution, 0x777U, 0x100U, 11U,
                             "CityWorld", "Root", "root.material"),
          "removed material retained current authority");
  const auto removed_sentinel = registry;
  Ogre14AuthenticatedMaterialScriptResolution foreign_replacement;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
              registry, "CityWorld", 1U, 0x900U, 91U, "Root",
              "root.material", 0x777U, foreign_replacement)
                  .code == ValidationCode::MISSING_REFERENCE &&
              !foreign_replacement.initialized() &&
              registry.SharesImmutableStateWith(removed_sentinel),
          "same-name foreign replacement inherited removed authority");
  Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Poison(registry);
  Require(!registry.initialized(), "poison did not invalidate the registry");
}

void TestSameGroupGenerationAdvanceInvalidatesAuthority() {
  const auto snapshot = MakeSnapshot();
  std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> sources;
  sources.push_back(MakeSource(snapshot, 1U, "root.material",
                               "root.material",
                               Ogre14MaterialScriptSourceRole::ROOT_SCRIPT));
  std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput> materials;
  materials.push_back(
      MakeMaterial(0U, 1U, 0x100U, 11U, "Root", "root.material"));
  auto registry = MakeRegistry();
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, sources, materials)
              .ok(),
          "same-group generation baseline commit failed");
  Ogre14AuthenticatedMaterialScriptResolution old_resolution;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
              registry, "CityWorld", 1U, 0x100U, 11U, "Root",
              "root.material", 0x777U, old_resolution)
              .ok(),
          "same-group generation baseline resolution failed");
  const auto *retained_receipt = old_resolution.receipt();
  Require(retained_receipt != nullptr &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  registry, "CityWorld", 2U)
                  .ok() &&
              registry.size() == 0U && registry.source_count() == 0U &&
              registry.maximum_group_generation_seen() == 2U &&
              !Testing::Ogre14AuthenticatedMaterialScriptTestAccess::
                  Revalidate(registry, old_resolution, 0x777U, 0x100U, 11U,
                             "CityWorld", "Root", "root.material") &&
              retained_receipt->original_bytes() ==
                  sources.front().original_bytes->data(),
          "same-group mount generation did not invalidate old authority");
}

void TestWholeGroupPublicationIsOneShotWithoutReceipts() {
  auto empty_registry = MakeRegistry();
  const std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> no_sources;
  const std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput>
      no_materials;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              empty_registry, "CityWorld", 1U, no_sources, no_materials)
                  .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
                  empty_registry, "CityWorld", 1U, no_sources, no_materials)
                      .code == ValidationCode::SEQUENCE_MISMATCH,
          "empty whole-group publication was not one-shot");

  const auto snapshot = MakeSnapshot();
  std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> sources;
  sources.push_back(MakeSource(snapshot, 1U, "root.material",
                               "root.material",
                               Ogre14MaterialScriptSourceRole::ROOT_SCRIPT));
  std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput> materials;
  materials.push_back(
      MakeMaterial(0U, 1U, 0x100U, 11U, "Root", "root.material"));
  auto removed_registry = MakeRegistry();
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              removed_registry, "CityWorld", 1U, sources, materials)
                  .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Remove(
                  removed_registry, "CityWorld", 0x100U, 11U, "Root",
                  "root.material")
                  .ok() &&
              removed_registry.size() == 0U &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
                  removed_registry, "CityWorld", 1U, sources, materials)
                      .code == ValidationCode::SEQUENCE_MISMATCH,
          "removing the last receipt reopened a committed generation");
}

void TestGroupIdentityAccounting() {
  Ogre14AuthenticatedMaterialScriptRegistryConfiguration config;
  config.maximum_total_identity_bytes = std::string("CityWorld").size();
  Ogre14AuthenticatedMaterialScriptRegistry registry;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              config, registry)
                  .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  registry, "CityWorld", 1U)
                  .ok(),
          "exact group identity cap was not admitted");
  const auto sentinel = registry;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
              registry, "X", 2U)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE &&
              registry.SharesImmutableStateWith(sentinel) &&
              registry.maximum_group_generation_seen() == 1U,
          "group identity cap+1 changed the registry");
}

void TestHostileClosureAndCaps() {
  const auto snapshot = MakeSnapshot();
  auto registry = MakeRegistry();
  std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> sources;
  sources.push_back(MakeSource(snapshot, 1U, "root.material",
                               "root.material",
                               Ogre14MaterialScriptSourceRole::ROOT_SCRIPT));
  sources.push_back(MakeSource(snapshot, 2U, "root.material",
                               "other.material",
                               Ogre14MaterialScriptSourceRole::
                                   COMPILER_DEPENDENCY));
  const std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput> materials = {
      MakeMaterial(0U, 1U, 0x100U, 11U, "Root", "root.material")};
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, sources, materials)
              .code == ValidationCode::DUPLICATE_IDENTIFIER &&
              registry.size() == 0U,
          "ambiguous compiler-file identity did not fail closed");

  Ogre14AuthenticatedMaterialScriptRegistryConfiguration config;
  config.maximum_live_sources = 1U;
  Ogre14AuthenticatedMaterialScriptRegistry capped;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              config, capped)
              .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  capped, "CityWorld", 1U)
                  .ok(),
          "capped registry setup failed");
  sources[1].metadata.compiler_file_identity = "other.material";
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              capped, "CityWorld", 1U, sources, materials)
              .code == ValidationCode::VALUE_OUT_OF_RANGE &&
              capped.size() == 0U,
          "source cap+1 did not reject atomically");

  config = {};
  config.maximum_live_receipts = 1U;
  Ogre14AuthenticatedMaterialScriptRegistry receipt_capped;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              config, receipt_capped)
              .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  receipt_capped, "CityWorld", 1U)
                  .ok(),
          "receipt-capped registry setup failed");
  const std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput>
      two_materials = {
          MakeMaterial(0U, 1U, 0x100U, 11U, "Root", "root.material"),
          MakeMaterial(0U, 2U, 0x101U, 12U, "Second", "root.material")};
  sources.resize(1U);
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              receipt_capped, "CityWorld", 1U, sources, two_materials)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE &&
              receipt_capped.size() == 0U,
          "receipt cap+1 did not reject atomically");

  config = {};
  config.maximum_retained_source_bytes =
      sources.front().original_bytes->size() * 2U - 1U;
  Ogre14AuthenticatedMaterialScriptRegistry byte_capped;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              config, byte_capped)
              .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  byte_capped, "CityWorld", 1U)
                  .ok(),
          "retained-byte capped registry setup failed");
  const auto byte_sentinel = byte_capped;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              byte_capped, "CityWorld", 1U, sources, materials)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE &&
              byte_capped.SharesImmutableStateWith(byte_sentinel),
          "retained-byte cap+1 did not reject");

  config = {};
  config.maximum_total_identity_bytes = std::string("CityWorld").size();
  Ogre14AuthenticatedMaterialScriptRegistry identity_capped;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              config, identity_capped)
              .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  identity_capped, "CityWorld", 1U)
                  .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
                  identity_capped, "CityWorld", 1U, sources, materials)
                      .code == ValidationCode::VALUE_OUT_OF_RANGE &&
              identity_capped.size() == 0U,
          "identity-byte cap+1 did not reject");

  auto missing_owner = sources;
  missing_owner.front().effective_bytes.reset();
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, missing_owner, materials)
                  .code == ValidationCode::MISSING_REFERENCE,
          "missing immutable byte owner did not reject");

  auto wrong_order = sources;
  wrong_order.front().metadata.source_open_ordinal = 2U;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, wrong_order, materials)
                  .code == ValidationCode::NON_DETERMINISTIC_ORDER,
          "noncontiguous root source ordinal did not reject");

  auto duplicate_materials = two_materials;
  duplicate_materials[1].binding.exact_material_name = "Root";
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, sources, duplicate_materials)
                  .code == ValidationCode::DUPLICATE_IDENTIFIER,
          "duplicate material identity did not reject");

  const auto registry_sentinel = registry;
  auto forged_snapshot = sources;
  forged_snapshot.front().authenticated_archive_snapshot = MakeSnapshot();
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, forged_snapshot, materials)
                  .code == ValidationCode::REVISION_MISMATCH &&
              registry.SharesImmutableStateWith(registry_sentinel),
          "foreign archive snapshot changed the registry");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 2U, sources, materials)
                  .code == ValidationCode::SEQUENCE_MISMATCH &&
              registry.SharesImmutableStateWith(registry_sentinel),
          "stale group generation changed the registry");
}

void TestInjectedCommitRollback() {
  const auto snapshot = MakeSnapshot();
  std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> sources;
  sources.push_back(MakeSource(snapshot, 1U, "root.material",
                               "root.material",
                               Ogre14MaterialScriptSourceRole::ROOT_SCRIPT));
  std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput> materials;
  materials.push_back(
      MakeMaterial(0U, 1U, 0x100U, 11U, "Root", "root.material"));
  auto registry = MakeRegistry();
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "CityWorld", 1U, sources, materials)
              .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  registry, "SecondCity", 2U)
                  .ok(),
          "fault rollback baseline setup failed");
  Ogre14AuthenticatedMaterialScriptResolution retained;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
              registry, "CityWorld", 1U, 0x100U, 11U, "Root",
              "root.material", 0x777U, retained)
              .ok(),
          "fault rollback sentinel resolution failed");
  const auto sentinel = registry;
  sources.front().metadata.effective_group = "SecondCity";
  sources.front().metadata.group_generation = 2U;
  materials.front().binding.exact_group = "SecondCity";

  ThrowingCommitFaultInjector bad_alloc(
      Ogre14AuthenticatedMaterialScriptCommitFaultPoint::
          AFTER_SOURCE_CANONICALIZATION,
      true);
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "SecondCity", 2U, sources, materials, &bad_alloc)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE &&
              registry.SharesImmutableStateWith(sentinel) &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::
                  Revalidate(registry, retained, 0x777U, 0x100U, 11U,
                             "CityWorld", "Root", "root.material"),
          "bad_alloc changed the deep registry sentinel");

  ThrowingCommitFaultInjector unexpected(
      Ogre14AuthenticatedMaterialScriptCommitFaultPoint::BEFORE_PUBLICATION,
      false);
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              registry, "SecondCity", 2U, sources, materials, &unexpected)
                  .code == ValidationCode::UNSUPPORTED_FEATURE &&
              registry.SharesImmutableStateWith(sentinel) &&
              retained.receipt() != nullptr &&
              retained.receipt()->original_bytes() ==
                  sources.front().original_bytes->data(),
          "unexpected exception changed registry values or shared owners");
}

void TestSerializedResourceThreadGate() {
  Ogre14AuthenticatedResourceThreadGate gate;
  Require(!gate.is_bound() && gate.IsCurrentThreadOrUnbound(),
          "fresh resource-thread gate was not unbound");

  std::atomic<bool> foreign_unbound{false};
  std::thread before_bind([&]() {
    foreign_unbound.store(gate.IsCurrentThreadOrUnbound(),
                          std::memory_order_release);
  });
  before_bind.join();
  Require(foreign_unbound.load(std::memory_order_acquire),
          "pure preflight observation bound the resource-thread gate");

  Require(gate.BindCurrentThread() && gate.is_bound() &&
              gate.IsCurrentThreadOrUnbound(),
          "resource-thread gate did not bind to the calling thread");
  std::atomic<bool> foreign_require{true};
  std::atomic<bool> foreign_bind{true};
  std::thread after_bind([&]() {
    foreign_require.store(gate.IsCurrentThreadOrUnbound(),
                          std::memory_order_release);
    foreign_bind.store(gate.BindCurrentThread(),
                       std::memory_order_release);
  });
  after_bind.join();
  Require(!foreign_require.load(std::memory_order_acquire) &&
              !foreign_bind.load(std::memory_order_acquire) &&
              gate.BindCurrentThread(),
          "foreign thread crossed or changed the process-lifetime gate");
}

void TestGroupTeardownRecoversCapacity() {
  Ogre14AuthenticatedMaterialScriptRegistryConfiguration config;
  config.maximum_group_records = 1U;
  Ogre14AuthenticatedMaterialScriptRegistry registry;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              config, registry)
              .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  registry, "EphemeralA", 1U)
                  .ok() &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
                  registry, "EphemeralA", 1U, {}, {})
                  .ok(),
          "ephemeral group baseline failed");
  const auto before_teardown = registry;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Teardown(
              registry, "EphemeralA", 2U)
                  .code == ValidationCode::SEQUENCE_MISMATCH &&
              registry.SharesImmutableStateWith(before_teardown),
          "stale group teardown changed registry state");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Teardown(
              registry, "EphemeralA", 1U)
              .ok() &&
              registry.size() == 0U && registry.source_count() == 0U &&
              registry.retained_identity_bytes() == 0U &&
              registry.maximum_group_generation_seen() == 1U &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  registry, "EphemeralB", 2U)
                  .ok(),
          "group teardown did not recover bounded record capacity");
}

} // namespace

int main() {
  TestImportedClosureAndResolution();
  TestRollbackAndStaleAuthority();
  TestSameGroupGenerationAdvanceInvalidatesAuthority();
  TestWholeGroupPublicationIsOneShotWithoutReceipts();
  TestGroupIdentityAccounting();
  TestHostileClosureAndCaps();
  TestInjectedCommitRollback();
  TestSerializedResourceThreadGate();
  TestGroupTeardownRecoversCapacity();
  std::cout << "authenticated-material-script-receipt=ok\n";
  return EXIT_SUCCESS;
}
