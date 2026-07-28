/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Strict JSON codec for calibrated-beam savegame payload schema v1.

#pragma once

#include "CalibratedBeamSavegame.h"

#include <rapidjson/document.h>

namespace RoR {
namespace CalibratedBeamSavegame {

inline bool HasExactJsonMembers(
    const rapidjson::Value& value,
    const char* const* names,
    rapidjson::SizeType count)
{
    if (!value.IsObject() || value.MemberCount() != count)
        return false;
    for (rapidjson::SizeType index = 0; index < count; ++index)
    {
        if (!value.HasMember(names[index]))
            return false;
    }
    return true;
}

inline bool ReadFiniteJsonDouble(
    const rapidjson::Value& object,
    const char* name,
    double& result)
{
    if (!object.HasMember(name) || !object[name].IsNumber())
        return false;
    result = object[name].GetDouble();
    return CalibratedBeamMaterial::IsFinite(result);
}

inline rapidjson::Value SerializeJson(
    const ActorPayload& payload,
    rapidjson::Document::AllocatorType& allocator)
{
    rapidjson::Value root(rapidjson::kObjectType);
    root.AddMember("schema_version", payload.schema_version, allocator);
    root.AddMember("beam_count", payload.beam_count, allocator);

    rapidjson::Value records(rapidjson::kArrayType);
    for (std::size_t index = 0; index < payload.records.size(); ++index)
    {
        const BeamRecord& record = payload.records[index];
        const CalibratedBeamMaterialAdapter::Configuration& configuration =
            record.runtime.configuration;
        const CalibratedBeamMaterial::Parameters& material =
            configuration.material;
        const CalibratedBeamMaterial::State& state =
            record.runtime.state;

        rapidjson::Value serialized(rapidjson::kObjectType);
        serialized.AddMember("beam_index", record.beam_index, allocator);
        serialized.AddMember("node_1", record.node_1, allocator);
        serialized.AddMember("node_2", record.node_2, allocator);
        serialized.AddMember("beam_type", record.beam_type, allocator);
        serialized.AddMember(
            "special_beam",
            record.special_beam,
            allocator);
        serialized.AddMember("disabled", record.disabled, allocator);
        serialized.AddMember("broken", record.broken, allocator);

        rapidjson::Value serialized_configuration(rapidjson::kObjectType);
        serialized_configuration.AddMember(
            "adapter_schema_version",
            configuration.schema_version,
            allocator);
        serialized_configuration.AddMember(
            "cross_section_area_m2",
            configuration.cross_section_area_m2,
            allocator);
        serialized_configuration.AddMember(
            "material_schema_version",
            material.schema_version,
            allocator);
        serialized_configuration.AddMember(
            "elastic_modulus_pa",
            material.elastic_modulus,
            allocator);
        serialized_configuration.AddMember(
            "yield_stress_pa",
            material.yield_stress,
            allocator);
        serialized_configuration.AddMember(
            "hardening_modulus_pa",
            material.hardening_modulus,
            allocator);
        serialized_configuration.AddMember(
            "damage_onset_plastic_strain",
            material.damage_onset_plastic_strain,
            allocator);
        serialized_configuration.AddMember(
            "damage_driver_capacity_density_j_m3",
            material.damage_driver_capacity_density,
            allocator);
        serialized.AddMember(
            "configuration",
            serialized_configuration,
            allocator);

        rapidjson::Value serialized_state(rapidjson::kObjectType);
        serialized_state.AddMember(
            "plastic_strain",
            state.plastic_strain,
            allocator);
        serialized_state.AddMember(
            "accumulated_plastic_strain",
            state.accumulated_plastic_strain,
            allocator);
        serialized_state.AddMember("damage", state.damage, allocator);
        serialized_state.AddMember(
            "damage_driver_density_j_m3",
            state.damage_driver_density,
            allocator);
        serialized_state.AddMember(
            "last_total_strain",
            state.last_total_strain,
            allocator);
        serialized_state.AddMember(
            "fractured",
            state.fractured,
            allocator);
        serialized.AddMember("state", serialized_state, allocator);

        rapidjson::Value serialized_fault(rapidjson::kObjectType);
        serialized_fault.AddMember(
            "faulted",
            record.runtime.faulted,
            allocator);
        serialized_fault.AddMember(
            "adapter_error",
            static_cast<int>(record.runtime.last_error),
            allocator);
        serialized_fault.AddMember(
            "material_error",
            static_cast<int>(record.runtime.last_material_error),
            allocator);
        serialized.AddMember("fault", serialized_fault, allocator);

        records.PushBack(serialized, allocator);
    }
    root.AddMember("records", records, allocator);
    return root;
}

inline bool ParseJsonConfiguration(
    const rapidjson::Value& serialized,
    CalibratedBeamMaterialAdapter::Configuration& configuration)
{
    static const char* const names[] = {
        "adapter_schema_version",
        "cross_section_area_m2",
        "material_schema_version",
        "elastic_modulus_pa",
        "yield_stress_pa",
        "hardening_modulus_pa",
        "damage_onset_plastic_strain",
        "damage_driver_capacity_density_j_m3"
    };
    if (!HasExactJsonMembers(
            serialized,
            names,
            static_cast<rapidjson::SizeType>(
                sizeof(names) / sizeof(names[0]))) ||
        !serialized["adapter_schema_version"].IsUint() ||
        !serialized["material_schema_version"].IsUint())
    {
        return false;
    }

    CalibratedBeamMaterialAdapter::Configuration candidate;
    candidate.schema_version =
        serialized["adapter_schema_version"].GetUint();
    candidate.material.schema_version =
        serialized["material_schema_version"].GetUint();
    if (!ReadFiniteJsonDouble(
            serialized,
            "cross_section_area_m2",
            candidate.cross_section_area_m2) ||
        !ReadFiniteJsonDouble(
            serialized,
            "elastic_modulus_pa",
            candidate.material.elastic_modulus) ||
        !ReadFiniteJsonDouble(
            serialized,
            "yield_stress_pa",
            candidate.material.yield_stress) ||
        !ReadFiniteJsonDouble(
            serialized,
            "hardening_modulus_pa",
            candidate.material.hardening_modulus) ||
        !ReadFiniteJsonDouble(
            serialized,
            "damage_onset_plastic_strain",
            candidate.material.damage_onset_plastic_strain) ||
        !ReadFiniteJsonDouble(
            serialized,
            "damage_driver_capacity_density_j_m3",
            candidate.material.damage_driver_capacity_density))
    {
        return false;
    }
    configuration = candidate;
    return true;
}

inline bool ParseJsonState(
    const rapidjson::Value& serialized,
    CalibratedBeamMaterial::State& state)
{
    static const char* const names[] = {
        "plastic_strain",
        "accumulated_plastic_strain",
        "damage",
        "damage_driver_density_j_m3",
        "last_total_strain",
        "fractured"
    };
    if (!HasExactJsonMembers(
            serialized,
            names,
            static_cast<rapidjson::SizeType>(
                sizeof(names) / sizeof(names[0]))) ||
        !serialized["fractured"].IsBool())
    {
        return false;
    }

    CalibratedBeamMaterial::State candidate;
    if (!ReadFiniteJsonDouble(
            serialized,
            "plastic_strain",
            candidate.plastic_strain) ||
        !ReadFiniteJsonDouble(
            serialized,
            "accumulated_plastic_strain",
            candidate.accumulated_plastic_strain) ||
        !ReadFiniteJsonDouble(
            serialized,
            "damage",
            candidate.damage) ||
        !ReadFiniteJsonDouble(
            serialized,
            "damage_driver_density_j_m3",
            candidate.damage_driver_density) ||
        !ReadFiniteJsonDouble(
            serialized,
            "last_total_strain",
            candidate.last_total_strain))
    {
        return false;
    }
    candidate.fractured = serialized["fractured"].GetBool();
    state = candidate;
    return true;
}

inline bool ParseJsonFault(
    const rapidjson::Value& serialized,
    CalibratedBeamMaterialAdapter::Runtime& runtime)
{
    static const char* const names[] = {
        "faulted",
        "adapter_error",
        "material_error"
    };
    if (!HasExactJsonMembers(
            serialized,
            names,
            static_cast<rapidjson::SizeType>(
                sizeof(names) / sizeof(names[0]))) ||
        !serialized["faulted"].IsBool() ||
        !serialized["adapter_error"].IsInt() ||
        !serialized["material_error"].IsInt())
    {
        return false;
    }
    runtime.faulted = serialized["faulted"].GetBool();
    runtime.last_error =
        static_cast<CalibratedBeamMaterialAdapter::Error>(
            serialized["adapter_error"].GetInt());
    runtime.last_material_error =
        static_cast<CalibratedBeamMaterial::Error>(
            serialized["material_error"].GetInt());
    return true;
}

/// Strictly decodes schema v1. Unknown or duplicate members are rejected so a
/// saved state never has two plausible interpretations.
inline bool ParseJson(
    const rapidjson::Value& serialized,
    ActorPayload& payload)
{
    static const char* const root_names[] = {
        "schema_version",
        "beam_count",
        "records"
    };
    if (!HasExactJsonMembers(
            serialized,
            root_names,
            static_cast<rapidjson::SizeType>(
                sizeof(root_names) / sizeof(root_names[0]))) ||
        !serialized["schema_version"].IsUint() ||
        !serialized["beam_count"].IsUint() ||
        !serialized["records"].IsArray() ||
        serialized["records"].Size() >
            serialized["beam_count"].GetUint())
    {
        return false;
    }

    ActorPayload candidate;
    candidate.schema_version =
        serialized["schema_version"].GetUint();
    candidate.beam_count = serialized["beam_count"].GetUint();
    candidate.records.reserve(serialized["records"].Size());

    static const char* const record_names[] = {
        "beam_index",
        "node_1",
        "node_2",
        "beam_type",
        "special_beam",
        "disabled",
        "broken",
        "configuration",
        "state",
        "fault"
    };
    for (rapidjson::Value::ConstValueIterator iterator =
             serialized["records"].Begin();
         iterator != serialized["records"].End();
         ++iterator)
    {
        const rapidjson::Value& value = *iterator;
        if (!HasExactJsonMembers(
                value,
                record_names,
                static_cast<rapidjson::SizeType>(
                    sizeof(record_names) / sizeof(record_names[0]))) ||
            !value["beam_index"].IsUint() ||
            !value["node_1"].IsInt() ||
            !value["node_2"].IsInt() ||
            !value["beam_type"].IsInt() ||
            !value["special_beam"].IsInt() ||
            !value["disabled"].IsBool() ||
            !value["broken"].IsBool())
        {
            return false;
        }

        BeamRecord record;
        record.beam_index = value["beam_index"].GetUint();
        record.node_1 = value["node_1"].GetInt();
        record.node_2 = value["node_2"].GetInt();
        record.beam_type = value["beam_type"].GetInt();
        record.special_beam = value["special_beam"].GetInt();
        record.disabled = value["disabled"].GetBool();
        record.broken = value["broken"].GetBool();
        record.runtime.enabled = true;
        if (!ParseJsonConfiguration(
                value["configuration"],
                record.runtime.configuration) ||
            !ParseJsonState(
                value["state"],
                record.runtime.state) ||
            !ParseJsonFault(
                value["fault"],
                record.runtime))
        {
            return false;
        }
        candidate.records.push_back(record);
    }
    payload = candidate;
    return true;
}

} // namespace CalibratedBeamSavegame
} // namespace RoR
