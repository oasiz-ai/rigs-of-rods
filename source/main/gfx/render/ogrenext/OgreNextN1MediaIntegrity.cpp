/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1MediaIntegrity.h"

#include "ror_ogre_next_n1_media_manifest.h"
#include "ror_ogre_next_presentation_media_manifest.h"
#include "ror_ogre_next_reflection_media_manifest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace RoR::Render {
namespace {

constexpr std::array<std::uint32_t, 64U> kSha256RoundConstants{{
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
    UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
    UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
    UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
    UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
    UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
    UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
    UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
    UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
    UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
    UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
    UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
    UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
    UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
    UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
    UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
    UINT32_C(0xc67178f2),
}};

std::uint32_t RotateRight(std::uint32_t value, unsigned int count) noexcept {
  return (value >> count) | (value << (32U - count));
}

class Sha256 final {
public:
  bool Update(const std::uint8_t *bytes, std::size_t size) noexcept {
    if (size > (std::numeric_limits<std::uint64_t>::max)() - byte_count_) {
      return false;
    }
    byte_count_ += static_cast<std::uint64_t>(size);
    while (size > 0U) {
      const std::size_t copied =
          (std::min)(size, block_.size() - block_size_);
      std::copy_n(bytes, copied, block_.begin() + block_size_);
      block_size_ += copied;
      bytes += copied;
      size -= copied;
      if (block_size_ == block_.size()) {
        Transform(block_);
        block_size_ = 0U;
      }
    }
    return true;
  }

  bool Final(std::array<std::uint8_t, 32U> &digest) noexcept {
    if (byte_count_ >
        (std::numeric_limits<std::uint64_t>::max)() / UINT64_C(8)) {
      return false;
    }
    const std::uint64_t bit_count = byte_count_ * UINT64_C(8);
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      std::fill(block_.begin() + block_size_, block_.end(), 0U);
      Transform(block_);
      block_size_ = 0U;
    }
    std::fill(block_.begin() + block_size_, block_.begin() + 56U, 0U);
    for (std::size_t index = 0U; index < 8U; ++index) {
      block_[63U - index] =
          static_cast<std::uint8_t>(bit_count >> (index * 8U));
    }
    Transform(block_);
    for (std::size_t word = 0U; word < state_.size(); ++word) {
      for (std::size_t byte = 0U; byte < 4U; ++byte) {
        digest[word * 4U + byte] = static_cast<std::uint8_t>(
            state_[word] >> ((3U - byte) * 8U));
      }
    }
    return true;
  }

private:
  void Transform(const std::array<std::uint8_t, 64U> &block) noexcept {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
      const std::size_t offset = index * 4U;
      words[index] =
          (static_cast<std::uint32_t>(block[offset]) << 24U) |
          (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
          (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
          static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const std::uint32_t previous = words[index - 15U];
      const std::uint32_t recent = words[index - 2U];
      const std::uint32_t sigma_zero =
          RotateRight(previous, 7U) ^ RotateRight(previous, 18U) ^
          (previous >> 3U);
      const std::uint32_t sigma_one =
          RotateRight(recent, 17U) ^ RotateRight(recent, 19U) ^
          (recent >> 10U);
      words[index] = words[index - 16U] + sigma_zero + words[index - 7U] +
                     sigma_one;
    }

    std::uint32_t a = state_[0U];
    std::uint32_t b = state_[1U];
    std::uint32_t c = state_[2U];
    std::uint32_t d = state_[3U];
    std::uint32_t e = state_[4U];
    std::uint32_t f = state_[5U];
    std::uint32_t g = state_[6U];
    std::uint32_t h = state_[7U];
    for (std::size_t index = 0U; index < words.size(); ++index) {
      const std::uint32_t sum_one =
          RotateRight(e, 6U) ^ RotateRight(e, 11U) ^ RotateRight(e, 25U);
      const std::uint32_t choice = (e & f) ^ ((~e) & g);
      const std::uint32_t temporary_one =
          h + sum_one + choice + kSha256RoundConstants[index] + words[index];
      const std::uint32_t sum_zero =
          RotateRight(a, 2U) ^ RotateRight(a, 13U) ^ RotateRight(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary_two = sum_zero + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary_one;
      d = c;
      c = b;
      b = a;
      a = temporary_one + temporary_two;
    }
    state_[0U] += a;
    state_[1U] += b;
    state_[2U] += c;
    state_[3U] += d;
    state_[4U] += e;
    state_[5U] += f;
    state_[6U] += g;
    state_[7U] += h;
  }

  std::array<std::uint32_t, 8U> state_{{
      UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372),
      UINT32_C(0xa54ff53a), UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
      UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19),
  }};
  std::array<std::uint8_t, 64U> block_{};
  std::size_t block_size_ = 0U;
  std::uint64_t byte_count_ = 0U;
};

std::string HexDigest(const std::array<std::uint8_t, 32U> &digest) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const std::uint8_t byte : digest) {
    output << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return output.str();
}

bool HashFile(const std::filesystem::path &path, std::uint64_t expected_size,
              std::string &digest) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  Sha256 hash;
  std::array<char, 65536U> buffer{};
  std::uint64_t observed_size = 0U;
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0) {
      const std::uint64_t unsigned_count =
          static_cast<std::uint64_t>(count);
      if (unsigned_count >
              (std::numeric_limits<std::uint64_t>::max)() - observed_size ||
          !hash.Update(reinterpret_cast<const std::uint8_t *>(buffer.data()),
                       static_cast<std::size_t>(count))) {
        return false;
      }
      observed_size += unsigned_count;
    }
  }
  if (input.bad() || observed_size != expected_size) {
    return false;
  }
  std::array<std::uint8_t, 32U> bytes{};
  if (!hash.Final(bytes)) {
    return false;
  }
  digest = HexDigest(bytes);
  return true;
}

RenderOperationResult IntegrityFailure(const char *kind,
                                       const std::string &detail) {
  return RenderOperationResult::Failure(
      RenderOperationCode::RESOURCE_STALE,
      std::string("Ogre-Next N1 ") + kind +
          " media integrity failure: " + detail);
}

RenderOperationResult ReflectionIntegrityFailure(const std::string &detail) {
  return RenderOperationResult::Failure(
      RenderOperationCode::RESOURCE_STALE,
      "Ogre-Next reflection media integrity failure: " + detail);
}

struct RuntimeFile final {
  std::string relative_path;
  std::filesystem::path absolute_path;
};

RenderOperationResult ResolveSafeManifestDirectory(
    const std::filesystem::path &media_root,
    const std::string &relative_root, const char *kind,
    std::filesystem::path &resolved_root) {
  std::error_code error;
  const std::filesystem::file_status media_status =
      std::filesystem::symlink_status(media_root, error);
  if (error || std::filesystem::is_symlink(media_status) ||
      !std::filesystem::is_directory(media_status)) {
    return IntegrityFailure(kind,
                            "media root is missing, indirect, or not a directory");
  }

  const std::filesystem::path relative =
      std::filesystem::u8path(relative_root);
  if (relative.empty() || relative.is_absolute() || relative.has_root_path()) {
    return IntegrityFailure(kind, relative_root + " root path is invalid");
  }

  std::filesystem::path current = media_root;
  for (const std::filesystem::path &component : relative) {
    const std::string component_text = component.generic_u8string();
    if (component_text.empty() || component_text == "." ||
        component_text == "..") {
      return IntegrityFailure(kind, relative_root + " root path is invalid");
    }
    current /= component;
    error.clear();
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(current, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return IntegrityFailure(kind,
                              relative_root +
                                  " root is missing, indirect, or not a directory");
    }
  }
  resolved_root = std::move(current);
  return RenderOperationResult::Success();
}

RenderOperationResult VerifyMediaManifest(
    const std::string &resolved_media_root,
    const std::vector<std::string> &scan_roots,
    bool paths_are_relative_to_media_root,
    const OgreNextN1MediaManifestEntry *manifest,
    std::size_t manifest_count, const char *kind) {
  const std::filesystem::path media_root =
      std::filesystem::u8path(resolved_media_root);
  std::vector<RuntimeFile> runtime_files;
  std::error_code error;
  for (const std::string &relative_root : scan_roots) {
    std::filesystem::path root;
    const RenderOperationResult root_result = ResolveSafeManifestDirectory(
        media_root, relative_root, kind, root);
    if (!root_result.ok()) {
      return root_result;
    }

    error.clear();
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::none, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
      return IntegrityFailure(kind, relative_root + " tree cannot be enumerated");
    }
    while (iterator != end) {
      const std::filesystem::directory_entry &entry = *iterator;
      const std::filesystem::file_status status = entry.symlink_status(error);
      if (error) {
        return IntegrityFailure(kind, relative_root + " entry status cannot be read");
      }
      if (std::filesystem::is_symlink(status)) {
        return IntegrityFailure(kind, relative_root + " tree contains a symbolic link");
      }
      if (std::filesystem::is_regular_file(status)) {
        const std::filesystem::path relative = entry.path().lexically_relative(
            paths_are_relative_to_media_root ? media_root : root);
        const std::string generic = relative.generic_u8string();
        if (generic.empty() || generic == "." ||
            generic.rfind("../", 0U) == 0U) {
          return IntegrityFailure(kind, relative_root +
                                            " entry escaped its manifest root");
        }
        runtime_files.push_back(RuntimeFile{generic, entry.path()});
      } else if (!std::filesystem::is_directory(status)) {
        return IntegrityFailure(kind, relative_root +
                                          " tree contains a non-file entry");
      }
      iterator.increment(error);
      if (error) {
        return IntegrityFailure(kind, relative_root + " tree enumeration failed");
      }
    }
  }

  std::sort(runtime_files.begin(), runtime_files.end(),
            [](const RuntimeFile &lhs, const RuntimeFile &rhs) {
              return lhs.relative_path < rhs.relative_path;
            });
  if (runtime_files.size() != manifest_count) {
    return IntegrityFailure(kind,
                            "file count differs from the pinned manifest");
  }
  for (std::size_t index = 0U; index < runtime_files.size(); ++index) {
    const RuntimeFile &runtime = runtime_files[index];
    const OgreNextN1MediaManifestEntry &expected = manifest[index];
    if (runtime.relative_path != expected.relative_path) {
      return IntegrityFailure(kind,
                              "path set differs from the pinned manifest");
    }
    error.clear();
    const std::uintmax_t size =
        std::filesystem::file_size(runtime.absolute_path, error);
    if (error || size != expected.size) {
      return IntegrityFailure(kind, "byte size differs for " +
                                        runtime.relative_path);
    }
    std::string digest;
    if (!HashFile(runtime.absolute_path, expected.size, digest)) {
      return IntegrityFailure(kind, "file could not be hashed exactly: " +
                                        runtime.relative_path);
    }
    error.clear();
    const std::filesystem::file_status status_after_hash =
        std::filesystem::symlink_status(runtime.absolute_path, error);
    if (error || std::filesystem::is_symlink(status_after_hash) ||
        !std::filesystem::is_regular_file(status_after_hash)) {
      return IntegrityFailure(kind, "file became indirect while hashing: " +
                                        runtime.relative_path);
    }
    error.clear();
    const std::uintmax_t size_after_hash =
        std::filesystem::file_size(runtime.absolute_path, error);
    if (error || size_after_hash != expected.size ||
        digest != expected.sha256) {
      return IntegrityFailure(kind, "SHA-256 differs for " +
                                        runtime.relative_path);
    }
  }
  return RenderOperationResult::Success();
}

constexpr std::array<const char *, 4U> kReflectionManifestRoots{{
    "2.0/scripts/materials/Common",
    "2.0/scripts/materials/LocalCubemaps",
    "Compute/Algorithms/IBL",
    "Compute/Tools/Any",
}};

constexpr const char *kReflectionCommonRoot =
    "2.0/scripts/materials/Common";

bool ManifestPathIsUnderRoot(const std::string &relative_path,
                             const char *relative_root) {
  const std::string root(relative_root);
  return relative_path.size() > root.size() &&
         relative_path.compare(0U, root.size(), root) == 0 &&
         relative_path[root.size()] == '/';
}

RenderOperationResult ResolveSafeReflectionDirectory(
    const std::filesystem::path &media_root, const char *relative_root,
    std::filesystem::path &resolved_root) {
  const std::filesystem::path relative =
      std::filesystem::u8path(relative_root);
  if (relative.empty() || relative.is_absolute() || relative.has_root_path()) {
    return ReflectionIntegrityFailure(std::string(relative_root) +
                                      " root path is invalid");
  }

  std::filesystem::path current = media_root;
  std::error_code error;
  for (const std::filesystem::path &component : relative) {
    const std::string component_text = component.generic_u8string();
    if (component_text.empty() || component_text == "." ||
        component_text == "..") {
      return ReflectionIntegrityFailure(std::string(relative_root) +
                                        " root path is invalid");
    }
    current /= component;
    error.clear();
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(current, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return ReflectionIntegrityFailure(
          std::string(relative_root) +
          " is missing, indirect, or not a directory");
    }
  }
  resolved_root = std::move(current);
  return RenderOperationResult::Success();
}

RenderOperationResult EnumerateReflectionRoot(
    const std::filesystem::path &media_root, const char *relative_root,
    std::vector<RuntimeFile> &runtime_files) {
  std::filesystem::path root;
  const RenderOperationResult root_result = ResolveSafeReflectionDirectory(
      media_root, relative_root, root);
  if (!root_result.ok()) {
    return root_result;
  }

  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  if (error) {
    return ReflectionIntegrityFailure(std::string(relative_root) +
                                      " cannot be enumerated");
  }
  while (iterator != end) {
    const std::filesystem::directory_entry &entry = *iterator;
    const std::filesystem::file_status status = entry.symlink_status(error);
    if (error) {
      return ReflectionIntegrityFailure(
          "reflection media entry status cannot be read");
    }
    if (std::filesystem::is_symlink(status)) {
      return ReflectionIntegrityFailure(
          "reflection media contains a symbolic link");
    }
    if (std::filesystem::is_regular_file(status)) {
      const std::filesystem::path manifest_relative =
          entry.path().lexically_relative(media_root);
      const std::string generic = manifest_relative.generic_u8string();
      if (generic.empty() || generic == "." ||
          generic.rfind("../", 0U) == 0U) {
        return ReflectionIntegrityFailure(
            "reflection media escaped its manifest root");
      }
      runtime_files.push_back(RuntimeFile{generic, entry.path()});
    } else if (!std::filesystem::is_directory(status)) {
      return ReflectionIntegrityFailure(
          "reflection media contains a non-file entry");
    }
    iterator.increment(error);
    if (error) {
      return ReflectionIntegrityFailure(
          "reflection media enumeration failed");
    }
  }
  return RenderOperationResult::Success();
}

RenderOperationResult ValidateReflectionManifestEntry(
    const std::filesystem::path &media_root,
    const OgreNextReflectionMediaManifestEntry &expected) {
  if (expected.relative_path == nullptr || expected.sha256 == nullptr) {
    return ReflectionIntegrityFailure("manifest contains a null field");
  }
  const std::string relative_text(expected.relative_path);
  bool belongs_to_declared_root = false;
  for (const char *relative_root : kReflectionManifestRoots) {
    belongs_to_declared_root =
        belongs_to_declared_root ||
        ManifestPathIsUnderRoot(relative_text, relative_root);
  }
  if (!belongs_to_declared_root || relative_text.find('\\') != std::string::npos) {
    return ReflectionIntegrityFailure("manifest path is outside its declared roots");
  }

  const std::filesystem::path relative =
      std::filesystem::u8path(relative_text);
  if (relative.empty() || relative.is_absolute() || relative.has_root_path() ||
      relative.filename().empty() ||
      relative.generic_u8string() != relative_text) {
    return ReflectionIntegrityFailure("manifest path is invalid");
  }

  std::filesystem::path current = media_root;
  std::error_code error;
  for (const std::filesystem::path &component : relative.parent_path()) {
    const std::string component_text = component.generic_u8string();
    if (component_text.empty() || component_text == "." ||
        component_text == "..") {
      return ReflectionIntegrityFailure("manifest path is invalid");
    }
    current /= component;
    error.clear();
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(current, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return ReflectionIntegrityFailure(relative_text +
                                        " has an indirect or missing parent");
    }
  }

  const std::string filename = relative.filename().generic_u8string();
  if (filename.empty() || filename == "." || filename == "..") {
    return ReflectionIntegrityFailure("manifest path is invalid");
  }
  current /= relative.filename();
  error.clear();
  const std::filesystem::file_status status =
      std::filesystem::symlink_status(current, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    return ReflectionIntegrityFailure(relative_text +
                                      " is missing, indirect, or not a regular file");
  }

  const std::uintmax_t size = std::filesystem::file_size(current, error);
  if (error || size != expected.size) {
    return ReflectionIntegrityFailure("byte size differs for " + relative_text);
  }
  std::string digest;
  if (!HashFile(current, expected.size, digest)) {
    return ReflectionIntegrityFailure("file could not be hashed exactly: " +
                                      relative_text);
  }
  error.clear();
  const std::filesystem::file_status status_after_hash =
      std::filesystem::symlink_status(current, error);
  if (error || std::filesystem::is_symlink(status_after_hash) ||
      !std::filesystem::is_regular_file(status_after_hash)) {
    return ReflectionIntegrityFailure(relative_text +
                                      " became indirect while hashing");
  }
  error.clear();
  const std::uintmax_t size_after_hash =
      std::filesystem::file_size(current, error);
  if (error || size_after_hash != expected.size || digest != expected.sha256) {
    return ReflectionIntegrityFailure("SHA-256 differs for " + relative_text);
  }
  return RenderOperationResult::Success();
}

} // namespace

RenderOperationResult VerifyOgreNextN1ShaderMedia(
    const std::string &resolved_media_root) {
  try {
    return VerifyMediaManifest(
        resolved_media_root, {"Hlms"}, false,
        kOgreNextN1ShaderMediaManifest,
        kOgreNextN1ShaderMediaManifestCount, "shader");
  } catch (const std::bad_alloc &) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "Ogre-Next N1 shader media integrity check ran out of memory");
  } catch (const std::filesystem::filesystem_error &) {
    return IntegrityFailure("shader", "HLMS filesystem operation failed");
  }
}

RenderOperationResult VerifyOgreNextN1HdrMedia(
    const std::string &resolved_media_root) {
  try {
    return VerifyMediaManifest(
        resolved_media_root,
        {"2.0/scripts/Compositors", "2.0/scripts/materials/Common",
         "2.0/scripts/materials/HDR"},
        true, kOgreNextN1HdrMediaManifest,
        kOgreNextN1HdrMediaManifestCount, "HDR compositor");
  } catch (const std::bad_alloc &) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "Ogre-Next N1 HDR compositor media integrity check ran out of memory");
  } catch (const std::filesystem::filesystem_error &) {
    return IntegrityFailure("HDR compositor",
                            "filesystem operation failed");
  }
}

RenderOperationResult VerifyOgreNextN1PresentationMedia(
    const std::string &resolved_presentation_media_root) {
  try {
    return VerifyMediaManifest(
        resolved_presentation_media_root, {"CommonCopy"}, true,
        kOgreNextN1PresentationMediaManifest,
        kOgreNextN1PresentationMediaManifestCount, "presentation copy");
  } catch (const std::bad_alloc &) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "Ogre-Next N1 presentation media integrity check ran out of memory");
  } catch (const std::filesystem::filesystem_error &) {
    return IntegrityFailure("presentation copy",
                            "filesystem operation failed");
  }
}

RenderOperationResult VerifyOgreNextReflectionProbeMedia(
    const std::string &resolved_media_root,
    bool hdr_common_tree_authenticated) {
  try {
    const std::filesystem::path media_root =
        std::filesystem::u8path(resolved_media_root);
    std::error_code error;
    const std::filesystem::file_status media_status =
        std::filesystem::symlink_status(media_root, error);
    if (error || !std::filesystem::is_directory(media_status) ||
        std::filesystem::is_symlink(media_status)) {
      return ReflectionIntegrityFailure(
          "media root is missing, indirect, or not a directory");
    }

    std::vector<RuntimeFile> runtime_files;
    for (const char *relative_root : kReflectionManifestRoots) {
      if (hdr_common_tree_authenticated &&
          std::string(relative_root) == kReflectionCommonRoot) {
        continue;
      }
      const RenderOperationResult enumeration_result =
          EnumerateReflectionRoot(media_root, relative_root, runtime_files);
      if (!enumeration_result.ok()) {
        return enumeration_result;
      }
    }

    std::sort(runtime_files.begin(), runtime_files.end(),
              [](const RuntimeFile &lhs, const RuntimeFile &rhs) {
                return lhs.relative_path < rhs.relative_path;
              });
    std::vector<const OgreNextReflectionMediaManifestEntry *>
        exact_manifest_entries;
    exact_manifest_entries.reserve(kOgreNextReflectionMediaManifestCount);
    for (std::size_t index = 0U;
         index < kOgreNextReflectionMediaManifestCount; ++index) {
      const OgreNextReflectionMediaManifestEntry &entry =
          kOgreNextReflectionMediaManifest[index];
      if (entry.relative_path == nullptr || entry.sha256 == nullptr) {
        return ReflectionIntegrityFailure("manifest contains a null field");
      }
      if (!hdr_common_tree_authenticated ||
          !ManifestPathIsUnderRoot(entry.relative_path,
                                   kReflectionCommonRoot)) {
        exact_manifest_entries.push_back(&entry);
      }
    }
    std::sort(exact_manifest_entries.begin(), exact_manifest_entries.end(),
              [](const OgreNextReflectionMediaManifestEntry *lhs,
                 const OgreNextReflectionMediaManifestEntry *rhs) {
                return std::string(lhs->relative_path) <
                       std::string(rhs->relative_path);
              });
    if (runtime_files.size() != exact_manifest_entries.size()) {
      return ReflectionIntegrityFailure(
          "file count differs from the pinned manifest");
    }
    for (std::size_t index = 0U; index < runtime_files.size(); ++index) {
      const RuntimeFile &runtime = runtime_files[index];
      const OgreNextReflectionMediaManifestEntry &expected =
          *exact_manifest_entries[index];
      if (runtime.relative_path != expected.relative_path) {
        return ReflectionIntegrityFailure(
            "path set differs from the pinned manifest");
      }
    }

    // Exact closure and per-entry authenticity are separate invariants. In
    // HDR mode the HDR manifest proves the entire shared Common tree, while
    // this pass still proves that every Common resource declared by the
    // reflection manifest is the expected regular file and exact bytes.
    for (std::size_t index = 0U;
         index < kOgreNextReflectionMediaManifestCount; ++index) {
      const RenderOperationResult entry_result =
          ValidateReflectionManifestEntry(
              media_root, kOgreNextReflectionMediaManifest[index]);
      if (!entry_result.ok()) {
        return entry_result;
      }
    }
    return RenderOperationResult::Success();
  } catch (const std::bad_alloc &) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "Ogre-Next reflection media integrity check ran out of memory");
  } catch (const std::filesystem::filesystem_error &) {
    return ReflectionIntegrityFailure("filesystem operation failed");
  }
}

} // namespace RoR::Render
