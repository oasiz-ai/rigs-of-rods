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

/// @file BeamNGOgreScriptPolicy.h
/// @brief Dependency-free pre-mount policy for unsafe Ogre script members.

#pragma once

#include <string>

namespace RoR {
namespace BeamNG {

/// Returns true when an archive member path names an executable Ogre script.
/// The caller remains responsible for applying this policy only to regular
/// files from an authenticated, normalized archive index.
bool HasUnsafeOgreScriptSuffix(const std::string& normalized_path);

} // namespace BeamNG
} // namespace RoR
