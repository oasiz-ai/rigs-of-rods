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
        << "       " << program << " --inspect TRACE.trace\n"
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

void AppendMetadataJson(
    std::ostringstream& report,
    const RoR::DeterministicStateTrace::Metadata& metadata)
{
    report
        << "{\"state_digest_schema_version\":"
        << metadata.state_digest_schema_version
        << ",\"worker_count\":" << metadata.worker_count
        << ",\"scenario_id\":" << metadata.scenario_id
        << ",\"first_physics_step\":" << metadata.first_physics_step
        << ",\"physics_step_numerator\":"
        << metadata.physics_step_numerator
        << ",\"physics_step_denominator\":"
        << metadata.physics_step_denominator
        << ",\"physics_flags\":" << metadata.physics_flags
        << '}';
}

void AppendStepJson(
    std::ostringstream& report,
    const RoR::DeterministicStateTrace::StepRecord& step)
{
    report
        << "{\"physics_step\":" << step.physics_step
        << ",\"actor_count\":" << step.actor_count
        << ",\"contact_count\":" << step.contact_count
        << ",\"state_digest\":";
    AppendJsonString(report, step.digest.ToHex());
    report << '}';
}

void WriteInspectionFailure(
    std::ostream& output,
    const std::string& path,
    const char* code,
    const RoR::DeterministicStateTrace::Status* status)
{
    std::ostringstream report;
    report.imbue(std::locale::classic());
    report
        << "{\"format\":\"ror-d0-state-trace-inspection-v1\""
        << ",\"status\":\"invalid_input\""
        << ",\"path\":";
    AppendJsonString(report, path);
    report << ",\"code\":";
    AppendJsonString(report, code != nullptr ? code : "unknown");
    report << ",\"byte_offset\":";
    if (status != nullptr)
        report << status->byte_offset;
    else
        report << "null";
    report << ",\"step_index\":";
    if (status != nullptr)
        report << status->step_index;
    else
        report << "null";
    report << "}\n";
    output << report.str();
}

int InspectTrace(
    const std::string& path,
    std::ostream& output)
{
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.is_open())
    {
        WriteInspectionFailure(output, path, "open_failed", nullptr);
        return RoR::DeterministicStateTrace::CLI_EXIT_INVALID;
    }

    RoR::DeterministicStateTrace::Reader reader(input);
    if (!reader.IsReady())
    {
        const RoR::DeterministicStateTrace::Status& status =
            reader.GetStatus();
        WriteInspectionFailure(
            output,
            path,
            RoR::DeterministicStateTrace::ToString(status.error),
            &status);
        return RoR::DeterministicStateTrace::CLI_EXIT_INVALID;
    }

    RoR::DeterministicStateTrace::StepRecord final_step;
    bool has_final_step = false;
    for (;;)
    {
        RoR::DeterministicStateTrace::StepRecord candidate;
        const RoR::DeterministicStateTrace::ReadResult result =
            reader.ReadNext(candidate);
        if (result == RoR::DeterministicStateTrace::ReadResult::STEP)
        {
            final_step = candidate;
            has_final_step = true;
            continue;
        }
        if (result == RoR::DeterministicStateTrace::ReadResult::READ_ERROR)
        {
            const RoR::DeterministicStateTrace::Status& status =
                reader.GetStatus();
            WriteInspectionFailure(
                output,
                path,
                RoR::DeterministicStateTrace::ToString(status.error),
                &status);
            return RoR::DeterministicStateTrace::CLI_EXIT_INVALID;
        }
        break;
    }

    std::ostringstream report;
    report.imbue(std::locale::classic());
    report
        << "{\"format\":\"ror-d0-state-trace-inspection-v1\""
        << ",\"status\":\"valid\""
        << ",\"path\":";
    AppendJsonString(report, path);
    report << ",\"metadata\":";
    AppendMetadataJson(report, reader.GetMetadata());
    report
        << ",\"step_count\":" << reader.GetStepCount()
        << ",\"bytes_read\":" << reader.GetBytesRead()
        << ",\"has_final_step\":"
        << (has_final_step ? "true" : "false")
        << ",\"final_step\":";
    if (has_final_step)
        AppendStepJson(report, final_step);
    else
        report << "null";
    report << "}\n";
    output << report.str();
    return RoR::DeterministicStateTrace::CLI_EXIT_MATCH;
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
    bool inspect = false;
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
        if (!positional_only && argument == "--inspect")
        {
            if (inspect)
            {
                error_output << "Duplicate --inspect option\n";
                WriteUsage(error_output, program);
                return CLI_EXIT_INVALID;
            }
            inspect = true;
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

    if (inspect)
    {
        if (option_seen)
        {
            error_output
                << "--inspect cannot be combined with comparison options\n";
            WriteUsage(error_output, program);
            return CLI_EXIT_INVALID;
        }
        if (paths.size() != 1)
        {
            error_output << "--inspect requires exactly one trace path\n";
            WriteUsage(error_output, program);
            return CLI_EXIT_INVALID;
        }
        return InspectTrace(paths[0], output);
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
