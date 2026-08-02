/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Deterministic bridge-session to renderer asset-registry identity.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RoR::Render {

constexpr std::uint32_t kRenderBridgeSessionIdentityContractVersion = 1U;
constexpr std::size_t kRenderBridgeSessionIdentityBytes = 16U;

using RenderBridgeSessionIdentity =
    std::array<std::uint8_t, kRenderBridgeSessionIdentityBytes>;

/// Derives the renderer-neutral registry domain shared by the game host and
/// presentation process. The SHA-256 input is pinned and domain-separated
/// from every transport payload digest. The all-zero bridge session is
/// invalid and maps to zero; every valid session maps to neither zero nor the
/// uint64 maximum, both of which are reserved terminal/sentinel identities.
/// `RoR::RendererBridgeSessionId` is the same 16-byte std::array contract and
/// can be passed directly; this portable layer deliberately does not import
/// the process/argv protocol header.
[[nodiscard]] std::uint64_t DeriveRenderAssetRegistryIdFromBridgeSession(
    const RenderBridgeSessionIdentity &session_id) noexcept;

} // namespace RoR::Render
