/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file JBeamHydroSavegameJson.h
/// @brief Strict JSON codec for native JBeam hydro save state schema v1.

#pragma once

#include "JBeamHydroSavegame.h"

#include <rapidjson/document.h>

#include <cstring>
#include <limits>

namespace RoR {
namespace JBeamHydroSavegame {

inline bool HasExactJsonMembers(
    const rapidjson::Value& value,
    const char* const* names,
    rapidjson::SizeType count)
{
    if (!value.IsObject() || value.MemberCount() != count)
        return false;
    for (rapidjson::SizeType index = 0U; index < count; ++index)
    {
        if (!value.HasMember(names[index]))
            return false;
    }
    return true;
}

inline bool ReadFiniteJsonDouble(
    const rapidjson::Value& object,
    const char* name,
    double& output)
{
    if (!object.HasMember(name) || !object[name].IsNumber())
        return false;
    const double candidate = object[name].GetDouble();
    if (!HydroActuatorDetail::IsFinite(candidate))
        return false;
    output = candidate;
    return true;
}

inline bool ReadNormalJsonFloat(
    const rapidjson::Value& object,
    const char* name,
    float& output)
{
    double candidate = 0.0;
    if (!ReadFiniteJsonDouble(object, name, candidate) ||
        !(candidate > 0.0) ||
        candidate > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }
    const float narrowed = static_cast<float>(candidate);
    if (!JBeamHydroRuntimeDetail::IsNormalBinary32(narrowed))
        return false;
    output = narrowed;
    return true;
}

inline rapidjson::Value SerializeJson(
    const ActorPayload& payload,
    rapidjson::Document::AllocatorType& allocator)
{
    rapidjson::Value root(rapidjson::kObjectType);
    root.AddMember("schema_version", payload.schema_version, allocator);
    root.AddMember("hydro_count", payload.hydro_count, allocator);
    rapidjson::Value records(rapidjson::kArrayType);
    for (std::size_t index = 0U; index < payload.records.size(); ++index)
    {
        const HydroRecord& record = payload.records[index];
        const HydroActuatorConfig& response = record.config.response;

        rapidjson::Value serialized(rapidjson::kObjectType);
        serialized.AddMember("hydro_index", record.hydro_index, allocator);
        serialized.AddMember("beam_index", record.beam_index, allocator);
        serialized.AddMember(
            "reference_length", record.reference_length, allocator);

        rapidjson::Value serialized_response(rapidjson::kObjectType);
        serialized_response.AddMember(
            "has_factor", response.has_factor, allocator);
        serialized_response.AddMember("factor", response.factor, allocator);
        serialized_response.AddMember(
            "in_limit", response.in_limit, allocator);
        serialized_response.AddMember(
            "out_limit", response.out_limit, allocator);
        serialized_response.AddMember(
            "input_factor", response.input_factor, allocator);
        serialized_response.AddMember(
            "input_center", response.input_center, allocator);
        serialized_response.AddMember(
            "input_in_limit", response.input_in_limit, allocator);
        serialized_response.AddMember(
            "input_out_limit", response.input_out_limit, allocator);
        serialized_response.AddMember(
            "in_rate", response.in_rate, allocator);
        serialized_response.AddMember(
            "out_rate", response.out_rate, allocator);
        serialized_response.AddMember(
            "auto_center_rate", response.auto_center_rate, allocator);

        rapidjson::Value configuration(rapidjson::kObjectType);
        configuration.AddMember(
            "response", serialized_response, allocator);
        configuration.AddMember(
            "input_route",
            rapidjson::StringRef("steering-input"),
            allocator);
        configuration.AddMember(
            "has_steering_wheel_lock",
            record.config.has_steering_wheel_lock,
            allocator);
        configuration.AddMember(
            "steering_wheel_lock",
            record.config.steering_wheel_lock,
            allocator);
        serialized.AddMember("configuration", configuration, allocator);

        rapidjson::Value state(rapidjson::kObjectType);
        state.AddMember(
            "length_ratio",
            record.state.response.length_ratio,
            allocator);
        state.AddMember(
            "accepted_step_count",
            record.state.accepted_step_count,
            allocator);
        state.AddMember(
            "fault_latched", record.state.fault_latched, allocator);
        state.AddMember(
            "fault", static_cast<int>(record.state.fault), allocator);
        serialized.AddMember("state", state, allocator);
        records.PushBack(serialized, allocator);
    }
    root.AddMember("records", records, allocator);
    return root;
}

inline bool ParseJsonResponse(
    const rapidjson::Value& serialized,
    HydroActuatorConfig& output)
{
    static const char* const names[] = {
        "has_factor",
        "factor",
        "in_limit",
        "out_limit",
        "input_factor",
        "input_center",
        "input_in_limit",
        "input_out_limit",
        "in_rate",
        "out_rate",
        "auto_center_rate"
    };
    if (!HasExactJsonMembers(
            serialized,
            names,
            static_cast<rapidjson::SizeType>(
                sizeof(names) / sizeof(names[0]))) ||
        !serialized["has_factor"].IsBool())
    {
        return false;
    }
    HydroActuatorConfig candidate;
    candidate.has_factor = serialized["has_factor"].GetBool();
    if (!ReadFiniteJsonDouble(serialized, "factor", candidate.factor) ||
        !ReadFiniteJsonDouble(serialized, "in_limit", candidate.in_limit) ||
        !ReadFiniteJsonDouble(serialized, "out_limit", candidate.out_limit) ||
        !ReadFiniteJsonDouble(
            serialized, "input_factor", candidate.input_factor) ||
        !ReadFiniteJsonDouble(
            serialized, "input_center", candidate.input_center) ||
        !ReadFiniteJsonDouble(
            serialized, "input_in_limit", candidate.input_in_limit) ||
        !ReadFiniteJsonDouble(
            serialized, "input_out_limit", candidate.input_out_limit) ||
        !ReadFiniteJsonDouble(serialized, "in_rate", candidate.in_rate) ||
        !ReadFiniteJsonDouble(serialized, "out_rate", candidate.out_rate) ||
        !ReadFiniteJsonDouble(
            serialized, "auto_center_rate", candidate.auto_center_rate))
    {
        return false;
    }
    output = candidate;
    return true;
}

inline bool ParseJsonConfiguration(
    const rapidjson::Value& serialized,
    JBeamHydroRuntimeConfig& output)
{
    static const char* const names[] = {
        "response",
        "input_route",
        "has_steering_wheel_lock",
        "steering_wheel_lock"
    };
    if (!HasExactJsonMembers(
            serialized,
            names,
            static_cast<rapidjson::SizeType>(
                sizeof(names) / sizeof(names[0]))) ||
        !serialized["input_route"].IsString() ||
        serialized["input_route"].GetStringLength() !=
            sizeof("steering-input") - 1U ||
        std::memcmp(
            serialized["input_route"].GetString(),
            "steering-input",
            sizeof("steering-input") - 1U) != 0 ||
        !serialized["has_steering_wheel_lock"].IsBool())
    {
        return false;
    }
    JBeamHydroRuntimeConfig candidate;
    candidate.input_route = JBeamHydroInputRoute::STEERING_INPUT;
    candidate.has_steering_wheel_lock =
        serialized["has_steering_wheel_lock"].GetBool();
    if (!ParseJsonResponse(serialized["response"], candidate.response) ||
        !ReadFiniteJsonDouble(
            serialized,
            "steering_wheel_lock",
            candidate.steering_wheel_lock))
    {
        return false;
    }
    output = candidate;
    return true;
}

inline bool ParseJsonState(
    const rapidjson::Value& serialized,
    JBeamHydroRuntimeState& output)
{
    static const char* const names[] = {
        "length_ratio",
        "accepted_step_count",
        "fault_latched",
        "fault"
    };
    if (!HasExactJsonMembers(
            serialized,
            names,
            static_cast<rapidjson::SizeType>(
                sizeof(names) / sizeof(names[0]))) ||
        !serialized["accepted_step_count"].IsUint64() ||
        !serialized["fault_latched"].IsBool() ||
        !serialized["fault"].IsInt())
    {
        return false;
    }
    JBeamHydroRuntimeState candidate;
    if (!ReadFiniteJsonDouble(
            serialized,
            "length_ratio",
            candidate.response.length_ratio))
    {
        return false;
    }
    candidate.accepted_step_count =
        serialized["accepted_step_count"].GetUint64();
    candidate.fault_latched = serialized["fault_latched"].GetBool();
    candidate.fault = static_cast<JBeamHydroRuntimeFault>(
        serialized["fault"].GetInt());
    if (!IsKnownFault(candidate.fault))
        return false;
    output = candidate;
    return true;
}

inline bool ParseJsonRecord(
    const rapidjson::Value& serialized,
    HydroRecord& output)
{
    static const char* const names[] = {
        "hydro_index",
        "beam_index",
        "reference_length",
        "configuration",
        "state"
    };
    if (!HasExactJsonMembers(
            serialized,
            names,
            static_cast<rapidjson::SizeType>(
                sizeof(names) / sizeof(names[0]))) ||
        !serialized["hydro_index"].IsUint() ||
        !serialized["beam_index"].IsUint() ||
        serialized["beam_index"].GetUint() >
            std::numeric_limits<std::uint16_t>::max())
    {
        return false;
    }
    HydroRecord candidate;
    candidate.hydro_index = serialized["hydro_index"].GetUint();
    candidate.beam_index = static_cast<std::uint16_t>(
        serialized["beam_index"].GetUint());
    if (!ReadNormalJsonFloat(
            serialized,
            "reference_length",
            candidate.reference_length) ||
        !ParseJsonConfiguration(
            serialized["configuration"], candidate.config) ||
        !ParseJsonState(serialized["state"], candidate.state))
    {
        return false;
    }
    output = candidate;
    return true;
}

/// Parses strict schema-1 JSON without mutating `output` on failure.
inline bool ParseJson(
    const rapidjson::Value& serialized,
    ActorPayload& output)
{
    static const char* const names[] = {
        "schema_version",
        "hydro_count",
        "records"
    };
    if (!HasExactJsonMembers(
            serialized,
            names,
            static_cast<rapidjson::SizeType>(
                sizeof(names) / sizeof(names[0]))) ||
        !serialized["schema_version"].IsUint() ||
        !serialized["hydro_count"].IsUint() ||
        !serialized["records"].IsArray())
    {
        return false;
    }
    ActorPayload candidate;
    candidate.schema_version = serialized["schema_version"].GetUint();
    candidate.hydro_count = serialized["hydro_count"].GetUint();
    const rapidjson::Value& records = serialized["records"];
    if (candidate.schema_version != PAYLOAD_SCHEMA_VERSION ||
        candidate.hydro_count > MAX_HYDRO_COUNT ||
        records.Size() > candidate.hydro_count)
    {
        return false;
    }
    candidate.records.reserve(records.Size());
    for (rapidjson::SizeType index = 0U; index < records.Size(); ++index)
    {
        HydroRecord record;
        if (!ParseJsonRecord(records[index], record))
            return false;
        candidate.records.push_back(record);
    }
    output = candidate;
    return true;
}

} // namespace JBeamHydroSavegame
} // namespace RoR
