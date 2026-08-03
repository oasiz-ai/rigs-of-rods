/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "WorldModelPlatformIdentity.h"
#include "WorldModelTelemetry.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Check(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    using namespace RoR::WorldModel;

    WindowsRuntimeVersion windows_11;
    windows_11.major_version = 10U;
    windows_11.minor_version = 0U;
    windows_11.build_number = 26100U;
    windows_11.processor_architecture = 9U;

    std::string platform_id;
    std::string error;
    const bool built_windows_identifier =
        BuildWindowsPlatformIdentifier(
            windows_11, platform_id, &error);
    Check(
        built_windows_identifier,
        error.c_str());
    Check(
        platform_id == "platform/windows-10-0-26100-arch-9",
        "Windows 11 runtime version was not preserved");
    Check(
        IsCanonicalWorldModelIdentifier(platform_id),
        "Windows platform identity is not canonical");

    WindowsRuntimeVersion incomplete;
    Check(
        !BuildWindowsPlatformIdentifier(
            incomplete, platform_id, &error),
        "incomplete Windows runtime version was accepted");

    const bool inspected_runtime =
        InspectRuntimePlatformIdentifier(platform_id, &error);
    Check(inspected_runtime, error.c_str());
    Check(
        IsCanonicalWorldModelIdentifier(platform_id),
        "inspected runtime platform identity is not canonical");
#if defined(_WIN32)
    Check(
        platform_id.rfind("platform/windows-", 0U) == 0U,
        "Windows runtime platform identity has the wrong prefix");
#endif

    std::cout << "World-model platform identity tests passed\n";
    return 0;
}
