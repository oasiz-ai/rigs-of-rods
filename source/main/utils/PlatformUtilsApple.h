/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2020 Petr Ohlidal

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file PlatformUtilsApple.h
/// @brief Dependency-free Darwin helpers used by PlatformUtils.cpp.

#pragma once

#if !defined(__APPLE__)
    #error "PlatformUtilsApple.h is only available on Apple platforms"
#endif

#include <mach-o/dyld.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace RoR {
namespace PlatformUtilsDetail {

/// Return the canonical path of the running Mach-O executable.
///
/// Unlike Linux, Darwin does not expose /proc/self/exe. `_NSGetExecutablePath`
/// also reports the buffer size that it needs, so this works for app bundles
/// and paths longer than a fixed MAX_PATH-style allocation.
inline std::string GetAppleExecutablePath()
{
    uint32_t buffer_size = 1;

    for (;;)
    {
        std::vector<char> path_buffer(buffer_size);
        uint32_t required_size = buffer_size;

        if (_NSGetExecutablePath(path_buffer.data(), &required_size) == 0)
        {
            char* canonical_path = ::realpath(path_buffer.data(), nullptr);
            if (canonical_path != nullptr)
            {
                std::string result(canonical_path);
                std::free(canonical_path);
                return result;
            }

            // The dyld path is still more useful than an empty result when the
            // filesystem cannot canonicalize it (for example during teardown).
            return std::string(path_buffer.data());
        }

        if (required_size <= buffer_size)
        {
            return std::string();
        }
        buffer_size = required_size;
    }
}

} // namespace PlatformUtilsDetail
} // namespace RoR
