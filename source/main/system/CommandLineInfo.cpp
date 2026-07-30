/*
    This source file is part of Rigs of Rods
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CommandLineInfo.h"

namespace
{

bool WriteAll(
    std::FILE* output,
    const char* data,
    std::size_t size)
{
    return size == 0 || std::fwrite(data, 1, size, output) == size;
}

} // namespace

bool RoR::WriteCommandLineInfo(
    std::FILE* output,
    const std::string& title,
    const std::string& message)
{
    if (output == nullptr)
    {
        return false;
    }

    static const char prefix[] = "\n\n";
    static const char separator[] = ": ";
    static const char suffix[] = "\n\n";
    if (!WriteAll(output, prefix, sizeof(prefix) - 1) ||
        !WriteAll(output, title.data(), title.size()) ||
        !WriteAll(output, separator, sizeof(separator) - 1) ||
        !WriteAll(output, message.data(), message.size()) ||
        !WriteAll(output, suffix, sizeof(suffix) - 1))
    {
        return false;
    }
    return std::fflush(output) == 0;
}
