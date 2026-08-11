/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderBridgeSessionIdentity.h"

#include "RenderTransportEnvelope.h"

#include <algorithm>
#include <array>
#include <limits>

namespace RoR::Render {
namespace {

// The terminating NUL is deliberately excluded from the hash material.
constexpr char kRegistryIdentityDomain[] =
    "ror.render.asset-registry-id/renderer-bridge-session/v1";
constexpr std::size_t kRegistryIdentityDomainBytes =
    sizeof(kRegistryIdentityDomain) - 1U;

} // namespace

std::uint64_t DeriveRenderAssetRegistryIdFromBridgeSession(
    const RenderBridgeSessionIdentity &session_id) noexcept {
  if (std::all_of(session_id.begin(), session_id.end(),
                  [](std::uint8_t byte) { return byte == 0U; })) {
    return 0U;
  }

  std::array<std::uint8_t,
             kRegistryIdentityDomainBytes + kRenderBridgeSessionIdentityBytes>
      material{};
  for (std::size_t index = 0U; index < kRegistryIdentityDomainBytes; ++index) {
    material[index] = static_cast<std::uint8_t>(
        static_cast<unsigned char>(kRegistryIdentityDomain[index]));
  }
  std::copy(session_id.begin(), session_id.end(),
            material.begin() + kRegistryIdentityDomainBytes);

  const std::array<std::uint8_t, 32U> digest =
      ComputeRenderTransportPayloadDigest(material.data(), material.size());
  std::uint64_t registry_id = 0U;
  for (std::size_t index = 0U; index < sizeof(registry_id); ++index) {
    registry_id |= static_cast<std::uint64_t>(digest[index]) << (index * 8U);
  }
  if (registry_id == 0U) {
    return 1U;
  }
  if (registry_id == (std::numeric_limits<std::uint64_t>::max)()) {
    return (std::numeric_limits<std::uint64_t>::max)() - 1U;
  }
  return registry_id;
}

} // namespace RoR::Render
