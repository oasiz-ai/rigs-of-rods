/*
    This source file is part of Rigs of Rods

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

/// @file BeamNGOgreScriptPolicy.cpp

#include "BeamNGOgreScriptPolicy.h"

#include <cstddef>
#include <cstring>

namespace RoR {
namespace BeamNG {

namespace {

bool HasAsciiCaseInsensitiveSuffix(
    const std::string& path,
    const char* suffix)
{
    const std::size_t suffix_size = std::strlen(suffix);
    if (suffix_size == 0U || path.size() < suffix_size)
    {
        return false;
    }
    const std::size_t offset = path.size() - suffix_size;
    for (std::size_t index = 0U; index < suffix_size; ++index)
    {
        const unsigned char left = static_cast<unsigned char>(
            path[offset + index]);
        const unsigned char right = static_cast<unsigned char>(suffix[index]);
        const unsigned char normalized_left = left >= 'A' && left <= 'Z'
            ? static_cast<unsigned char>(left - 'A' + 'a')
            : left;
        const unsigned char normalized_right = right >= 'A' && right <= 'Z'
            ? static_cast<unsigned char>(right - 'A' + 'a')
            : right;
        if (normalized_left != normalized_right)
        {
            return false;
        }
    }
    return true;
}

} // namespace

bool HasUnsafeOgreScriptSuffix(const std::string& normalized_path)
{
    // Ogre/Ogre-Next compiler, HLMS, overlay, and font loaders, the optional
    // BSP/Cg plugin loaders, and RoR's SoundScriptManager all parse these
    // patterns during resource-group initialization. Imported archives cannot
    // supply any of them, even when a plugin is disabled in the current build.
    static const char* const SUFFIXES[] = {
        ".material",
        ".material.json",
        ".program",
        ".compositor",
        ".particle",
        ".overlay",
        ".fontdef",
        ".os",
        ".soundscript",
        ".shader",
        ".cgfx"
    };
    for (const char* suffix : SUFFIXES)
    {
        if (HasAsciiCaseInsensitiveSuffix(normalized_path, suffix))
        {
            return true;
        }
    }
    return false;
}

} // namespace BeamNG
} // namespace RoR
