/*
    This source file is part of Rigs of Rods
    Copyright 2005-2026 Rigs of Rods contributors

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file MacOSUserDirectoryLayout.h
/// @brief Dependency-free macOS user-directory selection.

#pragma once

#if !defined(__APPLE__)
    #error "MacOSUserDirectoryLayout.h is only available on Apple platforms"
#endif

#include <string>

namespace RoR {
namespace PlatformUtilsDetail {

enum class MacOSUserDirectoryMode
{
    APP_BUNDLE_STANDARD,
    APP_BUNDLE_LEGACY,
    DEVELOPMENT_PORTABLE,
    DEVELOPMENT_LEGACY
};

struct MacOSUserDirectoryState
{
    bool process_config_exists = false;
    bool application_support_exists = false;
    bool legacy_user_data_exists = false;
};

struct MacOSUserDirectoryLayout
{
    std::string user_dir;
    std::string cache_dir;
    std::string thumbnails_dir;
    std::string logs_dir;
    MacOSUserDirectoryMode mode = MacOSUserDirectoryMode::DEVELOPMENT_LEGACY;
};

inline bool MacOSPathEndsWith(
    const std::string& value,
    const std::string& suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string JoinMacOSPath(
    const std::string& directory,
    const std::string& child)
{
    if (directory.empty())
    {
        return child;
    }
    if (directory[directory.size() - 1] == '/')
    {
        return directory + child;
    }
    return directory + "/" + child;
}

/// Test the canonical directory containing an executable, not argv[0].
///
/// A real application bundle executable lives directly in
/// `<name>.app/Contents/MacOS`. Requiring the complete suffix avoids treating a
/// development directory that merely contains ".app" as a packaged build.
inline bool IsMacOSAppBundleProcessDirectory(const std::string& process_dir)
{
    const std::string macos_suffix = "/Contents/MacOS";
    if (!MacOSPathEndsWith(process_dir, macos_suffix))
    {
        return false;
    }

    const std::string bundle_root =
        process_dir.substr(0, process_dir.size() - macos_suffix.size());
    const std::string::size_type slash = bundle_root.find_last_of('/');
    const std::string bundle_name =
        slash == std::string::npos ? bundle_root : bundle_root.substr(slash + 1);
    return bundle_name.size() > 4 &&
        MacOSPathEndsWith(bundle_name, ".app");
}

/// Resolve all writable roots before logging or configuration loading starts.
///
/// Packaged apps never select a directory inside their signed bundle, even if
/// a stale `Contents/MacOS/config` directory exists. New installations use
/// Apple's conventional Library directories. Existing installations that have
/// only `~/RigsOfRods` continue using it so configurations and mods are not
/// silently lost; once the Application Support directory exists it wins
/// deterministically. Non-bundle builds retain the historical portable marker
/// and `~/RigsOfRods` development behavior.
inline MacOSUserDirectoryLayout ResolveMacOSUserDirectoryLayout(
    const std::string& process_dir,
    const std::string& user_home,
    const MacOSUserDirectoryState& state)
{
    MacOSUserDirectoryLayout layout;
    const std::string legacy_user_dir =
        JoinMacOSPath(user_home, "RigsOfRods");
    const std::string application_support_dir =
        JoinMacOSPath(
            JoinMacOSPath(user_home, "Library/Application Support"),
            "Rigs of Rods");

    if (IsMacOSAppBundleProcessDirectory(process_dir))
    {
        if (!state.application_support_exists &&
            state.legacy_user_data_exists)
        {
            layout.user_dir = legacy_user_dir;
            layout.mode = MacOSUserDirectoryMode::APP_BUNDLE_LEGACY;
        }
        else
        {
            layout.user_dir = application_support_dir;
            layout.mode = MacOSUserDirectoryMode::APP_BUNDLE_STANDARD;
        }

        layout.cache_dir =
            JoinMacOSPath(
                JoinMacOSPath(user_home, "Library/Caches"),
                "Rigs of Rods");
        layout.thumbnails_dir =
            JoinMacOSPath(layout.cache_dir, "thumbnails");
        layout.logs_dir =
            JoinMacOSPath(
                JoinMacOSPath(user_home, "Library/Logs"),
                "Rigs of Rods");
        return layout;
    }

    if (state.process_config_exists)
    {
        layout.user_dir = JoinMacOSPath(process_dir, "config");
        layout.mode = MacOSUserDirectoryMode::DEVELOPMENT_PORTABLE;
    }
    else
    {
        layout.user_dir = legacy_user_dir;
        layout.mode = MacOSUserDirectoryMode::DEVELOPMENT_LEGACY;
    }

    layout.cache_dir = JoinMacOSPath(layout.user_dir, "cache");
    layout.thumbnails_dir = JoinMacOSPath(layout.user_dir, "thumbnails");
    layout.logs_dir = JoinMacOSPath(layout.user_dir, "logs");
    return layout;
}

} // namespace PlatformUtilsDetail
} // namespace RoR
