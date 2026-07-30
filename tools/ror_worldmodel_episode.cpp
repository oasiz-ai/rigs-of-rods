/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "EpisodeValidator.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage(const char* executable)
{
    std::cerr << "Usage: " << executable
              << " verify-integrity <episode-directory>\n"
              << "\n"
              << "Checks crash-safe artifact framing, inventory, hashes, "
                 "and sequencing only.\n"
              << "It does not validate telemetry semantics or mark an "
                 "episode training-ready.\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3 || std::string(argv[1]) != "verify-integrity")
    {
        PrintUsage(argv[0]);
        return 2;
    }

    const RoR::WorldModel::EpisodeValidationResult result =
        RoR::WorldModel::EpisodeValidator::Validate(
            std::filesystem::path(argv[2]));
    if (!result.IsValid())
    {
        std::cerr << "integrity-reject: "
                  << RoR::WorldModel::EpisodeValidationErrorName(
                         result.error)
                  << "\nartifact: "
                  << result.artifact.generic_string()
                  << "\ndetail: "
                  << result.detail << '\n';
        return 1;
    }

    std::cout << "{\"verdict\":\"integrity-pass\","
              << "\"semantic_validation\":\"not-performed\","
              << "\"telemetry_record_count\":"
              << result.telemetry_record_count << ','
              << "\"rgb_record_count\":"
              << result.rgb_record_count << ','
              << "\"telemetry_chunk_count\":"
              << result.telemetry_chunk_count << ','
              << "\"rgb_chunk_count\":"
              << result.rgb_chunk_count << "}\n";
    return 0;
}
