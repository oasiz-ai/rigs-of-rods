#include <OgreArchive.h>
#include <OgreArchiveFactory.h>
#include <OgreArchiveManager.h>
#include <OgreDataStream.h>
#include <OgreException.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreScriptLoader.h>
#include <OgreString.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if OGRE_VERSION != ((14 << 16) | (5 << 8) | 2)
#error "material-script pre-open probe requires pinned OGRE 14.5.2"
#endif

namespace {

const Ogre::String DEFAULT_GROUP = "ror-preopen-default-compatibility";
const Ogre::String METADATA_FALLBACK_GROUP =
    "ror-preopen-metadata-fallback-compatibility";
const Ogre::String HOSTILE_GROUP = "ror-preopen-hostile-sources";
const Ogre::String REJECTION_GROUP = "ror-preopen-handled-null-rejection";
const Ogre::String IMPORT_GROUP = "ror-preopen-import";

const Ogre::String DEFAULT_ARCHIVE = "ror-preopen-default-owner";
const Ogre::String METADATA_FALLBACK_ARCHIVE =
    "ror-preopen-throwing-metadata-owner";
const Ogre::String HOSTILE_ARCHIVE_A = "ror-preopen-identical-owner-a";
const Ogre::String HOSTILE_ARCHIVE_B = "ror-preopen-identical-owner-b";
const Ogre::String REJECTION_ARCHIVE = "ror-preopen-rejection-owner";
const Ogre::String IMPORT_ROOT_ARCHIVE = "ror-preopen-import-root-owner";
const Ogre::String IMPORT_DEPENDENCY_ARCHIVE =
    "ror-preopen-import-dependency-owner";
const Ogre::String IMPORT_SHADOW_ARCHIVE =
    "ror-preopen-import-shadow-unselected-owner";

const Ogre::String DEFAULT_MEMBER = "default.compat";
const Ogre::String METADATA_FALLBACK_MEMBER = "throwing-metadata.bin";
const Ogre::String HOSTILE_MEMBER_A = "city/shared.hostile";
const Ogre::String HOSTILE_MEMBER_B = "neo/shared.hostile";
const Ogre::String REJECTION_MEMBER = "quarantine/rejected.hostile";
const Ogre::String IMPORT_ROOT_MEMBER = "root.importprobe";
const Ogre::String IMPORT_DEPENDENCY_MEMBER = "dependencies/imported.inc";
const Ogre::String IMPORT_REJECTION_MEMBER = "dependencies/rejected.inc";

const Ogre::String DEFAULT_BYTES = "default-archive-payload\n";
const Ogre::String METADATA_FALLBACK_BYTES =
    "metadata lookup failed but archive fallback remained compatible\n";
const Ogre::String IDENTICAL_HOSTILE_BYTES = "identical-shadow-payload\n";
const Ogre::String REJECTION_ARCHIVE_BYTES =
    "archive bytes which handled-null must never open\n";
const Ogre::String IMPORT_ROOT_BYTES = "open dependencies/imported.inc\n";
const Ogre::String IMPORT_ARCHIVE_BYTES =
    "archive bytes which the handled import must replace\n";
const Ogre::String IMPORT_REJECTION_ARCHIVE_BYTES =
    "archive bytes which handled-null ordinary open must never read\n";

Ogre::DataStreamPtr MakeMemoryStream(const Ogre::String &name,
                                     const Ogre::String &payload) {
  Ogre::DataStreamPtr stream(
      OGRE_NEW Ogre::MemoryDataStream(name, payload.size(), true, false));
  if (!payload.empty()) {
    stream->write(payload.data(), payload.size());
  }
  stream->seek(0U);
  return stream;
}

struct ArchiveCounters {
  std::size_t open_attempt_count = 0U;
  std::size_t find_file_info_count = 0U;
  std::size_t list_file_info_count = 0U;
};

struct EntrySpec {
  Ogre::String full_name;
  Ogre::String payload;
  bool expose_basename_as_filename = false;
};

struct ArchiveSpec {
  std::vector<EntrySpec> entries;
  std::shared_ptr<ArchiveCounters> counters =
      std::make_shared<ArchiveCounters>();
  bool throw_metadata_lookup = false;
  bool return_wrong_qualified_candidate = false;
};

Ogre::FileInfo BuildFileInfo(const Ogre::Archive *archive,
                             const EntrySpec &entry) {
  Ogre::FileInfo info{};
  info.archive = archive;
  const std::size_t slash = entry.full_name.find_last_of("/\\");
  if (slash == Ogre::String::npos) {
    info.path.clear();
    info.basename = entry.full_name;
  } else {
    info.path = entry.full_name.substr(0U, slash + 1U);
    info.basename = entry.full_name.substr(slash + 1U);
  }
  info.filename =
      entry.expose_basename_as_filename ? info.basename : entry.full_name;
  info.compressedSize = entry.payload.size();
  info.uncompressedSize = entry.payload.size();
  return info;
}

class ProbeArchive final : public Ogre::Archive {
public:
  ProbeArchive(const Ogre::String &name, const Ogre::String &type,
               ArchiveSpec spec)
      : Ogre::Archive(name, type), m_spec(std::move(spec)) {}

  bool isCaseSensitive() const override { return true; }
  void load() override {}
  void unload() override {}

  Ogre::DataStreamPtr open(const Ogre::String &filename,
                           bool = true) const override {
    ++m_spec.counters->open_attempt_count;

    const EntrySpec *exact = nullptr;
    std::vector<const EntrySpec *> fallback;
    for (const EntrySpec &entry : m_spec.entries) {
      if (entry.full_name == filename) {
        exact = &entry;
        break;
      }
      const Ogre::FileInfo info = BuildFileInfo(this, entry);
      if (info.filename == filename || info.basename == filename) {
        fallback.push_back(&entry);
      }
    }

    const EntrySpec *selected = exact;
    if (selected == nullptr && fallback.size() == 1U) {
      selected = fallback.front();
    }
    if (selected == nullptr) {
      OGRE_EXCEPT(Ogre::Exception::ERR_FILE_NOT_FOUND,
                  "probe archive member is missing or basename-ambiguous: " +
                      filename,
                  "ProbeArchive::open");
    }
    return MakeMemoryStream(selected->full_name, selected->payload);
  }

  Ogre::StringVectorPtr list(bool recursive = true,
                             bool dirs = false) const override {
    Ogre::StringVectorPtr result(OGRE_NEW Ogre::StringVector());
    if (dirs) {
      return result;
    }
    for (const EntrySpec &entry : m_spec.entries) {
      const Ogre::FileInfo info = BuildFileInfo(this, entry);
      if (recursive || info.path.empty()) {
        result->push_back(entry.full_name);
      }
    }
    return result;
  }

  Ogre::FileInfoListPtr listFileInfo(bool recursive = true,
                                     bool dirs = false) const override {
    ++m_spec.counters->list_file_info_count;
    if (m_spec.throw_metadata_lookup) {
      throw std::runtime_error(
          "synthetic non-OGRE listFileInfo metadata failure");
    }
    Ogre::FileInfoListPtr result(OGRE_NEW Ogre::FileInfoList());
    if (dirs) {
      return result;
    }
    for (const EntrySpec &entry : m_spec.entries) {
      const Ogre::FileInfo info = BuildFileInfo(this, entry);
      if (recursive || info.path.empty()) {
        result->push_back(info);
      }
    }
    return result;
  }

  Ogre::StringVectorPtr find(const Ogre::String &pattern, bool recursive = true,
                             bool dirs = false) const override {
    Ogre::StringVectorPtr result(OGRE_NEW Ogre::StringVector());
    if (dirs) {
      return result;
    }
    for (const EntrySpec &entry : m_spec.entries) {
      const Ogre::FileInfo info = BuildFileInfo(this, entry);
      if ((recursive || info.path.empty()) &&
          (Ogre::StringUtil::match(entry.full_name, pattern, true) ||
           Ogre::StringUtil::match(info.basename, pattern, true))) {
        // ResourceGroupManager's index must retain the qualified name
        // even when hostile FileInfo intentionally exposes a basename.
        result->push_back(entry.full_name);
      }
    }
    return result;
  }

  Ogre::FileInfoListPtr findFileInfo(const Ogre::String &pattern,
                                     bool recursive = true,
                                     bool dirs = false) const override {
    ++m_spec.counters->find_file_info_count;
    if (m_spec.throw_metadata_lookup) {
      throw std::runtime_error(
          "synthetic non-OGRE findFileInfo metadata failure");
    }
    Ogre::FileInfoListPtr result(OGRE_NEW Ogre::FileInfoList());
    if (dirs) {
      return result;
    }
    const bool full_match = pattern.find('/') != Ogre::String::npos ||
                            pattern.find('\\') != Ogre::String::npos;
    if (full_match && m_spec.return_wrong_qualified_candidate &&
        m_spec.entries.size() > 1U) {
      result->push_back(BuildFileInfo(this, m_spec.entries.back()));
      return result;
    }
    for (const EntrySpec &entry : m_spec.entries) {
      const Ogre::FileInfo info = BuildFileInfo(this, entry);
      if ((recursive || info.path.empty()) &&
          Ogre::StringUtil::match(full_match ? info.filename : info.basename,
                                  pattern, true)) {
        result->push_back(info);
      }
    }
    return result;
  }

  bool exists(const Ogre::String &filename) const override {
    return std::any_of(m_spec.entries.begin(), m_spec.entries.end(),
                       [this, &filename](const EntrySpec &entry) {
                         const Ogre::FileInfo info = BuildFileInfo(this, entry);
                         return entry.full_name == filename ||
                                info.filename == filename ||
                                info.basename == filename;
                       });
  }

  std::time_t getModifiedTime(const Ogre::String &) const override { return 0; }

private:
  mutable ArchiveSpec m_spec;
};

class ProbeArchiveFactory final : public Ogre::ArchiveFactory {
public:
  void Add(const Ogre::String &name, const ArchiveSpec &spec) {
    m_specs.emplace(name, spec);
  }

  const Ogre::String &getType() const override {
    static const Ogre::String TYPE =
        "RorAuthenticatedMaterialScriptPreopenProbe";
    return TYPE;
  }

  Ogre::Archive *createInstance(const Ogre::String &name,
                                bool read_only) override {
    const auto spec = m_specs.find(name);
    if (!read_only || spec == m_specs.end()) {
      OGRE_EXCEPT(Ogre::Exception::ERR_INVALIDPARAMS,
                  "unknown or writable hostile pre-open probe archive: " + name,
                  "ProbeArchiveFactory::createInstance");
    }
    return OGRE_NEW ProbeArchive(name, getType(), spec->second);
  }

  void destroyInstance(Ogre::Archive *archive) override { OGRE_DELETE archive; }

private:
  std::map<Ogre::String, ArchiveSpec> m_specs;
};

struct ParseRecord {
  Ogre::String group;
  Ogre::String stream_name;
  Ogre::String payload;
};

class ProbeScriptLoader final : public Ogre::ScriptLoader {
public:
  explicit ProbeScriptLoader(
      std::shared_ptr<ArchiveCounters> import_shadow_counters)
      : m_import_shadow_counters(std::move(import_shadow_counters)) {
    m_patterns.push_back("*.compat");
    m_patterns.push_back("*.hostile");
    m_patterns.push_back("*.importprobe");
  }

  const Ogre::StringVector &getScriptPatterns() const override {
    return m_patterns;
  }

  void parseScript(Ogre::DataStreamPtr &stream,
                   const Ogre::String &group_name) override {
    ParseRecord record;
    record.group = group_name;
    record.stream_name = stream != nullptr ? stream->getName() : Ogre::String();
    record.payload = stream != nullptr ? stream->getAsString() : Ogre::String();
    m_records.push_back(record);

    if (group_name == IMPORT_GROUP &&
        record.stream_name == IMPORT_ROOT_MEMBER) {
      const std::size_t shadow_find_count_before_import =
          m_import_shadow_counters->find_file_info_count;
      const std::size_t shadow_list_count_before_import =
          m_import_shadow_counters->list_file_info_count;
      Ogre::DataStreamPtr imported =
          Ogre::ResourceGroupManager::getSingleton().openResource(
              IMPORT_DEPENDENCY_MEMBER, IMPORT_GROUP, nullptr, true);
      ParseRecord imported_record;
      imported_record.group = group_name;
      imported_record.stream_name =
          imported != nullptr ? imported->getName() : Ogre::String();
      imported_record.payload =
          imported != nullptr ? imported->getAsString() : Ogre::String();
      m_import_records.push_back(std::move(imported_record));

      Ogre::DataStreamPtr rejected =
          Ogre::ResourceGroupManager::getSingleton().openResource(
              IMPORT_REJECTION_MEMBER, IMPORT_GROUP, nullptr, true);
      ++m_import_rejection_attempt_count;
      m_import_rejection_was_null = rejected == nullptr;
      m_shadow_metadata_unchanged_during_import =
          m_import_shadow_counters->find_file_info_count ==
              shadow_find_count_before_import &&
          m_import_shadow_counters->list_file_info_count ==
              shadow_list_count_before_import;
    }
  }

  Ogre::Real getLoadingOrder() const override { return 17.0F; }

  const std::vector<ParseRecord> &records() const noexcept { return m_records; }

  const std::vector<ParseRecord> &import_records() const noexcept {
    return m_import_records;
  }

  std::size_t import_rejection_attempt_count() const noexcept {
    return m_import_rejection_attempt_count;
  }

  bool import_rejection_was_null() const noexcept {
    return m_import_rejection_was_null;
  }

  bool shadow_metadata_unchanged_during_import() const noexcept {
    return m_shadow_metadata_unchanged_during_import;
  }

private:
  Ogre::StringVector m_patterns;
  std::shared_ptr<ArchiveCounters> m_import_shadow_counters;
  std::vector<ParseRecord> m_records;
  std::vector<ParseRecord> m_import_records;
  std::size_t m_import_rejection_attempt_count = 0U;
  bool m_import_rejection_was_null = false;
  bool m_shadow_metadata_unchanged_during_import = false;
};

struct OpeningObservation {
  Ogre::String requested_name;
  Ogre::String group;
  bool has_resource = false;
  std::uintptr_t selected_archive_token = 0U;
  std::uintptr_t file_archive_token = 0U;
  Ogre::String selected_archive_name;
  bool has_file_info = false;
  bool file_archive_matches_selected = false;
  Ogre::String filename;
  Ogre::String path;
  Ogre::String basename;
  Ogre::String exact_member;
};

// Compilation of this default-derived listener is part of the compatibility
// contract: the new pre-open seam must be optional and non-pure.
class DefaultCompatibilityListener final
    : public Ogre::ResourceLoadingListener {};

class MetadataFallbackListener final : public Ogre::ResourceLoadingListener {
public:
  bool resourceStreamOpeningEnabled() const override { return true; }

  Ogre::DataStreamPtr
  resourceStreamOpening(const Ogre::String &, const Ogre::String &,
                        Ogre::Resource *, const Ogre::Archive *selected_archive,
                        const Ogre::FileInfo *exact_file_info,
                        bool &handled) override {
    ++m_opening_count;
    m_selected_archive_token =
        reinterpret_cast<std::uintptr_t>(selected_archive);
    m_selected_archive_name = selected_archive != nullptr
                                  ? selected_archive->getName()
                                  : Ogre::String();
    m_saw_null_file_info = exact_file_info == nullptr;
    handled = false;
    return Ogre::DataStreamPtr();
  }

  std::size_t opening_count() const noexcept { return m_opening_count; }

  std::uintptr_t selected_archive_token() const noexcept {
    return m_selected_archive_token;
  }

  const Ogre::String &selected_archive_name() const noexcept {
    return m_selected_archive_name;
  }

  bool saw_null_file_info() const noexcept { return m_saw_null_file_info; }

private:
  std::size_t m_opening_count = 0U;
  std::uintptr_t m_selected_archive_token = 0U;
  Ogre::String m_selected_archive_name;
  bool m_saw_null_file_info = false;
};

class HostileOpeningListener final : public Ogre::ResourceLoadingListener {
public:
  bool resourceStreamOpeningEnabled() const override { return true; }

  Ogre::DataStreamPtr resourceStreamOpening(
      const Ogre::String &requested_name, const Ogre::String &group,
      Ogre::Resource *resource, const Ogre::Archive *selected_archive,
      const Ogre::FileInfo *exact_file_info, bool &handled) override {
    OpeningObservation observation;
    observation.requested_name = requested_name;
    observation.group = group;
    observation.has_resource = resource != nullptr;
    observation.selected_archive_token =
        reinterpret_cast<std::uintptr_t>(selected_archive);
    observation.selected_archive_name = selected_archive != nullptr
                                            ? selected_archive->getName()
                                            : Ogre::String();
    observation.has_file_info = exact_file_info != nullptr;
    if (exact_file_info != nullptr) {
      observation.file_archive_token =
          reinterpret_cast<std::uintptr_t>(exact_file_info->archive);
      observation.file_archive_matches_selected =
          exact_file_info->archive == selected_archive;
      observation.filename = exact_file_info->filename;
      observation.path = exact_file_info->path;
      observation.basename = exact_file_info->basename;
      observation.exact_member =
          exact_file_info->path + exact_file_info->basename;
    }
    m_observations.push_back(observation);

    if (group == HOSTILE_GROUP) {
      handled = true;
      if (selected_archive == nullptr || exact_file_info == nullptr ||
          exact_file_info->archive != selected_archive) {
        ++m_identity_failures;
        return Ogre::DataStreamPtr();
      }
      const Ogre::String payload = "handled:" + selected_archive->getName() +
                                   ":" + observation.exact_member + "\n";
      return MakeMemoryStream(observation.exact_member, payload);
    }

    if (group == REJECTION_GROUP) {
      handled = true;
      if (selected_archive == nullptr || exact_file_info == nullptr ||
          exact_file_info->archive != selected_archive ||
          observation.exact_member != REJECTION_MEMBER) {
        ++m_identity_failures;
      }
      return Ogre::DataStreamPtr();
    }

    if (group == IMPORT_GROUP && requested_name == IMPORT_DEPENDENCY_MEMBER) {
      handled = true;
      if (selected_archive == nullptr || exact_file_info == nullptr ||
          exact_file_info->archive != selected_archive ||
          observation.exact_member != IMPORT_DEPENDENCY_MEMBER) {
        ++m_identity_failures;
        return Ogre::DataStreamPtr();
      }
      const Ogre::String payload =
          "synthetic-import:" + selected_archive->getName() + ":" +
          observation.exact_member + "\n";
      return MakeMemoryStream(observation.exact_member, payload);
    }

    if (group == IMPORT_GROUP && requested_name == IMPORT_REJECTION_MEMBER) {
      handled = true;
      if (selected_archive == nullptr || exact_file_info == nullptr ||
          exact_file_info->archive != selected_archive ||
          observation.exact_member != IMPORT_REJECTION_MEMBER) {
        ++m_identity_failures;
      }
      return Ogre::DataStreamPtr();
    }

    handled = false;
    return Ogre::DataStreamPtr();
  }

  const std::vector<OpeningObservation> &observations() const noexcept {
    return m_observations;
  }

  std::size_t identity_failures() const noexcept { return m_identity_failures; }

private:
  std::vector<OpeningObservation> m_observations;
  std::size_t m_identity_failures = 0U;
};

class ScriptLoaderRegistration final {
public:
  ScriptLoaderRegistration(Ogre::ResourceGroupManager &manager,
                           Ogre::ScriptLoader &loader)
      : m_manager(manager), m_loader(loader) {
    m_manager._registerScriptLoader(&m_loader);
  }

  ~ScriptLoaderRegistration() { m_manager._unregisterScriptLoader(&m_loader); }

private:
  Ogre::ResourceGroupManager &m_manager;
  Ogre::ScriptLoader &m_loader;
};

class LoadingListenerReset final {
public:
  explicit LoadingListenerReset(Ogre::ResourceGroupManager &manager)
      : m_manager(manager) {}

  ~LoadingListenerReset() { m_manager.setLoadingListener(nullptr); }

private:
  Ogre::ResourceGroupManager &m_manager;
};

class ResourceGroupCleanup final {
public:
  explicit ResourceGroupCleanup(Ogre::ResourceGroupManager &manager)
      : m_manager(manager) {}

  void Add(const Ogre::String &group) { m_groups.push_back(group); }

  ~ResourceGroupCleanup() {
    for (auto group = m_groups.rbegin(); group != m_groups.rend(); ++group) {
      try {
        if (m_manager.resourceGroupExists(*group)) {
          m_manager.destroyResourceGroup(*group);
        }
      } catch (...) {
      }
    }
  }

private:
  Ogre::ResourceGroupManager &m_manager;
  std::vector<Ogre::String> m_groups;
};

bool Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::vector<ParseRecord> RecordsForGroup(const ProbeScriptLoader &loader,
                                         const Ogre::String &group) {
  std::vector<ParseRecord> result;
  for (const ParseRecord &record : loader.records()) {
    if (record.group == group) {
      result.push_back(record);
    }
  }
  return result;
}

std::vector<OpeningObservation>
ObservationsForGroup(const HostileOpeningListener &listener,
                     const Ogre::String &group) {
  std::vector<OpeningObservation> result;
  for (const OpeningObservation &observation : listener.observations()) {
    if (observation.group == group) {
      result.push_back(observation);
    }
  }
  return result;
}

bool VerifyDefaultCompatibility(
    Ogre::ResourceGroupManager &manager, const ProbeScriptLoader &loader,
    const std::shared_ptr<ArchiveCounters> &counters) {
  manager.initialiseResourceGroup(DEFAULT_GROUP);
  const std::size_t find_count_before_open = counters->find_file_info_count;
  const std::size_t list_count_before_open = counters->list_file_info_count;
  Ogre::DataStreamPtr ordinary =
      manager.openResource(DEFAULT_MEMBER, DEFAULT_GROUP, nullptr, true);
  const std::vector<ParseRecord> records =
      RecordsForGroup(loader, DEFAULT_GROUP);
  return Require(records.size() == 1U,
                 "default listener did not parse exactly one script") &&
         Require(records.front().stream_name == DEFAULT_MEMBER,
                 "default listener changed the fallback stream name") &&
         Require(records.front().payload == DEFAULT_BYTES,
                 "default listener changed the fallback archive bytes") &&
         Require(ordinary != nullptr &&
                     ordinary->getAsString() == DEFAULT_BYTES,
                 "default listener changed ordinary fallback bytes") &&
         Require(counters->open_attempt_count == 2U,
                 "default listener did not preserve Archive::open fallback") &&
         Require(counters->find_file_info_count == find_count_before_open &&
                     counters->list_file_info_count == list_count_before_open,
                 "opted-out listener caused ordinary-open metadata I/O");
}

bool VerifyMetadataExceptionFallback(
    Ogre::ResourceGroupManager &manager, MetadataFallbackListener &listener,
    const std::shared_ptr<ArchiveCounters> &counters) {
  Ogre::DataStreamPtr stream = manager.openResource(
      METADATA_FALLBACK_MEMBER, METADATA_FALLBACK_GROUP, nullptr, true);
  return Require(
             listener.opening_count() == 1U,
             "throwing metadata did not reach opted-in pre-open listener") &&
         Require(listener.selected_archive_token() != 0U &&
                     listener.selected_archive_name() ==
                         METADATA_FALLBACK_ARCHIVE,
                 "throwing metadata exposed the wrong selected Archive*") &&
         Require(
             listener.saw_null_file_info(),
             "throwing metadata did not become an explicit null FileInfo") &&
         Require(stream != nullptr &&
                     stream->getAsString() == METADATA_FALLBACK_BYTES,
                 "throwing metadata did not preserve archive fallback") &&
         Require(counters->find_file_info_count == 1U &&
                     counters->list_file_info_count == 0U,
                 "throwing metadata fixture did not exercise exact lookup") &&
         Require(counters->open_attempt_count == 1U,
                 "throwing metadata did not fall back exactly once");
}

bool VerifyHostileSelectedSources(
    Ogre::ResourceGroupManager &manager, const ProbeScriptLoader &loader,
    const HostileOpeningListener &listener,
    const std::shared_ptr<ArchiveCounters> &counters_a,
    const std::shared_ptr<ArchiveCounters> &counters_b) {
  manager.initialiseResourceGroup(HOSTILE_GROUP);
  const std::vector<OpeningObservation> observations =
      ObservationsForGroup(listener, HOSTILE_GROUP);
  const std::vector<ParseRecord> records =
      RecordsForGroup(loader, HOSTILE_GROUP);

  std::set<std::uintptr_t> selected_owner_tokens;
  std::set<Ogre::String> exact_members;
  std::set<Ogre::String> observed_filenames;
  for (const OpeningObservation &observation : observations) {
    if (observation.selected_archive_token != 0U) {
      selected_owner_tokens.insert(observation.selected_archive_token);
    }
    exact_members.insert(observation.exact_member);
    observed_filenames.insert(observation.filename);
  }

  std::set<Ogre::String> parsed_payloads;
  std::set<Ogre::String> parsed_names;
  for (const ParseRecord &record : records) {
    parsed_payloads.insert(record.payload);
    parsed_names.insert(record.stream_name);
  }
  const std::set<Ogre::String> expected_payloads = {
      "handled:" + HOSTILE_ARCHIVE_A + ":" + HOSTILE_MEMBER_A + "\n",
      "handled:" + HOSTILE_ARCHIVE_B + ":" + HOSTILE_MEMBER_B + "\n"};
  const std::set<Ogre::String> expected_members = {HOSTILE_MEMBER_A,
                                                   HOSTILE_MEMBER_B};

  return Require(
             observations.size() == 2U,
             "pre-open seam did not expose both shadow archive selections") &&
         Require(listener.identity_failures() == 0U,
                 "selected Archive* did not match FileInfo::archive") &&
         Require(selected_owner_tokens.size() == 2U,
                 "same-byte archives collapsed to one archive owner") &&
         Require(exact_members == expected_members,
                 "path + basename did not preserve exact nested members") &&
         Require(observed_filenames == std::set<Ogre::String>{"shared.hostile"},
                 "hostile fixture did not exercise duplicate basenames") &&
         Require(records.size() == 2U,
                 "handled streams did not reach both script parses") &&
         Require(parsed_names == expected_members,
                 "handled stream names did not retain exact members") &&
         Require(
             parsed_payloads == expected_payloads,
             "handled streams did not retain distinct archive-owner lineage") &&
         Require(counters_a->open_attempt_count == 0U &&
                     counters_b->open_attempt_count == 0U,
                 "handled pre-open stream fell back to Archive::open");
}

bool VerifyHandledNullRejection(
    Ogre::ResourceGroupManager &manager, const ProbeScriptLoader &loader,
    const HostileOpeningListener &listener,
    const std::shared_ptr<ArchiveCounters> &counters) {
  manager.initialiseResourceGroup(REJECTION_GROUP);
  const std::vector<OpeningObservation> observations =
      ObservationsForGroup(listener, REJECTION_GROUP);
  const std::vector<ParseRecord> records =
      RecordsForGroup(loader, REJECTION_GROUP);

  return Require(observations.size() == 1U,
                 "handled-null fixture did not invoke pre-open exactly once") &&
         Require(observations.front().selected_archive_token != 0U &&
                     observations.front().selected_archive_name ==
                         REJECTION_ARCHIVE,
                 "handled-null fixture exposed the wrong selected Archive*") &&
         Require(observations.front().has_file_info &&
                     observations.front().file_archive_matches_selected &&
                     observations.front().file_archive_token ==
                         observations.front().selected_archive_token,
                 "handled-null fixture did not expose exact FileInfo owner") &&
         Require(observations.front().exact_member == REJECTION_MEMBER,
                 "handled-null FileInfo lost exact path + basename") &&
         Require(listener.identity_failures() == 0U,
                 "handled-null selected Archive* did not match FileInfo") &&
         Require(records.empty(),
                 "handled-null stream unexpectedly reached script parser") &&
         Require(counters->open_attempt_count == 0U,
                 "handled-null rejection fell back to Archive::open");
}

bool VerifyImportedScriptPreopen(
    Ogre::ResourceGroupManager &manager, const ProbeScriptLoader &loader,
    const HostileOpeningListener &listener,
    const std::shared_ptr<ArchiveCounters> &root_counters,
    const std::shared_ptr<ArchiveCounters> &dependency_counters,
    const std::shared_ptr<ArchiveCounters> &shadow_counters) {
  manager.initialiseResourceGroup(IMPORT_GROUP);
  const std::vector<ParseRecord> root_records =
      RecordsForGroup(loader, IMPORT_GROUP);
  const std::vector<OpeningObservation> observations =
      ObservationsForGroup(listener, IMPORT_GROUP);

  std::vector<OpeningObservation> import_observations;
  std::vector<OpeningObservation> rejection_observations;
  for (const OpeningObservation &observation : observations) {
    if (observation.requested_name == IMPORT_DEPENDENCY_MEMBER) {
      import_observations.push_back(observation);
    }
    if (observation.requested_name == IMPORT_REJECTION_MEMBER) {
      rejection_observations.push_back(observation);
    }
  }

  const Ogre::String expected_import_payload =
      "synthetic-import:" + IMPORT_DEPENDENCY_ARCHIVE + ":" +
      IMPORT_DEPENDENCY_MEMBER + "\n";

  std::uintptr_t selected_fixture_archive_token = 0U;
  std::uintptr_t shadow_fixture_archive_token = 0U;
  for (const Ogre::ResourceGroupManager::ResourceLocation &location :
       manager.getResourceLocationList(IMPORT_GROUP)) {
    if (location.archive->getName() == IMPORT_DEPENDENCY_ARCHIVE) {
      selected_fixture_archive_token =
          reinterpret_cast<std::uintptr_t>(location.archive);
    }
    if (location.archive->getName() == IMPORT_SHADOW_ARCHIVE) {
      shadow_fixture_archive_token =
          reinterpret_cast<std::uintptr_t>(location.archive);
    }
  }
  return Require(root_records.size() == 1U,
                 "import fixture did not parse exactly one root script") &&
         Require(root_records.front().stream_name == IMPORT_ROOT_MEMBER &&
                     root_records.front().payload == IMPORT_ROOT_BYTES,
                 "unhandled root script did not preserve fallback bytes") &&
         Require(import_observations.size() == 1U,
                 "openResourceImpl did not invoke pre-open seam for import") &&
         Require(selected_fixture_archive_token != 0U &&
                     shadow_fixture_archive_token != 0U &&
                     selected_fixture_archive_token !=
                         shadow_fixture_archive_token,
                 "shadow archives did not retain distinct runtime owners") &&
         Require(import_observations.front().selected_archive_token != 0U &&
                     import_observations.front().selected_archive_token ==
                         selected_fixture_archive_token,
                 "shadow precedence exposed the wrong selected Archive*") &&
         Require(
             import_observations.front().has_file_info &&
                 import_observations.front().file_archive_matches_selected &&
                 import_observations.front().file_archive_token ==
                     import_observations.front().selected_archive_token,
             "import pre-open seam did not expose exact FileInfo owner") &&
         Require(import_observations.front().exact_member ==
                     IMPORT_DEPENDENCY_MEMBER,
                 "import FileInfo path + basename lost the exact member") &&
         Require(import_observations.front().filename == "imported.inc",
                 "import fixture did not reproduce basename-only FileInfo") &&
         Require(!import_observations.front().has_resource,
                 "script import unexpectedly carried a Resource identity") &&
         Require(listener.identity_failures() == 0U,
                 "import selected Archive* did not match exact FileInfo") &&
         Require(loader.import_records().size() == 1U,
                 "script loader did not receive one handled import") &&
         Require(loader.import_records().front().stream_name ==
                         IMPORT_DEPENDENCY_MEMBER &&
                     loader.import_records().front().payload ==
                         expected_import_payload,
                 "handled import stream did not carry synthetic exact bytes") &&
         Require(
             rejection_observations.size() == 1U &&
                 rejection_observations.front().selected_archive_token ==
                     import_observations.front().selected_archive_token &&
                 rejection_observations.front().exact_member ==
                     IMPORT_REJECTION_MEMBER,
             "ordinary handled-null did not retain selected owner/member") &&
         Require(loader.import_rejection_attempt_count() == 1U &&
                     loader.import_rejection_was_null(),
                 "ordinary handled-null did not return an intentional null") &&
         Require(root_counters->open_attempt_count == 1U,
                 "unhandled import root did not use normal Archive::open") &&
         Require(dependency_counters->open_attempt_count == 0U &&
                     shadow_counters->open_attempt_count == 0U,
                 "handled shadow/import path fell back to Archive::open") &&
         Require(loader.shadow_metadata_unchanged_during_import(),
                 "ordinary import queried unselected shadow metadata") &&
         Require(
             IMPORT_ARCHIVE_BYTES != expected_import_payload,
             "import fixture cannot distinguish archive and synthetic bytes");
}

ArchiveSpec MakeArchiveSpec(const Ogre::String &member,
                            const Ogre::String &payload, bool expose_basename) {
  ArchiveSpec spec;
  spec.entries.push_back({member, payload, expose_basename});
  return spec;
}

} // namespace

int main() {
  ProbeArchiveFactory archive_factory;
  const ArchiveSpec default_spec =
      MakeArchiveSpec(DEFAULT_MEMBER, DEFAULT_BYTES, false);
  ArchiveSpec metadata_fallback_spec =
      MakeArchiveSpec(METADATA_FALLBACK_MEMBER, METADATA_FALLBACK_BYTES, false);
  metadata_fallback_spec.throw_metadata_lookup = true;
  const ArchiveSpec hostile_spec_a =
      MakeArchiveSpec(HOSTILE_MEMBER_A, IDENTICAL_HOSTILE_BYTES, true);
  const ArchiveSpec hostile_spec_b =
      MakeArchiveSpec(HOSTILE_MEMBER_B, IDENTICAL_HOSTILE_BYTES, true);
  const ArchiveSpec rejection_spec =
      MakeArchiveSpec(REJECTION_MEMBER, REJECTION_ARCHIVE_BYTES, false);
  const ArchiveSpec import_root_spec =
      MakeArchiveSpec(IMPORT_ROOT_MEMBER, IMPORT_ROOT_BYTES, false);
  ArchiveSpec import_dependency_spec =
      MakeArchiveSpec(IMPORT_DEPENDENCY_MEMBER, IMPORT_ARCHIVE_BYTES, true);
  import_dependency_spec.entries.push_back(
      {IMPORT_REJECTION_MEMBER, IMPORT_REJECTION_ARCHIVE_BYTES, true});
  import_dependency_spec.return_wrong_qualified_candidate = true;
  const ArchiveSpec import_shadow_spec =
      MakeArchiveSpec(IMPORT_DEPENDENCY_MEMBER, IMPORT_ARCHIVE_BYTES, true);
  archive_factory.Add(DEFAULT_ARCHIVE, default_spec);
  archive_factory.Add(METADATA_FALLBACK_ARCHIVE, metadata_fallback_spec);
  archive_factory.Add(HOSTILE_ARCHIVE_A, hostile_spec_a);
  archive_factory.Add(HOSTILE_ARCHIVE_B, hostile_spec_b);
  archive_factory.Add(REJECTION_ARCHIVE, rejection_spec);
  archive_factory.Add(IMPORT_ROOT_ARCHIVE, import_root_spec);
  archive_factory.Add(IMPORT_DEPENDENCY_ARCHIVE, import_dependency_spec);
  archive_factory.Add(IMPORT_SHADOW_ARCHIVE, import_shadow_spec);

  try {
    // Construct the factory first so it outlives Root's ArchiveManager.
    Ogre::Root root("", "", "");
    Ogre::ArchiveManager::getSingleton().addArchiveFactory(&archive_factory);
    Ogre::ResourceGroupManager &manager =
        Ogre::ResourceGroupManager::getSingleton();

    ProbeScriptLoader loader(import_shadow_spec.counters);
    ScriptLoaderRegistration loader_registration(manager, loader);
    ResourceGroupCleanup group_cleanup(manager);
    LoadingListenerReset listener_reset(manager);

    manager.createResourceGroup(DEFAULT_GROUP, false);
    group_cleanup.Add(DEFAULT_GROUP);
    manager.addResourceLocation(DEFAULT_ARCHIVE, archive_factory.getType(),
                                DEFAULT_GROUP, true, true);

    manager.createResourceGroup(METADATA_FALLBACK_GROUP, false);
    group_cleanup.Add(METADATA_FALLBACK_GROUP);
    manager.addResourceLocation(METADATA_FALLBACK_ARCHIVE,
                                archive_factory.getType(),
                                METADATA_FALLBACK_GROUP, true, true);

    manager.createResourceGroup(HOSTILE_GROUP, false);
    group_cleanup.Add(HOSTILE_GROUP);
    manager.addResourceLocation(HOSTILE_ARCHIVE_A, archive_factory.getType(),
                                HOSTILE_GROUP, true, true);
    manager.addResourceLocation(HOSTILE_ARCHIVE_B, archive_factory.getType(),
                                HOSTILE_GROUP, true, true);

    manager.createResourceGroup(REJECTION_GROUP, false);
    group_cleanup.Add(REJECTION_GROUP);
    manager.addResourceLocation(REJECTION_ARCHIVE, archive_factory.getType(),
                                REJECTION_GROUP, true, true);

    manager.createResourceGroup(IMPORT_GROUP, false);
    group_cleanup.Add(IMPORT_GROUP);
    manager.addResourceLocation(IMPORT_ROOT_ARCHIVE, archive_factory.getType(),
                                IMPORT_GROUP, true, true);
    manager.addResourceLocation(IMPORT_DEPENDENCY_ARCHIVE,
                                archive_factory.getType(), IMPORT_GROUP, true,
                                true);
    manager.addResourceLocation(IMPORT_SHADOW_ARCHIVE,
                                archive_factory.getType(), IMPORT_GROUP, true,
                                true);

    DefaultCompatibilityListener default_listener;
    manager.setLoadingListener(&default_listener);
    bool passed =
        VerifyDefaultCompatibility(manager, loader, default_spec.counters);

    MetadataFallbackListener metadata_fallback_listener;
    manager.setLoadingListener(&metadata_fallback_listener);
    passed =
        VerifyMetadataExceptionFallback(manager, metadata_fallback_listener,
                                        metadata_fallback_spec.counters) &&
        passed;

    HostileOpeningListener hostile_listener;
    manager.setLoadingListener(&hostile_listener);
    passed = VerifyHostileSelectedSources(manager, loader, hostile_listener,
                                          hostile_spec_a.counters,
                                          hostile_spec_b.counters) &&
             passed;
    passed = VerifyHandledNullRejection(manager, loader, hostile_listener,
                                        rejection_spec.counters) &&
             passed;
    passed = VerifyImportedScriptPreopen(manager, loader, hostile_listener,
                                         import_root_spec.counters,
                                         import_dependency_spec.counters,
                                         import_shadow_spec.counters) &&
             passed;

    if (!passed) {
      return 1;
    }
    std::cout << "authenticated-material-script-preopen=ok "
                 "default-optout metadata-exception-fallback exact-fileinfo "
                 "same-bytes same-digest true-shadow-precedence "
                 "duplicate-basename "
                 "handled-no-fallback handled-null-rejection ordinary-null "
                 "import-openresource\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "material-script pre-open probe failed: " << error.what()
              << '\n';
  }
  return 1;
}
