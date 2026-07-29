/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "WorldModelTelemetry.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <system_error>

namespace {

using RoR::WorldModel::CameraTelemetry;
using RoR::WorldModel::ContactSummary;
using RoR::WorldModel::ControlSample;
using RoR::WorldModel::EngineTelemetry;
using RoR::WorldModel::EventRecord;
using RoR::WorldModel::NamedScalar;
using RoR::WorldModel::ObservationId;
using RoR::WorldModel::OutcomeRecord;
using RoR::WorldModel::QuaternionWxyz;
using RoR::WorldModel::RgbDescriptor;
using RoR::WorldModel::Vector3;
using RoR::WorldModel::VehicleTelemetry;
using RoR::WorldModel::WorldTelemetry;

bool Fail(std::string* error, const std::string& message)
{
    if (error != nullptr)
        *error = message;
    return false;
}

bool IsFiniteCanonical(double value)
{
    return std::isfinite(value) &&
        !(value == 0.0 && std::signbit(value));
}

bool Finite(double value, const char* name, std::string* error)
{
    return IsFiniteCanonical(value) ||
        Fail(error, std::string(name) + " must be finite and not negative zero");
}

bool Nonnegative(double value, const char* name, std::string* error)
{
    return Finite(value, name, error) &&
        (value >= 0.0 ||
         Fail(error, std::string(name) + " must be nonnegative"));
}

bool UnitInterval(double value, const char* name, std::string* error)
{
    return Finite(value, name, error) &&
        ((value >= 0.0 && value <= 1.0) ||
         Fail(error, std::string(name) + " must be in [0,1]"));
}

bool ValidVector(const Vector3& value, const char* name, std::string* error)
{
    return Finite(value.x, (std::string(name) + ".x").c_str(), error) &&
        Finite(value.y, (std::string(name) + ".y").c_str(), error) &&
        Finite(value.z, (std::string(name) + ".z").c_str(), error);
}

bool ValidQuaternion(
    const QuaternionWxyz& value,
    const char* name,
    std::string* error)
{
    if (!Finite(value.w, (std::string(name) + ".w").c_str(), error) ||
        !Finite(value.x, (std::string(name) + ".x").c_str(), error) ||
        !Finite(value.y, (std::string(name) + ".y").c_str(), error) ||
        !Finite(value.z, (std::string(name) + ".z").c_str(), error))
    {
        return false;
    }
    const double norm_squared = value.w * value.w + value.x * value.x +
        value.y * value.y + value.z * value.z;
    return std::abs(norm_squared - 1.0) <= 1e-9 ||
        Fail(error, std::string(name) + " must be a unit quaternion");
}

template <std::size_t Size>
bool ValidMatrix(
    const std::array<double, Size>& values,
    const char* name,
    std::string* error)
{
    for (std::size_t index = 0U; index < Size; ++index)
    {
        if (!IsFiniteCanonical(values[index]))
            return Fail(
                error,
                std::string(name) + "[" + std::to_string(index) +
                    "] must be finite and not negative zero");
    }
    return true;
}

bool ValidNamedScalars(
    const std::vector<NamedScalar>& values,
    const char* name,
    std::string* error)
{
    std::string previous;
    for (std::size_t index = 0U; index < values.size(); ++index)
    {
        const NamedScalar& value = values[index];
        if (!RoR::WorldModel::IsCanonicalWorldModelIdentifier(value.name))
            return Fail(error, std::string(name) + " contains a noncanonical name");
        if (index != 0U && previous >= value.name)
            return Fail(error, std::string(name) + " must be sorted and unique");
        if (!Nonnegative(value.value, name, error))
            return false;
        previous = value.name;
    }
    return true;
}

bool ValidVehicle(const VehicleTelemetry& value, std::string* error)
{
    return ValidVector(value.position_m, "vehicle.position_m", error) &&
        ValidQuaternion(
            value.orientation_world_from_vehicle,
            "vehicle.orientation_world_from_vehicle",
            error) &&
        ValidVector(
            value.linear_velocity_mps,
            "vehicle.linear_velocity_mps",
            error) &&
        ValidVector(
            value.angular_velocity_radps,
            "vehicle.angular_velocity_radps",
            error) &&
        Nonnegative(value.speed_mps, "vehicle.speed_mps", error) &&
        Finite(value.mass_kg, "vehicle.mass_kg", error) &&
        (value.mass_kg > 0.0 ||
         Fail(error, "vehicle.mass_kg must be positive"));
}

bool ValidEngine(const EngineTelemetry& value, std::string* error)
{
    return Nonnegative(value.rpm, "engine.rpm", error) &&
        Finite(value.torque_nm, "engine.torque_nm", error) &&
        UnitInterval(value.throttle, "engine.throttle", error) &&
        UnitInterval(value.clutch, "engine.clutch", error) &&
        RoR::WorldModel::IsCanonicalWorldModelIdentifier(value.mode) &&
        ValidNamedScalars(value.timers_seconds, "engine.timers_seconds", error);
}

bool ValidWorld(const WorldTelemetry& value, std::string* error)
{
    return RoR::WorldModel::IsCanonicalWorldModelIdentifier(value.world_id) &&
        RoR::WorldModel::IsCanonicalWorldModelIdentifier(value.terrain_id) &&
        RoR::WorldModel::IsCanonicalSha256(value.terrain_sha256) &&
        ValidVector(value.gravity_mps2, "world.gravity_mps2", error) &&
        Finite(value.water_level_m, "world.water_level_m", error) &&
        RoR::WorldModel::IsCanonicalWorldModelIdentifier(value.weather_id);
}

bool ValidCamera(const CameraTelemetry& value, std::string* error)
{
    static const double PI = 3.14159265358979323846;
    return RoR::WorldModel::IsCanonicalWorldModelIdentifier(value.camera_id) &&
        RoR::WorldModel::IsCanonicalWorldModelIdentifier(
            value.coordinate_frame) &&
        ValidVector(value.position_m, "camera.position_m", error) &&
        ValidQuaternion(
            value.orientation_world_from_camera,
            "camera.orientation_world_from_camera",
            error) &&
        ValidMatrix(value.view_matrix, "camera.view_matrix", error) &&
        ValidMatrix(value.projection_matrix, "camera.projection_matrix", error) &&
        ValidMatrix(value.intrinsics, "camera.intrinsics", error) &&
        Finite(value.vertical_fov_radians, "camera.vertical_fov_radians", error) &&
        ((value.vertical_fov_radians > 0.0 &&
          value.vertical_fov_radians < PI) ||
         Fail(error, "camera.vertical_fov_radians must be in (0,pi)")) &&
        Finite(value.near_clip_m, "camera.near_clip_m", error) &&
        (value.near_clip_m > 0.0 ||
         Fail(error, "camera.near_clip_m must be positive")) &&
        Finite(value.far_clip_m, "camera.far_clip_m", error) &&
        (value.far_clip_m > value.near_clip_m ||
         Fail(error, "camera.far_clip_m must exceed near_clip_m"));
}

bool ValidRgb(const RgbDescriptor& value, std::string* error)
{
    if (value.record_id == 0U || value.width == 0U || value.height == 0U)
        return Fail(error, "rgb record id and dimensions must be nonzero");
    if (value.pixel_format != "rgb8")
        return Fail(error, "rgb pixel_format must be rgb8");
    if (value.color_space != "srgb")
        return Fail(error, "rgb color_space must be srgb");
    if (value.row_origin != "top-left")
        return Fail(error, "rgb row_origin must be top-left");
    if (value.width > std::numeric_limits<std::uint32_t>::max() / 3U ||
        value.row_stride_bytes != value.width * 3U)
    {
        return Fail(error, "rgb row stride must equal width * 3 for RGB8");
    }
    return RoR::WorldModel::IsCanonicalSha256(value.raw_sha256) ||
        Fail(error, "rgb raw_sha256 must be 64 lowercase hexadecimal characters");
}

bool ValidContacts(const ContactSummary& value, std::string* error)
{
    return value.wheel_contact_count <= value.contact_count &&
        Nonnegative(
            value.maximum_normal_impulse_ns,
            "contacts.maximum_normal_impulse_ns",
            error) &&
        Nonnegative(
            value.maximum_penetration_m,
            "contacts.maximum_penetration_m",
            error);
}

bool ValidRational(
    const RoR::WorldModel::RationalTime& value,
    std::uint64_t expected_numerator,
    const char* name,
    std::string* error)
{
    if (value.numerator != expected_numerator ||
        value.denominator !=
            RoR::WorldModel::WORLD_MODEL_OBSERVATION_RATE_HZ)
    {
        return Fail(
            error,
            std::string(name) + " must be the exact observation index / 48");
    }
    return true;
}

class JsonWriter
{
public:
    void BeginObject()
    {
        Prefix();
        m_text.push_back('{');
        m_first.push_back(true);
    }

    void EndObject()
    {
        m_text.push_back('}');
        m_first.pop_back();
    }

    void BeginArray()
    {
        Prefix();
        m_text.push_back('[');
        m_first.push_back(true);
    }

    void EndArray()
    {
        m_text.push_back(']');
        m_first.pop_back();
    }

    void Key(const char* key)
    {
        Prefix();
        AppendString(key);
        m_text.push_back(':');
        m_after_key = true;
    }

    void String(const std::string& value)
    {
        Prefix();
        AppendString(value);
    }

    void UInt(std::uint64_t value)
    {
        Prefix();
        char buffer[32];
        const std::to_chars_result result =
            std::to_chars(buffer, buffer + sizeof(buffer), value);
        m_text.append(buffer, result.ptr);
    }

    void Int(std::int64_t value)
    {
        Prefix();
        char buffer[32];
        const std::to_chars_result result =
            std::to_chars(buffer, buffer + sizeof(buffer), value);
        m_text.append(buffer, result.ptr);
    }

    void Number(double value)
    {
        Prefix();
        // libc++ marks floating-point std::to_chars unavailable on the
        // project's macOS 11 deployment target. A classic-locale stream with
        // max_digits10 preserves an exact binary64 round trip without letting
        // a process locale introduce commas or other non-JSON punctuation.
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(
            std::numeric_limits<double>::max_digits10)
               << value;
        m_text.append(stream.str());
    }

    void Bool(bool value)
    {
        Prefix();
        m_text.append(value ? "true" : "false");
    }

    const std::string& Text() const { return m_text; }

private:
    void Prefix()
    {
        if (m_after_key)
        {
            m_after_key = false;
            return;
        }
        if (!m_first.empty())
        {
            if (!m_first.back())
                m_text.push_back(',');
            m_first.back() = false;
        }
    }

    void AppendString(const std::string& value)
    {
        static const char hex[] = "0123456789abcdef";
        m_text.push_back('"');
        for (unsigned char character : value)
        {
            switch (character)
            {
            case '"': m_text.append("\\\""); break;
            case '\\': m_text.append("\\\\"); break;
            case '\b': m_text.append("\\b"); break;
            case '\f': m_text.append("\\f"); break;
            case '\n': m_text.append("\\n"); break;
            case '\r': m_text.append("\\r"); break;
            case '\t': m_text.append("\\t"); break;
            default:
                if (character < 0x20U)
                {
                    m_text.append("\\u00");
                    m_text.push_back(hex[(character >> 4U) & 0xfU]);
                    m_text.push_back(hex[character & 0xfU]);
                }
                else
                {
                    m_text.push_back(static_cast<char>(character));
                }
            }
        }
        m_text.push_back('"');
    }

    std::string m_text;
    std::vector<bool> m_first;
    bool m_after_key = false;
};

void WriteVector(JsonWriter& writer, const Vector3& value)
{
    writer.BeginObject();
    writer.Key("x"); writer.Number(value.x);
    writer.Key("y"); writer.Number(value.y);
    writer.Key("z"); writer.Number(value.z);
    writer.EndObject();
}

void WriteQuaternion(JsonWriter& writer, const QuaternionWxyz& value)
{
    writer.BeginObject();
    writer.Key("w"); writer.Number(value.w);
    writer.Key("x"); writer.Number(value.x);
    writer.Key("y"); writer.Number(value.y);
    writer.Key("z"); writer.Number(value.z);
    writer.EndObject();
}

template <std::size_t Size>
void WriteMatrix(JsonWriter& writer, const std::array<double, Size>& values)
{
    writer.BeginArray();
    for (double value : values)
        writer.Number(value);
    writer.EndArray();
}

void WriteObservationId(JsonWriter& writer, const ObservationId& value)
{
    writer.BeginObject();
    writer.Key("index"); writer.UInt(value.observation_index);
    writer.Key("completed_physics_steps");
    writer.UInt(value.completed_physics_steps);
    writer.EndObject();
}

void WriteContacts(JsonWriter& writer, const ContactSummary& value)
{
    writer.BeginObject();
    writer.Key("contact_count"); writer.UInt(value.contact_count);
    writer.Key("wheel_contact_count"); writer.UInt(value.wheel_contact_count);
    writer.Key("maximum_normal_impulse_ns");
    writer.Number(value.maximum_normal_impulse_ns);
    writer.Key("maximum_penetration_m");
    writer.Number(value.maximum_penetration_m);
    writer.EndObject();
}

void WriteControlSamples(
    JsonWriter& writer,
    const std::vector<ControlSample>& samples)
{
    writer.BeginArray();
    for (const ControlSample& sample : samples)
    {
        writer.BeginObject();
        writer.Key("sample_id"); writer.String(sample.sample_id);
        writer.Key("control_id"); writer.String(sample.control_id);
        writer.Key("source_id"); writer.String(sample.source_id);
        writer.Key("source_tick"); writer.UInt(sample.source_tick);
        writer.Key("effective_tick"); writer.UInt(sample.effective_tick);
        writer.Key("value"); writer.Number(sample.value);
        writer.Key("parent_sample_ids");
        writer.BeginArray();
        for (const std::string& parent : sample.parent_sample_ids)
            writer.String(parent);
        writer.EndArray();
        writer.EndObject();
    }
    writer.EndArray();
}

bool SampleLess(const ControlSample& first, const ControlSample& second)
{
    if (first.effective_tick != second.effective_tick)
        return first.effective_tick < second.effective_tick;
    if (first.control_id != second.control_id)
        return first.control_id < second.control_id;
    return first.sample_id < second.sample_id;
}

bool ValidControlStage(
    const std::vector<ControlSample>& samples,
    RoR::WorldModel::ControlStage stage,
    std::uint64_t first_tick,
    std::uint64_t last_tick,
    const std::map<std::string, const ControlSample*>& allowed_parents,
    std::set<std::string>& all_ids,
    std::map<std::string, const ControlSample*>& stage_samples,
    std::string* error)
{
    for (std::size_t index = 0U; index < samples.size(); ++index)
    {
        const ControlSample& sample = samples[index];
        if (index != 0U && !SampleLess(samples[index - 1U], sample))
            return Fail(error, "control samples must be canonically sorted and unique");
        if (index != 0U &&
            samples[index - 1U].effective_tick == sample.effective_tick &&
            samples[index - 1U].control_id == sample.control_id)
        {
            return Fail(
                error,
                "control_id must be unique within an effective tick and stage");
        }
        if (!RoR::WorldModel::IsCanonicalWorldModelIdentifier(sample.sample_id) ||
            !RoR::WorldModel::IsCanonicalWorldModelIdentifier(sample.control_id) ||
            !RoR::WorldModel::IsCanonicalWorldModelIdentifier(sample.source_id))
        {
            return Fail(error, "control sample contains a noncanonical identifier");
        }
        if (!all_ids.insert(sample.sample_id).second)
            return Fail(error, "control sample_id must be globally unique");
        stage_samples.insert(std::make_pair(sample.sample_id, &sample));
        if (sample.effective_tick < first_tick ||
            sample.effective_tick >= last_tick)
        {
            return Fail(error, "control effective_tick is outside transition range");
        }
        if (sample.source_tick > sample.effective_tick)
            return Fail(error, "control source_tick exceeds effective_tick");
        if (!IsFiniteCanonical(sample.value))
            return Fail(error, "control value must be finite and not negative zero");

        std::string previous_parent;
        for (std::size_t parent_index = 0U;
             parent_index < sample.parent_sample_ids.size();
             ++parent_index)
        {
            const std::string& parent = sample.parent_sample_ids[parent_index];
            if (!RoR::WorldModel::IsCanonicalWorldModelIdentifier(parent))
                return Fail(error, "control parent contains a noncanonical identifier");
            if (parent_index != 0U && previous_parent >= parent)
                return Fail(error, "control parents must be sorted and unique");
            const auto parent_position = allowed_parents.find(parent);
            if (parent_position == allowed_parents.end())
                return Fail(error, "control parent does not join to the preceding stage");
            const ControlSample& parent_sample = *parent_position->second;
            if (parent_sample.control_id != sample.control_id ||
                parent_sample.effective_tick > sample.effective_tick ||
                parent_sample.source_tick > sample.source_tick)
            {
                return Fail(
                    error,
                    "control parent does not preserve control/tick lineage");
            }
            previous_parent = parent;
        }
        if (stage == RoR::WorldModel::ControlStage::RAW)
        {
            if (!sample.parent_sample_ids.empty())
                return Fail(error, "raw controls cannot have parents");
        }
        else if (sample.parent_sample_ids.empty())
        {
            return Fail(error, "non-raw controls require explicit ancestry");
        }
    }
    return true;
}

bool ValidEvents(
    const std::vector<EventRecord>& events,
    std::uint64_t first_tick,
    std::uint64_t last_tick,
    std::string* error)
{
    std::set<std::string> event_ids;
    for (std::size_t index = 0U; index < events.size(); ++index)
    {
        const EventRecord& event = events[index];
        if (!RoR::WorldModel::IsCanonicalWorldModelIdentifier(event.event_id) ||
            !RoR::WorldModel::IsCanonicalWorldModelIdentifier(event.event_type) ||
            !RoR::WorldModel::IsStrictUtf8(event.detail))
        {
            return Fail(error, "event contains invalid text or identifier");
        }
        if (!event_ids.insert(event.event_id).second)
            return Fail(error, "event_id must be unique within a transition");
        if (event.physics_tick < first_tick || event.physics_tick >= last_tick)
            return Fail(error, "event physics_tick is outside transition range");
        if (index != 0U)
        {
            const EventRecord& previous = events[index - 1U];
            if (previous.physics_tick > event.physics_tick ||
                (previous.physics_tick == event.physics_tick &&
                 previous.event_id >= event.event_id))
            {
                return Fail(error, "events must be canonically sorted and unique");
            }
        }
    }
    return true;
}

bool AppliedControlsCoverEveryTick(
    const std::vector<ControlSample>& samples,
    std::uint64_t first_tick,
    std::uint64_t last_tick,
    std::string* error)
{
    if (samples.empty() || first_tick >= last_tick)
        return Fail(
            error,
            "transition requires applied controls for every fixed step");

    std::uint64_t expected_tick = first_tick;
    for (std::size_t index = 0U; index < samples.size(); ++index)
    {
        if (samples[index].effective_tick != expected_tick)
        {
            return Fail(
                error,
                "applied controls do not cover every fixed-step boundary");
        }
        const bool last_for_tick =
            index + 1U == samples.size() ||
            samples[index + 1U].effective_tick != expected_tick;
        if (last_for_tick)
            ++expected_tick;
    }
    return expected_tick == last_tick ||
        Fail(
            error,
            "applied controls do not cover every fixed-step boundary");
}

bool ValidOutcome(const OutcomeRecord& outcome, std::string* error)
{
    return RoR::WorldModel::IsCanonicalWorldModelIdentifier(outcome.status) &&
        Finite(outcome.reward, "outcome.reward", error) &&
        RoR::WorldModel::IsStrictUtf8(outcome.detail);
}

std::string QualifiedSchemaName(RoR::WorldModel::SchemaKind kind)
{
    return std::string(RoR::WorldModel::SchemaName(kind)) + "@1.0";
}

} // namespace

namespace RoR {
namespace WorldModel {

bool IsStrictUtf8(const std::string& value)
{
    const unsigned char* bytes =
        reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0U;
    while (index < value.size())
    {
        const unsigned char first = bytes[index++];
        if (first <= 0x7fU)
            continue;
        std::uint32_t codepoint = 0U;
        std::size_t continuation = 0U;
        if (first >= 0xc2U && first <= 0xdfU)
        {
            codepoint = first & 0x1fU;
            continuation = 1U;
        }
        else if (first >= 0xe0U && first <= 0xefU)
        {
            codepoint = first & 0x0fU;
            continuation = 2U;
        }
        else if (first >= 0xf0U && first <= 0xf4U)
        {
            codepoint = first & 0x07U;
            continuation = 3U;
        }
        else
        {
            return false;
        }
        if (index + continuation > value.size())
            return false;
        for (std::size_t offset = 0U; offset < continuation; ++offset)
        {
            const unsigned char next = bytes[index++];
            if ((next & 0xc0U) != 0x80U)
                return false;
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((continuation == 1U && codepoint < 0x80U) ||
            (continuation == 2U && codepoint < 0x800U) ||
            (continuation == 3U && codepoint < 0x10000U) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) ||
            codepoint > 0x10ffffU)
        {
            return false;
        }
    }
    return true;
}

bool IsCanonicalWorldModelIdentifier(const std::string& value)
{
    if (value.empty() || value.size() > 128U)
        return false;
    bool previous_was_separator = false;
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        const char character = value[index];
        const bool alpha = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        const bool separator = character == '.' || character == '_' ||
            character == '-' || character == ':' || character == '/';
        if (!(alpha || digit || separator) ||
            (separator &&
             (index == 0U ||
              index + 1U == value.size() ||
              previous_was_separator)))
        {
            return false;
        }
        previous_was_separator = separator;
    }
    return true;
}

bool IsCanonicalSha256(const std::string& value)
{
    if (value.size() != 64U)
        return false;
    return std::all_of(
        value.begin(),
        value.end(),
        [](char character)
        {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f');
        });
}

bool ValidateObservationRecord(
    const ObservationRecord& record,
    std::string* error)
{
    if (!IsValidEpisodeId(record.episode_id) ||
        record.observation_id.episode != record.episode_id)
    {
        return Fail(error, "observation episode join is invalid");
    }
    if (record.frame_id != record.observation_id.observation_index)
        return Fail(error, "frame_id must equal observation index");
    if (!IsCanonicalWorldModelIdentifier(record.target_id))
        return Fail(error, "target_id is not canonical");
    if (!ValidRational(
            record.nominal_time,
            record.observation_id.observation_index,
            "nominal_time",
            error))
    {
        return false;
    }
    if (record.physics_steps.last_completed_step !=
            record.observation_id.completed_physics_steps ||
        record.physics_steps.first_completed_step >
            record.physics_steps.last_completed_step ||
        record.physics_steps.last_completed_step -
                record.physics_steps.first_completed_step !=
            record.physics_steps.substep_count)
    {
        return Fail(error, "observation physics step range is invalid");
    }
    if (record.observation_id.observation_index == 0U)
    {
        if (record.physics_steps.substep_count != 0U)
            return Fail(error, "baseline observation must have zero substeps");
    }
    else
    {
        TransitionId prior;
        prior.source.episode = record.episode_id;
        prior.source.observation_index =
            record.observation_id.observation_index - 1U;
        prior.source.completed_physics_steps =
            record.physics_steps.first_completed_step;
        prior.target = record.observation_id;
        if (!IsValidTransitionId(prior))
            return Fail(error, "observation boundary does not follow 41/42/42 cadence");
    }
    return ValidVehicle(record.vehicle, error) &&
        ValidEngine(record.engine, error) &&
        ValidWorld(record.world, error) &&
        ValidCamera(record.camera, error) &&
        ValidRgb(record.rgb, error) &&
        ValidContacts(record.contacts, error) &&
        (IsCanonicalSha256(record.state_sha256) ||
         Fail(error, "state_sha256 must be 64 lowercase hexadecimal characters"));
}

bool ValidateTransitionRecord(
    const TransitionRecord& record,
    std::string* error)
{
    if (!IsValidEpisodeId(record.episode_id) ||
        !IsValidTransitionId(record.transition_id) ||
        record.transition_id.source.episode != record.episode_id ||
        record.transition_index !=
            record.transition_id.source.observation_index)
    {
        return Fail(error, "transition identity join is invalid");
    }
    if (!IsCanonicalWorldModelIdentifier(record.target_id))
        return Fail(error, "target_id is not canonical");
    if (!ValidRational(
            record.source_time,
            record.transition_id.source.observation_index,
            "source_time",
            error) ||
        !ValidRational(
            record.target_time,
            record.transition_id.target.observation_index,
            "target_time",
            error))
    {
        return false;
    }
    if (record.effective_steps.first_completed_step !=
            record.transition_id.source.completed_physics_steps ||
        record.effective_steps.last_completed_step !=
            record.transition_id.target.completed_physics_steps ||
        record.effective_steps.last_completed_step -
                record.effective_steps.first_completed_step !=
            record.effective_steps.substep_count)
    {
        return Fail(error, "transition effective step range is invalid");
    }

    std::set<std::string> all_ids;
    std::map<std::string, const ControlSample*> raw_samples;
    std::map<std::string, const ControlSample*> issued_samples;
    std::map<std::string, const ControlSample*> resolved_samples;
    std::map<std::string, const ControlSample*> applied_samples;
    const std::map<std::string, const ControlSample*> none;
    if (!ValidControlStage(
            record.controls.raw,
            ControlStage::RAW,
            record.effective_steps.first_completed_step,
            record.effective_steps.last_completed_step,
            none,
            all_ids,
            raw_samples,
            error) ||
        !ValidControlStage(
            record.controls.issued,
            ControlStage::ISSUED,
            record.effective_steps.first_completed_step,
            record.effective_steps.last_completed_step,
            raw_samples,
            all_ids,
            issued_samples,
            error) ||
        !ValidControlStage(
            record.controls.resolved,
            ControlStage::RESOLVED,
            record.effective_steps.first_completed_step,
            record.effective_steps.last_completed_step,
            issued_samples,
            all_ids,
            resolved_samples,
            error) ||
        !ValidControlStage(
            record.controls.applied,
            ControlStage::APPLIED,
            record.effective_steps.first_completed_step,
            record.effective_steps.last_completed_step,
            resolved_samples,
            all_ids,
            applied_samples,
            error))
    {
        return false;
    }
    if (!AppliedControlsCoverEveryTick(
            record.controls.applied,
            record.effective_steps.first_completed_step,
            record.effective_steps.last_completed_step,
            error))
    {
        return false;
    }
    return ValidContacts(record.contacts, error) &&
        ValidEvents(
            record.events,
            record.effective_steps.first_completed_step,
            record.effective_steps.last_completed_step,
            error) &&
        ValidOutcome(record.outcome, error);
}

bool SerializeObservationRecord(
    const ObservationRecord& record,
    std::string& output,
    std::string* error)
{
    if (!ValidateObservationRecord(record, error))
        return false;
    std::string episode;
    if (!FormatEpisodeId(record.episode_id, episode))
        return Fail(error, "episode_id could not be formatted");

    JsonWriter writer;
    writer.BeginObject();
    writer.Key("schema");
    writer.String(QualifiedSchemaName(SchemaKind::OBSERVATION));
    writer.Key("episode_id"); writer.String(episode);
    writer.Key("observation_id"); WriteObservationId(writer, record.observation_id);
    writer.Key("frame_id"); writer.UInt(record.frame_id);
    writer.Key("target_id"); writer.String(record.target_id);
    writer.Key("nominal_time");
    writer.BeginObject();
    writer.Key("numerator"); writer.UInt(record.nominal_time.numerator);
    writer.Key("denominator"); writer.UInt(record.nominal_time.denominator);
    writer.EndObject();
    writer.Key("physics_steps");
    writer.BeginObject();
    writer.Key("first_completed_step");
    writer.UInt(record.physics_steps.first_completed_step);
    writer.Key("last_completed_step");
    writer.UInt(record.physics_steps.last_completed_step);
    writer.Key("substep_count"); writer.UInt(record.physics_steps.substep_count);
    writer.EndObject();
    writer.Key("vehicle");
    writer.BeginObject();
    writer.Key("position_m"); WriteVector(writer, record.vehicle.position_m);
    writer.Key("orientation_world_from_vehicle_wxyz");
    WriteQuaternion(writer, record.vehicle.orientation_world_from_vehicle);
    writer.Key("linear_velocity_mps");
    WriteVector(writer, record.vehicle.linear_velocity_mps);
    writer.Key("angular_velocity_radps");
    WriteVector(writer, record.vehicle.angular_velocity_radps);
    writer.Key("speed_mps"); writer.Number(record.vehicle.speed_mps);
    writer.Key("mass_kg"); writer.Number(record.vehicle.mass_kg);
    writer.EndObject();
    writer.Key("engine");
    writer.BeginObject();
    writer.Key("running"); writer.Bool(record.engine.running);
    writer.Key("contact"); writer.Bool(record.engine.contact);
    writer.Key("starter"); writer.Bool(record.engine.starter);
    writer.Key("rpm"); writer.Number(record.engine.rpm);
    writer.Key("torque_nm"); writer.Number(record.engine.torque_nm);
    writer.Key("throttle"); writer.Number(record.engine.throttle);
    writer.Key("clutch"); writer.Number(record.engine.clutch);
    writer.Key("gear"); writer.Int(record.engine.gear);
    writer.Key("gear_range"); writer.Int(record.engine.gear_range);
    writer.Key("mode"); writer.String(record.engine.mode);
    writer.Key("timers_seconds");
    writer.BeginObject();
    for (const NamedScalar& timer : record.engine.timers_seconds)
    {
        writer.Key(timer.name.c_str());
        writer.Number(timer.value);
    }
    writer.EndObject();
    writer.EndObject();
    writer.Key("world");
    writer.BeginObject();
    writer.Key("world_id"); writer.String(record.world.world_id);
    writer.Key("terrain_id"); writer.String(record.world.terrain_id);
    writer.Key("terrain_sha256"); writer.String(record.world.terrain_sha256);
    writer.Key("gravity_mps2"); WriteVector(writer, record.world.gravity_mps2);
    writer.Key("water_enabled"); writer.Bool(record.world.water_enabled);
    writer.Key("water_level_m"); writer.Number(record.world.water_level_m);
    writer.Key("weather_id"); writer.String(record.world.weather_id);
    writer.EndObject();
    writer.Key("camera");
    writer.BeginObject();
    writer.Key("camera_id"); writer.String(record.camera.camera_id);
    writer.Key("coordinate_frame"); writer.String(record.camera.coordinate_frame);
    writer.Key("position_m"); WriteVector(writer, record.camera.position_m);
    writer.Key("orientation_world_from_camera_wxyz");
    WriteQuaternion(writer, record.camera.orientation_world_from_camera);
    writer.Key("view_matrix"); WriteMatrix(writer, record.camera.view_matrix);
    writer.Key("projection_matrix");
    WriteMatrix(writer, record.camera.projection_matrix);
    writer.Key("intrinsics"); WriteMatrix(writer, record.camera.intrinsics);
    writer.Key("vertical_fov_radians");
    writer.Number(record.camera.vertical_fov_radians);
    writer.Key("near_clip_m"); writer.Number(record.camera.near_clip_m);
    writer.Key("far_clip_m"); writer.Number(record.camera.far_clip_m);
    writer.EndObject();
    writer.Key("rgb");
    writer.BeginObject();
    writer.Key("record_id"); writer.UInt(record.rgb.record_id);
    writer.Key("pixel_format"); writer.String(record.rgb.pixel_format);
    writer.Key("color_space"); writer.String(record.rgb.color_space);
    writer.Key("row_origin"); writer.String(record.rgb.row_origin);
    writer.Key("width"); writer.UInt(record.rgb.width);
    writer.Key("height"); writer.UInt(record.rgb.height);
    writer.Key("row_stride_bytes"); writer.UInt(record.rgb.row_stride_bytes);
    writer.Key("raw_sha256"); writer.String(record.rgb.raw_sha256);
    writer.EndObject();
    writer.Key("contacts"); WriteContacts(writer, record.contacts);
    writer.Key("state_sha256"); writer.String(record.state_sha256);
    writer.EndObject();
    output = writer.Text();
    return true;
}

bool SerializeTransitionRecord(
    const TransitionRecord& record,
    std::string& output,
    std::string* error)
{
    if (!ValidateTransitionRecord(record, error))
        return false;
    std::string episode;
    if (!FormatEpisodeId(record.episode_id, episode))
        return Fail(error, "episode_id could not be formatted");

    JsonWriter writer;
    writer.BeginObject();
    writer.Key("schema");
    writer.String(QualifiedSchemaName(SchemaKind::TRANSITION));
    writer.Key("episode_id"); writer.String(episode);
    writer.Key("transition_id");
    writer.BeginObject();
    writer.Key("index"); writer.UInt(record.transition_index);
    writer.Key("source"); WriteObservationId(writer, record.transition_id.source);
    writer.Key("target"); WriteObservationId(writer, record.transition_id.target);
    writer.EndObject();
    writer.Key("target_id"); writer.String(record.target_id);
    writer.Key("source_time");
    writer.BeginObject();
    writer.Key("numerator"); writer.UInt(record.source_time.numerator);
    writer.Key("denominator"); writer.UInt(record.source_time.denominator);
    writer.EndObject();
    writer.Key("target_time");
    writer.BeginObject();
    writer.Key("numerator"); writer.UInt(record.target_time.numerator);
    writer.Key("denominator"); writer.UInt(record.target_time.denominator);
    writer.EndObject();
    writer.Key("effective_steps");
    writer.BeginObject();
    writer.Key("first_completed_step");
    writer.UInt(record.effective_steps.first_completed_step);
    writer.Key("last_completed_step");
    writer.UInt(record.effective_steps.last_completed_step);
    writer.Key("substep_count");
    writer.UInt(record.effective_steps.substep_count);
    writer.EndObject();
    writer.Key("controls");
    writer.BeginObject();
    writer.Key("raw"); WriteControlSamples(writer, record.controls.raw);
    writer.Key("issued"); WriteControlSamples(writer, record.controls.issued);
    writer.Key("resolved"); WriteControlSamples(writer, record.controls.resolved);
    writer.Key("applied"); WriteControlSamples(writer, record.controls.applied);
    writer.EndObject();
    writer.Key("contacts"); WriteContacts(writer, record.contacts);
    writer.Key("events");
    writer.BeginArray();
    for (const EventRecord& event : record.events)
    {
        writer.BeginObject();
        writer.Key("event_id"); writer.String(event.event_id);
        writer.Key("event_type"); writer.String(event.event_type);
        writer.Key("physics_tick"); writer.UInt(event.physics_tick);
        writer.Key("detail"); writer.String(event.detail);
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("outcome");
    writer.BeginObject();
    writer.Key("status"); writer.String(record.outcome.status);
    writer.Key("terminal"); writer.Bool(record.outcome.terminal);
    writer.Key("reset"); writer.Bool(record.outcome.reset);
    writer.Key("success"); writer.Bool(record.outcome.success);
    writer.Key("reward"); writer.Number(record.outcome.reward);
    writer.Key("detail"); writer.String(record.outcome.detail);
    writer.EndObject();
    writer.EndObject();
    output = writer.Text();
    return true;
}

} // namespace WorldModel
} // namespace RoR
