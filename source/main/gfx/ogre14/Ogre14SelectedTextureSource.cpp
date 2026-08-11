/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14SelectedTextureSource.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <tuple>
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
         value.size() <= kOgre14SelectedTextureSourceMaximumIdentifierBytes &&
         value.find('\0') == std::string::npos;
}

bool IsKnownSourceKind(Ogre14SelectedTextureSourceKind kind) noexcept {
  switch (kind) {
  case Ogre14SelectedTextureSourceKind::
      UNAUTHENTICATED_PACKAGE_ARCHIVE_MEMBER:
    return true;
  }
  return false;
}

bool AddBytes(std::uint64_t value, std::uint64_t &total) noexcept {
  if (value > (std::numeric_limits<std::uint64_t>::max)() - total) {
    return false;
  }
  total += value;
  return true;
}

std::uint64_t IdentityBytes(
    const Ogre14SelectedTextureSourceCaptureInput &input) noexcept {
  const std::array<const std::string *, 9U> strings = {
      &input.effective_resource_group,
      &input.selected_archive_name,
      &input.selected_archive_type,
      &input.file_info_filename,
      &input.file_info_path,
      &input.file_info_basename,
      &input.exact_member_name,
      &input.opened_stream_name,
      &input.exact_resource_name};
  std::uint64_t total = 0U;
  for (const std::string *value : strings) {
    if (!AddBytes(static_cast<std::uint64_t>(value->size()), total)) {
      return (std::numeric_limits<std::uint64_t>::max)();
    }
  }
  if (!AddBytes(64U, total)) {
    return (std::numeric_limits<std::uint64_t>::max)();
  }
  return total;
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

  if (bytes.size() >
      (std::numeric_limits<std::uint64_t>::max)() / 8ULL) {
    throw std::length_error("SHA-256 bit count overflow");
  }
  const std::uint64_t bit_count =
      static_cast<std::uint64_t>(bytes.size()) * 8ULL;
  if (bytes.size() >
      (std::numeric_limits<std::size_t>::max)() - 9U) {
    throw std::length_error("SHA-256 padded byte count overflow");
  }
  std::size_t padded_size = bytes.size() + 9U;
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

bool IsLowercaseSha256(const std::string &value) noexcept {
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

bool SameResourceIdentity(
    const Ogre14SelectedTextureSourceCaptureInput &lhs,
    const Ogre14SelectedTextureSourceCaptureInput &rhs) noexcept {
  return lhs.effective_resource_group == rhs.effective_resource_group &&
         lhs.group_generation == rhs.group_generation &&
         lhs.resource_pointer_token == rhs.resource_pointer_token &&
         lhs.resource_handle == rhs.resource_handle &&
         lhs.exact_resource_name == rhs.exact_resource_name;
}

bool SameCapture(const Ogre14SelectedTextureSourceReceiptMetadata &lhs,
                 const Ogre14SelectedTextureSourceReceiptMetadata &rhs)
    noexcept {
  const auto &a = lhs.source;
  const auto &b = rhs.source;
  return a.version == b.version && a.source_kind == b.source_kind &&
         SameResourceIdentity(a, b) &&
         a.selected_archive_name == b.selected_archive_name &&
         a.selected_archive_type == b.selected_archive_type &&
         a.selected_archive_pointer_token ==
             b.selected_archive_pointer_token &&
         a.file_info_archive_pointer_token ==
             b.file_info_archive_pointer_token &&
         a.file_info_filename == b.file_info_filename &&
         a.file_info_path == b.file_info_path &&
         a.file_info_basename == b.file_info_basename &&
         a.exact_member_name == b.exact_member_name &&
         a.file_info_compressed_size == b.file_info_compressed_size &&
         a.file_info_uncompressed_size == b.file_info_uncompressed_size &&
         a.opened_stream_name == b.opened_stream_name &&
         a.opened_stream_size == b.opened_stream_size &&
         a.resource_state_count_before_load ==
             b.resource_state_count_before_load &&
         lhs.byte_count == rhs.byte_count &&
         lhs.observed_bytes_sha256 == rhs.observed_bytes_sha256;
}

struct ResourceKey final {
  std::uintptr_t pointer_token = 0U;

  bool operator<(const ResourceKey &other) const noexcept {
    return pointer_token < other.pointer_token;
  }
};

struct GroupRecord final {
  std::uint64_t generation = 0U;
  bool active = false;
};

} // namespace

struct Ogre14SelectedTextureSourceReceipt::State final {
  Ogre14SelectedTextureSourceReceiptMetadata metadata;
  std::vector<std::uint8_t> bytes;
  std::uint64_t identity_bytes = 0U;
};

struct Ogre14SelectedTextureSourceReceiptRegistry::State final {
  Ogre14SelectedTextureSourceRegistryConfiguration configuration;
  std::map<std::string, GroupRecord> groups;
  std::map<ResourceKey, Ogre14SelectedTextureSourceReceipt> receipts;
  std::uint64_t retained_source_bytes = 0U;
  std::uint64_t retained_identity_bytes = 0U;
  std::uint64_t maximum_group_generation_seen = 0U;
};

struct Ogre14SelectedTextureSourceResolution::State final {
  std::uint32_t version = kOgre14SelectedTextureSourceResolutionVersion;
  Ogre14SelectedTextureSourceReceiptRegistry registry_snapshot;
  Ogre14SelectedTextureSourceReceipt exact_source_receipt;
  std::uintptr_t resolver_pointer_token = 0U;
  std::uint64_t loaded_resource_state_count = 0U;
};

Ogre14SelectedTextureSourceReceipt::Ogre14SelectedTextureSourceReceipt(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14SelectedTextureSourceReceipt::initialized() const noexcept {
  return state_ != nullptr;
}

const Ogre14SelectedTextureSourceReceiptMetadata *
Ogre14SelectedTextureSourceReceipt::metadata() const noexcept {
  return state_ != nullptr ? &state_->metadata : nullptr;
}

const std::uint8_t *
Ogre14SelectedTextureSourceReceipt::source_bytes() const noexcept {
  return state_ != nullptr && !state_->bytes.empty() ? state_->bytes.data()
                                                     : nullptr;
}

std::size_t Ogre14SelectedTextureSourceReceipt::source_size() const noexcept {
  return state_ != nullptr ? state_->bytes.size() : 0U;
}

std::uint64_t
Ogre14SelectedTextureSourceReceipt::identity_size() const noexcept {
  return state_ != nullptr ? state_->identity_bytes : 0U;
}

bool Ogre14SelectedTextureSourceReceipt::ReplacementBytesMatch(
    const void *bytes, std::size_t size) const noexcept {
  if (state_ == nullptr || size != state_->bytes.size() ||
      (size != 0U && bytes == nullptr)) {
    return false;
  }
  return size == 0U || std::memcmp(state_->bytes.data(), bytes, size) == 0;
}

bool Ogre14SelectedTextureSourceReceipt::SharesImmutableStateWith(
    const Ogre14SelectedTextureSourceReceipt &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

Ogre14SelectedTextureSourceReceiptRegistry::
    Ogre14SelectedTextureSourceReceiptRegistry(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14SelectedTextureSourceReceiptRegistry::initialized() const noexcept {
  return state_ != nullptr;
}

std::size_t Ogre14SelectedTextureSourceReceiptRegistry::size() const noexcept {
  return state_ != nullptr ? state_->receipts.size() : 0U;
}

std::uint64_t
Ogre14SelectedTextureSourceReceiptRegistry::retained_source_bytes() const
    noexcept {
  return state_ != nullptr ? state_->retained_source_bytes : 0U;
}

std::uint64_t
Ogre14SelectedTextureSourceReceiptRegistry::retained_identity_bytes() const
    noexcept {
  return state_ != nullptr ? state_->retained_identity_bytes : 0U;
}

std::uint64_t Ogre14SelectedTextureSourceReceiptRegistry::
    maximum_group_generation_seen() const noexcept {
  return state_ != nullptr ? state_->maximum_group_generation_seen : 0U;
}

bool Ogre14SelectedTextureSourceReceiptRegistry::SharesImmutableStateWith(
    const Ogre14SelectedTextureSourceReceiptRegistry &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

Ogre14SelectedTextureSourceResolution::Ogre14SelectedTextureSourceResolution(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14SelectedTextureSourceResolution::initialized() const noexcept {
  return state_ != nullptr &&
         state_->version == kOgre14SelectedTextureSourceResolutionVersion &&
         state_->registry_snapshot.initialized() &&
         state_->exact_source_receipt.initialized() &&
         state_->resolver_pointer_token != 0U &&
         state_->loaded_resource_state_count != 0U;
}

std::uint32_t Ogre14SelectedTextureSourceResolution::version() const noexcept {
  return state_ != nullptr ? state_->version : 0U;
}

const Ogre14SelectedTextureSourceReceipt *
Ogre14SelectedTextureSourceResolution::source_receipt() const noexcept {
  return initialized() ? &state_->exact_source_receipt : nullptr;
}

std::uint64_t Ogre14SelectedTextureSourceResolution::
    loaded_resource_state_count() const noexcept {
  return initialized() ? state_->loaded_resource_state_count : 0U;
}

bool Ogre14SelectedTextureSourceResolution::
    SharesLoadedResourceAuthorityWith(
        const Ogre14SelectedTextureSourceResolution &other) const noexcept {
  return initialized() && other.initialized() &&
         state_->registry_snapshot.SharesImmutableStateWith(
             other.state_->registry_snapshot) &&
         state_->exact_source_receipt.SharesImmutableStateWith(
             other.state_->exact_source_receipt) &&
         state_->resolver_pointer_token == other.state_->resolver_pointer_token &&
         state_->loaded_resource_state_count ==
             other.state_->loaded_resource_state_count;
}

bool Ogre14SelectedTextureSourceResolution::MatchesResolver(
    const IOgre14SelectedTextureSourceResolver &resolver) const noexcept {
  return initialized() &&
         state_->resolver_pointer_token ==
             reinterpret_cast<std::uintptr_t>(&resolver);
}

bool Ogre14SelectedTextureSourceResolution::MatchesLoadedResourceIdentity(
    std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
    const std::string &exact_resource_group,
    const std::string &exact_resource_name,
    std::uint64_t loaded_resource_state_count) const noexcept {
  if (!initialized() || resource_pointer_token == 0U ||
      loaded_resource_state_count == 0U ||
      loaded_resource_state_count != state_->loaded_resource_state_count) {
    return false;
  }
  const auto *metadata = state_->exact_source_receipt.metadata();
  return metadata != nullptr &&
         metadata->source.resource_pointer_token == resource_pointer_token &&
         metadata->source.resource_handle == resource_handle &&
         metadata->source.effective_resource_group == exact_resource_group &&
         metadata->source.exact_resource_name == exact_resource_name;
}

ValidationResult ValidateOgre14SelectedTextureSourceRegistryConfiguration(
    const Ogre14SelectedTextureSourceRegistryConfiguration &configuration) {
  if (configuration.version != kOgre14SelectedTextureSourceRegistryVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "selected_texture_registry.configuration.version",
                   "unsupported selected-texture registry version");
  }
  if (configuration.maximum_live_receipts == 0U ||
      configuration.maximum_live_receipts >
          kOgre14SelectedTextureSourceMaximumLiveReceipts ||
      configuration.maximum_group_records == 0U ||
      configuration.maximum_group_records >
          kOgre14SelectedTextureSourceMaximumGroupRecords ||
      configuration.maximum_source_bytes == 0U ||
      configuration.maximum_source_bytes >
          kOgre14SelectedTextureSourceMaximumBytes ||
      configuration.maximum_retained_source_bytes == 0U ||
      configuration.maximum_retained_source_bytes >
          kOgre14SelectedTextureSourceMaximumRetainedBytes ||
      configuration.maximum_source_bytes >
          configuration.maximum_retained_source_bytes ||
      configuration.maximum_total_identity_bytes == 0U ||
      configuration.maximum_total_identity_bytes >
          kOgre14SelectedTextureSourceMaximumIdentityBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_registry.configuration.limits",
                   "selected-texture limits are zero, inconsistent, or exceed hard caps");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateOgre14SelectedTextureSourceCaptureInput(
    const Ogre14SelectedTextureSourceCaptureInput &input) {
  if (input.version != kOgre14SelectedTextureSourceCaptureInputVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "selected_texture_capture.version",
                   "unsupported selected-texture capture version");
  }
  if (!IsKnownSourceKind(input.source_kind)) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "selected_texture_capture.source_kind",
                   "selected-texture source kind is invalid");
  }
  const std::array<const std::string *, 8U> required = {
      &input.effective_resource_group,
      &input.selected_archive_name,
      &input.selected_archive_type,
      &input.file_info_filename,
      &input.file_info_basename,
      &input.exact_member_name,
      &input.opened_stream_name,
      &input.exact_resource_name};
  for (const std::string *value : required) {
    if (!IsIdentifier(*value)) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "selected_texture_capture.identifier",
                     "required selected-texture identity is invalid");
    }
  }
  if (!IsIdentifier(input.file_info_path, true) ||
      input.file_info_path.size() >
          kOgre14SelectedTextureSourceMaximumIdentifierBytes -
              input.file_info_basename.size() ||
      input.exact_member_name.size() !=
          input.file_info_path.size() + input.file_info_basename.size() ||
      input.exact_member_name.compare(0U, input.file_info_path.size(),
                                      input.file_info_path) != 0 ||
      input.exact_member_name.compare(input.file_info_path.size(),
                                      input.file_info_basename.size(),
                                      input.file_info_basename) != 0) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "selected_texture_capture.exact_member_name",
                   "exact member must equal FileInfo path plus basename");
  }
  if (input.group_generation == 0U ||
      input.selected_archive_pointer_token == 0U ||
      input.file_info_archive_pointer_token == 0U ||
      input.opened_stream_pointer_token == 0U ||
      input.resource_pointer_token == 0U) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "selected_texture_capture.pointer_identity",
                   "generation, archive, stream, and resource identities must be nonzero");
  }
  if (input.file_info_archive_pointer_token !=
      input.selected_archive_pointer_token) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "selected_texture_capture.file_info_archive",
                   "FileInfo archive is not the exact selected archive");
  }
  if (input.file_info_compressed_size == 0U ||
      input.file_info_uncompressed_size == 0U ||
      input.opened_stream_size == 0U ||
      input.opened_stream_size != input.file_info_uncompressed_size) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "selected_texture_capture.source_sizes",
                   "FileInfo and opened-stream sizes are empty or inconsistent");
  }
  return ValidationResult::Success();
}

ValidationResult BuildOgre14SelectedTextureSourceReceipt(
    const Ogre14SelectedTextureSourceRegistryConfiguration &configuration,
    const Ogre14SelectedTextureSourceCaptureInput &input,
    const void *source_bytes, std::size_t source_size,
    Ogre14SelectedTextureSourceReceipt &output,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector) {
  ValidationResult validation =
      ValidateOgre14SelectedTextureSourceRegistryConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  validation = ValidateOgre14SelectedTextureSourceCaptureInput(input);
  if (!validation) {
    return validation;
  }
  if (source_bytes == nullptr || source_size == 0U) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "selected_texture_receipt.bytes",
                   "selected source texture bytes are empty");
  }
  if (static_cast<std::uint64_t>(source_size) !=
          input.file_info_uncompressed_size ||
      static_cast<std::uint64_t>(source_size) != input.opened_stream_size) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "selected_texture_receipt.byte_count",
                   "retained bytes do not match FileInfo and stream sizes");
  }
  if (static_cast<std::uint64_t>(source_size) >
      configuration.maximum_source_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_receipt.byte_count",
                   "selected source texture exceeds its byte cap");
  }
  const std::uint64_t identity_bytes = IdentityBytes(input);
  if (identity_bytes > configuration.maximum_total_identity_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_receipt.identity_bytes",
                   "selected texture identity exceeds its byte cap");
  }

  try {
    auto candidate =
        std::make_shared<Ogre14SelectedTextureSourceReceipt::State>();
    candidate->metadata.source = input;
    candidate->bytes.resize(source_size);
    std::memcpy(candidate->bytes.data(), source_bytes, source_size);
    candidate->identity_bytes = identity_bytes;
    if (fault_injector != nullptr) {
      fault_injector->BeforeSelectedTextureSourceStage(
          Ogre14SelectedTextureSourceTransactionStage::
              AFTER_SOURCE_BYTES_COPIED);
    }
    candidate->metadata.byte_count =
        static_cast<std::uint64_t>(candidate->bytes.size());
    candidate->metadata.observed_bytes_sha256 = Sha256(candidate->bytes);
    if (fault_injector != nullptr) {
      fault_injector->BeforeSelectedTextureSourceStage(
          Ogre14SelectedTextureSourceTransactionStage::BEFORE_RECEIPT_COMMIT);
    }
    output = Ogre14SelectedTextureSourceReceipt(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "selected_texture_receipt.allocation",
                   "allocation failed before selected texture receipt commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_receipt.allocation",
                   "selected texture receipt exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "selected_texture_receipt.exception",
                   "unexpected exception before selected texture receipt commit");
  }
}

ValidationResult InitializeOgre14SelectedTextureSourceRegistry(
    const Ogre14SelectedTextureSourceRegistryConfiguration &configuration,
    Ogre14SelectedTextureSourceReceiptRegistry &output) {
  const ValidationResult validation =
      ValidateOgre14SelectedTextureSourceRegistryConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  try {
    auto candidate = std::make_shared<
        Ogre14SelectedTextureSourceReceiptRegistry::State>();
    candidate->configuration = configuration;
    output = Ogre14SelectedTextureSourceReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "selected_texture_registry.allocation",
                   "allocation failed before selected texture registry initialization");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "selected_texture_registry.exception",
                   "unexpected exception before selected texture registry initialization");
  }
}

ValidationResult AdvanceOgre14SelectedTextureSourceGroupGeneration(
    const std::string &effective_resource_group,
    std::uint64_t new_group_generation,
    Ogre14SelectedTextureSourceReceiptRegistry &registry,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector) {
  if (registry.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "selected_texture_registry",
                   "selected texture registry is not initialized");
  }
  if (!IsIdentifier(effective_resource_group)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "selected_texture_registry.group",
                   "effective resource group is invalid");
  }
  if (new_group_generation == 0U ||
      new_group_generation <=
          registry.state_->maximum_group_generation_seen) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "selected_texture_registry.group_generation",
                   "group generation is not globally strictly monotonic");
  }
  const bool new_group = registry.state_->groups.find(effective_resource_group) ==
                         registry.state_->groups.end();
  if (new_group && registry.state_->groups.size() >=
                       registry.state_->configuration.maximum_group_records) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_registry.group_count",
                   "selected texture group-record cap was exceeded");
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
                   "selected_texture_registry.group_identity_bytes",
                   "selected texture group identities exceed the byte cap");
  }

  try {
    auto candidate = std::make_shared<
        Ogre14SelectedTextureSourceReceiptRegistry::State>(*registry.state_);
    for (auto receipt = candidate->receipts.begin();
         receipt != candidate->receipts.end();) {
      const auto *metadata = receipt->second.metadata();
      if (metadata != nullptr && metadata->source.effective_resource_group ==
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
      fault_injector->BeforeSelectedTextureSourceStage(
          Ogre14SelectedTextureSourceTransactionStage::
              BEFORE_GROUP_TRANSITION_COMMIT);
    }
    registry =
        Ogre14SelectedTextureSourceReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "selected_texture_registry.group_transition.allocation",
                   "allocation failed before group generation commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_registry.group_transition.allocation",
                   "group generation exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "selected_texture_registry.group_transition.exception",
                   "unexpected exception before group generation commit");
  }
}

ValidationResult TeardownOgre14SelectedTextureSourceGroup(
    const std::string &effective_resource_group,
    std::uint64_t exact_group_generation,
    Ogre14SelectedTextureSourceReceiptRegistry &registry,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector) {
  if (registry.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "selected_texture_registry",
                   "selected texture registry is not initialized");
  }
  const auto group = registry.state_->groups.find(effective_resource_group);
  if (group == registry.state_->groups.end() || !group->second.active ||
      group->second.generation != exact_group_generation) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "selected_texture_registry.group_teardown",
                   "group teardown does not match one active generation");
  }

  try {
    auto candidate = std::make_shared<
        Ogre14SelectedTextureSourceReceiptRegistry::State>(*registry.state_);
    for (auto receipt = candidate->receipts.begin();
         receipt != candidate->receipts.end();) {
      const auto *metadata = receipt->second.metadata();
      if (metadata != nullptr && metadata->source.effective_resource_group ==
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
      fault_injector->BeforeSelectedTextureSourceStage(
          Ogre14SelectedTextureSourceTransactionStage::
              BEFORE_GROUP_TRANSITION_COMMIT);
    }
    registry =
        Ogre14SelectedTextureSourceReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "selected_texture_registry.group_teardown.allocation",
                   "allocation failed before group teardown commit");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "selected_texture_registry.group_teardown.exception",
                   "unexpected exception before group teardown commit");
  }
}

ValidationResult CommitOgre14SelectedTextureSourceReceipt(
    const Ogre14SelectedTextureSourceReceipt &receipt,
    Ogre14SelectedTextureSourceReceiptRegistry &registry,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector) {
  if (registry.state_ == nullptr || receipt.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "selected_texture_registry",
                   "registry or selected source receipt is not initialized");
  }
  const auto &metadata = receipt.state_->metadata;
  const ValidationResult validation =
      ValidateOgre14SelectedTextureSourceCaptureInput(metadata.source);
  if (!validation ||
      metadata.version != kOgre14SelectedTextureSourceReceiptVersion ||
      metadata.byte_count != receipt.state_->bytes.size() ||
      metadata.byte_count != metadata.source.file_info_uncompressed_size ||
      metadata.byte_count != metadata.source.opened_stream_size ||
      !IsLowercaseSha256(metadata.observed_bytes_sha256)) {
    return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                   "selected_texture_registry.receipt",
                   "receipt is not a complete immutable selected-source capture");
  }
  const auto group = registry.state_->groups.find(
      metadata.source.effective_resource_group);
  if (group == registry.state_->groups.end() || !group->second.active ||
      group->second.generation != metadata.source.group_generation) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "selected_texture_registry.receipt.group_generation",
                   "receipt does not target the active group generation");
  }
  if (metadata.byte_count >
          registry.state_->configuration.maximum_source_bytes ||
      receipt.state_->identity_bytes >
          registry.state_->configuration.maximum_total_identity_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_registry.receipt.limits",
                   "receipt exceeds the registry's configured limits");
  }

  const ResourceKey key{metadata.source.resource_pointer_token};
  const auto existing = registry.state_->receipts.find(key);
  bool reload = false;
  if (existing != registry.state_->receipts.end()) {
    const auto *prior = existing->second.metadata();
    if (prior == nullptr || !SameResourceIdentity(prior->source,
                                                  metadata.source)) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "selected_texture_registry.pointer_collision",
                     "live resource pointer was reused for a different identity");
    }
    if (metadata.source.resource_state_count_before_load ==
        prior->source.resource_state_count_before_load) {
      if (SameCapture(*prior, metadata) &&
          existing->second.ReplacementBytesMatch(receipt.source_bytes(),
                                                 receipt.source_size())) {
        // The selected member and bytes are unchanged, but a downstream load
        // retry normally arrives through a fresh DataStream. Replace the
        // publication so its exact stream observation describes the attempt
        // that can actually complete the native load.
        reload = true;
      } else {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "selected_texture_registry.same_state_change",
                       "same pre-load state selected different source provenance or bytes");
      }
    } else if (metadata.source.resource_state_count_before_load <
        prior->source.resource_state_count_before_load) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "selected_texture_registry.reload_state",
                     "stale texture reload cannot replace a newer receipt");
    } else {
      reload = true;
    }
  }

  for (const auto &entry : registry.state_->receipts) {
    if (existing != registry.state_->receipts.end() &&
        entry.first.pointer_token == existing->first.pointer_token) {
      continue;
    }
    const auto *other = entry.second.metadata();
    if (other != nullptr &&
        other->source.effective_resource_group ==
            metadata.source.effective_resource_group &&
        other->source.group_generation == metadata.source.group_generation &&
        other->source.resource_handle == metadata.source.resource_handle) {
      return Failure(ValidationCode::INVALID_HANDLE,
                     "selected_texture_registry.resource_handle_collision",
                     "one live resource handle maps to multiple pointers");
    }
  }

  const std::size_t prospective_count =
      registry.state_->receipts.size() + (reload ? 0U : 1U);
  if (prospective_count >
      registry.state_->configuration.maximum_live_receipts) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_registry.receipt_count",
                   "live selected texture receipt cap was exceeded");
  }
  const std::uint64_t prior_source_bytes =
      reload ? existing->second.metadata()->byte_count : 0U;
  const std::uint64_t prior_identity_bytes =
      reload ? existing->second.identity_size() : 0U;
  if (registry.state_->retained_source_bytes < prior_source_bytes ||
      registry.state_->retained_identity_bytes < prior_identity_bytes) {
    return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                   "selected_texture_registry.accounting",
                   "selected texture registry accounting is inconsistent");
  }
  const std::uint64_t base_source_bytes =
      registry.state_->retained_source_bytes - prior_source_bytes;
  const std::uint64_t base_identity_bytes =
      registry.state_->retained_identity_bytes - prior_identity_bytes;
  if (metadata.byte_count >
          registry.state_->configuration.maximum_retained_source_bytes ||
      base_source_bytes >
          registry.state_->configuration.maximum_retained_source_bytes -
              metadata.byte_count ||
      receipt.identity_size() >
          registry.state_->configuration.maximum_total_identity_bytes ||
      base_identity_bytes >
          registry.state_->configuration.maximum_total_identity_bytes -
              receipt.identity_size()) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_registry.retained_bytes",
                   "retained selected texture data exceeds configured caps");
  }

  try {
    auto candidate = std::make_shared<
        Ogre14SelectedTextureSourceReceiptRegistry::State>(*registry.state_);
    if (reload) {
      candidate->receipts[key] = receipt;
    } else {
      const auto inserted = candidate->receipts.emplace(key, receipt);
      if (!inserted.second) {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "selected_texture_registry.pointer_collision",
                       "candidate rejected a duplicate resource pointer");
      }
    }
    candidate->retained_source_bytes =
        base_source_bytes + metadata.byte_count;
    candidate->retained_identity_bytes =
        base_identity_bytes + receipt.identity_size();
    if (fault_injector != nullptr) {
      fault_injector->BeforeSelectedTextureSourceStage(
          Ogre14SelectedTextureSourceTransactionStage::BEFORE_REGISTRY_COMMIT);
    }
    registry =
        Ogre14SelectedTextureSourceReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "selected_texture_registry.receipt_commit.allocation",
                   "allocation failed before selected texture receipt commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_registry.receipt_commit.allocation",
                   "selected texture registry exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "selected_texture_registry.receipt_commit.exception",
                   "unexpected exception before selected texture receipt commit");
  }
}

ValidationResult RemoveOgre14SelectedTextureSourceResource(
    const std::string &effective_resource_group,
    std::uint64_t exact_group_generation,
    std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
    const std::string &exact_resource_name,
    Ogre14SelectedTextureSourceReceiptRegistry &registry,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector) {
  if (registry.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "selected_texture_registry",
                   "selected texture registry is not initialized");
  }
  if (!IsIdentifier(effective_resource_group) ||
      !IsIdentifier(exact_resource_name) || exact_group_generation == 0U ||
      resource_pointer_token == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "selected_texture_registry.resource_remove",
                   "resource removal identity is incomplete");
  }
  const ResourceKey key{resource_pointer_token};
  const auto found = registry.state_->receipts.find(key);
  if (found == registry.state_->receipts.end()) {
    return ValidationResult::Success();
  }
  const auto *metadata = found->second.metadata();
  if (metadata == nullptr ||
      metadata->source.effective_resource_group != effective_resource_group ||
      metadata->source.group_generation != exact_group_generation ||
      metadata->source.resource_handle != resource_handle ||
      metadata->source.exact_resource_name != exact_resource_name) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "selected_texture_registry.resource_remove",
                   "live pointer removal does not match its exact receipt binding");
  }

  try {
    auto candidate = std::make_shared<
        Ogre14SelectedTextureSourceReceiptRegistry::State>(*registry.state_);
    candidate->retained_source_bytes -= metadata->byte_count;
    candidate->retained_identity_bytes -= found->second.identity_size();
    candidate->receipts.erase(key);
    if (fault_injector != nullptr) {
      fault_injector->BeforeSelectedTextureSourceStage(
          Ogre14SelectedTextureSourceTransactionStage::BEFORE_REGISTRY_COMMIT);
    }
    registry =
        Ogre14SelectedTextureSourceReceiptRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "selected_texture_registry.resource_remove.allocation",
                   "allocation failed before selected texture removal commit");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "selected_texture_registry.resource_remove.exception",
                   "unexpected exception before selected texture removal commit");
  }
}

ValidationResult Ogre14SelectedTextureSourceReceiptRegistry::FindResource(
    const std::string &effective_resource_group,
    std::uint64_t group_generation, std::uintptr_t resource_pointer_token,
    std::uint64_t resource_handle, const std::string &exact_resource_name,
    Ogre14SelectedTextureSourceReceipt &receipt) const {
  if (state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "selected_texture_registry",
                   "selected texture registry is not initialized");
  }
  if (!IsIdentifier(effective_resource_group) ||
      !IsIdentifier(exact_resource_name) || group_generation == 0U ||
      resource_pointer_token == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "selected_texture_registry.resource_lookup",
                   "resource lookup identity is incomplete");
  }
  const auto group = state_->groups.find(effective_resource_group);
  if (group == state_->groups.end() || !group->second.active ||
      group->second.generation != group_generation) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "selected_texture_registry.resource_lookup.group_generation",
                   "resource lookup does not target the active generation");
  }
  const auto found = state_->receipts.find(ResourceKey{resource_pointer_token});
  if (found == state_->receipts.end() || found->second.metadata() == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "selected_texture_registry.resource_lookup",
                   "exact selected texture resource is absent");
  }
  const auto &source = found->second.metadata()->source;
  if (source.effective_resource_group != effective_resource_group ||
      source.group_generation != group_generation ||
      source.resource_handle != resource_handle ||
      source.exact_resource_name != exact_resource_name) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "selected_texture_registry.resource_lookup",
                   "live pointer does not match its selected-source binding");
  }
  receipt = found->second;
  return ValidationResult::Success();
}

ValidationResult Ogre14SelectedTextureSourceReceiptRegistry::
    MintLoadedResourceResolution(
        const std::string &effective_resource_group,
        std::uint64_t group_generation,
        std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
        const std::string &exact_resource_name,
        std::uint64_t loaded_resource_state_count,
        std::uintptr_t resolver_pointer_token,
        Ogre14SelectedTextureSourceResolution &resolution,
        IOgre14SelectedTextureSourceFaultInjector *fault_injector) const {
  if (resolver_pointer_token == 0U || loaded_resource_state_count == 0U) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "selected_texture_resolution.identity",
                   "resolver or loaded resource identity is empty");
  }
  Ogre14SelectedTextureSourceReceipt exact_receipt;
  ValidationResult lookup =
      FindResource(effective_resource_group, group_generation,
                   resource_pointer_token, resource_handle,
                   exact_resource_name, exact_receipt);
  if (!lookup) {
    return lookup;
  }
  const auto *metadata = exact_receipt.metadata();
  if (metadata == nullptr ||
      metadata->source.resource_state_count_before_load ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      loaded_resource_state_count !=
          metadata->source.resource_state_count_before_load + 1U) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "selected_texture_resolution.resource_state_count",
                   "loaded texture is not exactly one successful load after source selection");
  }

  try {
    auto candidate =
        std::make_shared<Ogre14SelectedTextureSourceResolution::State>();
    candidate->registry_snapshot = *this;
    candidate->exact_source_receipt = exact_receipt;
    candidate->resolver_pointer_token = resolver_pointer_token;
    candidate->loaded_resource_state_count = loaded_resource_state_count;
    if (fault_injector != nullptr) {
      fault_injector->BeforeSelectedTextureSourceStage(
          Ogre14SelectedTextureSourceTransactionStage::
              BEFORE_RESOLUTION_COMMIT);
    }
    resolution =
        Ogre14SelectedTextureSourceResolution(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "selected_texture_resolution.allocation",
                   "allocation failed before selected texture resolution publication");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "selected_texture_resolution.allocation",
                   "selected texture resolution exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "selected_texture_resolution.exception",
                   "unexpected exception before selected texture resolution publication");
  }
}

bool Ogre14SelectedTextureSourceReceiptRegistry::
    RevalidateLoadedResourceResolution(
        const Ogre14SelectedTextureSourceResolution &resolution,
        std::uintptr_t resolver_pointer_token,
        std::uintptr_t resource_pointer_token,
        std::uint64_t resource_handle,
        const std::string &exact_resource_group,
        const std::string &exact_resource_name,
        std::uint64_t loaded_resource_state_count) const noexcept {
  if (state_ == nullptr || !resolution.initialized() ||
      resolver_pointer_token == 0U ||
      resolution.state_->resolver_pointer_token != resolver_pointer_token ||
      resolution.state_->registry_snapshot.state_ != state_ ||
      !resolution.MatchesLoadedResourceIdentity(
          resource_pointer_token, resource_handle, exact_resource_group,
          exact_resource_name, loaded_resource_state_count)) {
    return false;
  }
  const auto *metadata = resolution.state_->exact_source_receipt.metadata();
  if (metadata == nullptr ||
      metadata->source.resource_state_count_before_load ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      loaded_resource_state_count !=
          metadata->source.resource_state_count_before_load + 1U) {
    return false;
  }
  const auto group = state_->groups.find(exact_resource_group);
  if (group == state_->groups.end() || !group->second.active ||
      group->second.generation != metadata->source.group_generation) {
    return false;
  }
  const auto found = state_->receipts.find(ResourceKey{resource_pointer_token});
  return found != state_->receipts.end() &&
         found->second.SharesImmutableStateWith(
             resolution.state_->exact_source_receipt);
}

void PoisonOgre14SelectedTextureSourceRegistry(
    Ogre14SelectedTextureSourceReceiptRegistry &registry) noexcept {
  registry.state_.reset();
}

} // namespace RoR::Render
