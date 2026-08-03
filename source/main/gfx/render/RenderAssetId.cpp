/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderAssetId.h"

#include <functional>

namespace RoR::Render {

std::size_t RenderAssetIdHash::operator()(RenderAssetId id) const noexcept {
  const std::size_t high = std::hash<std::uint64_t>{}(id.high());
  const std::size_t low = std::hash<std::uint64_t>{}(id.low());
  // 64-bit golden-ratio mix; also remains well-defined on 32-bit hosts.
  return high ^ (low + static_cast<std::size_t>(0x9E3779B9U) + (high << 6U) +
                 (high >> 2U));
}

} // namespace RoR::Render
