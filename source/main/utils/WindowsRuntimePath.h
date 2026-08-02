/*
    This source file is part of Rigs of Rods
    Copyright 2005-2026 Rigs of Rods contributors

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include <string>

namespace RoR {
namespace PlatformUtilsDetail {

/// Return the conventional Win32 spelling of a validated extended DOS path.
///
/// The renderer launcher intentionally uses the `\\?\` namespace while
/// resolving and starting its exact sibling child. `GetModuleFileNameW()`
/// preserves that spelling in the child. OGRE 14's FileSystemArchive then
/// appends resource names with `/`; extended-path syntax deliberately disables
/// Win32 slash conversion, so an existing file can no longer be opened.
///
/// The launcher accepts only extended drive or UNC paths. Keep every other
/// namespace untouched so this helper cannot reinterpret device paths.
inline std::string NormalizeWindowsExtendedPathForRuntime(
    const std::string& path)
{
    const std::string extended_unc_prefix = "\\\\?\\UNC\\";
    if (path.compare(0U, extended_unc_prefix.size(), extended_unc_prefix) == 0)
    {
        return "\\\\" + path.substr(extended_unc_prefix.size());
    }

    const std::string extended_prefix = "\\\\?\\";
    if (path.size() >= extended_prefix.size() + 3U &&
        path.compare(0U, extended_prefix.size(), extended_prefix) == 0)
    {
        const char drive = path[extended_prefix.size()];
        const bool is_ascii_drive =
            (drive >= 'A' && drive <= 'Z') ||
            (drive >= 'a' && drive <= 'z');
        if (is_ascii_drive &&
            path[extended_prefix.size() + 1U] == ':' &&
            path[extended_prefix.size() + 2U] == '\\')
        {
            return path.substr(extended_prefix.size());
        }
    }

    return path;
}

} // namespace PlatformUtilsDetail
} // namespace RoR
