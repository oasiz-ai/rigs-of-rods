/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererPackageRuntimeProbe.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

using Artifact = RoR::RendererPackageRuntimeArtifactStatus;
using Probe = RoR::RendererPackageRuntimeProbeStatus;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer package runtime probe test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::HostRenderPlatform CurrentPlatform() {
#if defined(_WIN32)
  return RoR::HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return RoR::HostRenderPlatform::MACOS;
#else
  return RoR::HostRenderPlatform::LINUX;
#endif
}

RoR::RendererStartupPackageAvailability AdmittedPackage() {
  RoR::RendererStartupPackageAvailability package;
  package.package_platform = CurrentPlatform();
  package.ogre14_child_present = true;
  package.ogre_next_child_present = true;
  package.ogre_next_child_production_ready = true;
  package.ogre_next_pssm_admitted = true;
  package.native_directional_shadow_backend =
      RoR::NativeRayTracingBackend::NONE;
  return package;
}

RoR::RendererPackageRuntimeObservation ReadyObservation() {
  RoR::RendererPackageRuntimeObservation observation;
  observation.package_platform = CurrentPlatform();
  observation.ogre14_child = Artifact::READY;
  observation.ogre_next_child = Artifact::READY;
  observation.ogre_next_shader_media = Artifact::READY;
  observation.ogre_next_presentation_media = Artifact::READY;
  return observation;
}

void RequireAllOgreNextCleared(
    const RoR::RendererStartupPackageAvailability &availability,
    const char *message) {
  Require(!availability.ogre_next_child_present &&
              !availability.ogre_next_child_production_ready &&
              !availability.ogre_next_pssm_admitted &&
              availability.native_directional_shadow_backend ==
                  RoR::NativeRayTracingBackend::NONE,
          message);
}

void TestStableStatusContracts() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const Artifact artifact = static_cast<Artifact>(value);
    Require(RoR::IsKnownRendererPackageRuntimeArtifactStatus(artifact) ==
                (value <= 6U),
            "artifact status classifier accepted an unknown value");
    const Probe probe = static_cast<Probe>(value);
    Require(RoR::IsKnownRendererPackageRuntimeProbeStatus(probe) ==
                (value <= 8U),
            "probe status classifier accepted an unknown value");
  }
  Require(RoR::kRendererPackageRuntimeProbeContractVersion == 1U,
          "probe contract version changed");
  Require(std::strcmp(RoR::ToString(Artifact::REJECTED_NOT_EXECUTABLE),
                      "rejected-not-executable") == 0,
          "artifact status spelling changed");
  Require(std::strcmp(RoR::ToString(Probe::READY_OGRE14_FALLBACK),
                      "ready-ogre14-fallback") == 0,
          "fallback status spelling changed");
  Require(std::strcmp(RoR::ToString(static_cast<Probe>(255U)), "invalid") ==
              0,
          "unknown status did not stringify fail-closed");
}

void TestAdmittedPackageRemainsAdmittedOnlyWithCompleteEvidence() {
  const auto result = RoR::ResolveRendererPackageRuntimeObservation(
      AdmittedPackage(), ReadyObservation());
  Require(result.accepted && result.status == Probe::READY &&
              !result.ogre_next_runtime_degraded &&
              result.effective_availability.ogre14_child_present &&
              result.effective_availability.ogre_next_child_present &&
              result.effective_availability.ogre_next_child_production_ready &&
              result.effective_availability.ogre_next_pssm_admitted,
          "complete admitted package evidence was not preserved");
}

void TestEveryOgreNextArtifactFailureNarrowsToLegacy() {
  const Artifact failures[] = {
      Artifact::MISSING, Artifact::REJECTED_LINK_OR_REPARSE_POINT,
      Artifact::REJECTED_WRONG_TYPE, Artifact::REJECTED_NOT_EXECUTABLE,
      Artifact::FAILED_INSPECTION};
  for (const Artifact failure : failures) {
    for (unsigned int field = 0U; field < 3U; ++field) {
      auto observation = ReadyObservation();
      if (field == 0U) {
        observation.ogre_next_child = failure;
      } else if (field == 1U) {
        observation.ogre_next_shader_media = failure;
      } else {
        observation.ogre_next_presentation_media = failure;
      }
      const auto result = RoR::ResolveRendererPackageRuntimeObservation(
          AdmittedPackage(), observation);
      Require(result.accepted &&
                  result.status == Probe::READY_OGRE14_FALLBACK &&
                  result.ogre_next_runtime_degraded &&
                  result.effective_availability.ogre14_child_present,
              "Ogre-Next artifact failure did not preserve legacy fallback");
      RequireAllOgreNextCleared(
          result.effective_availability,
          "Ogre-Next artifact failure retained an admission fact");
    }
  }
}

void TestLegacyHostIsMandatoryForBothFrontends() {
  const Artifact failures[] = {
      Artifact::MISSING, Artifact::REJECTED_LINK_OR_REPARSE_POINT,
      Artifact::REJECTED_WRONG_TYPE, Artifact::REJECTED_NOT_EXECUTABLE,
      Artifact::FAILED_INSPECTION};
  for (const Artifact failure : failures) {
    auto observation = ReadyObservation();
    observation.ogre14_child = failure;
    const auto result = RoR::ResolveRendererPackageRuntimeObservation(
        AdmittedPackage(), observation);
    Require(!result.accepted &&
                result.status == Probe::REJECTED_OGRE14_CHILD &&
                !result.effective_availability.ogre14_child_present,
            "unusable game host did not fail the complete package closed");
    RequireAllOgreNextCleared(
        result.effective_availability,
        "unusable game host retained Ogre-Next admission facts");
  }
}

void TestLegacyOnlyDeclarationDoesNotRequireOgreNextFiles() {
  auto package = AdmittedPackage();
  package.ogre_next_child_present = false;
  package.ogre_next_child_production_ready = false;
  package.ogre_next_pssm_admitted = false;
  RoR::RendererPackageRuntimeObservation observation;
  observation.package_platform = CurrentPlatform();
  observation.ogre14_child = Artifact::READY;
  const auto result = RoR::ResolveRendererPackageRuntimeObservation(
      package, observation);
  Require(result.accepted && result.status == Probe::READY &&
              result.effective_availability.ogre14_child_present &&
              !result.ogre_next_runtime_degraded,
          "legacy-only declaration required undeclared Ogre-Next files");
  RequireAllOgreNextCleared(
      result.effective_availability,
      "legacy-only declaration gained Ogre-Next admission facts");
}

void TestMalformedDeclarationsAndObservationsFailClosed() {
  auto invalid_declaration = AdmittedPackage();
  invalid_declaration.ogre14_child_present = false;
  auto result = RoR::ResolveRendererPackageRuntimeObservation(
      invalid_declaration, ReadyObservation());
  Require(!result.accepted &&
              result.status == Probe::REJECTED_INVALID_DECLARATION &&
              !result.effective_availability.ogre14_child_present,
          "invalid package declaration was accepted");
  RequireAllOgreNextCleared(
      result.effective_availability,
      "invalid package declaration retained Ogre-Next facts");

  auto invalid_observation = ReadyObservation();
  invalid_observation.version += 1U;
  result = RoR::ResolveRendererPackageRuntimeObservation(
      AdmittedPackage(), invalid_observation);
  Require(!result.accepted &&
              result.status == Probe::REJECTED_INVALID_OBSERVATION &&
              !result.effective_availability.ogre14_child_present,
          "invalid runtime observation was accepted");
  RequireAllOgreNextCleared(
      result.effective_availability,
      "invalid runtime observation retained Ogre-Next facts");

  invalid_observation = ReadyObservation();
  invalid_observation.ogre_next_child = Artifact::NOT_REQUIRED;
  result = RoR::ResolveRendererPackageRuntimeObservation(
      AdmittedPackage(), invalid_observation);
  Require(!result.accepted &&
              result.status == Probe::REJECTED_INVALID_OBSERVATION,
          "required artifact with not-required evidence was accepted");
}

} // namespace

int main() {
  TestStableStatusContracts();
  TestAdmittedPackageRemainsAdmittedOnlyWithCompleteEvidence();
  TestEveryOgreNextArtifactFailureNarrowsToLegacy();
  TestLegacyHostIsMandatoryForBothFrontends();
  TestLegacyOnlyDeclarationDoesNotRequireOgreNextFiles();
  TestMalformedDeclarationsAndObservationsFailClosed();
  return EXIT_SUCCESS;
}
