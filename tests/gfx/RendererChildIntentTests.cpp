/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

// Deliberately include only the process-independent child protocol. This
// target never compiles or links RendererChildLauncher.cpp, proving the native
// Ogre-Next child can consume startup intent without importing OS spawning.
#include "RendererChildIntent.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

#if defined(_WIN32)
#define ROR_NATIVE_TEXT(value) L##value
#else
#define ROR_NATIVE_TEXT(value) value
#endif

using NativeString = std::basic_string<RoR::RendererChildLauncherChar>;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer child intent test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::HostRenderPlatform CurrentPlatform() {
#if defined(_WIN32)
  return RoR::HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return RoR::HostRenderPlatform::MACOS;
#elif defined(__linux__)
  return RoR::HostRenderPlatform::LINUX;
#else
  return RoR::HostRenderPlatform::UNKNOWN;
#endif
}

RoR::RendererStartupHandoffResult MakeAdmittedHandoff() {
  RoR::RendererStartupHandoffRequest request;
  request.startup.frontend =
      RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE;
  request.startup.directional_shadows =
      RoR::DirectionalShadowPreference::PSSM;
  request.startup.host_platform = CurrentPlatform();

  RoR::RendererStartupPackageAvailability package;
  package.package_platform = CurrentPlatform();
  package.ogre14_child_present = true;
  package.ogre_next_child_present = true;
  package.ogre_next_child_production_ready = true;
  package.ogre_next_pssm_admitted = true;
  return RoR::ResolveRendererStartupHandoff(request, package);
}

void TestStandaloneRoundTrip() {
  const RoR::RendererStartupHandoffResult handoff = MakeAdmittedHandoff();
  Require(handoff.accepted &&
              handoff.child == RoR::RendererFrontendChild::OGRE_NEXT,
          "test handoff was not admitted");

  const RoR::RendererChildLauncherChar *game_arguments[] = {
      ROR_NATIVE_TEXT("RoR"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("City World"), ROR_NATIVE_TEXT("unicode-\u03a9")};
  const RoR::RendererOgreNextChildIntentEncoding encoded =
      RoR::EncodeRendererOgreNextChildIntent(handoff, 4, game_arguments);
  Require(encoded.accepted &&
              encoded.status ==
                  RoR::RendererOgreNextChildIntentArgvStatus::READY &&
              encoded.arguments.size() == 8U,
          "standalone encoder rejected admitted startup intent");

  std::vector<const RoR::RendererChildLauncherChar *> encoded_arguments;
  encoded_arguments.reserve(encoded.arguments.size());
  for (const NativeString &argument : encoded.arguments) {
    encoded_arguments.push_back(argument.c_str());
  }
  const RoR::RendererOgreNextChildIntentParseResult parsed =
      RoR::ParseRendererOgreNextChildIntent(
          static_cast<int>(encoded_arguments.size()),
          encoded_arguments.data());
  Require(parsed.accepted &&
              parsed.status ==
                  RoR::RendererOgreNextChildIntentArgvStatus::READY &&
              parsed.startup.frontend ==
                  RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE &&
              parsed.startup.directional_shadows ==
                  RoR::DirectionalShadowPreference::PSSM &&
              parsed.startup.host_platform == CurrentPlatform() &&
              parsed.declared_native_backend ==
                  RoR::NativeRayTracingBackend::NONE &&
              parsed.forwarded_arguments.size() == 4U,
          "standalone decoder changed the startup request");
  for (std::size_t index = 0U;
       index < parsed.forwarded_arguments.size(); ++index) {
    Require(parsed.forwarded_arguments[index] == game_arguments[index],
            "standalone decoder changed the game suffix");
  }
}

void TestStandaloneDecoderFailsClosed() {
  const RoR::RendererChildLauncherChar *missing_contract[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"), ROR_NATIVE_TEXT("-map")};
  const RoR::RendererOgreNextChildIntentParseResult parsed =
      RoR::ParseRendererOgreNextChildIntent(2, missing_contract);
  Require(!parsed.accepted &&
              parsed.status ==
                  RoR::RendererOgreNextChildIntentArgvStatus::
                      REJECTED_MISSING_CONTRACT &&
              parsed.forwarded_arguments.empty(),
          "missing protocol prefix reached game argument parsing");
}

} // namespace

int main() {
  Require(CurrentPlatform() != RoR::HostRenderPlatform::UNKNOWN,
          "test host is not supported by the child protocol");
  TestStandaloneRoundTrip();
  TestStandaloneDecoderFailsClosed();
  return EXIT_SUCCESS;
}
