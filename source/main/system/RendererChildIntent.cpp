/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererChildIntent.h"

#include <cstddef>
#include <limits>

namespace RoR {
namespace {

template <typename Character>
bool HasValidArguments(int argc, const Character *const argv[]) {
  if (argc < 1 || argv == nullptr) {
    return false;
  }
  for (int index = 0; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return false;
    }
  }
  return true;
}

template <typename Character>
bool EqualsAscii(const Character *value, const char *expected) {
  if (value == nullptr || expected == nullptr) {
    return false;
  }
  while (*expected != '\0') {
    if (*value !=
        static_cast<Character>(static_cast<unsigned char>(*expected))) {
      return false;
    }
    ++value;
    ++expected;
  }
  return *value == static_cast<Character>('\0');
}

template <typename Character>
bool StartsWithAscii(const Character *value, const char *prefix) {
  if (value == nullptr || prefix == nullptr) {
    return false;
  }
  while (*prefix != '\0') {
    if (*value !=
        static_cast<Character>(static_cast<unsigned char>(*prefix))) {
      return false;
    }
    ++value;
    ++prefix;
  }
  return true;
}

template <typename Character>
const Character *ValueAfterAsciiPrefix(const Character *value,
                                       const char *prefix) {
  if (!StartsWithAscii(value, prefix)) {
    return nullptr;
  }
  while (*prefix != '\0') {
    ++value;
    ++prefix;
  }
  return value;
}

RendererChildLauncherString NativeAscii(const char *value) {
  RendererChildLauncherString result;
  while (value != nullptr && *value != '\0') {
    result.push_back(static_cast<RendererChildLauncherChar>(
        static_cast<unsigned char>(*value)));
    ++value;
  }
  return result;
}

const char *FrontendIntentValue(RendererFrontendPreference frontend) {
  switch (frontend) {
  case RendererFrontendPreference::OGRE_NEXT_PREFER:
    return "ogre-next-prefer";
  case RendererFrontendPreference::OGRE_NEXT_REQUIRE:
    return "ogre-next-require";
  case RendererFrontendPreference::LEGACY_ONLY:
    break;
  }
  return nullptr;
}

const char *DirectionalShadowIntentValue(
    DirectionalShadowPreference directional_shadows) {
  switch (directional_shadows) {
  case DirectionalShadowPreference::PSSM:
    return "pssm";
  case DirectionalShadowPreference::PREFER_NATIVE:
    return "prefer-native";
  case DirectionalShadowPreference::REQUIRE_NATIVE:
    return "require-native";
  }
  return nullptr;
}

const char *NativeBackendIntentValue(NativeRayTracingBackend backend) {
  switch (backend) {
  case NativeRayTracingBackend::NONE:
    return "none";
  case NativeRayTracingBackend::METAL:
    return "metal";
  case NativeRayTracingBackend::DXR:
    return "dxr";
  case NativeRayTracingBackend::VULKAN_KHR:
    return "vulkan-khr";
  }
  return nullptr;
}

bool ParseFrontendIntentValue(
    const RendererChildLauncherChar *value,
    RendererFrontendPreference &frontend) {
  if (EqualsAscii(value, "ogre-next-prefer")) {
    frontend = RendererFrontendPreference::OGRE_NEXT_PREFER;
    return true;
  }
  if (EqualsAscii(value, "ogre-next-require")) {
    frontend = RendererFrontendPreference::OGRE_NEXT_REQUIRE;
    return true;
  }
  return false;
}

bool ParseDirectionalShadowIntentValue(
    const RendererChildLauncherChar *value,
    DirectionalShadowPreference &directional_shadows) {
  if (EqualsAscii(value, "pssm")) {
    directional_shadows = DirectionalShadowPreference::PSSM;
    return true;
  }
  if (EqualsAscii(value, "prefer-native")) {
    directional_shadows = DirectionalShadowPreference::PREFER_NATIVE;
    return true;
  }
  if (EqualsAscii(value, "require-native")) {
    directional_shadows = DirectionalShadowPreference::REQUIRE_NATIVE;
    return true;
  }
  return false;
}

bool ParseNativeBackendIntentValue(
    const RendererChildLauncherChar *value,
    NativeRayTracingBackend &backend) {
  if (EqualsAscii(value, "none")) {
    backend = NativeRayTracingBackend::NONE;
    return true;
  }
  if (EqualsAscii(value, "metal")) {
    backend = NativeRayTracingBackend::METAL;
    return true;
  }
  if (EqualsAscii(value, "dxr")) {
    backend = NativeRayTracingBackend::DXR;
    return true;
  }
  if (EqualsAscii(value, "vulkan-khr")) {
    backend = NativeRayTracingBackend::VULKAN_KHR;
    return true;
  }
  return false;
}

bool HasSafeChildBasename(const char *basename) {
  if (basename == nullptr || basename[0] == '\0') {
    return false;
  }
  for (const char *cursor = basename; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') {
      return false;
    }
  }
  return true;
}

HostRenderPlatform CompileTimeHostPlatform() noexcept {
#if defined(_WIN32)
  return HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return HostRenderPlatform::MACOS;
#elif defined(__linux__)
  return HostRenderPlatform::LINUX;
#else
  return HostRenderPlatform::UNKNOWN;
#endif
}

} // namespace

RendererOgreNextChildIntentEncoding EncodeRendererOgreNextChildIntent(
    const RendererStartupHandoffResult &handoff, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept {
  RendererOgreNextChildIntentEncoding result;
  try {
    const char *child_basename =
        RendererFrontendChildExecutableName(handoff);
    if (!handoff.accepted ||
        handoff.child != RendererFrontendChild::OGRE_NEXT ||
        handoff.package_platform != CompileTimeHostPlatform() ||
        !HasSafeChildBasename(child_basename)) {
      return result;
    }
    result.status =
        RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_ARGUMENTS;
    if (!HasValidArguments(argc, argv) || argv[0][0] == 0 ||
        argc > (std::numeric_limits<int>::max)() - 4) {
      return result;
    }

    const char *frontend_value =
        FrontendIntentValue(handoff.requested_frontend);
    const char *shadow_value = DirectionalShadowIntentValue(
        handoff.requested_directional_shadows);
    const char *native_backend_value =
        NativeBackendIntentValue(handoff.declared_native_backend);
    if (frontend_value == nullptr || shadow_value == nullptr ||
        native_backend_value == nullptr) {
      result.status =
          RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_HANDOFF;
      return result;
    }

    result.startup.version = kRendererStartupPlanContractVersion;
    result.startup.frontend = handoff.requested_frontend;
    result.startup.directional_shadows =
        handoff.requested_directional_shadows;
    result.startup.host_platform = handoff.package_platform;
    result.declared_native_backend = handoff.declared_native_backend;

    result.arguments.reserve(static_cast<std::size_t>(argc) + 4U);
    result.arguments.emplace_back(argv[0]);
    result.arguments.push_back(
        NativeAscii("--ror-renderer-child-intent-version=1"));
    result.arguments.push_back(
        NativeAscii("--ror-renderer-child-frontend=") +
        NativeAscii(frontend_value));
    result.arguments.push_back(
        NativeAscii("--ror-renderer-child-directional-shadows=") +
        NativeAscii(shadow_value));
    result.arguments.push_back(
        NativeAscii("--ror-renderer-child-native-backend=") +
        NativeAscii(native_backend_value));
    for (int index = 1; index < argc; ++index) {
      result.arguments.emplace_back(argv[index]);
    }
    result.status = RendererOgreNextChildIntentArgvStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    result.arguments.clear();
    result.accepted = false;
    result.status = RendererOgreNextChildIntentArgvStatus::FAILED_INTERNAL;
    return result;
  }
}

RendererOgreNextChildIntentParseResult ParseRendererOgreNextChildIntent(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept {
  RendererOgreNextChildIntentParseResult result;
  try {
    const HostRenderPlatform child_host_platform = CompileTimeHostPlatform();
    if (!IsKnownHostRenderPlatform(child_host_platform) ||
        child_host_platform == HostRenderPlatform::UNKNOWN) {
      result.status =
          RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_PLATFORM;
      return result;
    }
    if (!HasValidArguments(argc, argv) || argv[0][0] == 0) {
      return result;
    }
    if (argc < 5 ||
        !EqualsAscii(argv[1],
                     "--ror-renderer-child-intent-version=1")) {
      result.status =
          RendererOgreNextChildIntentArgvStatus::REJECTED_MISSING_CONTRACT;
      return result;
    }

    static const char frontend_prefix[] =
        "--ror-renderer-child-frontend=";
    static const char shadow_prefix[] =
        "--ror-renderer-child-directional-shadows=";
    static const char native_backend_prefix[] =
        "--ror-renderer-child-native-backend=";
    const RendererChildLauncherChar *frontend_value =
        ValueAfterAsciiPrefix(argv[2], frontend_prefix);
    const RendererChildLauncherChar *shadow_value =
        ValueAfterAsciiPrefix(argv[3], shadow_prefix);
    const RendererChildLauncherChar *native_backend_value =
        ValueAfterAsciiPrefix(argv[4], native_backend_prefix);
    if (frontend_value == nullptr || shadow_value == nullptr ||
        native_backend_value == nullptr ||
        !ParseFrontendIntentValue(frontend_value, result.startup.frontend) ||
        !ParseDirectionalShadowIntentValue(
            shadow_value, result.startup.directional_shadows) ||
        !ParseNativeBackendIntentValue(
            native_backend_value, result.declared_native_backend) ||
        (result.startup.directional_shadows ==
             DirectionalShadowPreference::REQUIRE_NATIVE &&
         result.declared_native_backend == NativeRayTracingBackend::NONE) ||
        (result.declared_native_backend != NativeRayTracingBackend::NONE &&
         result.declared_native_backend !=
             ExpectedNativeRayTracingBackend(child_host_platform))) {
      result.status =
          RendererOgreNextChildIntentArgvStatus::REJECTED_MALFORMED_CONTRACT;
      return result;
    }

    result.startup.version = kRendererStartupPlanContractVersion;
    result.startup.host_platform = child_host_platform;
    for (int index = 5; index < argc; ++index) {
      if (StartsWithAscii(argv[index], "--ror-renderer-child-")) {
        result.status = RendererOgreNextChildIntentArgvStatus::
            REJECTED_MALFORMED_CONTRACT;
        return result;
      }
    }
    result.forwarded_arguments.reserve(
        static_cast<std::size_t>(argc - 4));
    result.forwarded_arguments.emplace_back(argv[0]);
    for (int index = 5; index < argc; ++index) {
      result.forwarded_arguments.emplace_back(argv[index]);
    }
    result.status = RendererOgreNextChildIntentArgvStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    result.forwarded_arguments.clear();
    result.accepted = false;
    result.status = RendererOgreNextChildIntentArgvStatus::FAILED_INTERNAL;
    return result;
  }
}

bool IsKnownRendererOgreNextChildIntentArgvStatus(
    RendererOgreNextChildIntentArgvStatus status) noexcept {
  switch (status) {
  case RendererOgreNextChildIntentArgvStatus::READY:
  case RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_HANDOFF:
  case RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_ARGUMENTS:
  case RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_PLATFORM:
  case RendererOgreNextChildIntentArgvStatus::REJECTED_MISSING_CONTRACT:
  case RendererOgreNextChildIntentArgvStatus::REJECTED_MALFORMED_CONTRACT:
  case RendererOgreNextChildIntentArgvStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(
    RendererOgreNextChildIntentArgvStatus status) noexcept {
  switch (status) {
  case RendererOgreNextChildIntentArgvStatus::READY:
    return "ready";
  case RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_HANDOFF:
    return "rejected-invalid-handoff";
  case RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_ARGUMENTS:
    return "rejected-invalid-arguments";
  case RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_PLATFORM:
    return "rejected-invalid-platform";
  case RendererOgreNextChildIntentArgvStatus::REJECTED_MISSING_CONTRACT:
    return "rejected-missing-contract";
  case RendererOgreNextChildIntentArgvStatus::REJECTED_MALFORMED_CONTRACT:
    return "rejected-malformed-contract";
  case RendererOgreNextChildIntentArgvStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
