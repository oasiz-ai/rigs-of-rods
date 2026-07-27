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

#include "PlatformUtilsApple.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int Fail(const std::string& message)
{
    std::cerr << "Apple executable-path test failed: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 1 || argv[0] == nullptr)
    {
        return Fail("the process has no argv[0]");
    }

    const std::string executable_path = RoR::PlatformUtilsDetail::GetAppleExecutablePath();
    if (executable_path.empty())
    {
        return Fail("the returned path is empty");
    }
    if (executable_path[0] != '/')
    {
        return Fail("the returned path is not absolute: " + executable_path);
    }
    if (executable_path.find('\0') != std::string::npos)
    {
        return Fail("the returned path contains an embedded NUL");
    }

    char* expected_path = ::realpath(argv[0], nullptr);
    if (expected_path == nullptr)
    {
        return Fail("realpath(argv[0]) failed");
    }
    const std::string expected(expected_path);
    std::free(expected_path);

    if (executable_path != expected)
    {
        return Fail("expected '" + expected + "', got '" + executable_path + "'");
    }

    std::cout << "Apple executable path: " << executable_path << '\n';
    return EXIT_SUCCESS;
}
