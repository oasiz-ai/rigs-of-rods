/*
    This source file is part of Rigs of Rods
    Copyright 2005-2026 Rigs of Rods contributors

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "WindowsRuntimePath.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using RoR::PlatformUtilsDetail::NormalizeWindowsExtendedPathForRuntime;

int Fail(
    const std::string& label,
    const std::string& actual,
    const std::string& expected)
{
    std::cerr << "Windows runtime-path test failed for " << label
              << ": expected '" << expected << "', got '" << actual
              << "'\n";
    return EXIT_FAILURE;
}

bool ExpectEqual(
    const std::string& label,
    const std::string& actual,
    const std::string& expected)
{
    if (actual == expected)
    {
        return true;
    }
    Fail(label, actual, expected);
    return false;
}

} // namespace

int main()
{
    if (!ExpectEqual(
            "extended drive path",
            NormalizeWindowsExtendedPathForRuntime(
                "\\\\?\\D:\\Games\\Rigs of Rods\\RoR-Ogre14.exe"),
            "D:\\Games\\Rigs of Rods\\RoR-Ogre14.exe") ||
        !ExpectEqual(
            "lowercase drive path",
            NormalizeWindowsExtendedPathForRuntime(
                "\\\\?\\d:\\RoR\\RoR-Ogre14.exe"),
            "d:\\RoR\\RoR-Ogre14.exe") ||
        !ExpectEqual(
            "extended UNC path",
            NormalizeWindowsExtendedPathForRuntime(
                "\\\\?\\UNC\\server\\share\\RoR\\RoR-Ogre14.exe"),
            "\\\\server\\share\\RoR\\RoR-Ogre14.exe") ||
        !ExpectEqual(
            "ordinary drive path",
            NormalizeWindowsExtendedPathForRuntime(
                "C:\\RoR\\RoR-Ogre14.exe"),
            "C:\\RoR\\RoR-Ogre14.exe") ||
        !ExpectEqual(
            "ordinary UNC path",
            NormalizeWindowsExtendedPathForRuntime(
                "\\\\server\\share\\RoR-Ogre14.exe"),
            "\\\\server\\share\\RoR-Ogre14.exe") ||
        !ExpectEqual(
            "device namespace",
            NormalizeWindowsExtendedPathForRuntime(
                "\\\\.\\PhysicalDrive0"),
            "\\\\.\\PhysicalDrive0") ||
        !ExpectEqual(
            "volume namespace",
            NormalizeWindowsExtendedPathForRuntime(
                "\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\RoR.exe"),
            "\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\RoR.exe") ||
        !ExpectEqual(
            "malformed drive namespace",
            NormalizeWindowsExtendedPathForRuntime("\\\\?\\D:/RoR.exe"),
            "\\\\?\\D:/RoR.exe") ||
        !ExpectEqual(
            "empty path",
            NormalizeWindowsExtendedPathForRuntime(""),
            ""))
    {
        return EXIT_FAILURE;
    }

    std::cout << "Windows runtime paths normalize deterministically\n";
    return EXIT_SUCCESS;
}
