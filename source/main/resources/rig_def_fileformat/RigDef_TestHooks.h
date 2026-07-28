/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief In-process RigDef integration hooks compiled only for test builds.

#pragma once

#include <string>

namespace RigDef {

int RunCalibratedBeamMaterialRoundTripIntegration(
    const std::string& fixture_path);

} // namespace RigDef
