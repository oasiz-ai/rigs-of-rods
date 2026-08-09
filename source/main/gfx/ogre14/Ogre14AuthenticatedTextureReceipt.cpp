/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14AuthenticatedTextureReceipt.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

bool IsIdentifier(const std::string &value, bool allow_empty = false) noexcept {
  return (allow_empty || !value.empty()) &&
         value.size() <= kOgre14AuthenticatedTextureMaximumIdentifierBytes &&
         value.find('\0') == std::string::npos;
}

bool IsKnownSourceKind(Ogre14AuthenticatedTextureSourceKind kind) noexcept {
  switch (kind) {
  case Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER:
  case Ogre14AuthenticatedTextureSourceKind::VERSIONED_GENERATED_FALLBACK:
    return true;
  }
  return false;
}

bool IsKnownBindingKind(Ogre14AuthenticatedTextureBindingKind kind) noexcept {
  switch (kind) {
  case Ogre14AuthenticatedTextureBindingKind::RESOURCE:
  case Ogre14AuthenticatedTextureBindingKind::PRE_RESOURCE_TOKEN:
    return true;
  }
  return false;
}

std::uint32_t ReadLittleEndian32(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint32_t RotateRight(std::uint32_t value, std::uint32_t count) noexcept {
  return (value >> count) | (value << (32U - count));
}

std::string Sha256(const std::vector<std::uint8_t> &bytes) {
  static constexpr std::array<std::uint32_t, 64U> kRoundConstants = {
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
      0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
      0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
      0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
      0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
      0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
      0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
      0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
      0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
      0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
      0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
      0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
      0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
      0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
      0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
  std::array<std::uint32_t, 8U> hash = {
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

  const std::uint64_t bit_count =
      static_cast<std::uint64_t>(bytes.size()) * 8ULL;
  std::size_t padded_size = bytes.size() + 1U + 8U;
  if (padded_size < bytes.size()) {
    throw std::length_error("SHA-256 padded byte count overflow");
  }
  const std::size_t remainder = padded_size % 64U;
  if (remainder != 0U) {
    const std::size_t padding = 64U - remainder;
    if (padded_size >
        (std::numeric_limits<std::size_t>::max)() - padding) {
      throw std::length_error("SHA-256 padded byte count overflow");
    }
    padded_size += padding;
  }
  std::vector<std::uint8_t> padded(padded_size, 0U);
  std::copy(bytes.begin(), bytes.end(), padded.begin());
  padded[bytes.size()] = 0x80U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    padded[padded_size - 1U - index] = static_cast<std::uint8_t>(
        (bit_count >> (index * 8U)) & 0xffU);
  }

  for (std::size_t block = 0U; block < padded.size(); block += 64U) {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t word = 0U; word < 16U; ++word) {
      const std::size_t offset = block + word * 4U;
      words[word] = (static_cast<std::uint32_t>(padded[offset]) << 24U) |
                    (static_cast<std::uint32_t>(padded[offset + 1U]) << 16U) |
                    (static_cast<std::uint32_t>(padded[offset + 2U]) << 8U) |
                    static_cast<std::uint32_t>(padded[offset + 3U]);
    }
    for (std::size_t word = 16U; word < words.size(); ++word) {
      const std::uint32_t s0 =
          RotateRight(words[word - 15U], 7U) ^
          RotateRight(words[word - 15U], 18U) ^
          (words[word - 15U] >> 3U);
      const std::uint32_t s1 =
          RotateRight(words[word - 2U], 17U) ^
          RotateRight(words[word - 2U], 19U) ^
          (words[word - 2U] >> 10U);
      words[word] = words[word - 16U] + s0 + words[word - 7U] + s1;
    }

    std::uint32_t a = hash[0U];
    std::uint32_t b = hash[1U];
    std::uint32_t c = hash[2U];
    std::uint32_t d = hash[3U];
    std::uint32_t e = hash[4U];
    std::uint32_t f = hash[5U];
    std::uint32_t g = hash[6U];
    std::uint32_t h = hash[7U];
    for (std::size_t round = 0U; round < words.size(); ++round) {
      const std::uint32_t big_s1 =
          RotateRight(e, 6U) ^ RotateRight(e, 11U) ^ RotateRight(e, 25U);
      const std::uint32_t choose = (e & f) ^ ((~e) & g);
      const std::uint32_t temporary1 =
          h + big_s1 + choose + kRoundConstants[round] + words[round];
      const std::uint32_t big_s0 =
          RotateRight(a, 2U) ^ RotateRight(a, 13U) ^ RotateRight(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary2 = big_s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    hash[0U] += a;
    hash[1U] += b;
    hash[2U] += c;
    hash[3U] += d;
    hash[4U] += e;
    hash[5U] += f;
    hash[6U] += g;
    hash[7U] += h;
  }

  static constexpr char kHex[] = "0123456789abcdef";
  std::string digest(64U, '0');
  for (std::size_t word = 0U; word < hash.size(); ++word) {
    for (std::size_t nibble = 0U; nibble < 8U; ++nibble) {
      const std::uint32_t shift =
          static_cast<std::uint32_t>((7U - nibble) * 4U);
      digest[word * 8U + nibble] = kHex[(hash[word] >> shift) & 0x0fU];
    }
  }
  return digest;
}

ValidationResult ParseDdsFacts(const std::vector<std::uint8_t> &bytes,
                               Ogre14SourceDdsHeaderFacts &facts) {
  Ogre14SourceDdsHeaderFacts candidate;
  if (bytes.size() < 4U || bytes[0U] != 'D' || bytes[1U] != 'D' ||
      bytes[2U] != 'S' || bytes[3U] != ' ') {
    facts = candidate;
    return ValidationResult::Success();
  }
  if (bytes.size() < 128U) {
    return Failure(ValidationCode::SIZE_MISMATCH, "texture_receipt.dds",
                   "DDS magic is present but the legacy header is truncated");
  }
  candidate.header_size = ReadLittleEndian32(bytes.data() + 4U);
  candidate.flags = ReadLittleEndian32(bytes.data() + 8U);
  candidate.height = ReadLittleEndian32(bytes.data() + 12U);
  candidate.width = ReadLittleEndian32(bytes.data() + 16U);
  candidate.pitch_or_linear_size = ReadLittleEndian32(bytes.data() + 20U);
  candidate.depth = ReadLittleEndian32(bytes.data() + 24U);
  candidate.mip_map_count = ReadLittleEndian32(bytes.data() + 28U);
  for (std::size_t index = 0U; index < candidate.reserved1.size(); ++index) {
    candidate.reserved1[index] =
        ReadLittleEndian32(bytes.data() + 32U + index * 4U);
  }
  candidate.pixel_format_size = ReadLittleEndian32(bytes.data() + 76U);
  candidate.pixel_format_flags = ReadLittleEndian32(bytes.data() + 80U);
  candidate.four_cc = ReadLittleEndian32(bytes.data() + 84U);
  candidate.rgb_bit_count = ReadLittleEndian32(bytes.data() + 88U);
  candidate.red_mask = ReadLittleEndian32(bytes.data() + 92U);
  candidate.green_mask = ReadLittleEndian32(bytes.data() + 96U);
  candidate.blue_mask = ReadLittleEndian32(bytes.data() + 100U);
  candidate.alpha_mask = ReadLittleEndian32(bytes.data() + 104U);
  candidate.caps = ReadLittleEndian32(bytes.data() + 108U);
  candidate.caps2 = ReadLittleEndian32(bytes.data() + 112U);
  candidate.caps3 = ReadLittleEndian32(bytes.data() + 116U);
  candidate.caps4 = ReadLittleEndian32(bytes.data() + 120U);
  candidate.reserved2 = ReadLittleEndian32(bytes.data() + 124U);
  if (candidate.header_size != 124U ||
      candidate.pixel_format_size != 32U) {
    return Failure(ValidationCode::SIZE_MISMATCH, "texture_receipt.dds",
                   "DDS header or pixel-format structure size is invalid");
  }
  if (candidate.width == 0U || candidate.height == 0U) {
    return Failure(ValidationCode::INVALID_DIMENSIONS, "texture_receipt.dds",
                   "DDS source dimensions must be nonzero");
  }

  static constexpr std::uint32_t kDx10FourCc = 0x30315844U;
  if (candidate.four_cc == kDx10FourCc) {
    if (bytes.size() < 148U) {
      return Failure(ValidationCode::SIZE_MISMATCH, "texture_receipt.dds.dx10",
                     "DDS DX10 marker is present but its header is truncated");
    }
    candidate.kind = Ogre14SourceDdsHeaderKind::DX10;
    candidate.dxgi_format = ReadLittleEndian32(bytes.data() + 128U);
    candidate.resource_dimension = ReadLittleEndian32(bytes.data() + 132U);
    candidate.misc_flag = ReadLittleEndian32(bytes.data() + 136U);
    candidate.array_size = ReadLittleEndian32(bytes.data() + 140U);
    candidate.misc_flags2 = ReadLittleEndian32(bytes.data() + 144U);
    if (candidate.array_size == 0U) {
      return Failure(ValidationCode::INVALID_DIMENSIONS,
                     "texture_receipt.dds.dx10.array_size",
                     "DDS DX10 array size must be nonzero");
    }
  } else {
    candidate.kind = Ogre14SourceDdsHeaderKind::LEGACY;
  }
  facts = candidate;
  return ValidationResult::Success();
}

std::uint64_t IdentityBytes(
    const Ogre14AuthenticatedTextureCaptureInput &input) noexcept {
  const std::array<const std::string *, 8U> strings = {
      &input.effective_resource_group,
      &input.archive_identity,
      &input.archive_name,
      &input.archive_type,
      &input.archive_sha256,
      &input.exact_member_name,
      &input.generated_fallback_rule,
      &input.binding.exact_resource_name};
  std::uint64_t total = 0U;
  for (const std::string *value : strings) {
    total += static_cast<std::uint64_t>(value->size());
  }
  return total;
}

bool SameSourceProvenance(
    const Ogre14AuthenticatedTextureReceiptMetadata &lhs,
    const Ogre14AuthenticatedTextureReceiptMetadata &rhs) noexcept {
  const auto &a = lhs.source;
  const auto &b = rhs.source;
  return a.source_kind == b.source_kind &&
         a.effective_resource_group == b.effective_resource_group &&
         a.group_generation == b.group_generation &&
         a.archive_identity == b.archive_identity &&
         a.archive_name == b.archive_name &&
         a.archive_type == b.archive_type &&
         a.archive_sha256 == b.archive_sha256 &&
         a.archive_pointer_token == b.archive_pointer_token &&
         a.exact_member_name == b.exact_member_name &&
         a.generated_fallback_rule == b.generated_fallback_rule &&
         a.generated_fallback_rule_version ==
             b.generated_fallback_rule_version &&
         a.binding.kind == b.binding.kind &&
         a.binding.resource_pointer_token ==
             b.binding.resource_pointer_token &&
         a.binding.resource_handle == b.binding.resource_handle &&
         a.binding.pre_resource_token == b.binding.pre_resource_token &&
         a.binding.exact_resource_name == b.binding.exact_resource_name &&
         lhs.byte_count == rhs.byte_count &&
         lhs.bytes_sha256 == rhs.bytes_sha256;
}

struct BindingKey final {
  Ogre14AuthenticatedTextureBindingKind kind =
      Ogre14AuthenticatedTextureBindingKind::RESOURCE;
  std::uintptr_t resource_pointer_token = 0U;
  std::uint64_t pre_resource_token = 0U;
};

struct BindingKeyLess final {
  bool operator()(const BindingKey &lhs, const BindingKey &rhs) const noexcept {
    if (lhs.kind != rhs.kind) {
      return static_cast<std::uint8_t>(lhs.kind) <
             static_cast<std::uint8_t>(rhs.kind);
    }
    if (lhs.resource_pointer_token != rhs.resource_pointer_token) {
      return lhs.resource_pointer_token < rhs.resource_pointer_token;
    }
    return lhs.pre_resource_token < rhs.pre_resource_token;
  }
};

BindingKey MakeBindingKey(
    const Ogre14AuthenticatedTextureResourceBinding &binding) noexcept {
  BindingKey key;
  key.kind = binding.kind;
  key.resource_pointer_token = binding.resource_pointer_token;
  key.pre_resource_token = binding.pre_resource_token;
  return key;
}

struct GroupRecord final {
  std::uint64_t generation = 0U;
  bool active = false;
};

} // namespace

struct Ogre14AuthenticatedTextureReceipt::State final {
  Ogre14AuthenticatedTextureReceiptMetadata metadata;
  std::vector<std::uint8_t> bytes;
  std::uint64_t identity_bytes = 0U;
};

struct Ogre14AuthenticatedTextureReceiptRegistry::State final {
  Ogre14AuthenticatedTextureRegistryConfiguration configuration;
  std::map<std::string, GroupRecord> groups;
  std::map<BindingKey, Ogre14AuthenticatedTextureReceipt, BindingKeyLess>
      receipts;
  std::uint64_t retained_source_bytes = 0U;
  std::uint64_t retained_identity_bytes = 0U;
  std::uint64_t maximum_group_generation_seen = 0U;
};

Ogre14AuthenticatedTextureReceipt::Ogre14AuthenticatedTextureReceipt(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14AuthenticatedTextureReceipt::initialized() const noexcept {
  return state_ != nullptr;
}

const Ogre14AuthenticatedTextureReceiptMetadata *
Ogre14AuthenticatedTextureReceipt::metadata() const noexcept {
  return state_ != nullptr ? &state_->metadata : nullptr;
}

const std::uint8_t *
Ogre14AuthenticatedTextureReceipt::source_bytes() const noexcept {
  return state_ != nullptr && !state_->bytes.empty() ? state_->bytes.data()
                                                     : nullptr;
}

std::size_t Ogre14AuthenticatedTextureReceipt::source_size() const noexcept {
  return state_ != nullptr ? state_->bytes.size() : 0U;
}

std::uint64_t
Ogre14AuthenticatedTextureReceipt::identity_size() const noexcept {
  return state_ != nullptr ? state_->identity_bytes : 0U;
}

bool Ogre14AuthenticatedTextureReceipt::ReplacementBytesMatch(
    const void *bytes, std::size_t size) const noexcept {
  if (state_ == nullptr || size != state_->bytes.size() ||
      (size != 0U && bytes == nullptr)) {
    return false;
  }
  return size == 0U ||
         std::memcmp(state_->bytes.data(), bytes, size) == 0;
}

bool Ogre14AuthenticatedTextureReceipt::SharesImmutableStateWith(
    const Ogre14AuthenticatedTextureReceipt &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

Ogre14AuthenticatedTextureReceiptRegistry::
    Ogre14AuthenticatedTextureReceiptRegistry(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14AuthenticatedTextureReceiptRegistry::initialized() const noexcept {
  return state_ != nullptr;
}

std::size_t Ogre14AuthenticatedTextureReceiptRegistry::size() const noexcept {
  return state_ != nullptr ? state_->receipts.size() : 0U;
}

std::uint64_t
Ogre14AuthenticatedTextureReceiptRegistry::retained_source_bytes() const
    noexcept {
  return state_ != nullptr ? state_->retained_source_bytes : 0U;
}

std::uint64_t
Ogre14AuthenticatedTextureReceiptRegistry::maximum_group_generation_seen()
    const noexcept {
  return state_ != nullptr ? state_->maximum_group_generation_seen : 0U;
}

bool Ogre14AuthenticatedTextureReceiptRegistry::SharesImmutableStateWith(
    const Ogre14AuthenticatedTextureReceiptRegistry &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult Ogre14AuthenticatedTextureReceiptRegistry::FindResource(
    const std::string &effective_resource_group,
    std::uint64_t group_generation, std::uintptr_t resource_pointer_token,
    std::uint64_t resource_handle, const std::string &exact_resource_name,
    Ogre14AuthenticatedTextureReceipt &receipt) const {
  if (state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE, "texture_registry",
                   "authenticated texture registry is not initialized");
  }
  if (!IsIdentifier(effective_resource_group) ||
      !IsIdentifier(exact_resource_name) || group_generation == 0U ||
      resource_pointer_token == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "texture_registry.resource_lookup",
                   "resource lookup identity is incomplete");
  }
  const auto group = state_->groups.find(effective_resource_group);
  if (group == state_->groups.end() || !group->second.active ||
      group->second.generation != group_generation) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "texture_registry.resource_lookup.group_generation",
                   "resource lookup does not target the active generation");
  }
  BindingKey key;
  key.kind = Ogre14AuthenticatedTextureBindingKind::RESOURCE;
  key.resource_pointer_token = resource_pointer_token;
  const auto found = state_->receipts.find(key);
  if (found == state_->receipts.end() || found->second.metadata() == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "texture_registry.resource_lookup",
                   "exact authenticated texture resource is absent");
  }
  const auto &source = found->second.metadata()->source;
  if (source.effective_resource_group != effective_resource_group ||
      source.group_generation != group_generation ||
      source.binding.resource_handle != resource_handle ||
      source.binding.exact_resource_name != exact_resource_name) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "texture_registry.resource_lookup",
                   "live pointer does not match its authenticated binding");
  }
  receipt = found->second;
  return ValidationResult::Success();
}

bool IsLowercaseOgre14Sha256(const std::string &value) noexcept {
  if (value.size() != 64U) {
    return false;
  }
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

ValidationResult SelectOgre14AuthenticatedTextureArchiveMember(
    bool archive_case_sensitive, bool allow_zip_basename_fallback,
    const Ogre14AuthenticatedTextureArchiveMemberObservation *observations,
    std::size_t observation_count, std::string &exact_member_name) {
  if (observation_count == 0U || observations == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "texture_archive_selection.members",
                   "selected archive exposes no member observations");
  }
  if (observation_count >
      kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_archive_selection.member_count",
                   "selected archive member count exceeds its hard cap");
  }
  if (archive_case_sensitive && allow_zip_basename_fallback) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "texture_archive_selection.mode",
                   "case-sensitive archives cannot use a folded Zip fallback");
  }

  std::size_t exact_full_count = 0U;
  std::size_t folded_full_count = 0U;
  std::size_t folded_basename_count = 0U;
  const std::string *first_exact_full = nullptr;
  const std::string *first_folded_full = nullptr;
  const std::string *first_folded_basename = nullptr;
  std::uint64_t identity_bytes = 0U;
  for (std::size_t index = 0U; index < observation_count; ++index) {
    const auto &observation = observations[index];
    if (!IsIdentifier(observation.exact_member_name) ||
        (observation.exact_full_match &&
         !observation.folded_full_match)) {
      return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                     "texture_archive_selection.observation",
                     "archive member observation is incomplete or inconsistent");
    }
    const std::uint64_t member_identity_bytes =
        static_cast<std::uint64_t>(observation.exact_member_name.size());
    if (identity_bytes >
        kOgre14AuthenticatedTextureMaximumArchiveMemberIdentityBytes -
            member_identity_bytes) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "texture_archive_selection.identity_bytes",
                     "archive member identities exceed their hard byte cap");
    }
    identity_bytes += member_identity_bytes;
    if (observation.exact_full_match) {
      if (exact_full_count == 0U) {
        first_exact_full = &observation.exact_member_name;
      }
      ++exact_full_count;
    }
    if (observation.folded_full_match) {
      if (folded_full_count == 0U) {
        first_folded_full = &observation.exact_member_name;
      }
      ++folded_full_count;
    }
    if (observation.folded_basename_match) {
      if (folded_basename_count == 0U) {
        first_folded_basename = &observation.exact_member_name;
      }
      ++folded_basename_count;
    }
  }

  const std::string *selected = nullptr;
  std::size_t selected_count = 0U;
  if (archive_case_sensitive) {
    selected = first_exact_full;
    selected_count = exact_full_count;
  } else if (folded_full_count != 0U) {
    selected = first_folded_full;
    selected_count = folded_full_count;
  } else if (allow_zip_basename_fallback) {
    selected = first_folded_basename;
    selected_count = folded_basename_count;
  }
  if (selected == nullptr || selected_count == 0U) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "texture_archive_selection.member",
                   "selected archive has no exact unambiguous member");
  }
  if (selected_count != 1U) {
    return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                   "texture_archive_selection.member_collision",
                   "selected archive member identity is ambiguous");
  }

  try {
    std::string candidate = *selected;
    exact_member_name.swap(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "texture_archive_selection.allocation",
                   "allocation failed before exact member publication");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_archive_selection.allocation",
                   "exact member publication exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "texture_archive_selection.exception",
                   "unexpected exception before exact member publication");
  }
}

ValidationResult ValidateOgre14AuthenticatedTextureRegistryConfiguration(
    const Ogre14AuthenticatedTextureRegistryConfiguration &configuration) {
  if (configuration.version != kOgre14AuthenticatedTextureRegistryVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "texture_registry.configuration.version",
                   "unsupported authenticated texture registry version");
  }
  if (configuration.maximum_live_receipts == 0U ||
      configuration.maximum_live_receipts >
          kOgre14AuthenticatedTextureMaximumLiveReceipts ||
      configuration.maximum_group_records == 0U ||
      configuration.maximum_group_records >
          kOgre14AuthenticatedTextureMaximumGroupRecords ||
      configuration.maximum_source_bytes == 0U ||
      configuration.maximum_source_bytes >
          kOgre14AuthenticatedTextureMaximumSourceBytes ||
      configuration.maximum_retained_source_bytes == 0U ||
      configuration.maximum_retained_source_bytes >
          kOgre14AuthenticatedTextureMaximumRetainedBytes ||
      configuration.maximum_source_bytes >
          configuration.maximum_retained_source_bytes ||
      configuration.maximum_total_identity_bytes == 0U ||
      configuration.maximum_total_identity_bytes >
          kOgre14AuthenticatedTextureMaximumIdentityBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_registry.configuration.limits",
                   "authenticated texture limits are zero, inconsistent, or exceed hard caps");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateOgre14AuthenticatedTextureCaptureInput(
    const Ogre14AuthenticatedTextureCaptureInput &input) {
  if (input.version != kOgre14AuthenticatedTextureCaptureInputVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "texture_capture.version",
                   "unsupported authenticated texture capture version");
  }
  if (!IsKnownSourceKind(input.source_kind) ||
      !IsKnownBindingKind(input.binding.kind)) {
    return Failure(ValidationCode::INVALID_ENUM, "texture_capture",
                   "authenticated texture capture contains an unknown enum");
  }
  if (!IsIdentifier(input.effective_resource_group) ||
      !IsIdentifier(input.archive_sha256) ||
      !IsIdentifier(input.exact_member_name) ||
      !IsIdentifier(input.binding.exact_resource_name) ||
      input.group_generation == 0U ||
      !IsLowercaseOgre14Sha256(input.archive_sha256)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "texture_capture.identity",
                   "group, generation, exact names, or authenticated SHA-256 are invalid");
  }

  if (input.binding.kind ==
      Ogre14AuthenticatedTextureBindingKind::RESOURCE) {
    if (input.binding.resource_pointer_token == 0U ||
        input.binding.pre_resource_token != 0U) {
      return Failure(ValidationCode::INVALID_HANDLE,
                     "texture_capture.resource_binding",
                     "resource binding requires one exact nonzero pointer token");
    }
  } else if (input.binding.resource_pointer_token != 0U ||
             input.binding.resource_handle != 0U ||
             input.binding.resource_state_count != 0U ||
             input.binding.pre_resource_token == 0U) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "texture_capture.pre_resource_binding",
                   "pre-resource binding must contain only one nonzero minted token");
  }

  if (input.source_kind ==
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER) {
    if (!IsIdentifier(input.archive_identity) ||
        !IsIdentifier(input.archive_name) ||
        !IsIdentifier(input.archive_type) ||
        input.archive_pointer_token == 0U ||
        !input.generated_fallback_rule.empty() ||
        input.generated_fallback_rule_version != 0U) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "texture_capture.archive",
                     "archive source lacks one exact authenticated archive identity or carries fallback metadata");
    }
  } else {
    if (!input.archive_identity.empty() || !input.archive_name.empty() ||
        !input.archive_type.empty() || input.archive_pointer_token != 0U ||
        input.generated_fallback_rule !=
            kOgre14GeneratedTextureFallbackRule ||
        input.generated_fallback_rule_version !=
            kOgre14GeneratedTextureFallbackRuleVersion ||
        input.exact_member_name != input.binding.exact_resource_name) {
      return Failure(ValidationCode::UNSUPPORTED_VERSION,
                     "texture_capture.generated_fallback",
                     "generated bytes do not name the exact supported fallback rule/version");
    }
  }
  return ValidationResult::Success();
}

ValidationResult BuildOgre14AuthenticatedTextureReceipt(
    const Ogre14AuthenticatedTextureRegistryConfiguration &configuration,
    const Ogre14AuthenticatedTextureCaptureInput &input,
    const void *source_bytes, std::size_t source_size,
    Ogre14AuthenticatedTextureReceipt &output,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector) {
  const ValidationResult configuration_validation =
      ValidateOgre14AuthenticatedTextureRegistryConfiguration(configuration);
  if (!configuration_validation) {
    return configuration_validation;
  }
  const ValidationResult input_validation =
      ValidateOgre14AuthenticatedTextureCaptureInput(input);
  if (!input_validation) {
    return input_validation;
  }
  if (source_size == 0U || source_bytes == nullptr) {
    return Failure(ValidationCode::EMPTY_PAYLOAD, "texture_receipt.bytes",
                   "authenticated source texture bytes are empty");
  }
  if (static_cast<std::uint64_t>(source_size) >
      configuration.maximum_source_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_receipt.byte_count",
                   "authenticated source texture exceeds its byte cap");
  }
  const std::uint64_t identity_bytes = IdentityBytes(input);
  if (identity_bytes > configuration.maximum_total_identity_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_receipt.identity_bytes",
                   "authenticated source texture identity exceeds its byte cap");
  }

  try {
    auto candidate =
        std::make_shared<Ogre14AuthenticatedTextureReceipt::State>();
    candidate->metadata.source = input;
    candidate->bytes.resize(source_size);
    std::memcpy(candidate->bytes.data(), source_bytes, source_size);
    candidate->identity_bytes = identity_bytes;
    if (fault_injector != nullptr) {
      fault_injector->BeforeAuthenticatedTextureStage(
          Ogre14AuthenticatedTextureTransactionStage::
              AFTER_SOURCE_BYTES_COPIED);
    }
    candidate->metadata.byte_count =
        static_cast<std::uint64_t>(candidate->bytes.size());
    candidate->metadata.bytes_sha256 = Sha256(candidate->bytes);
    const ValidationResult dds_validation =
        ParseDdsFacts(candidate->bytes, candidate->metadata.dds);
    if (!dds_validation) {
      return dds_validation;
    }
    if (fault_injector != nullptr) {
      fault_injector->BeforeAuthenticatedTextureStage(
          Ogre14AuthenticatedTextureTransactionStage::BEFORE_RECEIPT_COMMIT);
    }
    output = Ogre14AuthenticatedTextureReceipt(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "texture_receipt.allocation",
                   "allocation failed before texture receipt commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_receipt.allocation",
                   "texture receipt allocation exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "texture_receipt.exception",
                   "unexpected exception before texture receipt commit");
  }
}

ValidationResult InitializeOgre14AuthenticatedTextureReceiptRegistry(
    const Ogre14AuthenticatedTextureRegistryConfiguration &configuration,
    Ogre14AuthenticatedTextureReceiptRegistry &output) {
  const ValidationResult validation =
      ValidateOgre14AuthenticatedTextureRegistryConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  try {
    auto candidate =
        std::make_shared<Ogre14AuthenticatedTextureReceiptRegistry::State>();
    candidate->configuration = configuration;
    output = Ogre14AuthenticatedTextureReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "texture_registry.allocation",
                   "allocation failed before texture registry initialization");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "texture_registry.exception",
                   "unexpected exception before texture registry initialization");
  }
}

ValidationResult AdvanceOgre14AuthenticatedTextureGroupGeneration(
    const std::string &effective_resource_group,
    std::uint64_t new_group_generation,
    Ogre14AuthenticatedTextureReceiptRegistry &registry,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector) {
  if (registry.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE, "texture_registry",
                   "authenticated texture registry is not initialized");
  }
  if (!IsIdentifier(effective_resource_group)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "texture_registry.group",
                   "effective resource group is invalid");
  }
  if (new_group_generation == 0U ||
      new_group_generation <=
          registry.state_->maximum_group_generation_seen) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "texture_registry.group_generation",
                   "group generation is not globally strictly monotonic");
  }
  const bool new_group =
      registry.state_->groups.find(effective_resource_group) ==
      registry.state_->groups.end();
  if (new_group &&
      registry.state_->groups.size() >=
          registry.state_->configuration.maximum_group_records) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_registry.group_count",
                   "authenticated texture group-record cap was exceeded");
  }
  const std::uint64_t group_identity_bytes =
      static_cast<std::uint64_t>(effective_resource_group.size());
  if (new_group &&
      (group_identity_bytes >
           registry.state_->configuration.maximum_total_identity_bytes ||
       registry.state_->retained_identity_bytes >
           registry.state_->configuration.maximum_total_identity_bytes -
               group_identity_bytes)) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_registry.group_identity_bytes",
                   "authenticated texture group identities exceed the byte cap");
  }
  try {
    auto candidate = std::make_shared<
        Ogre14AuthenticatedTextureReceiptRegistry::State>(*registry.state_);
    for (auto receipt = candidate->receipts.begin();
         receipt != candidate->receipts.end();) {
      const auto *metadata = receipt->second.metadata();
      if (metadata != nullptr &&
          metadata->source.effective_resource_group ==
              effective_resource_group) {
        candidate->retained_source_bytes -= metadata->byte_count;
        candidate->retained_identity_bytes -= receipt->second.identity_size();
        receipt = candidate->receipts.erase(receipt);
      } else {
        ++receipt;
      }
    }
    candidate->groups[effective_resource_group] =
        GroupRecord{new_group_generation, true};
    if (new_group) {
      candidate->retained_identity_bytes += group_identity_bytes;
    }
    candidate->maximum_group_generation_seen = new_group_generation;
    if (fault_injector != nullptr) {
      fault_injector->BeforeAuthenticatedTextureStage(
          Ogre14AuthenticatedTextureTransactionStage::
              BEFORE_GROUP_TRANSITION_COMMIT);
    }
    registry =
        Ogre14AuthenticatedTextureReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "texture_registry.group_transition.allocation",
                   "allocation failed before group generation commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_registry.group_transition.allocation",
                   "group generation allocation exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "texture_registry.group_transition.exception",
                   "unexpected exception before group generation commit");
  }
}

ValidationResult TeardownOgre14AuthenticatedTextureGroup(
    const std::string &effective_resource_group,
    std::uint64_t exact_group_generation,
    Ogre14AuthenticatedTextureReceiptRegistry &registry,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector) {
  if (registry.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE, "texture_registry",
                   "authenticated texture registry is not initialized");
  }
  const auto group = registry.state_->groups.find(effective_resource_group);
  if (group == registry.state_->groups.end() || !group->second.active ||
      group->second.generation != exact_group_generation) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "texture_registry.group_teardown",
                   "group teardown does not match one active generation");
  }
  try {
    auto candidate = std::make_shared<
        Ogre14AuthenticatedTextureReceiptRegistry::State>(*registry.state_);
    for (auto receipt = candidate->receipts.begin();
         receipt != candidate->receipts.end();) {
      const auto *metadata = receipt->second.metadata();
      if (metadata != nullptr &&
          metadata->source.effective_resource_group ==
              effective_resource_group) {
        candidate->retained_source_bytes -= metadata->byte_count;
        candidate->retained_identity_bytes -= receipt->second.identity_size();
        receipt = candidate->receipts.erase(receipt);
      } else {
        ++receipt;
      }
    }
    candidate->groups[effective_resource_group].active = false;
    if (fault_injector != nullptr) {
      fault_injector->BeforeAuthenticatedTextureStage(
          Ogre14AuthenticatedTextureTransactionStage::
              BEFORE_GROUP_TRANSITION_COMMIT);
    }
    registry =
        Ogre14AuthenticatedTextureReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "texture_registry.group_teardown.allocation",
                   "allocation failed before group teardown commit");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "texture_registry.group_teardown.exception",
                   "unexpected exception before group teardown commit");
  }
}

ValidationResult CommitOgre14AuthenticatedTextureReceipt(
    const Ogre14AuthenticatedTextureReceipt &receipt,
    Ogre14AuthenticatedTextureReceiptRegistry &registry,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector) {
  if (registry.state_ == nullptr || receipt.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE, "texture_registry",
                   "registry or immutable receipt is not initialized");
  }
  const auto &metadata = receipt.state_->metadata;
  const ValidationResult input_validation =
      ValidateOgre14AuthenticatedTextureCaptureInput(metadata.source);
  if (!input_validation || metadata.version !=
                               kOgre14AuthenticatedTextureReceiptVersion ||
      metadata.byte_count != receipt.state_->bytes.size() ||
      !IsLowercaseOgre14Sha256(metadata.bytes_sha256)) {
    return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                   "texture_registry.receipt",
                   "receipt is not a complete versioned immutable capture");
  }
  const auto group = registry.state_->groups.find(
      metadata.source.effective_resource_group);
  if (group == registry.state_->groups.end() || !group->second.active ||
      group->second.generation != metadata.source.group_generation) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "texture_registry.receipt.group_generation",
                   "receipt does not target the active group generation");
  }
  if (metadata.byte_count >
          registry.state_->configuration.maximum_source_bytes ||
      receipt.state_->identity_bytes >
          registry.state_->configuration.maximum_total_identity_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_registry.receipt.limits",
                   "receipt exceeds the registry's configured limits");
  }

  const BindingKey key = MakeBindingKey(metadata.source.binding);
  const auto existing = registry.state_->receipts.find(key);
  bool reload = false;
  if (existing != registry.state_->receipts.end()) {
    const auto *prior = existing->second.metadata();
    if (prior == nullptr ||
        metadata.source.binding.kind !=
            Ogre14AuthenticatedTextureBindingKind::RESOURCE ||
        !SameSourceProvenance(*prior, metadata) ||
        !existing->second.ReplacementBytesMatch(
            receipt.source_bytes(), receipt.source_size()) ||
        metadata.source.binding.resource_state_count <
            prior->source.binding.resource_state_count) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "texture_registry.binding_collision",
                     "binding collides with a live receipt or stale pointer generation");
    }
    if (metadata.source.binding.resource_state_count ==
        prior->source.binding.resource_state_count) {
      // resourceLoading() runs before OGRE decodes or uploads the returned
      // MemoryDataStream. A downstream failure leaves getStateCount()
      // unchanged, so the exact same immutable source must be retryable. The
      // existing registry snapshot is already the desired state; publish
      // nothing and preserve strong rollback semantics.
      return ValidationResult::Success();
    }
    reload = true;
  }

  if (metadata.source.binding.kind ==
      Ogre14AuthenticatedTextureBindingKind::RESOURCE) {
    for (const auto &entry : registry.state_->receipts) {
      if (existing != registry.state_->receipts.end() &&
          &entry == &*existing) {
        continue;
      }
      const auto *other = entry.second.metadata();
      if (other != nullptr &&
          other->source.binding.kind ==
              Ogre14AuthenticatedTextureBindingKind::RESOURCE &&
          other->source.effective_resource_group ==
              metadata.source.effective_resource_group &&
          other->source.group_generation ==
              metadata.source.group_generation &&
          other->source.binding.resource_handle ==
              metadata.source.binding.resource_handle) {
        return Failure(ValidationCode::INVALID_HANDLE,
                       "texture_registry.resource_handle_collision",
                       "one live resource handle maps to multiple pointers");
      }
    }
  }

  const std::size_t prospective_count =
      registry.state_->receipts.size() + (reload ? 0U : 1U);
  const std::uint64_t prior_source_bytes =
      reload ? existing->second.metadata()->byte_count : 0U;
  const std::uint64_t prior_identity_bytes =
      reload ? existing->second.identity_size() : 0U;
  if (prospective_count >
      registry.state_->configuration.maximum_live_receipts) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_registry.receipt_count",
                   "live authenticated texture receipt cap was exceeded");
  }
  if (registry.state_->retained_source_bytes < prior_source_bytes ||
      registry.state_->retained_identity_bytes < prior_identity_bytes) {
    return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                   "texture_registry.accounting",
                   "immutable texture registry accounting is inconsistent");
  }
  const std::uint64_t base_source_bytes =
      registry.state_->retained_source_bytes - prior_source_bytes;
  const std::uint64_t base_identity_bytes =
      registry.state_->retained_identity_bytes - prior_identity_bytes;
  if (base_source_bytes >
          registry.state_->configuration.maximum_retained_source_bytes -
              metadata.byte_count ||
      base_identity_bytes >
          registry.state_->configuration.maximum_total_identity_bytes -
              receipt.identity_size()) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_registry.retained_bytes",
                   "retained authenticated texture data exceeds configured caps");
  }

  try {
    auto candidate = std::make_shared<
        Ogre14AuthenticatedTextureReceiptRegistry::State>(*registry.state_);
    if (reload) {
      candidate->receipts[key] = receipt;
    } else {
      const auto inserted = candidate->receipts.emplace(key, receipt);
      if (!inserted.second) {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "texture_registry.binding_collision",
                       "candidate registry rejected a duplicate binding");
      }
    }
    candidate->retained_source_bytes =
        base_source_bytes + metadata.byte_count;
    candidate->retained_identity_bytes =
        base_identity_bytes + receipt.identity_size();
    if (fault_injector != nullptr) {
      fault_injector->BeforeAuthenticatedTextureStage(
          Ogre14AuthenticatedTextureTransactionStage::BEFORE_REGISTRY_COMMIT);
    }
    registry =
        Ogre14AuthenticatedTextureReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "texture_registry.receipt_commit.allocation",
                   "allocation failed before receipt registry commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "texture_registry.receipt_commit.allocation",
                   "receipt registry allocation exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "texture_registry.receipt_commit.exception",
                   "unexpected exception before receipt registry commit");
  }
}

ValidationResult RemoveOgre14AuthenticatedTextureResource(
    const std::string &effective_resource_group,
    std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
    const std::string &exact_resource_name,
    Ogre14AuthenticatedTextureReceiptRegistry &registry,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector) {
  if (registry.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE, "texture_registry",
                   "authenticated texture registry is not initialized");
  }
  if (!IsIdentifier(effective_resource_group) ||
      !IsIdentifier(exact_resource_name) || resource_pointer_token == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "texture_registry.resource_remove",
                   "resource removal identity is incomplete");
  }
  BindingKey key;
  key.kind = Ogre14AuthenticatedTextureBindingKind::RESOURCE;
  key.resource_pointer_token = resource_pointer_token;
  const auto found = registry.state_->receipts.find(key);
  if (found == registry.state_->receipts.end()) {
    return ValidationResult::Success();
  }
  const auto *metadata = found->second.metadata();
  if (metadata == nullptr ||
      metadata->source.effective_resource_group != effective_resource_group ||
      metadata->source.binding.resource_handle != resource_handle ||
      metadata->source.binding.exact_resource_name != exact_resource_name) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "texture_registry.resource_remove",
                   "live pointer removal does not match its exact receipt binding");
  }

  try {
    auto candidate = std::make_shared<
        Ogre14AuthenticatedTextureReceiptRegistry::State>(*registry.state_);
    candidate->retained_source_bytes -= metadata->byte_count;
    candidate->retained_identity_bytes -= found->second.identity_size();
    candidate->receipts.erase(key);
    if (fault_injector != nullptr) {
      fault_injector->BeforeAuthenticatedTextureStage(
          Ogre14AuthenticatedTextureTransactionStage::BEFORE_REGISTRY_COMMIT);
    }
    registry =
        Ogre14AuthenticatedTextureReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "texture_registry.resource_remove.allocation",
                   "allocation failed before resource removal commit");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "texture_registry.resource_remove.exception",
                   "unexpected exception before resource removal commit");
  }
}

} // namespace RoR::Render
