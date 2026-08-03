/*
    This source file is part of Rigs of Rods
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstdio>
#include <string>

namespace RoR
{

/// Write UI-free command-line information to the caller-provided stream.
bool WriteCommandLineInfo(
    std::FILE* output,
    const std::string& title,
    const std::string& message);

} // namespace RoR
