/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererPackagedMediaPath.h"

#include <cstddef>

namespace RoR {
namespace {

using NativeString = RendererChildLauncherString;
using NativeChar = RendererChildLauncherChar;

bool IsSupportedPackagePlatform(HostRenderPlatform platform) noexcept {
  return platform == HostRenderPlatform::MACOS ||
         platform == HostRenderPlatform::WINDOWS ||
         platform == HostRenderPlatform::LINUX;
}

HostRenderPlatform CompiledHostPlatform() noexcept {
#if defined(__APPLE__)
  return HostRenderPlatform::MACOS;
#elif defined(_WIN32)
  return HostRenderPlatform::WINDOWS;
#elif defined(__linux__)
  return HostRenderPlatform::LINUX;
#else
  return HostRenderPlatform::UNKNOWN;
#endif
}

NativeChar Separator(HostRenderPlatform platform) noexcept {
  return static_cast<NativeChar>(
      platform == HostRenderPlatform::WINDOWS ? '\\' : '/');
}

bool EqualsAscii(const NativeString &value, const char *ascii) noexcept {
  if (ascii == nullptr) {
    return false;
  }
  std::size_t length = 0U;
  while (ascii[length] != '\0') {
    ++length;
  }
  if (value.size() != length) {
    return false;
  }
  for (std::size_t index = 0U; index < length; ++index) {
    if (value[index] != static_cast<NativeChar>(
                            static_cast<unsigned char>(ascii[index]))) {
      return false;
    }
  }
  return true;
}

bool AppendAsciiComponent(NativeString &path, NativeChar separator,
                          const char *component) {
  if (path.empty() || component == nullptr || component[0] == '\0') {
    return false;
  }
  if (path.back() != separator) {
    path.push_back(separator);
  }
  for (const char *cursor = component; *cursor != '\0'; ++cursor) {
    const unsigned char value = static_cast<unsigned char>(*cursor);
    const bool safe =
        (value >= static_cast<unsigned char>('A') &&
         value <= static_cast<unsigned char>('Z')) ||
        (value >= static_cast<unsigned char>('a') &&
         value <= static_cast<unsigned char>('z')) ||
        (value >= static_cast<unsigned char>('0') &&
         value <= static_cast<unsigned char>('9')) ||
        *cursor == '-' || *cursor == '_';
    if (!safe) {
      return false;
    }
    path.push_back(static_cast<NativeChar>(value));
  }
  return true;
}

bool SplitLeaf(const NativeString &path, NativeChar separator,
               NativeString &parent, NativeString &leaf) {
  if (path.empty() || path.back() == separator) {
    return false;
  }
  const NativeString::size_type position = path.find_last_of(separator);
  if (position == NativeString::npos || position == 0U ||
      position + 1U >= path.size()) {
    return false;
  }
  parent.assign(path, 0U, position);
  leaf.assign(path, position + 1U, NativeString::npos);
  return !parent.empty() && !leaf.empty();
}

RendererPackagedMediaPathResult Failure(
    HostRenderPlatform platform,
    RendererPackagedMediaPathStatus status) noexcept {
  RendererPackagedMediaPathResult result;
  result.package_platform = platform;
  result.status = status;
  return result;
}

} // namespace

RendererPackagedMediaPathResult
ResolveRendererPackagedMediaPathFromExecutable(
    HostRenderPlatform package_platform,
    const RendererChildLauncherString &canonical_executable_path) noexcept {
  if (!IsSupportedPackagePlatform(package_platform)) {
    return Failure(package_platform,
                   RendererPackagedMediaPathStatus::REJECTED_INVALID_PLATFORM);
  }
  try {
    const NativeChar separator = Separator(package_platform);
    NativeString executable_directory;
    NativeString executable_name;
    if (!SplitLeaf(canonical_executable_path, separator,
                   executable_directory, executable_name)) {
      return Failure(
          package_platform,
          RendererPackagedMediaPathStatus::REJECTED_INVALID_EXECUTABLE_PATH);
    }
    const char *const expected_executable =
        package_platform == HostRenderPlatform::WINDOWS
            ? "RoR-OgreNext.exe"
            : "RoR-OgreNext";
    if (!EqualsAscii(executable_name, expected_executable)) {
      return Failure(
          package_platform,
          RendererPackagedMediaPathStatus::REJECTED_INVALID_EXECUTABLE_PATH);
    }

    NativeString resource_root;
    if (package_platform == HostRenderPlatform::MACOS) {
      NativeString contents_directory;
      NativeString executable_directory_name;
      if (!SplitLeaf(executable_directory, separator, contents_directory,
                     executable_directory_name) ||
          !EqualsAscii(executable_directory_name, "MacOS")) {
        return Failure(
            package_platform,
            RendererPackagedMediaPathStatus::
                REJECTED_INVALID_MACOS_BUNDLE_LAYOUT);
      }
      NativeString bundle_directory;
      NativeString contents_name;
      if (!SplitLeaf(contents_directory, separator, bundle_directory,
                     contents_name) ||
          !EqualsAscii(contents_name, "Contents") ||
          bundle_directory.empty()) {
        return Failure(
            package_platform,
            RendererPackagedMediaPathStatus::
                REJECTED_INVALID_MACOS_BUNDLE_LAYOUT);
      }
      resource_root = contents_directory;
      if (!AppendAsciiComponent(resource_root, separator, "Resources")) {
        return Failure(package_platform,
                       RendererPackagedMediaPathStatus::FAILED_INTERNAL);
      }
    } else {
      resource_root = executable_directory;
      if (!AppendAsciiComponent(resource_root, separator, "resources")) {
        return Failure(package_platform,
                       RendererPackagedMediaPathStatus::FAILED_INTERNAL);
      }
    }

    if (!AppendAsciiComponent(resource_root, separator, "ogrenext")) {
      return Failure(package_platform,
                     RendererPackagedMediaPathStatus::FAILED_INTERNAL);
    }
    RendererPackagedMediaPathResult result;
    result.package_platform = package_platform;
    result.shader_media_root = resource_root;
    result.presentation_media_root = resource_root;
    if (!AppendAsciiComponent(result.shader_media_root, separator, "Hlms") ||
        !AppendAsciiComponent(result.presentation_media_root, separator,
                              "Presentation")) {
      return Failure(package_platform,
                     RendererPackagedMediaPathStatus::FAILED_INTERNAL);
    }
    result.status = RendererPackagedMediaPathStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    return Failure(package_platform,
                   RendererPackagedMediaPathStatus::FAILED_INTERNAL);
  }
}

RendererPackagedMediaPathResult ResolveRendererPackagedMediaPath(
    HostRenderPlatform package_platform) noexcept {
  if (!IsSupportedPackagePlatform(package_platform) ||
      package_platform != CompiledHostPlatform()) {
    return Failure(package_platform,
                   RendererPackagedMediaPathStatus::REJECTED_INVALID_PLATFORM);
  }
  const RendererCurrentExecutablePathResult executable =
      ResolveRendererCurrentExecutablePath();
  if (!executable.accepted) {
    RendererPackagedMediaPathResult result = Failure(
        package_platform,
        RendererPackagedMediaPathStatus::FAILED_CURRENT_EXECUTABLE_PATH);
    result.native_error_code = executable.native_error_code;
    return result;
  }
  RendererPackagedMediaPathResult result =
      ResolveRendererPackagedMediaPathFromExecutable(package_platform,
                                                     executable.path);
  result.native_error_code = executable.native_error_code;
  return result;
}

bool IsKnownRendererPackagedMediaPathStatus(
    RendererPackagedMediaPathStatus status) noexcept {
  switch (status) {
  case RendererPackagedMediaPathStatus::READY:
  case RendererPackagedMediaPathStatus::REJECTED_INVALID_PLATFORM:
  case RendererPackagedMediaPathStatus::REJECTED_INVALID_EXECUTABLE_PATH:
  case RendererPackagedMediaPathStatus::REJECTED_INVALID_MACOS_BUNDLE_LAYOUT:
  case RendererPackagedMediaPathStatus::FAILED_CURRENT_EXECUTABLE_PATH:
  case RendererPackagedMediaPathStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererPackagedMediaPathStatus status) noexcept {
  switch (status) {
  case RendererPackagedMediaPathStatus::READY:
    return "ready";
  case RendererPackagedMediaPathStatus::REJECTED_INVALID_PLATFORM:
    return "rejected-invalid-platform";
  case RendererPackagedMediaPathStatus::REJECTED_INVALID_EXECUTABLE_PATH:
    return "rejected-invalid-executable-path";
  case RendererPackagedMediaPathStatus::REJECTED_INVALID_MACOS_BUNDLE_LAYOUT:
    return "rejected-invalid-macos-bundle-layout";
  case RendererPackagedMediaPathStatus::FAILED_CURRENT_EXECUTABLE_PATH:
    return "failed-current-executable-path";
  case RendererPackagedMediaPathStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
