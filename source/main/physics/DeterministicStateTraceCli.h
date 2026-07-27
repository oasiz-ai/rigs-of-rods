/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free CLI seam for deterministic state-trace comparison.

#pragma once

#include <iosfwd>

namespace RoR {
namespace DeterministicStateTrace {

enum CliExitCode
{
    CLI_EXIT_MATCH = 0,
    CLI_EXIT_DIVERGED = 1,
    CLI_EXIT_INVALID = 2
};

int RunComparisonCli(
    int argc,
    const char* const* argv,
    std::ostream& output,
    std::ostream& error_output);

} // namespace DeterministicStateTrace
} // namespace RoR
