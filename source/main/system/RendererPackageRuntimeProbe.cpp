/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererPackageRuntimeProbe.h"

#include <cerrno>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace RoR {
namespace {

using ArtifactStatus = RendererPackageRuntimeArtifactStatus;
using ProbeStatus = RendererPackageRuntimeProbeStatus;

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

bool IsSupportedPlatform(HostRenderPlatform platform) noexcept {
  return platform == HostRenderPlatform::MACOS ||
         platform == HostRenderPlatform::WINDOWS ||
         platform == HostRenderPlatform::LINUX;
}

bool HasValidDeclaration(
    const RendererStartupPackageAvailability &availability) noexcept {
  if (availability.version != kRendererStartupHandoffContractVersion ||
      !IsSupportedPlatform(availability.package_platform) ||
      !IsKnownNativeRayTracingBackend(
          availability.native_directional_shadow_backend) ||
      !availability.ogre14_child_present) {
    return false;
  }
  if (availability.ogre_next_child_production_ready &&
      !availability.ogre_next_child_present) {
    return false;
  }
  if (availability.ogre_next_pssm_admitted &&
      (!availability.ogre_next_child_present ||
       !availability.ogre_next_child_production_ready)) {
    return false;
  }
  return availability.native_directional_shadow_backend ==
                 NativeRayTracingBackend::NONE ||
         (availability.ogre_next_child_present &&
          availability.ogre_next_child_production_ready);
}

bool ObservationShapeMatchesDeclaration(
    const RendererStartupPackageAvailability &availability,
    const RendererPackageRuntimeObservation &observation) noexcept {
  if (observation.version != kRendererPackageRuntimeProbeContractVersion ||
      observation.package_platform != availability.package_platform ||
      !IsKnownRendererPackageRuntimeArtifactStatus(
          observation.ogre14_child) ||
      !IsKnownRendererPackageRuntimeArtifactStatus(
          observation.ogre_next_child) ||
      !IsKnownRendererPackageRuntimeArtifactStatus(
          observation.ogre_next_shader_media) ||
      !IsKnownRendererPackageRuntimeArtifactStatus(
          observation.ogre_next_presentation_media)) {
    return false;
  }
  if (observation.ogre14_child == ArtifactStatus::NOT_REQUIRED) {
    return false;
  }
  if (!availability.ogre_next_child_present) {
    return observation.ogre_next_child == ArtifactStatus::NOT_REQUIRED &&
           observation.ogre_next_shader_media ==
               ArtifactStatus::NOT_REQUIRED &&
           observation.ogre_next_presentation_media ==
               ArtifactStatus::NOT_REQUIRED;
  }
  if (observation.ogre_next_child == ArtifactStatus::NOT_REQUIRED) {
    return false;
  }
  const bool media_required =
      availability.ogre_next_child_production_ready;
  return media_required
             ? observation.ogre_next_shader_media !=
                       ArtifactStatus::NOT_REQUIRED &&
                   observation.ogre_next_presentation_media !=
                       ArtifactStatus::NOT_REQUIRED
             : observation.ogre_next_shader_media ==
                       ArtifactStatus::NOT_REQUIRED &&
                   observation.ogre_next_presentation_media ==
                       ArtifactStatus::NOT_REQUIRED;
}

void ClearOgreNextAvailability(
    RendererStartupPackageAvailability &availability) noexcept {
  availability.ogre_next_child_present = false;
  availability.ogre_next_child_production_ready = false;
  availability.ogre_next_pssm_admitted = false;
  availability.native_directional_shadow_backend =
      NativeRayTracingBackend::NONE;
}

void RejectAllAvailability(
    RendererStartupPackageAvailability &availability) noexcept {
  ClearOgreNextAvailability(availability);
  availability.ogre14_child_present = false;
}

const char *Ogre14Basename(HostRenderPlatform platform) noexcept {
  return platform == HostRenderPlatform::WINDOWS ? "RoR-Ogre14.exe"
                                                  : "RoR-Ogre14";
}

const char *OgreNextBasename(HostRenderPlatform platform) noexcept {
  return platform == HostRenderPlatform::WINDOWS ? "RoR-OgreNext.exe"
                                                  : "RoR-OgreNext";
}

#if defined(_WIN32)

ArtifactStatus InspectArtifact(const RendererChildLauncherString &path,
                               bool require_executable,
                               std::uint32_t &native_error_code) noexcept {
  ::SetLastError(ERROR_SUCCESS);
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = ::GetLastError();
    if (native_error_code == 0U) {
      native_error_code = static_cast<std::uint32_t>(error);
    }
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
               ? ArtifactStatus::MISSING
               : ArtifactStatus::FAILED_INSPECTION;
  }
  if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return ArtifactStatus::REJECTED_LINK_OR_REPARSE_POINT;
  }
  const bool is_directory =
      (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
  if (require_executable == is_directory) {
    return ArtifactStatus::REJECTED_WRONG_TYPE;
  }
  // Windows has no POSIX executable bit. The fixed .exe basename plus a
  // regular, non-reparse file is the platform's package-level executable
  // predicate; CreateProcessW remains the authoritative loader check.
  return ArtifactStatus::READY;
}

#else

ArtifactStatus InspectArtifact(const RendererChildLauncherString &path,
                               bool require_executable,
                               std::uint32_t &native_error_code) noexcept {
  struct stat attributes {};
  if (::lstat(path.c_str(), &attributes) != 0) {
    const int error = errno;
    if (native_error_code == 0U) {
      native_error_code = static_cast<std::uint32_t>(error);
    }
    return error == ENOENT || error == ENOTDIR
               ? ArtifactStatus::MISSING
               : ArtifactStatus::FAILED_INSPECTION;
  }
  if (S_ISLNK(attributes.st_mode)) {
    return ArtifactStatus::REJECTED_LINK_OR_REPARSE_POINT;
  }
  if (require_executable) {
    if (!S_ISREG(attributes.st_mode)) {
      return ArtifactStatus::REJECTED_WRONG_TYPE;
    }
    if (::access(path.c_str(), X_OK) != 0) {
      if (native_error_code == 0U) {
        native_error_code = static_cast<std::uint32_t>(errno);
      }
      return ArtifactStatus::REJECTED_NOT_EXECUTABLE;
    }
  } else if (!S_ISDIR(attributes.st_mode)) {
    return ArtifactStatus::REJECTED_WRONG_TYPE;
  }
  return ArtifactStatus::READY;
}

#endif

RendererPackageRuntimeProbeResult Failure(
    const RendererStartupPackageAvailability &declared,
    ProbeStatus status) noexcept {
  RendererPackageRuntimeProbeResult result;
  result.declared_availability = declared;
  result.effective_availability = declared;
  RejectAllAvailability(result.effective_availability);
  result.status = status;
  result.accepted = false;
  return result;
}

} // namespace

RendererPackageRuntimeProbeResult ResolveRendererPackageRuntimeObservation(
    const RendererStartupPackageAvailability &declared_availability,
    const RendererPackageRuntimeObservation &observation) noexcept {
  RendererPackageRuntimeProbeResult result;
  result.declared_availability = declared_availability;
  result.effective_availability = declared_availability;
  result.observation = observation;
  try {
    if (!HasValidDeclaration(declared_availability)) {
      RejectAllAvailability(result.effective_availability);
      return result;
    }
    if (!ObservationShapeMatchesDeclaration(declared_availability,
                                            observation)) {
      RejectAllAvailability(result.effective_availability);
      result.status = ProbeStatus::REJECTED_INVALID_OBSERVATION;
      return result;
    }
    if (observation.ogre14_child != ArtifactStatus::READY) {
      RejectAllAvailability(result.effective_availability);
      result.status = ProbeStatus::REJECTED_OGRE14_CHILD;
      return result;
    }

    bool ogre_next_ready = declared_availability.ogre_next_child_present;
    if (ogre_next_ready) {
      ogre_next_ready =
          observation.ogre_next_child == ArtifactStatus::READY;
      if (declared_availability.ogre_next_child_production_ready) {
        ogre_next_ready =
            ogre_next_ready &&
            observation.ogre_next_shader_media == ArtifactStatus::READY &&
            observation.ogre_next_presentation_media == ArtifactStatus::READY;
      }
    }
    if (declared_availability.ogre_next_child_present &&
        !ogre_next_ready) {
      ClearOgreNextAvailability(result.effective_availability);
      result.status = ProbeStatus::READY_OGRE14_FALLBACK;
      result.ogre_next_runtime_degraded = true;
    } else {
      result.status = ProbeStatus::READY;
    }
    result.accepted = true;
    return result;
  } catch (...) {
    RejectAllAvailability(result.effective_availability);
    result.status = ProbeStatus::FAILED_INTERNAL;
    result.accepted = false;
    return result;
  }
}

RendererPackageRuntimeProbeResult
ProbeRendererPackageRuntimeAvailabilityFromExecutable(
    const RendererStartupPackageAvailability &declared_availability,
    const RendererChildLauncherString &canonical_public_executable_path)
    noexcept {
  if (!HasValidDeclaration(declared_availability)) {
    return Failure(declared_availability,
                   ProbeStatus::REJECTED_INVALID_DECLARATION);
  }
  if (declared_availability.package_platform != CompiledHostPlatform()) {
    return Failure(declared_availability,
                   ProbeStatus::REJECTED_PLATFORM_MISMATCH);
  }
  try {
    RendererPackageRuntimeObservation observation;
    observation.package_platform = declared_availability.package_platform;
    RendererPackageRuntimeProbeResult result;
    result.declared_availability = declared_availability;
    result.effective_availability = declared_availability;
    result.observation = observation;

    const RendererSiblingPathResult ogre14_path =
        ResolveRendererSiblingPathFromExecutable(
            canonical_public_executable_path,
            Ogre14Basename(declared_availability.package_platform));
    result.sibling_path_status = ogre14_path.status;
    if (!ogre14_path.accepted) {
      RejectAllAvailability(result.effective_availability);
      result.status = ProbeStatus::FAILED_PACKAGE_LAYOUT;
      return result;
    }
    observation.ogre14_child =
        InspectArtifact(ogre14_path.path, true, result.native_error_code);

    if (declared_availability.ogre_next_child_present) {
      const RendererSiblingPathResult ogre_next_path =
          ResolveRendererSiblingPathFromExecutable(
              canonical_public_executable_path,
              OgreNextBasename(declared_availability.package_platform));
      result.sibling_path_status = ogre_next_path.status;
      if (!ogre_next_path.accepted) {
        RejectAllAvailability(result.effective_availability);
        result.observation = observation;
        result.status = ProbeStatus::FAILED_PACKAGE_LAYOUT;
        return result;
      }
      observation.ogre_next_child = InspectArtifact(
          ogre_next_path.path, true, result.native_error_code);
      if (declared_availability.ogre_next_child_production_ready) {
        const RendererPackagedMediaPathResult media =
            ResolveRendererPackagedMediaPathFromExecutable(
                declared_availability.package_platform,
                ogre_next_path.path);
        result.media_path_status = media.status;
        if (!media.accepted) {
          RejectAllAvailability(result.effective_availability);
          result.observation = observation;
          result.status = ProbeStatus::FAILED_PACKAGE_LAYOUT;
          return result;
        }
        observation.ogre_next_shader_media = InspectArtifact(
            media.shader_media_root, false, result.native_error_code);
        observation.ogre_next_presentation_media = InspectArtifact(
            media.presentation_media_root, false,
            result.native_error_code);
      }
    }

    const std::uint32_t native_error_code = result.native_error_code;
    result = ResolveRendererPackageRuntimeObservation(declared_availability,
                                                      observation);
    result.native_error_code = native_error_code;
    result.sibling_path_status = RendererSiblingPathStatus::READY;
    if (declared_availability.ogre_next_child_production_ready) {
      result.media_path_status = RendererPackagedMediaPathStatus::READY;
    }
    return result;
  } catch (...) {
    return Failure(declared_availability, ProbeStatus::FAILED_INTERNAL);
  }
}

RendererPackageRuntimeProbeResult ProbeRendererPackageRuntimeAvailability(
    const RendererStartupPackageAvailability &declared_availability) noexcept {
  const RendererCurrentExecutablePathResult executable =
      ResolveRendererCurrentExecutablePath();
  if (!executable.accepted) {
    RendererPackageRuntimeProbeResult result =
        Failure(declared_availability,
                ProbeStatus::FAILED_CURRENT_EXECUTABLE_PATH);
    result.sibling_path_status = executable.status;
    result.native_error_code = executable.native_error_code;
    return result;
  }
  RendererPackageRuntimeProbeResult result =
      ProbeRendererPackageRuntimeAvailabilityFromExecutable(
          declared_availability, executable.path);
  if (result.native_error_code == 0U) {
    result.native_error_code = executable.native_error_code;
  }
  return result;
}

bool IsKnownRendererPackageRuntimeArtifactStatus(
    RendererPackageRuntimeArtifactStatus status) noexcept {
  switch (status) {
  case ArtifactStatus::NOT_REQUIRED:
  case ArtifactStatus::READY:
  case ArtifactStatus::MISSING:
  case ArtifactStatus::REJECTED_LINK_OR_REPARSE_POINT:
  case ArtifactStatus::REJECTED_WRONG_TYPE:
  case ArtifactStatus::REJECTED_NOT_EXECUTABLE:
  case ArtifactStatus::FAILED_INSPECTION:
    return true;
  }
  return false;
}

bool IsKnownRendererPackageRuntimeProbeStatus(
    RendererPackageRuntimeProbeStatus status) noexcept {
  switch (status) {
  case ProbeStatus::READY:
  case ProbeStatus::READY_OGRE14_FALLBACK:
  case ProbeStatus::REJECTED_INVALID_DECLARATION:
  case ProbeStatus::REJECTED_INVALID_OBSERVATION:
  case ProbeStatus::REJECTED_PLATFORM_MISMATCH:
  case ProbeStatus::FAILED_CURRENT_EXECUTABLE_PATH:
  case ProbeStatus::FAILED_PACKAGE_LAYOUT:
  case ProbeStatus::REJECTED_OGRE14_CHILD:
  case ProbeStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererPackageRuntimeArtifactStatus status) noexcept {
  switch (status) {
  case ArtifactStatus::NOT_REQUIRED:
    return "not-required";
  case ArtifactStatus::READY:
    return "ready";
  case ArtifactStatus::MISSING:
    return "missing";
  case ArtifactStatus::REJECTED_LINK_OR_REPARSE_POINT:
    return "rejected-link-or-reparse-point";
  case ArtifactStatus::REJECTED_WRONG_TYPE:
    return "rejected-wrong-type";
  case ArtifactStatus::REJECTED_NOT_EXECUTABLE:
    return "rejected-not-executable";
  case ArtifactStatus::FAILED_INSPECTION:
    return "failed-inspection";
  }
  return "invalid";
}

const char *ToString(RendererPackageRuntimeProbeStatus status) noexcept {
  switch (status) {
  case ProbeStatus::READY:
    return "ready";
  case ProbeStatus::READY_OGRE14_FALLBACK:
    return "ready-ogre14-fallback";
  case ProbeStatus::REJECTED_INVALID_DECLARATION:
    return "rejected-invalid-declaration";
  case ProbeStatus::REJECTED_INVALID_OBSERVATION:
    return "rejected-invalid-observation";
  case ProbeStatus::REJECTED_PLATFORM_MISMATCH:
    return "rejected-platform-mismatch";
  case ProbeStatus::FAILED_CURRENT_EXECUTABLE_PATH:
    return "failed-current-executable-path";
  case ProbeStatus::FAILED_PACKAGE_LAYOUT:
    return "failed-package-layout";
  case ProbeStatus::REJECTED_OGRE14_CHILD:
    return "rejected-ogre14-child";
  case ProbeStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
