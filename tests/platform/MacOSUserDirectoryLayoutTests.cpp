/*
    This source file is part of Rigs of Rods
    Copyright 2005-2026 Rigs of Rods contributors

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "MacOSUserDirectoryLayout.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using RoR::PlatformUtilsDetail::IsMacOSAppBundleProcessDirectory;
using RoR::PlatformUtilsDetail::MacOSUserDirectoryLayout;
using RoR::PlatformUtilsDetail::MacOSUserDirectoryMode;
using RoR::PlatformUtilsDetail::MacOSUserDirectoryState;
using RoR::PlatformUtilsDetail::ResolveMacOSUserDirectoryLayout;

int Fail(const std::string& message)
{
    std::cerr << "macOS user-directory layout test failed: "
              << message << '\n';
    return EXIT_FAILURE;
}

bool ExpectEqual(
    const std::string& actual,
    const std::string& expected,
    const std::string& label)
{
    if (actual == expected)
    {
        return true;
    }
    std::cerr << label << ": expected '" << expected
              << "', got '" << actual << "'\n";
    return false;
}

bool ExpectBundleStandardLayout(const MacOSUserDirectoryLayout& layout)
{
    return
        layout.mode == MacOSUserDirectoryMode::APP_BUNDLE_STANDARD &&
        ExpectEqual(
            layout.user_dir,
            "/Users/tester/Library/Application Support/Rigs of Rods",
            "standard user directory") &&
        ExpectEqual(
            layout.cache_dir,
            "/Users/tester/Library/Caches/Rigs of Rods",
            "standard cache directory") &&
        ExpectEqual(
            layout.thumbnails_dir,
            "/Users/tester/Library/Caches/Rigs of Rods/thumbnails",
            "standard thumbnails directory") &&
        ExpectEqual(
            layout.logs_dir,
            "/Users/tester/Library/Logs/Rigs of Rods",
            "standard logs directory");
}

} // namespace

int main()
{
    const std::string bundle_process_dir =
        "/Applications/RoR.app/Contents/MacOS";

    if (!IsMacOSAppBundleProcessDirectory(bundle_process_dir))
    {
        return Fail("a canonical .app executable directory was not detected");
    }
    if (!IsMacOSAppBundleProcessDirectory(
            "/Volumes/Build Output/Rigs of Rods.app/Contents/MacOS"))
    {
        return Fail("a bundle path containing spaces was not detected");
    }
    if (IsMacOSAppBundleProcessDirectory(
            "/tmp/RoR.app/Contents/MacOS/tools"))
    {
        return Fail("a nested bundle tool directory was misclassified");
    }
    if (IsMacOSAppBundleProcessDirectory("/tmp/RoR.app-build/Contents/MacOS"))
    {
        return Fail("a non-.app directory was misclassified");
    }

    MacOSUserDirectoryState fresh_bundle_state;
    const MacOSUserDirectoryLayout fresh_bundle =
        ResolveMacOSUserDirectoryLayout(
            bundle_process_dir,
            "/Users/tester",
            fresh_bundle_state);
    if (!ExpectBundleStandardLayout(fresh_bundle))
    {
        return EXIT_FAILURE;
    }

    MacOSUserDirectoryState stale_in_bundle_config;
    stale_in_bundle_config.process_config_exists = true;
    const MacOSUserDirectoryLayout safe_bundle =
        ResolveMacOSUserDirectoryLayout(
            bundle_process_dir,
            "/Users/tester",
            stale_in_bundle_config);
    if (!ExpectBundleStandardLayout(safe_bundle))
    {
        return Fail("an in-bundle config directory changed the writable roots");
    }

    MacOSUserDirectoryState legacy_only;
    legacy_only.legacy_user_data_exists = true;
    const MacOSUserDirectoryLayout migrated_bundle =
        ResolveMacOSUserDirectoryLayout(
            bundle_process_dir,
            "/Users/tester",
            legacy_only);
    if (migrated_bundle.mode !=
            MacOSUserDirectoryMode::APP_BUNDLE_LEGACY ||
        !ExpectEqual(
            migrated_bundle.user_dir,
            "/Users/tester/RigsOfRods",
            "legacy compatibility user directory") ||
        !ExpectEqual(
            migrated_bundle.cache_dir,
            "/Users/tester/Library/Caches/Rigs of Rods",
            "legacy compatibility cache directory") ||
        !ExpectEqual(
            migrated_bundle.logs_dir,
            "/Users/tester/Library/Logs/Rigs of Rods",
            "legacy compatibility logs directory"))
    {
        return EXIT_FAILURE;
    }

    MacOSUserDirectoryState both_layouts;
    both_layouts.application_support_exists = true;
    both_layouts.legacy_user_data_exists = true;
    if (!ExpectBundleStandardLayout(
            ResolveMacOSUserDirectoryLayout(
                bundle_process_dir,
                "/Users/tester/",
                both_layouts)))
    {
        return Fail("Application Support did not win over the legacy directory");
    }

    MacOSUserDirectoryState portable_state;
    portable_state.process_config_exists = true;
    const MacOSUserDirectoryLayout portable =
        ResolveMacOSUserDirectoryLayout(
            "/Users/tester/Development/ror/bin",
            "/Users/tester",
            portable_state);
    if (portable.mode !=
            MacOSUserDirectoryMode::DEVELOPMENT_PORTABLE ||
        !ExpectEqual(
            portable.user_dir,
            "/Users/tester/Development/ror/bin/config",
            "portable user directory") ||
        !ExpectEqual(
            portable.cache_dir,
            "/Users/tester/Development/ror/bin/config/cache",
            "portable cache directory") ||
        !ExpectEqual(
            portable.logs_dir,
            "/Users/tester/Development/ror/bin/config/logs",
            "portable logs directory"))
    {
        return EXIT_FAILURE;
    }

    const MacOSUserDirectoryLayout development =
        ResolveMacOSUserDirectoryLayout(
            "/Users/tester/Development/ror/bin",
            "/Users/tester",
            MacOSUserDirectoryState());
    if (development.mode !=
            MacOSUserDirectoryMode::DEVELOPMENT_LEGACY ||
        !ExpectEqual(
            development.user_dir,
            "/Users/tester/RigsOfRods",
            "development user directory") ||
        !ExpectEqual(
            development.thumbnails_dir,
            "/Users/tester/RigsOfRods/thumbnails",
            "development thumbnails directory"))
    {
        return EXIT_FAILURE;
    }

    std::cout << "macOS user-directory layouts are deterministic\n";
    return EXIT_SUCCESS;
}
