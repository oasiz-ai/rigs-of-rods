/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "WorldModelPlatformIdentity.h"

#include "WorldModelTelemetry.h"

#include <cctype>
#include <cstring>
#include <string>

#if defined(_WIN32)
#   if !defined(NOMINMAX)
#       define NOMINMAX
#   endif
#   if !defined(WIN32_LEAN_AND_MEAN)
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#else
#   include <sys/utsname.h>
#endif

namespace RoR {
namespace WorldModel {
namespace {

void SetError(std::string* error, const std::string& text)
{
    if (error != nullptr)
        *error = text;
}

std::string CanonicalIdentifier(
    const std::string& prefix,
    const std::string& input)
{
    std::string output = prefix;
    bool prior_separator =
        !output.empty() &&
        !std::isalnum(static_cast<unsigned char>(output.back()));
    for (const unsigned char byte : input)
    {
        const char character = static_cast<char>(std::tolower(byte));
        const bool alnum =
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9');
        if (alnum)
        {
            output.push_back(character);
            prior_separator = false;
        }
        else if (!output.empty() && !prior_separator)
        {
            output.push_back('-');
            prior_separator = true;
        }
        if (output.size() == 128U)
            break;
    }
    while (!output.empty() &&
           !std::isalnum(static_cast<unsigned char>(output.back())))
    {
        output.pop_back();
    }
    return output;
}

#if defined(_WIN32)
bool InspectWindowsRuntimeVersion(
    WindowsRuntimeVersion& output,
    std::string* error)
{
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        SetError(error, "GetModuleHandleW(ntdll.dll) failed");
        return false;
    }

    using RtlGetVersionFunction = LONG (WINAPI*)(OSVERSIONINFOW*);
    const FARPROC symbol = ::GetProcAddress(ntdll, "RtlGetVersion");
    if (symbol == nullptr)
    {
        SetError(error, "RtlGetVersion is unavailable in ntdll.dll");
        return false;
    }
    RtlGetVersionFunction rtl_get_version = nullptr;
    static_assert(
        sizeof(rtl_get_version) == sizeof(symbol),
        "Windows function pointers must match FARPROC");
    // Copy the ABI-compatible pointer representation instead of casting
    // FARPROC, which is diagnosed as C4191 under the project's MSVC /WX build.
    std::memcpy(
        &rtl_get_version,
        &symbol,
        sizeof(rtl_get_version));

    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    const LONG status = rtl_get_version(&version);
    if (status < 0)
    {
        SetError(
            error,
            "RtlGetVersion failed with NTSTATUS " +
                std::to_string(static_cast<unsigned long>(status)));
        return false;
    }

    SYSTEM_INFO system = {};
    ::GetNativeSystemInfo(&system);
    output.major_version = version.dwMajorVersion;
    output.minor_version = version.dwMinorVersion;
    output.build_number = version.dwBuildNumber;
    output.processor_architecture = system.wProcessorArchitecture;
    return true;
}
#endif

} // namespace

bool BuildWindowsPlatformIdentifier(
    const WindowsRuntimeVersion& version,
    std::string& platform_id,
    std::string* error)
{
    if (version.major_version == 0U || version.build_number == 0U)
    {
        SetError(error, "Windows runtime version is incomplete");
        return false;
    }

    platform_id = CanonicalIdentifier(
        "platform/",
        "windows-" +
            std::to_string(version.major_version) + "-" +
            std::to_string(version.minor_version) + "-" +
            std::to_string(version.build_number) + "-arch-" +
            std::to_string(version.processor_architecture));
    if (!IsCanonicalWorldModelIdentifier(platform_id))
    {
        SetError(error, "runtime platform identity is not canonical");
        return false;
    }
    return true;
}

bool InspectRuntimePlatformIdentifier(
    std::string& platform_id,
    std::string* error)
{
#if defined(_WIN32)
    WindowsRuntimeVersion version;
    if (!InspectWindowsRuntimeVersion(version, error))
        return false;
    return BuildWindowsPlatformIdentifier(version, platform_id, error);
#else
    struct utsname identity = {};
    if (uname(&identity) != 0)
    {
        SetError(error, "uname failed");
        return false;
    }
    platform_id = CanonicalIdentifier(
        "platform/",
        std::string(identity.sysname) + "-" +
            identity.release + "-" + identity.machine);
    if (!IsCanonicalWorldModelIdentifier(platform_id))
    {
        SetError(error, "runtime platform identity is not canonical");
        return false;
    }
    return true;
#endif
}

} // namespace WorldModel
} // namespace RoR
