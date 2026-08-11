/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral deterministic payload SHA-256.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RoR::Render {

using RenderPayloadDigest = std::array<std::uint8_t, 32U>;

/// Computes SHA-256 over an arbitrary renderer payload. A null pointer is
/// accepted only for an empty payload; hostile null/non-empty input fails
/// closed to the all-zero digest and never dereferences the pointer.
[[nodiscard]] RenderPayloadDigest
ComputeRenderPayloadDigest(const std::uint8_t *payload,
                           std::size_t payload_size) noexcept;

} // namespace RoR::Render
