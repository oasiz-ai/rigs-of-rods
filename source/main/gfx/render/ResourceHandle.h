/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Compact generational handles for renderer-owned resources.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RoR::Render {

enum class ResourceKind : std::uint8_t {
  INVALID = 0,
  MESH = 1,
  TEXTURE = 2,
  MATERIAL = 3,
  SAMPLER = 4,
  RENDER_TARGET = 5,
  ACCELERATION_STRUCTURE = 6,
};

[[nodiscard]] constexpr bool IsKnownResourceKind(ResourceKind kind) noexcept {
  switch (kind) {
  case ResourceKind::MESH:
  case ResourceKind::TEXTURE:
  case ResourceKind::MATERIAL:
  case ResourceKind::SAMPLER:
  case ResourceKind::RENDER_TARGET:
  case ResourceKind::ACCELERATION_STRUCTURE:
    return true;
  case ResourceKind::INVALID:
    return false;
  }
  return false;
}

/// A frontend-local, generation-checked resource identity.
///
/// The stable 64-bit representation stores an 8-bit kind, a 16-bit pool
/// domain, a 16-bit one-based slot, and a 24-bit nonzero generation. The
/// domain prevents handles from one frontend registry (or frontend lifetime)
/// from aliasing another registry's resources. Raw values are process-local
/// and must not be persisted across runs.
class ResourceHandle final {
public:
  static constexpr std::uint32_t kInvalidSlot = 0xFFFFFFFFU;
  static constexpr std::uint32_t kMaxSlot = 0x0000FFFEU;
  static constexpr std::uint32_t kMaxDomain = 0x0000FFFFU;
  static constexpr std::uint32_t kMaxGeneration = 0x00FFFFFFU;

  constexpr ResourceHandle() noexcept = default;

  [[nodiscard]] static constexpr ResourceHandle
  Create(ResourceKind kind, std::uint32_t domain, std::uint32_t slot,
         std::uint32_t generation) noexcept {
    if (!IsKnownResourceKind(kind) || domain == 0U || domain > kMaxDomain ||
        slot > kMaxSlot || generation == 0U || generation > kMaxGeneration) {
      return {};
    }
    const std::uint64_t encoded_kind =
        static_cast<std::uint64_t>(static_cast<std::uint8_t>(kind)) << 56U;
    const std::uint64_t encoded_domain = static_cast<std::uint64_t>(domain)
                                         << 40U;
    const std::uint64_t encoded_slot = static_cast<std::uint64_t>(slot + 1U)
                                       << 24U;
    return ResourceHandle(encoded_kind | encoded_domain | encoded_slot |
                          generation);
  }

  [[nodiscard]] static constexpr ResourceHandle
  FromRaw(std::uint64_t value) noexcept {
    const ResourceKind kind =
        static_cast<ResourceKind>(static_cast<std::uint8_t>(value >> 56U));
    const std::uint32_t domain =
        static_cast<std::uint32_t>((value >> 40U) & 0x0000FFFFU);
    const std::uint32_t encoded_slot =
        static_cast<std::uint32_t>((value >> 24U) & 0x0000FFFFU);
    const std::uint32_t generation =
        static_cast<std::uint32_t>(value & 0x00FFFFFFU);
    if (!IsKnownResourceKind(kind) || domain == 0U || encoded_slot == 0U ||
        generation == 0U) {
      return {};
    }
    return Create(kind, domain, encoded_slot - 1U, generation);
  }

  [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0U; }
  explicit constexpr operator bool() const noexcept { return valid(); }

  [[nodiscard]] constexpr ResourceKind kind() const noexcept {
    return valid() ? static_cast<ResourceKind>(
                         static_cast<std::uint8_t>(value_ >> 56U))
                   : ResourceKind::INVALID;
  }

  [[nodiscard]] constexpr std::uint32_t slot() const noexcept {
    return valid()
               ? static_cast<std::uint32_t>((value_ >> 24U) & 0x0000FFFFU) - 1U
               : kInvalidSlot;
  }

  [[nodiscard]] constexpr std::uint32_t domain() const noexcept {
    return valid() ? static_cast<std::uint32_t>((value_ >> 40U) & 0x0000FFFFU)
                   : 0U;
  }

  [[nodiscard]] constexpr std::uint32_t generation() const noexcept {
    return valid() ? static_cast<std::uint32_t>(value_ & 0x00FFFFFFU) : 0U;
  }

  [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value_; }

  friend constexpr bool operator==(ResourceHandle lhs,
                                   ResourceHandle rhs) noexcept {
    return lhs.value_ == rhs.value_;
  }

  friend constexpr bool operator!=(ResourceHandle lhs,
                                   ResourceHandle rhs) noexcept {
    return !(lhs == rhs);
  }

  friend constexpr bool operator<(ResourceHandle lhs,
                                  ResourceHandle rhs) noexcept {
    return lhs.value_ < rhs.value_;
  }

private:
  explicit constexpr ResourceHandle(std::uint64_t value) noexcept
      : value_(value) {}

  std::uint64_t value_ = 0U;
};

struct ResourceHandleHash {
  [[nodiscard]] std::size_t operator()(ResourceHandle handle) const noexcept;
};

/// Single-thread-owned allocator used by frontend resource registries.
///
/// Release increments the slot generation before reuse. This makes all older
/// handles stale and prevents a stale release from destroying a new resource.
class ResourceHandlePool final {
public:
  /// `initial_generation` is public only to make terminal-generation behavior
  /// testable without billions of allocations. Production callers omit it.
  explicit ResourceHandlePool(ResourceKind kind,
                              std::uint32_t initial_generation = 1U);
  ResourceHandlePool(const ResourceHandlePool &) = delete;
  ResourceHandlePool &operator=(const ResourceHandlePool &) = delete;
  ResourceHandlePool(ResourceHandlePool &&) = delete;
  ResourceHandlePool &operator=(ResourceHandlePool &&) = delete;

  [[nodiscard]] ResourceHandle Allocate();
  [[nodiscard]] bool Release(ResourceHandle handle) noexcept;
  [[nodiscard]] bool IsLive(ResourceHandle handle) const noexcept;

  [[nodiscard]] ResourceKind kind() const noexcept { return kind_; }
  [[nodiscard]] std::uint32_t domain() const noexcept { return domain_; }
  [[nodiscard]] std::size_t live_count() const noexcept { return live_count_; }
  [[nodiscard]] std::size_t slot_count() const noexcept {
    return slots_.size();
  }

private:
  struct Slot {
    std::uint32_t generation = 0U;
    bool live = false;
    bool retired = false;
  };

  ResourceKind kind_ = ResourceKind::INVALID;
  std::uint32_t domain_ = 0U;
  std::uint32_t initial_generation_ = 1U;
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> free_slots_;
  std::size_t live_count_ = 0U;
};

} // namespace RoR::Render
