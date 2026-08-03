/*
    This source file is part of Rigs of Rods
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CommandLineInfo.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "command-line info test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void TestExactBinaryOutput()
{
    std::FILE* stream = std::tmpfile();
    Require(stream != nullptr, "could not create a temporary stream");

    const std::string title("Command 100% Line");
    const std::string message("before\0after", 12);
    Require(
        RoR::WriteCommandLineInfo(stream, title, message),
        "valid stream rejected");
    Require(std::fseek(stream, 0, SEEK_END) == 0, "seek to end failed");
    const long size = std::ftell(stream);
    Require(size >= 0, "stream size unavailable");
    Require(std::fseek(stream, 0, SEEK_SET) == 0, "rewind failed");

    std::vector<char> actual(static_cast<std::size_t>(size));
    Require(
        actual.empty() ||
            std::fread(actual.data(), 1, actual.size(), stream) ==
                actual.size(),
        "could not read complete output");
    Require(std::fclose(stream) == 0, "temporary stream close failed");

    const std::string expected = "\n\n" + title + ": " + message + "\n\n";
    Require(
        actual == std::vector<char>(expected.begin(), expected.end()),
        "output bytes changed or an embedded NUL was truncated");
}

void TestNullStreamFailsClosed()
{
    Require(
        !RoR::WriteCommandLineInfo(nullptr, "title", "message"),
        "null output stream was accepted");
}

} // namespace

int main()
{
    TestExactBinaryOutput();
    TestNullStreamFailsClosed();
    std::cout << "command-line info tests passed\n";
    return EXIT_SUCCESS;
}
