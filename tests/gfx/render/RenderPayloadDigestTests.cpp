/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderPayloadDigest.h"
#include "RenderTransportEnvelope.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using RoR::Render::ComputeRenderPayloadDigest;
using RoR::Render::ComputeRenderTransportPayloadDigest;
using RoR::Render::RenderPayloadDigest;

[[noreturn]] void Fail(const std::string &message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

void Require(bool condition, const std::string &message) {
  if (!condition) {
    Fail(message);
  }
}

RenderPayloadDigest ParseDigest(const char *hex) {
  RenderPayloadDigest result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    const auto nibble = [](char character) -> std::uint8_t {
      if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
      }
      if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint8_t>(character - 'a' + 10);
      }
      Fail("test vector contains a non-hexadecimal character");
    };
    result[index] = static_cast<std::uint8_t>(
        (nibble(hex[index * 2U]) << 4U) | nibble(hex[index * 2U + 1U]));
  }
  return result;
}

} // namespace

int main() {
  const RenderPayloadDigest empty = ComputeRenderPayloadDigest(nullptr, 0U);
  Require(empty == ParseDigest(
                       "e3b0c44298fc1c149afbf4c8996fb924"
                       "27ae41e4649b934ca495991b7852b855"),
          "empty SHA-256 vector changed");

  constexpr std::array<std::uint8_t, 3U> abc{{'a', 'b', 'c'}};
  const RenderPayloadDigest canonical =
      ComputeRenderPayloadDigest(abc.data(), abc.size());
  Require(canonical == ParseDigest(
                           "ba7816bf8f01cfea414140de5dae2223"
                           "b00361a396177a9cb410ff61f20015ad"),
          "abc SHA-256 vector changed");
  Require(ComputeRenderTransportPayloadDigest(abc.data(), abc.size()) ==
              canonical,
          "public transport compatibility spelling diverged");

  const RenderPayloadDigest hostile = ComputeRenderPayloadDigest(nullptr, 1U);
  Require(hostile == RenderPayloadDigest{},
          "null/non-empty payload did not fail closed");
  return 0;
}
