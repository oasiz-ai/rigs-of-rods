/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBridgeEndpoint.h"

#include <cstdlib>
#include <iostream>
#include <limits>
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
    std::cerr << "renderer bridge endpoint test failed: " << message << '\n';
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

RoR::HostRenderPlatform ForeignPlatform() {
  return CurrentPlatform() == RoR::HostRenderPlatform::WINDOWS
             ? RoR::HostRenderPlatform::LINUX
             : RoR::HostRenderPlatform::WINDOWS;
}

RoR::RendererBridgeEndpoint MakeEndpoint(RoR::RendererBridgeRole role) {
  RoR::RendererBridgeEndpoint endpoint;
  endpoint.platform = CurrentPlatform();
  endpoint.role = role;
  for (std::size_t index = 0U; index < endpoint.session_id.size(); ++index) {
    endpoint.session_id[index] = static_cast<std::uint8_t>(index + 1U);
  }
  endpoint.inbound_native_handle = 3U;
  endpoint.outbound_native_handle = 4U;
  return endpoint;
}

std::vector<const RoR::RendererChildLauncherChar *>
Pointers(const std::vector<NativeString> &values) {
  std::vector<const RoR::RendererChildLauncherChar *> pointers;
  pointers.reserve(values.size());
  for (const NativeString &value : values) {
    pointers.push_back(value.c_str());
  }
  return pointers;
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

void TestRoundTrip(RoR::RendererBridgeRole role) {
  const RoR::RendererBridgeEndpoint endpoint = MakeEndpoint(role);
  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("RoR"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("City World"), ROR_NATIVE_TEXT("unicode-\u03a9"),
      ROR_NATIVE_TEXT("")};
  const RoR::RendererBridgeEndpointArgvEncoding encoded =
      RoR::EncodeRendererBridgeEndpoint(endpoint, 5, arguments);
  Require(encoded.accepted &&
              encoded.status ==
                  RoR::RendererBridgeEndpointArgvStatus::READY &&
              encoded.arguments.size() == 11U,
          "encoder rejected a valid endpoint");
  Require(encoded.arguments[1] ==
              ROR_NATIVE_TEXT("--ror-render-bridge-version=1") &&
              encoded.arguments[2] ==
                  (role == RoR::RendererBridgeRole::GAME_HOST
                       ? ROR_NATIVE_TEXT("--ror-render-bridge-role=game-host")
                       : ROR_NATIVE_TEXT("--ror-render-bridge-role=presentation-frontend")) &&
              encoded.arguments[4] ==
                  ROR_NATIVE_TEXT("--ror-render-bridge-session=0102030405060708090a0b0c0d0e0f10") &&
              encoded.arguments[5] ==
                  ROR_NATIVE_TEXT("--ror-render-bridge-inbound=0000000000000003") &&
              encoded.arguments[6] ==
                  ROR_NATIVE_TEXT("--ror-render-bridge-outbound=0000000000000004"),
          "encoder changed canonical bridge records");

  const auto pointers = Pointers(encoded.arguments);
  const RoR::RendererBridgeEndpointArgvParseResult parsed =
      RoR::ParseRendererBridgeEndpoint(
          static_cast<int>(pointers.size()), pointers.data());
  Require(parsed.accepted &&
              parsed.status ==
                  RoR::RendererBridgeEndpointArgvStatus::READY &&
              parsed.endpoint.version ==
                  RoR::kRendererBridgeEndpointArgvContractVersion &&
              parsed.endpoint.platform == CurrentPlatform() &&
              parsed.endpoint.role == role &&
              parsed.endpoint.session_id == endpoint.session_id &&
              parsed.endpoint.inbound_native_handle == 3U &&
              parsed.endpoint.outbound_native_handle == 4U &&
              parsed.forwarded_arguments.size() == 5U,
          "decoder changed a valid endpoint");
  for (std::size_t index = 0U;
       index < parsed.forwarded_arguments.size(); ++index) {
    Require(parsed.forwarded_arguments[index] == arguments[index],
            "decoder changed the forwarded game suffix");
  }
}

void TestRendererIntentComposition() {
  const RoR::RendererBridgeEndpoint endpoint = MakeEndpoint(
      RoR::RendererBridgeRole::PRESENTATION_FRONTEND);
  const RoR::RendererChildLauncherChar *game[] = {
      ROR_NATIVE_TEXT("RoR"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("City World")};
  const RoR::RendererBridgeEndpointArgvEncoding bridge =
      RoR::EncodeRendererBridgeEndpoint(endpoint, 3, game);
  Require(bridge.accepted, "bridge composition setup failed");
  const auto bridge_pointers = Pointers(bridge.arguments);
  const RoR::RendererOgreNextChildIntentEncoding renderer =
      RoR::EncodeRendererOgreNextChildIntent(
          MakeAdmittedHandoff(),
          static_cast<int>(bridge_pointers.size()), bridge_pointers.data());
  Require(renderer.accepted,
          "renderer intent rejected the bridge-owned game suffix");

  const auto renderer_pointers = Pointers(renderer.arguments);
  const RoR::RendererOgreNextChildIntentParseResult renderer_parsed =
      RoR::ParseRendererOgreNextChildIntent(
          static_cast<int>(renderer_pointers.size()),
          renderer_pointers.data());
  Require(renderer_parsed.accepted,
          "renderer child could not expose the bridge prefix");
  const auto exposed_pointers = Pointers(
      renderer_parsed.forwarded_arguments);
  const RoR::RendererBridgeEndpointArgvParseResult bridge_parsed =
      RoR::ParseRendererBridgeEndpoint(
          static_cast<int>(exposed_pointers.size()),
          exposed_pointers.data());
  Require(bridge_parsed.accepted &&
              bridge_parsed.endpoint.role ==
                  RoR::RendererBridgeRole::PRESENTATION_FRONTEND &&
              bridge_parsed.forwarded_arguments.size() == 3U &&
              bridge_parsed.forwarded_arguments[1] ==
                  ROR_NATIVE_TEXT("-map") &&
              bridge_parsed.forwarded_arguments[2] ==
                  ROR_NATIVE_TEXT("City World"),
          "renderer/bridge protocol composition changed game arguments");
}

void TestInvalidEndpointEncoding() {
  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("RoR")};
  RoR::RendererBridgeEndpoint endpoint =
      MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST);
  endpoint.version = 2U;
  Require(!RoR::EncodeRendererBridgeEndpoint(endpoint, 1, arguments).accepted,
          "unknown endpoint version encoded");
  endpoint = MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST);
  endpoint.platform = ForeignPlatform();
  Require(!RoR::EncodeRendererBridgeEndpoint(endpoint, 1, arguments).accepted,
          "foreign endpoint platform encoded");
  endpoint = MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST);
  endpoint.role = static_cast<RoR::RendererBridgeRole>(255U);
  Require(!RoR::EncodeRendererBridgeEndpoint(endpoint, 1, arguments).accepted,
          "unknown endpoint role encoded");
  endpoint = MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST);
  endpoint.session_id.fill(0U);
  Require(!RoR::EncodeRendererBridgeEndpoint(endpoint, 1, arguments).accepted,
          "zero session encoded");
  endpoint = MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST);
  endpoint.inbound_native_handle = 2U;
  Require(!RoR::EncodeRendererBridgeEndpoint(endpoint, 1, arguments).accepted,
          "reserved native handle encoded");
  endpoint = MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST);
  endpoint.outbound_native_handle = endpoint.inbound_native_handle;
  Require(!RoR::EncodeRendererBridgeEndpoint(endpoint, 1, arguments).accepted,
          "equal native handles encoded");
  endpoint = MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST);
  endpoint.inbound_native_handle =
      (std::numeric_limits<std::uint64_t>::max)();
  Require(!RoR::EncodeRendererBridgeEndpoint(endpoint, 1, arguments).accepted,
          "invalid native handle sentinel encoded");
  const RoR::RendererChildLauncherChar *reserved_arguments[] = {
      ROR_NATIVE_TEXT("RoR"),
      ROR_NATIVE_TEXT("--ror-render-bridge-session=duplicate")};
  Require(!RoR::EncodeRendererBridgeEndpoint(
               MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST), 2,
               reserved_arguments)
               .accepted,
          "reserved game suffix encoded");
}

void RequireMalformed(std::vector<NativeString> arguments,
                      const char *message) {
  const auto pointers = Pointers(arguments);
  const RoR::RendererBridgeEndpointArgvParseResult result =
      RoR::ParseRendererBridgeEndpoint(
          static_cast<int>(pointers.size()), pointers.data());
  Require(!result.accepted &&
              result.status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MALFORMED_CONTRACT &&
              result.forwarded_arguments.empty(),
          message);
}

std::vector<NativeString> CanonicalArguments() {
  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("RoR"), ROR_NATIVE_TEXT("-map")};
  const RoR::RendererBridgeEndpointArgvEncoding encoded =
      RoR::EncodeRendererBridgeEndpoint(
          MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST), 2, arguments);
  Require(encoded.accepted, "canonical malformed-test setup failed");
  return encoded.arguments;
}

void TestMalformedDecode() {
  const RoR::RendererChildLauncherChar *missing[] = {
      ROR_NATIVE_TEXT("RoR"), ROR_NATIVE_TEXT("-map")};
  const auto missing_result = RoR::ParseRendererBridgeEndpoint(2, missing);
  Require(!missing_result.accepted &&
              missing_result.status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MISSING_CONTRACT,
          "missing bridge prefix was not classified exactly");

  std::vector<NativeString> values = CanonicalArguments();
  values[1] = ROR_NATIVE_TEXT("--ror-render-bridge-version=2");
  const auto pointers = Pointers(values);
  const auto version = RoR::ParseRendererBridgeEndpoint(
      static_cast<int>(pointers.size()), pointers.data());
  Require(!version.accepted &&
              version.status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MISSING_CONTRACT,
          "unknown version did not fail before field parsing");

  values = CanonicalArguments();
  values[2] = ROR_NATIVE_TEXT("--ror-render-bridge-role=frontend");
  RequireMalformed(values, "unknown role decoded");
  values = CanonicalArguments();
  values[3] = ROR_NATIVE_TEXT("--ror-render-bridge-platform=foreign");
  RequireMalformed(values, "unknown platform decoded");
  values = CanonicalArguments();
  values[4] = ROR_NATIVE_TEXT(
      "--ror-render-bridge-session=00000000000000000000000000000000");
  RequireMalformed(values, "zero session decoded");
  values = CanonicalArguments();
  values[4] = ROR_NATIVE_TEXT(
      "--ror-render-bridge-session=0102030405060708090A0b0c0d0e0f10");
  RequireMalformed(values, "noncanonical uppercase session decoded");
  values = CanonicalArguments();
  values[4] = ROR_NATIVE_TEXT(
      "--ror-render-bridge-session=0102030405060708090a0b0c0d0e0f1");
  RequireMalformed(values, "short session decoded");
  values = CanonicalArguments();
  values[5] =
      ROR_NATIVE_TEXT("--ror-render-bridge-inbound=0000000000000000");
  RequireMalformed(values, "zero handle decoded");
  values = CanonicalArguments();
  values[5] =
      ROR_NATIVE_TEXT("--ror-render-bridge-inbound=0000000000000003f");
  RequireMalformed(values, "long handle decoded");
  values = CanonicalArguments();
  values[5] =
      ROR_NATIVE_TEXT("--ror-render-bridge-inbound=0000000000000004");
  RequireMalformed(values, "equal handles decoded");
  values = CanonicalArguments();
  const NativeString temporary = values[2];
  values[2] = values[3];
  values[3] = temporary;
  RequireMalformed(values, "reordered bridge records decoded");
  values = CanonicalArguments();
  values.push_back(
      ROR_NATIVE_TEXT("--ror-render-bridge-session=duplicate"));
  RequireMalformed(values, "duplicate reserved suffix decoded");

  values = CanonicalArguments();
  auto null_pointers = Pointers(values);
  null_pointers[4] = nullptr;
  const auto null_result = RoR::ParseRendererBridgeEndpoint(
      static_cast<int>(null_pointers.size()), null_pointers.data());
  Require(!null_result.accepted &&
              null_result.status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_INVALID_ARGUMENTS,
          "null bridge record reached parsing");
}

void TestKnownEnums() {
  for (int value = 0;
       value <= static_cast<int>(RoR::RendererBridgeRole::
                                     PRESENTATION_FRONTEND);
       ++value) {
    const auto role = static_cast<RoR::RendererBridgeRole>(value);
    Require(RoR::IsKnownRendererBridgeRole(role) &&
                std::string(RoR::ToString(role)) != "invalid",
            "known bridge role was omitted");
  }
  Require(!RoR::IsKnownRendererBridgeRole(
              static_cast<RoR::RendererBridgeRole>(255U)),
          "unknown bridge role was accepted");
  for (int value = 0;
       value <= static_cast<int>(
                    RoR::RendererBridgeEndpointArgvStatus::FAILED_INTERNAL);
       ++value) {
    const auto status =
        static_cast<RoR::RendererBridgeEndpointArgvStatus>(value);
    Require(RoR::IsKnownRendererBridgeEndpointArgvStatus(status) &&
                std::string(RoR::ToString(status)) != "invalid",
            "known bridge status was omitted");
  }
}

} // namespace

int main() {
  Require(CurrentPlatform() != RoR::HostRenderPlatform::UNKNOWN,
          "test host is unsupported");
  TestRoundTrip(RoR::RendererBridgeRole::GAME_HOST);
  TestRoundTrip(RoR::RendererBridgeRole::PRESENTATION_FRONTEND);
  TestRendererIntentComposition();
  TestInvalidEndpointEncoding();
  TestMalformedDecode();
  TestKnownEnums();
  return EXIT_SUCCESS;
}
