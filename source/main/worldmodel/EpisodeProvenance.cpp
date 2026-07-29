/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "EpisodeProvenance.h"

#include "WorldModelTelemetry.h"

#include <limits>
#include <set>
#include <sstream>
#include <vector>

namespace {

bool Fail(std::string* error, const std::string& message)
{
    if (error != nullptr)
        *error = message;
    return false;
}

bool NonzeroSha256(const std::string& value)
{
    return RoR::WorldModel::IsCanonicalSha256(value) &&
        value.find_first_not_of('0') != std::string::npos;
}

bool CommitId(const std::string& value)
{
    if (value.size() != 40U && value.size() != 64U)
        return false;
    for (const char character : value)
    {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return value.find_first_not_of('0') != std::string::npos;
}

bool Field(
    const std::string& value,
    const char* name,
    std::string* error)
{
    return RoR::WorldModel::IsCanonicalWorldModelIdentifier(value) ||
        Fail(error, std::string(name) + " is not a canonical identifier");
}

bool HashField(
    const std::string& value,
    const char* name,
    std::string* error)
{
    return NonzeroSha256(value) ||
        Fail(error, std::string(name) + " is not a nonzero SHA-256");
}

std::vector<std::string> Lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
        lines.push_back(line);
    if (text.empty() || text.back() != '\n')
        lines.push_back("<missing-final-newline>");
    return lines;
}

bool ParseString(
    const std::string& line,
    const std::string& key,
    bool comma,
    std::string& output)
{
    const std::string prefix = "  \"" + key + "\": \"";
    const std::string suffix = comma ? "\"," : "\"";
    if (line.size() < prefix.size() + suffix.size() ||
        line.compare(0U, prefix.size(), prefix) != 0 ||
        line.compare(
            line.size() - suffix.size(),
            suffix.size(),
            suffix) != 0)
    {
        return false;
    }
    output = line.substr(
        prefix.size(),
        line.size() - prefix.size() - suffix.size());
    return true;
}

bool ParseU64(
    const std::string& line,
    const std::string& key,
    std::uint64_t& output)
{
    const std::string prefix = "  \"" + key + "\": ";
    if (line.size() <= prefix.size() ||
        line.compare(0U, prefix.size(), prefix) != 0 ||
        line.back() != ',')
    {
        return false;
    }
    const std::string digits = line.substr(
        prefix.size(),
        line.size() - prefix.size() - 1U);
    if (digits.empty() ||
        (digits.size() != 1U && digits.front() == '0'))
    {
        return false;
    }
    std::uint64_t value = 0U;
    for (const char digit : digits)
    {
        if (digit < '0' || digit > '9')
            return false;
        const std::uint64_t unit =
            static_cast<std::uint64_t>(digit - '0');
        if (value >
            (std::numeric_limits<std::uint64_t>::max() - unit) / 10U)
        {
            return false;
        }
        value = value * 10U + unit;
    }
    output = value;
    return true;
}

bool ParseStringArray(
    const std::string& line,
    const std::string& key,
    std::vector<std::string>& output)
{
    const std::string prefix = "  \"" + key + "\": [";
    const std::string suffix = "],";
    if (line.size() <= prefix.size() + suffix.size() ||
        line.compare(0U, prefix.size(), prefix) != 0 ||
        line.compare(
            line.size() - suffix.size(),
            suffix.size(),
            suffix) != 0)
    {
        return false;
    }
    const std::string body = line.substr(
        prefix.size(),
        line.size() - prefix.size() - suffix.size());
    if (body.size() < 2U ||
        body.front() != '"' ||
        body.back() != '"')
    {
        return false;
    }
    std::vector<std::string> values;
    std::size_t begin = 1U;
    while (begin <= body.size() - 1U)
    {
        const std::size_t separator = body.find("\",\"", begin);
        const std::size_t end =
            separator == std::string::npos
                ? body.size() - 1U
                : separator;
        if (end == begin)
            return false;
        values.push_back(body.substr(begin, end - begin));
        if (separator == std::string::npos)
            break;
        begin = separator + 3U;
    }
    output = values;
    return true;
}

} // namespace

namespace RoR {
namespace WorldModel {

namespace {

bool ValidateEpisodeProvenanceFields(
    const EpisodeProvenance& value,
    std::string* error)
{
    if (value.root_seed == 0U)
        return Fail(error, "root_seed must be nonzero");
    if (value.reset_seed == 0U)
        return Fail(error, "reset_seed must be nonzero");
    if (!CommitId(value.engine_commit))
        return Fail(error, "engine_commit must be a nonzero lowercase commit");
    if (!Field(value.engine_branch, "engine_branch", error) ||
        !Field(value.build_id, "build_id", error) ||
        !HashField(value.build_sha256, "build_sha256", error) ||
        !Field(value.os_id, "os_id", error) ||
        !Field(value.gpu_id, "gpu_id", error) ||
        !Field(value.driver_id, "driver_id", error) ||
        !HashField(value.config_sha256, "config_sha256", error) ||
        !Field(value.vehicle_id, "vehicle_id", error) ||
        !HashField(value.vehicle_sha256, "vehicle_sha256", error) ||
        !Field(value.terrain_id, "terrain_id", error) ||
        !HashField(value.terrain_sha256, "terrain_sha256", error) ||
        !Field(
            value.controller_profile_id,
            "controller_profile_id",
            error) ||
        !HashField(
            value.controller_profile_sha256,
            "controller_profile_sha256",
            error))
    {
        return false;
    }
    if (value.control_ids.empty())
        return Fail(error, "control_ids must not be empty");
    static const std::set<std::string> SUPPORTED_CONTROL_IDS = {
        "vehicle.brake",
        "vehicle.clutch",
        "vehicle.parking-brake",
        "vehicle.steering",
        "vehicle.throttle"};
    for (std::size_t index = 0U;
         index < value.control_ids.size();
         ++index)
    {
        const std::string& control = value.control_ids[index];
        if (SUPPORTED_CONTROL_IDS.count(control) == 0U)
        {
            return Fail(
                error,
                "control_ids contains an unsupported schema-1 control");
        }
        if (index != 0U &&
            value.control_ids[index - 1U] >= control)
        {
            return Fail(
                error,
                "control_ids must be sorted and unique");
        }
    }
    if (
        !Field(value.camera_profile_id, "camera_profile_id", error) ||
        !HashField(
            value.camera_profile_sha256,
            "camera_profile_sha256",
            error) ||
        !HashField(
            value.reset_state_sha256,
            "reset_state_sha256",
            error) ||
        !HashField(
            value.rights_manifest_sha256,
            "rights_manifest_sha256",
            error) ||
        !Field(value.data_source_id, "data_source_id", error) ||
        !Field(
            value.participant_release_id,
            "participant_release_id",
            error) ||
        !Field(value.allowed_use_id, "allowed_use_id", error))
    {
        return false;
    }
    if (value.matrix_order != "row-major")
        return Fail(error, "matrix_order must be row-major");
    if (value.coordinate_frame != "ror.world.rh-y-up")
    {
        return Fail(
            error,
            "coordinate_frame must be ror.world.rh-y-up");
    }
    if (value.color_space != "srgb")
        return Fail(error, "color_space must be srgb");
    if (value.pixel_format != "rgb8")
        return Fail(error, "pixel_format must be rgb8");
    return true;
}

} // namespace

bool ValidateEpisodeProvenance(
    const EpisodeProvenance& value,
    std::string* error)
{
    return ValidateEpisodeProvenanceFields(value, error);
}

bool SerializeEpisodeProvenance(
    const EpisodeProvenance& value,
    std::string& output,
    std::string* error)
{
    if (!ValidateEpisodeProvenanceFields(value, error))
        return false;
    std::ostringstream stream;
    stream
        << "{\n"
        << "  \"schema\": "
           "\"org.rigsofrods.worldmodel.provenance@1.0\",\n"
        << "  \"root_seed\": " << value.root_seed << ",\n"
        << "  \"reset_seed\": " << value.reset_seed << ",\n"
        << "  \"engine_commit\": \"" << value.engine_commit << "\",\n"
        << "  \"engine_branch\": \"" << value.engine_branch << "\",\n"
        << "  \"build_id\": \"" << value.build_id << "\",\n"
        << "  \"build_sha256\": \"" << value.build_sha256 << "\",\n"
        << "  \"os_id\": \"" << value.os_id << "\",\n"
        << "  \"gpu_id\": \"" << value.gpu_id << "\",\n"
        << "  \"driver_id\": \"" << value.driver_id << "\",\n"
        << "  \"config_sha256\": \"" << value.config_sha256 << "\",\n"
        << "  \"vehicle_id\": \"" << value.vehicle_id << "\",\n"
        << "  \"vehicle_sha256\": \"" << value.vehicle_sha256 << "\",\n"
        << "  \"terrain_id\": \"" << value.terrain_id << "\",\n"
        << "  \"terrain_sha256\": \"" << value.terrain_sha256 << "\",\n"
        << "  \"controller_profile_id\": \""
        << value.controller_profile_id << "\",\n"
        << "  \"controller_profile_sha256\": \""
        << value.controller_profile_sha256 << "\",\n"
        << "  \"control_ids\": [";
    for (std::size_t index = 0U;
         index < value.control_ids.size();
         ++index)
    {
        if (index != 0U)
            stream << ',';
        stream << '"' << value.control_ids[index] << '"';
    }
    stream
        << "],\n"
        << "  \"camera_profile_id\": \""
        << value.camera_profile_id << "\",\n"
        << "  \"camera_profile_sha256\": \""
        << value.camera_profile_sha256 << "\",\n"
        << "  \"reset_state_sha256\": \""
        << value.reset_state_sha256 << "\",\n"
        << "  \"rights_manifest_sha256\": \""
        << value.rights_manifest_sha256 << "\",\n"
        << "  \"data_source_id\": \"" << value.data_source_id << "\",\n"
        << "  \"participant_release_id\": \""
        << value.participant_release_id << "\",\n"
        << "  \"allowed_use_id\": \"" << value.allowed_use_id << "\",\n"
        << "  \"matrix_order\": \"" << value.matrix_order << "\",\n"
        << "  \"coordinate_frame\": \""
        << value.coordinate_frame << "\",\n"
        << "  \"color_space\": \"" << value.color_space << "\",\n"
        << "  \"pixel_format\": \"" << value.pixel_format << "\",\n"
        << "  \"physics_rate_hz\": 2000,\n"
        << "  \"observation_rate_hz\": 48\n"
        << "}\n";
    output = stream.str();
    return true;
}

bool ParseEpisodeProvenance(
    const std::string& text,
    EpisodeProvenance& output,
    std::string* error)
{
    const std::vector<std::string> lines = Lines(text);
    if (lines.size() != 33U ||
        lines[0] != "{" ||
        lines[1] !=
            "  \"schema\": "
            "\"org.rigsofrods.worldmodel.provenance@1.0\"," ||
        lines[30] != "  \"physics_rate_hz\": 2000," ||
        lines[31] != "  \"observation_rate_hz\": 48" ||
        lines[32] != "}")
    {
        return Fail(error, "provenance structure is not canonical schema 1");
    }
    EpisodeProvenance value;
    if (!ParseU64(lines[2], "root_seed", value.root_seed) ||
        !ParseU64(lines[3], "reset_seed", value.reset_seed) ||
        !ParseString(lines[4], "engine_commit", true, value.engine_commit) ||
        !ParseString(lines[5], "engine_branch", true, value.engine_branch) ||
        !ParseString(lines[6], "build_id", true, value.build_id) ||
        !ParseString(lines[7], "build_sha256", true, value.build_sha256) ||
        !ParseString(lines[8], "os_id", true, value.os_id) ||
        !ParseString(lines[9], "gpu_id", true, value.gpu_id) ||
        !ParseString(lines[10], "driver_id", true, value.driver_id) ||
        !ParseString(lines[11], "config_sha256", true, value.config_sha256) ||
        !ParseString(lines[12], "vehicle_id", true, value.vehicle_id) ||
        !ParseString(lines[13], "vehicle_sha256", true, value.vehicle_sha256) ||
        !ParseString(lines[14], "terrain_id", true, value.terrain_id) ||
        !ParseString(lines[15], "terrain_sha256", true, value.terrain_sha256) ||
        !ParseString(
            lines[16],
            "controller_profile_id",
            true,
            value.controller_profile_id) ||
        !ParseString(
            lines[17],
            "controller_profile_sha256",
            true,
            value.controller_profile_sha256) ||
        !ParseStringArray(
            lines[18],
            "control_ids",
            value.control_ids) ||
        !ParseString(
            lines[19],
            "camera_profile_id",
            true,
            value.camera_profile_id) ||
        !ParseString(
            lines[20],
            "camera_profile_sha256",
            true,
            value.camera_profile_sha256) ||
        !ParseString(
            lines[21],
            "reset_state_sha256",
            true,
            value.reset_state_sha256) ||
        !ParseString(
            lines[22],
            "rights_manifest_sha256",
            true,
            value.rights_manifest_sha256) ||
        !ParseString(
            lines[23],
            "data_source_id",
            true,
            value.data_source_id) ||
        !ParseString(
            lines[24],
            "participant_release_id",
            true,
            value.participant_release_id) ||
        !ParseString(
            lines[25],
            "allowed_use_id",
            true,
            value.allowed_use_id) ||
        !ParseString(lines[26], "matrix_order", true, value.matrix_order) ||
        !ParseString(
            lines[27],
            "coordinate_frame",
            true,
            value.coordinate_frame) ||
        !ParseString(lines[28], "color_space", true, value.color_space) ||
        !ParseString(lines[29], "pixel_format", true, value.pixel_format))
    {
        return Fail(error, "provenance field is invalid");
    }
    if (!ValidateEpisodeProvenanceFields(value, error))
        return false;
    std::string canonical;
    if (!SerializeEpisodeProvenance(value, canonical, error) ||
        canonical != text)
    {
        return Fail(error, "provenance encoding is not canonical");
    }
    output = value;
    return true;
}

} // namespace WorldModel
} // namespace RoR
