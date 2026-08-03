/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Stable renderer-neutral asset identities and revisions.

#pragma once

#include <cstddef>
#include <cstdint>

namespace RoR::Render {

enum class RenderAssetKind : std::uint8_t {
  INVALID = 0,
  MESH = 1,
  TEXTURE = 2,
  MATERIAL = 3,
  SAMPLER = 4,
};

[[nodiscard]] constexpr bool
IsKnownRenderAssetKind(RenderAssetKind kind) noexcept {
  switch (kind) {
  case RenderAssetKind::MESH:
  case RenderAssetKind::TEXTURE:
  case RenderAssetKind::MATERIAL:
  case RenderAssetKind::SAMPLER:
    return true;
  case RenderAssetKind::INVALID:
    return false;
  }
  return false;
}

/// Stable logical identity shared by scene producers and every renderer.
///
/// IDs are 128-bit so imported package UUIDs and content-addressed identities
/// do not need to be truncated to a frontend handle. They may be persisted,
/// recorded, and submitted to multiple frontends. An all-zero ID is invalid.
class RenderAssetId final {
public:
  constexpr RenderAssetId() noexcept = default;

  [[nodiscard]] static constexpr RenderAssetId
  FromWords(std::uint64_t high, std::uint64_t low) noexcept {
    return (high == 0U && low == 0U) ? RenderAssetId{}
                                    : RenderAssetId{high, low};
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return high_ != 0U || low_ != 0U;
  }
  explicit constexpr operator bool() const noexcept { return valid(); }

  [[nodiscard]] constexpr std::uint64_t high() const noexcept { return high_; }
  [[nodiscard]] constexpr std::uint64_t low() const noexcept { return low_; }

  friend constexpr bool operator==(RenderAssetId lhs,
                                   RenderAssetId rhs) noexcept {
    return lhs.high_ == rhs.high_ && lhs.low_ == rhs.low_;
  }
  friend constexpr bool operator!=(RenderAssetId lhs,
                                   RenderAssetId rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator<(RenderAssetId lhs,
                                  RenderAssetId rhs) noexcept {
    return lhs.high_ < rhs.high_ ||
           (lhs.high_ == rhs.high_ && lhs.low_ < rhs.low_);
  }

private:
  constexpr RenderAssetId(std::uint64_t high, std::uint64_t low) noexcept
      : high_(high), low_(low) {}

  std::uint64_t high_ = 0U;
  std::uint64_t low_ = 0U;
};

struct RenderAssetIdHash {
  [[nodiscard]] std::size_t operator()(RenderAssetId id) const noexcept;
};

/// Exact immutable asset contents required by a scene.
///
/// An ID is never reused for a different asset kind during one registry
/// lifetime. Revision one creates it; each update or tombstone advances the
/// revision by exactly one. Frontends map this portable reference to their own
/// generation-checked ResourceHandle without exposing that handle to scenes.
struct RenderAssetReference {
  RenderAssetId id;
  RenderAssetKind kind = RenderAssetKind::INVALID;
  std::uint64_t revision = 0U;

  [[nodiscard]] static constexpr RenderAssetReference
  Create(RenderAssetKind asset_kind, RenderAssetId asset_id,
         std::uint64_t asset_revision) noexcept {
    return IsKnownRenderAssetKind(asset_kind) && asset_id.valid() &&
                   asset_revision != 0U
               ? RenderAssetReference{asset_id, asset_kind, asset_revision}
               : RenderAssetReference{};
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return id.valid() && IsKnownRenderAssetKind(kind) && revision != 0U;
  }
  explicit constexpr operator bool() const noexcept { return valid(); }

  friend constexpr bool operator==(const RenderAssetReference &lhs,
                                   const RenderAssetReference &rhs) noexcept {
    return lhs.id == rhs.id && lhs.kind == rhs.kind &&
           lhs.revision == rhs.revision;
  }
  friend constexpr bool operator!=(const RenderAssetReference &lhs,
                                   const RenderAssetReference &rhs) noexcept {
    return !(lhs == rhs);
  }
};

/// Optional references use one canonical absent representation. A partially
/// populated invalid reference is malformed and must never be treated as if it
/// were absent.
[[nodiscard]] constexpr bool IsAbsentRenderAssetReference(
    const RenderAssetReference &reference) noexcept {
  return !reference.id.valid() && reference.kind == RenderAssetKind::INVALID &&
         reference.revision == 0U;
}

} // namespace RoR::Render
