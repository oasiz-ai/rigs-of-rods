/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Allocation-free audit of calibrated beam state at a committed step.

#pragma once

#include "CalibratedBeamMaterialAdapter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace RoR {
namespace CalibratedBeamRuntimeAudit {

static const std::uint32_t AUDIT_SCHEMA_VERSION = 1U;

struct Sample
{
    bool enabled = false;
    bool faulted = false;
    bool fractured = false;
    bool disabled = false;
    const CalibratedBeamMaterialAdapter::Configuration* configuration = nullptr;
    const CalibratedBeamMaterial::State* state = nullptr;
};

struct Result
{
    std::uint32_t schema_version = AUDIT_SCHEMA_VERSION;
    std::uint32_t calibrated_count = 0U;
    std::uint32_t fault_count = 0U;
    std::uint32_t fracture_count = 0U;
    std::uint32_t disabled_count = 0U;
    std::uint32_t active_history_count = 0U;
    bool finite = true;
    bool state_valid = true;
    std::uint32_t first_invalid_index =
        std::numeric_limits<std::uint32_t>::max();
    double max_abs_total_strain = 0.0;
    double max_accumulated_plastic_strain = 0.0;
    double max_damage = 0.0;
};

inline double Absolute(double value)
{
    return value < 0.0 ? -value : value;
}

inline bool HasHistory(const CalibratedBeamMaterial::State& state)
{
    return state.plastic_strain != 0.0 ||
        state.accumulated_plastic_strain != 0.0 ||
        state.damage != 0.0 ||
        state.damage_driver_density != 0.0 ||
        state.last_total_strain != 0.0 ||
        state.fractured;
}

class Builder
{
public:
    void Add(const Sample& sample, std::uint32_t source_index)
    {
        if (!sample.enabled)
            return;

        if (m_result.calibrated_count ==
            std::numeric_limits<std::uint32_t>::max())
        {
            MarkInvalid(source_index, false);
            return;
        }
        ++m_result.calibrated_count;
        m_result.fault_count += sample.faulted ? 1U : 0U;
        m_result.fracture_count += sample.fractured ? 1U : 0U;
        m_result.disabled_count += sample.disabled ? 1U : 0U;

        if (sample.configuration == nullptr || sample.state == nullptr)
        {
            MarkInvalid(source_index, false);
            return;
        }

        const CalibratedBeamMaterial::State& state = *sample.state;
        const bool finite =
            CalibratedBeamMaterial::IsFinite(state.plastic_strain) &&
            CalibratedBeamMaterial::IsFinite(
                state.accumulated_plastic_strain) &&
            CalibratedBeamMaterial::IsFinite(state.damage) &&
            CalibratedBeamMaterial::IsFinite(state.damage_driver_density) &&
            CalibratedBeamMaterial::IsFinite(state.last_total_strain);
        if (!finite)
        {
            MarkInvalid(source_index, true);
            return;
        }

        if (CalibratedBeamMaterialAdapter::ValidateConfiguration(
                *sample.configuration) !=
            CalibratedBeamMaterialAdapter::Error::NONE)
        {
            MarkInvalid(source_index, false);
            return;
        }

        if (!CalibratedBeamMaterial::IsValidState(
                state,
                sample.configuration->material))
        {
            MarkInvalid(source_index, false);
            return;
        }

        if (HasHistory(state))
            ++m_result.active_history_count;
        m_result.max_abs_total_strain = std::max(
            m_result.max_abs_total_strain,
            Absolute(state.last_total_strain));
        m_result.max_accumulated_plastic_strain = std::max(
            m_result.max_accumulated_plastic_strain,
            state.accumulated_plastic_strain);
        m_result.max_damage = std::max(m_result.max_damage, state.damage);
    }

    const Result& Get() const { return m_result; }

private:
    void MarkInvalid(std::uint32_t source_index, bool nonfinite)
    {
        m_result.state_valid = false;
        if (nonfinite)
            m_result.finite = false;
        if (m_result.first_invalid_index ==
            std::numeric_limits<std::uint32_t>::max())
        {
            m_result.first_invalid_index = source_index;
        }
    }

    Result m_result;
};

} // namespace CalibratedBeamRuntimeAudit
} // namespace RoR
