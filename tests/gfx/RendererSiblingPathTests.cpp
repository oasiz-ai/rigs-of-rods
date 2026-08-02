/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererSiblingPath.h"
#include "RendererPackagedMediaPath.h"

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

RoR::RendererChildLauncherString Native(const char *ascii) {
  RoR::RendererChildLauncherString result;
  if (ascii == nullptr) {
    return result;
  }
  while (*ascii != '\0') {
    result.push_back(static_cast<RoR::RendererChildLauncherChar>(
        static_cast<unsigned char>(*ascii)));
    ++ascii;
  }
  return result;
}

bool EqualsAscii(const RoR::RendererChildLauncherString &path,
                 const char *ascii) {
  return path == Native(ascii);
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

void TestPackagedMediaStatusContract() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const auto status =
        static_cast<RoR::RendererPackagedMediaPathStatus>(value);
    Require(RoR::IsKnownRendererPackagedMediaPathStatus(status) ==
                (value <= 5U),
            "packaged-media status classifier accepted an unknown value");
  }
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererPackagedMediaPathStatus::READY),
              "ready") == 0,
          "packaged-media ready string changed");
  Require(std::strcmp(
              RoR::ToString(static_cast<
                            RoR::RendererPackagedMediaPathStatus>(255U)),
              "invalid") == 0,
          "unknown packaged-media status did not fail closed");
}

void RequireMediaLayout(
    RoR::HostRenderPlatform platform, const char *executable,
    const char *expected_shader_root,
    const char *expected_presentation_root) {
  const RoR::RendererPackagedMediaPathResult result =
      RoR::ResolveRendererPackagedMediaPathFromExecutable(
          platform, Native(executable));
  Require(result.version ==
              RoR::kRendererPackagedMediaPathContractVersion &&
              result.accepted &&
              result.status ==
                  RoR::RendererPackagedMediaPathStatus::READY &&
              result.package_platform == platform &&
              result.native_error_code == 0U &&
              EqualsAscii(result.shader_media_root,
                          expected_shader_root) &&
              EqualsAscii(result.presentation_media_root,
                          expected_presentation_root),
          "fixed packaged-media layout changed");
}

void TestPackagedMediaLayouts() {
  RequireMediaLayout(
      RoR::HostRenderPlatform::MACOS,
      "/Applications/RoR.app/Contents/MacOS/RoR-OgreNext",
      "/Applications/RoR.app/Contents/Resources/ogrenext/Hlms",
      "/Applications/RoR.app/Contents/Resources/ogrenext/Presentation");
  RequireMediaLayout(
      RoR::HostRenderPlatform::LINUX, "/opt/ror/RoR-OgreNext",
      "/opt/ror/resources/ogrenext/Hlms",
      "/opt/ror/resources/ogrenext/Presentation");
  RequireMediaLayout(
      RoR::HostRenderPlatform::WINDOWS,
      R"(\\?\C:\Games\RoR\RoR-OgreNext.exe)",
      R"(\\?\C:\Games\RoR\resources\ogrenext\Hlms)",
      R"(\\?\C:\Games\RoR\resources\ogrenext\Presentation)");

  const auto unknown =
      RoR::ResolveRendererPackagedMediaPathFromExecutable(
          RoR::HostRenderPlatform::UNKNOWN, Native("/x/RoR-OgreNext"));
  Require(!unknown.accepted && unknown.shader_media_root.empty() &&
              unknown.presentation_media_root.empty() &&
              unknown.status == RoR::RendererPackagedMediaPathStatus::
                                    REJECTED_INVALID_PLATFORM,
          "unknown package platform did not fail closed");

  const char *invalid_macos[] = {
      "",
      "/Applications/RoR.app/Contents/MacOS/",
      "/Applications/RoR.app/Contents/MacOS/RoR-Renamed",
      "/Applications/RoR.app/MacOS/RoR-OgreNext",
      "/tmp/RoR-OgreNext",
  };
  for (const char *path : invalid_macos) {
    const auto rejected =
        RoR::ResolveRendererPackagedMediaPathFromExecutable(
            RoR::HostRenderPlatform::MACOS, Native(path));
    Require(!rejected.accepted && rejected.shader_media_root.empty() &&
                rejected.presentation_media_root.empty(),
            "invalid macOS media layout was accepted");
  }

  const auto renamed_linux =
      RoR::ResolveRendererPackagedMediaPathFromExecutable(
          RoR::HostRenderPlatform::LINUX, Native("/opt/ror/renamed"));
  Require(!renamed_linux.accepted &&
              renamed_linux.status ==
                  RoR::RendererPackagedMediaPathStatus::
                      REJECTED_INVALID_EXECUTABLE_PATH,
          "renamed Linux child was accepted as a package anchor");
}

} // namespace

int main() {
  TestStatusContract();
  TestInvalidBasenamesFailBeforePathDiscovery();
  TestCanonicalSiblingResolution();
  TestPackagedMediaStatusContract();
  TestPackagedMediaLayouts();
  return EXIT_SUCCESS;
}
