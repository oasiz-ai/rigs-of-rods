/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "DeterministicStateTraceCli.h"

#include "DeterministicStateTrace.h"

#include <fstream>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void AppendJsonString(std::ostringstream& stream, const std::string& value)
{
    static const char HEX[] = "0123456789abcdef";

    stream << '"';
    for (std::string::const_iterator iterator = value.begin();
         iterator != value.end();
         ++iterator)
    {
        const unsigned char byte = static_cast<unsigned char>(*iterator);
        switch (byte)
        {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (byte >= 0x20 && byte <= 0x7e)
            {
                stream << static_cast<char>(byte);
            }
            else
            {
                stream << "\\u00"
                       << HEX[(byte >> 4U) & 0xfU]
                       << HEX[byte & 0xfU];
            }
            break;
        }
    }
    stream << '"';
}

void WriteUsage(std::ostream& output, const char* program)
{
    output
        << "Usage: " << program
        << " [--allow-worker-count-difference] LEFT.trace RIGHT.trace\n"
        << "Exit 0: exact trace match; 1: valid divergence; "
        << "2: invalid input or invocation.\n";
}

void WriteOpenFailure(
    std::ostream& output,
    const char* side,
    const std::string& path)
{
    std::ostringstream report;
    report.imbue(std::locale::classic());
    report
        << "{\"format\":\"ror-d0-state-trace-comparison-v1\""
        << ",\"status\":\"invalid_input\""
        << ",\"difference\":";
    AppendJsonString(report, std::string(side) + "_open_failed");
    report << ",\"path\":";
    AppendJsonString(report, path);
    report << "}\n";
    output << report.str();
}

} // namespace

namespace RoR {
namespace DeterministicStateTrace {

int RunComparisonCli(
    int argc,
    const char* const* argv,
    std::ostream& output,
    std::ostream& error_output)
{
    const char* const program =
        argc > 0 && argv != nullptr && argv[0] != nullptr
        ? argv[0]
        : "ror-state-trace";
    if (argv == nullptr || argc < 1)
    {
        WriteUsage(error_output, program);
        return CLI_EXIT_INVALID;
    }

    ComparisonOptions options;
    bool option_seen = false;
    bool positional_only = false;
    std::vector<std::string> paths;
    for (int index = 1; index < argc; ++index)
    {
        if (argv[index] == nullptr)
        {
            error_output << "Null command-line argument\n";
            WriteUsage(error_output, program);
            return CLI_EXIT_INVALID;
        }
        const std::string argument(argv[index]);
        if (!positional_only && argument == "--help")
        {
            if (argc != 2)
            {
                error_output << "--help cannot be combined with inputs\n";
                WriteUsage(error_output, program);
                return CLI_EXIT_INVALID;
            }
            WriteUsage(output, program);
            return CLI_EXIT_MATCH;
        }
        if (!positional_only && argument == "--")
        {
            positional_only = true;
            continue;
        }
        if (!positional_only &&
            argument == "--allow-worker-count-difference")
        {
            if (option_seen)
            {
                error_output
                    << "Duplicate --allow-worker-count-difference option\n";
                WriteUsage(error_output, program);
                return CLI_EXIT_INVALID;
            }
            option_seen = true;
            options.allow_worker_count_difference = true;
            continue;
        }
        if (!positional_only &&
            !argument.empty() &&
            argument[0] == '-')
        {
            error_output << "Unknown option: " << argument << '\n';
            WriteUsage(error_output, program);
            return CLI_EXIT_INVALID;
        }
        paths.push_back(argument);
    }

    if (paths.size() != 2)
    {
        error_output << "Exactly two trace paths are required\n";
        WriteUsage(error_output, program);
        return CLI_EXIT_INVALID;
    }

    std::ifstream left(paths[0].c_str(), std::ios::in | std::ios::binary);
    if (!left.is_open())
    {
        WriteOpenFailure(output, "left", paths[0]);
        return CLI_EXIT_INVALID;
    }
    std::ifstream right(paths[1].c_str(), std::ios::in | std::ios::binary);
    if (!right.is_open())
    {
        WriteOpenFailure(output, "right", paths[1]);
        return CLI_EXIT_INVALID;
    }

    const ComparisonResult result = Compare(left, right, options);
    output << FormatComparisonJson(result, paths[0], paths[1]);
    if (result.status == ComparisonStatus::MATCH)
        return CLI_EXIT_MATCH;
    if (result.status == ComparisonStatus::DIVERGED)
        return CLI_EXIT_DIVERGED;
    return CLI_EXIT_INVALID;
}

} // namespace DeterministicStateTrace
} // namespace RoR
