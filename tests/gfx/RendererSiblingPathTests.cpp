/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererSiblingPath.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer sibling path test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool EndsWithAscii(const RoR::RendererChildLauncherString &path,
                   const char *suffix) {
  std::size_t suffix_length = 0U;
  while (suffix[suffix_length] != '\0') {
    ++suffix_length;
  }
  if (suffix_length > path.size()) {
    return false;
  }
  const std::size_t offset = path.size() - suffix_length;
  for (std::size_t index = 0U; index < suffix_length; ++index) {
    if (path[offset + index] !=
        static_cast<RoR::RendererChildLauncherChar>(
            static_cast<unsigned char>(suffix[index]))) {
      return false;
    }
  }
  return true;
}

void TestStatusContract() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const auto status = static_cast<RoR::RendererSiblingPathStatus>(value);
    Require(RoR::IsKnownRendererSiblingPathStatus(status) == (value <= 4U),
            "status classifier accepted an unknown value");
  }
  Require(std::strcmp(
              RoR::ToString(RoR::RendererSiblingPathStatus::READY),
              "ready") == 0,
          "ready status string changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererSiblingPathStatus::FAILED_INTERNAL),
              "failed-internal") == 0,
          "internal failure string changed");
  Require(std::strcmp(
              RoR::ToString(
                  static_cast<RoR::RendererSiblingPathStatus>(255U)),
              "invalid") == 0,
          "unknown status did not fail closed");
}

void TestInvalidBasenamesFailBeforePathDiscovery() {
  const char non_ascii[] = {'R', 'o', 'R', '-', static_cast<char>(0x80), '\0'};
  const char *invalid[] = {
      nullptr,
      "",
      ".",
      "..",
      ".RoR-OgreNext",
      "RoR-OgreNext.",
      "../RoR-OgreNext",
      "folder/RoR-OgreNext",
      "folder\\RoR-OgreNext",
      "RoR OgreNext",
      "RoR:OgreNext",
      non_ascii,
  };
  for (const char *basename : invalid) {
    const RoR::RendererSiblingPathResult result =
        RoR::ResolveRendererSiblingPath(basename);
    Require(result.version == RoR::kRendererSiblingPathContractVersion,
            "invalid result version changed");
    Require(!result.accepted && result.path.empty() &&
                result.status == RoR::RendererSiblingPathStatus::
                                     REJECTED_INVALID_BASENAME &&
                result.native_error_code == 0U,
            "invalid basename reached native path discovery");
  }

  char too_long[257];
  for (std::size_t index = 0U; index < sizeof(too_long) - 1U; ++index) {
    too_long[index] = 'a';
  }
  too_long[sizeof(too_long) - 1U] = '\0';
  const RoR::RendererSiblingPathResult result =
      RoR::ResolveRendererSiblingPath(too_long);
  Require(!result.accepted && result.path.empty() &&
              result.status == RoR::RendererSiblingPathStatus::
                                   REJECTED_INVALID_BASENAME,
          "overlong basename reached native path discovery");
}

void TestCanonicalSiblingResolution() {
#if defined(_WIN32)
  constexpr const char *first_basename = "RoR-Ogre14.exe";
  constexpr const char *second_basename = "RoR-OgreNext.exe";
  constexpr const char *first_suffix = "\\RoR-Ogre14.exe";
  constexpr const char *second_suffix = "\\RoR-OgreNext.exe";
#else
  constexpr const char *first_basename = "RoR-Ogre14";
  constexpr const char *second_basename = "RoR-OgreNext";
  constexpr const char *first_suffix = "/RoR-Ogre14";
  constexpr const char *second_suffix = "/RoR-OgreNext";
#endif
  const RoR::RendererSiblingPathResult first =
      RoR::ResolveRendererSiblingPath(first_basename);
  const RoR::RendererSiblingPathResult second =
      RoR::ResolveRendererSiblingPath(second_basename);
  for (const RoR::RendererSiblingPathResult *result : {&first, &second}) {
    Require(result->version == RoR::kRendererSiblingPathContractVersion &&
                result->accepted &&
                result->status == RoR::RendererSiblingPathStatus::READY &&
                result->native_error_code == 0U && !result->path.empty(),
            "safe sibling did not resolve from the running executable");
  }
  Require(EndsWithAscii(first.path, first_suffix) &&
              EndsWithAscii(second.path, second_suffix),
          "resolved sibling basename changed");
  const std::size_t first_prefix = first.path.size() - std::strlen(first_suffix);
  const std::size_t second_prefix =
      second.path.size() - std::strlen(second_suffix);
  Require(first_prefix == second_prefix &&
              first.path.compare(0U, first_prefix, second.path, 0U,
                                 second_prefix) == 0,
          "siblings did not share one canonical executable directory");
}

} // namespace

int main() {
  TestStatusContract();
  TestInvalidBasenamesFailBeforePathDiscovery();
  TestCanonicalSiblingResolution();
  return EXIT_SUCCESS;
}
