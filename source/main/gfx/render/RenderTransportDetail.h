/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Internal padding-free render transport primitives.

#pragma once

#include "RenderTransportEnvelope.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace RoR::Render::TransportDetail {

class WireWriter final {
public:
  WireWriter(std::vector<std::uint8_t> *output,
             std::uint64_t maximum_bytes) noexcept
      : output_(output), maximum_bytes_(maximum_bytes) {}

  bool AddByte(std::uint8_t value) {
    if (!Advance(1U)) {
      return false;
    }
    if (output_ != nullptr) {
      output_->push_back(value);
    }
    return true;
  }

  bool AddBytes(const std::uint8_t *bytes, std::size_t size) {
    if (!Advance(size)) {
      return false;
    }
    if (output_ != nullptr && size != 0U) {
      output_->insert(output_->end(), bytes, bytes + size);
    }
    return true;
  }

  bool AddU16(std::uint16_t value) {
    return AddByte(static_cast<std::uint8_t>(value)) &&
           AddByte(static_cast<std::uint8_t>(value >> 8U));
  }

  bool AddU32(std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
      if (!AddByte(static_cast<std::uint8_t>(value >> (byte * 8U)))) {
        return false;
      }
    }
    return true;
  }

  bool AddU64(std::uint64_t value) {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
      if (!AddByte(static_cast<std::uint8_t>(value >> (byte * 8U)))) {
        return false;
      }
    }
    return true;
  }

  bool AddBool(bool value) { return AddByte(value ? 1U : 0U); }

  /// Scene values fold signed zero so equivalent snapshots have one wire form.
  bool AddFloat(float value) {
    if (value == 0.0F) {
      value = 0.0F;
    }
    return AddFloatExact(value);
  }

  /// Asset revisions preserve every admitted object-representation bit.
  bool AddFloatExact(float value) {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return AddU32(bits);
  }

  bool AddDouble(double value) {
    if (value == 0.0) {
      value = 0.0;
    }
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return AddU64(bits);
  }

  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
  [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
  bool Advance(std::size_t amount) noexcept {
    if (!ok_ || size_ > maximum_bytes_ ||
        static_cast<std::uint64_t>(amount) > maximum_bytes_ - size_) {
      ok_ = false;
      return false;
    }
    size_ += static_cast<std::uint64_t>(amount);
    return true;
  }

  std::vector<std::uint8_t> *output_ = nullptr;
  std::uint64_t maximum_bytes_ = 0U;
  std::uint64_t size_ = 0U;
  bool ok_ = true;
};

class AllocationBudget final {
public:
  explicit AllocationBudget(std::uint64_t maximum_bytes) noexcept
      : maximum_bytes_(maximum_bytes) {}

  bool Charge(std::uint64_t count, std::size_t item_size) noexcept {
    if (count != 0U &&
        static_cast<std::uint64_t>(item_size) > maximum_bytes_ / count) {
      return false;
    }
    const std::uint64_t bytes = count * static_cast<std::uint64_t>(item_size);
    if (used_bytes_ > maximum_bytes_ || bytes > maximum_bytes_ - used_bytes_) {
      return false;
    }
    used_bytes_ += bytes;
    return true;
  }

  [[nodiscard]] std::uint64_t used_bytes() const noexcept {
    return used_bytes_;
  }

private:
  std::uint64_t maximum_bytes_ = 0U;
  std::uint64_t used_bytes_ = 0U;
};

class WireReader final {
public:
  WireReader(const std::uint8_t *bytes, std::size_t size,
             AllocationBudget &allocation_budget) noexcept
      : bytes_(bytes), size_(size), allocation_budget_(allocation_budget) {}

  bool ReadByte(std::uint8_t &value) noexcept {
    if (remaining() < 1U) {
      Fail(RenderTransportStatus::MALFORMED_PAYLOAD);
      return false;
    }
    value = bytes_[offset_++];
    return true;
  }

  bool ReadU16(std::uint16_t &value) noexcept {
    value = 0U;
    for (std::size_t byte = 0U; byte < 2U; ++byte) {
      std::uint8_t part = 0U;
      if (!ReadByte(part)) {
        return false;
      }
      value |= static_cast<std::uint16_t>(part) << (byte * 8U);
    }
    return true;
  }

  bool ReadU32(std::uint32_t &value) noexcept {
    value = 0U;
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
      std::uint8_t part = 0U;
      if (!ReadByte(part)) {
        return false;
      }
      value |= static_cast<std::uint32_t>(part) << (byte * 8U);
    }
    return true;
  }

  bool ReadU64(std::uint64_t &value) noexcept {
    value = 0U;
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
      std::uint8_t part = 0U;
      if (!ReadByte(part)) {
        return false;
      }
      value |= static_cast<std::uint64_t>(part) << (byte * 8U);
    }
    return true;
  }

  bool ReadBool(bool &value) noexcept {
    std::uint8_t encoded = 0U;
    if (!ReadByte(encoded)) {
      return false;
    }
    if (encoded > 1U) {
      Fail(RenderTransportStatus::MALFORMED_PAYLOAD);
      return false;
    }
    value = encoded != 0U;
    return true;
  }

  bool ReadFloat(float &value) noexcept {
    std::uint32_t bits = 0U;
    if (!ReadU32(bits)) {
      return false;
    }
    if (bits == 0x80000000U || !AdmitFiniteFloat(bits)) {
      Fail(RenderTransportStatus::NON_CANONICAL_FLOAT);
      return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
  }

  bool ReadFloatExact(float &value) noexcept {
    std::uint32_t bits = 0U;
    if (!ReadU32(bits)) {
      return false;
    }
    if (!AdmitFiniteFloat(bits)) {
      Fail(RenderTransportStatus::NON_CANONICAL_FLOAT);
      return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
  }

  bool ReadDouble(double &value) noexcept {
    std::uint64_t bits = 0U;
    if (!ReadU64(bits)) {
      return false;
    }
    if (bits == 0x8000000000000000ULL ||
        (bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) {
      Fail(RenderTransportStatus::NON_CANONICAL_FLOAT);
      return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
  }

  bool ReadCount(std::uint32_t maximum, std::size_t minimum_item_bytes,
                 std::uint32_t &count) noexcept {
    if (!ReadU32(count)) {
      return false;
    }
    if (count > maximum) {
      Fail(RenderTransportStatus::COUNT_LIMIT_EXCEEDED);
      return false;
    }
    if (minimum_item_bytes != 0U &&
        static_cast<std::uint64_t>(count) >
            static_cast<std::uint64_t>(remaining() / minimum_item_bytes)) {
      Fail(RenderTransportStatus::MALFORMED_PAYLOAD);
      return false;
    }
    return true;
  }

  template <typename Value>
  bool Reserve(std::vector<Value> &values, std::uint64_t count) {
    if (!allocation_budget_.Charge(count, sizeof(Value))) {
      Fail(RenderTransportStatus::DECODED_ALLOCATION_LIMIT_EXCEEDED);
      return false;
    }
    values.reserve(static_cast<std::size_t>(count));
    return true;
  }

  bool ChargeAllocation(std::uint64_t count,
                        std::size_t item_size) noexcept {
    if (!allocation_budget_.Charge(count, item_size)) {
      Fail(RenderTransportStatus::DECODED_ALLOCATION_LIMIT_EXCEEDED);
      return false;
    }
    return true;
  }

  bool ReadView(std::size_t count, const std::uint8_t *&view) noexcept {
    if (count > remaining()) {
      Fail(RenderTransportStatus::MALFORMED_PAYLOAD);
      return false;
    }
    view = bytes_ + offset_;
    offset_ += count;
    return true;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return offset_ <= size_ ? size_ - offset_ : 0U;
  }
  [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
  [[nodiscard]] bool consumed() const noexcept { return offset_ == size_; }
  [[nodiscard]] RenderTransportStatus status() const noexcept {
    return status_;
  }

  void Fail(RenderTransportStatus status) noexcept {
    if (status_ == RenderTransportStatus::OK) {
      status_ = status;
    }
  }

private:
  static bool AdmitFiniteFloat(std::uint32_t bits) noexcept {
    return (bits & 0x7f800000U) != 0x7f800000U;
  }

  const std::uint8_t *bytes_ = nullptr;
  std::size_t size_ = 0U;
  std::size_t offset_ = 0U;
  AllocationBudget &allocation_budget_;
  RenderTransportStatus status_ = RenderTransportStatus::OK;
};

} // namespace RoR::Render::TransportDetail
