/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file
/// @brief Dependency-light authored RigDef contract for calibrated beam material.

#pragma once

#include "CalibratedBeamMaterialAdapter.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace RigDef {

static const std::uint32_t CALIBRATED_BEAM_MATERIAL_DIRECTIVE_VERSION = 1;
static const std::size_t CALIBRATED_BEAM_MATERIAL_NO_FIELD =
    std::numeric_limits<std::size_t>::max();

/// Snapshot carried by BeamDefaults. The leading disabled state is deliberately
/// inert so every truck that omits the opt-in retains the legacy beam law.
struct CalibratedBeamMaterialDefaults
{
    std::uint32_t schema_version =
        CALIBRATED_BEAM_MATERIAL_DIRECTIVE_VERSION;
    bool enabled = false;
    double cross_section_area_m2 = 0.0;
    double elastic_modulus_pa = 0.0;
    double yield_stress_pa = 0.0;
    double hardening_modulus_pa = 0.0;
    double damage_onset_plastic_strain = 0.0;
    double damage_driver_capacity_density_j_m3 = 0.0;
    bool _is_user_defined = false;
};

enum class CalibratedBeamMaterialDirectiveError
{
    NONE,
    WRONG_FIELD_COUNT,
    UNSUPPORTED_SCHEMA,
    INVALID_MODE,
    INVALID_NUMBER,
    INVALID_CONFIGURATION
};

struct CalibratedBeamMaterialDirectiveResult
{
    CalibratedBeamMaterialDirectiveError error =
        CalibratedBeamMaterialDirectiveError::NONE;
    std::size_t field_index = CALIBRATED_BEAM_MATERIAL_NO_FIELD;
    RoR::CalibratedBeamMaterialAdapter::Error adapter_error =
        RoR::CalibratedBeamMaterialAdapter::Error::NONE;
    RoR::CalibratedBeamMaterial::Error material_error =
        RoR::CalibratedBeamMaterial::Error::NONE;

    bool IsValid() const
    {
        return error == CalibratedBeamMaterialDirectiveError::NONE;
    }

    bool HasField() const
    {
        return field_index != CALIBRATED_BEAM_MATERIAL_NO_FIELD;
    }
};

inline const char* CalibratedBeamMaterialDirectiveErrorToString(
    CalibratedBeamMaterialDirectiveError error)
{
    switch (error)
    {
    case CalibratedBeamMaterialDirectiveError::NONE:
        return "none";
    case CalibratedBeamMaterialDirectiveError::WRONG_FIELD_COUNT:
        return "wrong field count";
    case CalibratedBeamMaterialDirectiveError::UNSUPPORTED_SCHEMA:
        return "unsupported schema";
    case CalibratedBeamMaterialDirectiveError::INVALID_MODE:
        return "invalid mode";
    case CalibratedBeamMaterialDirectiveError::INVALID_NUMBER:
        return "invalid finite decimal number";
    case CalibratedBeamMaterialDirectiveError::INVALID_CONFIGURATION:
        return "invalid material configuration";
    }
    return "unknown error";
}

inline bool IsStrictDecimalNumber(const std::string& text)
{
    if (text.empty())
        return false;

    std::size_t offset = 0;
    if (text[offset] == '+' || text[offset] == '-')
    {
        ++offset;
        if (offset == text.size())
            return false;
    }

    bool has_mantissa_digit = false;
    while (offset < text.size() &&
        text[offset] >= '0' && text[offset] <= '9')
    {
        has_mantissa_digit = true;
        ++offset;
    }
    if (offset < text.size() && text[offset] == '.')
    {
        ++offset;
        while (offset < text.size() &&
            text[offset] >= '0' && text[offset] <= '9')
        {
            has_mantissa_digit = true;
            ++offset;
        }
    }
    if (!has_mantissa_digit)
        return false;

    if (offset < text.size() &&
        (text[offset] == 'e' || text[offset] == 'E'))
    {
        ++offset;
        if (offset < text.size() &&
            (text[offset] == '+' || text[offset] == '-'))
        {
            ++offset;
        }
        const std::size_t exponent_start = offset;
        while (offset < text.size() &&
            text[offset] >= '0' && text[offset] <= '9')
        {
            ++offset;
        }
        if (offset == exponent_start)
            return false;
    }
    return offset == text.size();
}

inline bool TryParseFiniteDouble(
    const std::string& text,
    double& value)
{
    if (!IsStrictDecimalNumber(text))
        return false;

    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::noskipws;

    double parsed = 0.0;
    input >> parsed;
    if (input.fail() ||
        input.peek() != std::char_traits<char>::eof() ||
        !RoR::CalibratedBeamMaterial::IsFinite(parsed))
    {
        return false;
    }
    value = parsed;
    return true;
}

inline bool TryBuildCalibratedBeamMaterialConfiguration(
    const CalibratedBeamMaterialDefaults& authored,
    RoR::CalibratedBeamMaterialAdapter::Configuration& configuration,
    RoR::CalibratedBeamMaterialAdapter::Error* adapter_error = nullptr,
    RoR::CalibratedBeamMaterial::Error* material_error = nullptr)
{
    using namespace RoR;

    if (adapter_error != nullptr)
        *adapter_error = CalibratedBeamMaterialAdapter::Error::NONE;
    if (material_error != nullptr)
        *material_error = CalibratedBeamMaterial::Error::NONE;

    if (!authored.enabled)
    {
        if (adapter_error != nullptr)
            *adapter_error = CalibratedBeamMaterialAdapter::Error::DISABLED;
        return false;
    }

    CalibratedBeamMaterialAdapter::Configuration candidate;
    candidate.schema_version = authored.schema_version;
    candidate.cross_section_area_m2 = authored.cross_section_area_m2;
    candidate.material.schema_version = authored.schema_version;
    candidate.material.elastic_modulus = authored.elastic_modulus_pa;
    candidate.material.yield_stress = authored.yield_stress_pa;
    candidate.material.hardening_modulus = authored.hardening_modulus_pa;
    candidate.material.damage_onset_plastic_strain =
        authored.damage_onset_plastic_strain;
    candidate.material.damage_driver_capacity_density =
        authored.damage_driver_capacity_density_j_m3;

    CalibratedBeamMaterial::Error candidate_material_error =
        CalibratedBeamMaterial::Error::NONE;
    const CalibratedBeamMaterialAdapter::Error candidate_adapter_error =
        CalibratedBeamMaterialAdapter::ValidateConfiguration(
            candidate,
            &candidate_material_error);
    if (adapter_error != nullptr)
        *adapter_error = candidate_adapter_error;
    if (material_error != nullptr)
        *material_error = candidate_material_error;
    if (candidate_adapter_error !=
        CalibratedBeamMaterialAdapter::Error::NONE)
    {
        return false;
    }

    configuration = candidate;
    return true;
}

/// Performs the fail-closed authored-material preparation used by
/// ActorSpawner before a beam is allocated.
///
/// The output runtime changes only after role validation, authored
/// configuration validation, and adapter configuration all succeed. Disabled
/// authored state is reported as DISABLED and leaves legacy spawning
/// completely untouched.
inline bool TryPrepareCalibratedBeamMaterialForSpawn(
    const CalibratedBeamMaterialDefaults& authored,
    bool is_plain_normal_noshock_beam,
    RoR::CalibratedBeamMaterialAdapter::Runtime& runtime,
    RoR::CalibratedBeamMaterialAdapter::Error* adapter_error = nullptr,
    RoR::CalibratedBeamMaterial::Error* material_error = nullptr)
{
    using namespace RoR;

    if (adapter_error != nullptr)
        *adapter_error = CalibratedBeamMaterialAdapter::Error::NONE;
    if (material_error != nullptr)
        *material_error = CalibratedBeamMaterial::Error::NONE;

    if (!authored.enabled)
    {
        if (adapter_error != nullptr)
            *adapter_error = CalibratedBeamMaterialAdapter::Error::DISABLED;
        return false;
    }
    if (!is_plain_normal_noshock_beam)
    {
        if (adapter_error != nullptr)
        {
            *adapter_error =
                CalibratedBeamMaterialAdapter::Error::UNSUPPORTED_BEAM_ROLE;
        }
        return false;
    }

    CalibratedBeamMaterialAdapter::Configuration configuration;
    CalibratedBeamMaterialAdapter::Error configuration_error =
        CalibratedBeamMaterialAdapter::Error::NONE;
    CalibratedBeamMaterial::Error configuration_material_error =
        CalibratedBeamMaterial::Error::NONE;
    if (!TryBuildCalibratedBeamMaterialConfiguration(
            authored,
            configuration,
            &configuration_error,
            &configuration_material_error))
    {
        if (adapter_error != nullptr)
            *adapter_error = configuration_error;
        if (material_error != nullptr)
            *material_error = configuration_material_error;
        return false;
    }

    CalibratedBeamMaterialAdapter::Runtime candidate;
    if (!CalibratedBeamMaterialAdapter::TryConfigure(
            candidate,
            configuration,
            &configuration_error,
            &configuration_material_error))
    {
        if (adapter_error != nullptr)
            *adapter_error = configuration_error;
        if (material_error != nullptr)
            *material_error = configuration_material_error;
        return false;
    }

    runtime = candidate;
    return true;
}

inline bool IsAsciiEqualNoCase(
    const std::string& first,
    const char* second)
{
    std::size_t offset = 0;
    while (offset < first.size() && second[offset] != '\0')
    {
        char value = first[offset];
        if (value >= 'A' && value <= 'Z')
            value = static_cast<char>(value - 'A' + 'a');
        if (value != second[offset])
            return false;
        ++offset;
    }
    return offset == first.size() && second[offset] == '\0';
}

inline std::size_t CalibratedBeamMaterialConfigurationErrorFieldIndex(
    RoR::CalibratedBeamMaterialAdapter::Error adapter_error,
    RoR::CalibratedBeamMaterial::Error material_error)
{
    using namespace RoR;

    switch (adapter_error)
    {
    case CalibratedBeamMaterialAdapter::Error::UNSUPPORTED_ADAPTER_SCHEMA:
        return 0;
    case CalibratedBeamMaterialAdapter::Error::INVALID_CROSS_SECTION_AREA:
        return 2;
    case CalibratedBeamMaterialAdapter::Error::MATERIAL_FAILURE:
        break;
    default:
        return CALIBRATED_BEAM_MATERIAL_NO_FIELD;
    }

    switch (material_error)
    {
    case CalibratedBeamMaterial::Error::UNSUPPORTED_SCHEMA:
        return 0;
    case CalibratedBeamMaterial::Error::INVALID_ELASTIC_MODULUS:
        return 3;
    case CalibratedBeamMaterial::Error::INVALID_YIELD_STRESS:
        return 4;
    case CalibratedBeamMaterial::Error::INVALID_HARDENING_MODULUS:
        return 5;
    case CalibratedBeamMaterial::Error::INVALID_DAMAGE_ONSET:
        return 6;
    case CalibratedBeamMaterial::Error::INVALID_DAMAGE_DRIVER_CAPACITY:
        return 7;
    default:
        return CALIBRATED_BEAM_MATERIAL_NO_FIELD;
    }
}

/// Parses fields after the keyword.
///
/// Version 1:
///   1, off
///   1, on, area_m2, E_pa, yield_pa, hardening_pa,
///      damage_onset_plastic_strain, damage_driver_capacity_density_j_m3
inline CalibratedBeamMaterialDirectiveResult
TryParseCalibratedBeamMaterialDirective(
    const std::vector<std::string>& fields,
    CalibratedBeamMaterialDefaults& authored)
{
    CalibratedBeamMaterialDirectiveResult result;
    if (fields.size() < 2)
    {
        result.error =
            CalibratedBeamMaterialDirectiveError::WRONG_FIELD_COUNT;
        return result;
    }
    if (fields[0] != "1")
    {
        result.error =
            CalibratedBeamMaterialDirectiveError::UNSUPPORTED_SCHEMA;
        result.field_index = 0;
        return result;
    }

    if (IsAsciiEqualNoCase(fields[1], "off"))
    {
        if (fields.size() != 2)
        {
            result.error =
                CalibratedBeamMaterialDirectiveError::WRONG_FIELD_COUNT;
            return result;
        }
        CalibratedBeamMaterialDefaults disabled;
        disabled._is_user_defined = true;
        authored = disabled;
        return result;
    }

    if (!IsAsciiEqualNoCase(fields[1], "on"))
    {
        result.error =
            CalibratedBeamMaterialDirectiveError::INVALID_MODE;
        result.field_index = 1;
        return result;
    }
    if (fields.size() != 8)
    {
        result.error =
            CalibratedBeamMaterialDirectiveError::WRONG_FIELD_COUNT;
        return result;
    }

    CalibratedBeamMaterialDefaults candidate;
    candidate.enabled = true;
    candidate._is_user_defined = true;
    double* numeric_fields[] = {
        &candidate.cross_section_area_m2,
        &candidate.elastic_modulus_pa,
        &candidate.yield_stress_pa,
        &candidate.hardening_modulus_pa,
        &candidate.damage_onset_plastic_strain,
        &candidate.damage_driver_capacity_density_j_m3
    };
    for (std::size_t index = 0;
        index < sizeof(numeric_fields) / sizeof(numeric_fields[0]);
        ++index)
    {
        if (!TryParseFiniteDouble(fields[index + 2], *numeric_fields[index]))
        {
            result.error =
                CalibratedBeamMaterialDirectiveError::INVALID_NUMBER;
            result.field_index = index + 2;
            return result;
        }
    }

    RoR::CalibratedBeamMaterialAdapter::Configuration configuration;
    if (!TryBuildCalibratedBeamMaterialConfiguration(
            candidate,
            configuration,
            &result.adapter_error,
            &result.material_error))
    {
        result.error =
            CalibratedBeamMaterialDirectiveError::INVALID_CONFIGURATION;
        result.field_index =
            CalibratedBeamMaterialConfigurationErrorFieldIndex(
                result.adapter_error,
                result.material_error);
        return result;
    }

    authored = candidate;
    return result;
}

/// Applies a parsed directive to a BeamDefaults-compatible snapshot.
///
/// The active pointer changes only after parsing, validation, allocation, and
/// copy construction succeed. Keeping this copy-on-write transition here lets
/// the dependency-light tests exercise the exact state mutation used by the
/// native parser without linking Ogre or the application console.
template <typename BeamDefaultsType>
inline CalibratedBeamMaterialDirectiveResult
TryApplyCalibratedBeamMaterialDirective(
    const std::vector<std::string>& fields,
    std::shared_ptr<BeamDefaultsType>& active_defaults)
{
    CalibratedBeamMaterialDefaults candidate;
    const CalibratedBeamMaterialDirectiveResult result =
        TryParseCalibratedBeamMaterialDirective(fields, candidate);
    if (!result.IsValid())
        return result;

    std::shared_ptr<BeamDefaultsType> updated(
        new BeamDefaultsType(*active_defaults));
    updated->calibrated_material = candidate;
    active_defaults = updated;
    return result;
}

inline bool TryFormatCalibratedBeamMaterialDirectiveArguments(
    const CalibratedBeamMaterialDefaults& authored,
    std::string& arguments)
{
    arguments.clear();
    if (!authored._is_user_defined)
        return true;
    if (authored.schema_version !=
        CALIBRATED_BEAM_MATERIAL_DIRECTIVE_VERSION)
    {
        return false;
    }
    if (!authored.enabled)
    {
        arguments = "1, off";
        return true;
    }

    RoR::CalibratedBeamMaterialAdapter::Configuration configuration;
    if (!TryBuildCalibratedBeamMaterialConfiguration(
            authored,
            configuration))
    {
        return false;
    }

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "1, on, "
        << authored.cross_section_area_m2 << ", "
        << authored.elastic_modulus_pa << ", "
        << authored.yield_stress_pa << ", "
        << authored.hardening_modulus_pa << ", "
        << authored.damage_onset_plastic_strain << ", "
        << authored.damage_driver_capacity_density_j_m3;
    arguments = output.str();
    return true;
}

struct CalibratedBeamMaterialSerializationTransition
{
    bool emit_directive = false;
    bool authored_state_valid = true;
    std::string arguments;
};

/// Advances the global directive state for one serialized beam-default
/// snapshot. Unsupported roles pass `allow_calibrated_material=false`, which
/// emits an explicit off transition whenever necessary. A later supported beam
/// may then re-enable the same authored snapshot without leaking the material
/// law through the unsupported role.
inline CalibratedBeamMaterialSerializationTransition
AdvanceCalibratedBeamMaterialSerialization(
    const CalibratedBeamMaterialDefaults* authored,
    bool allow_calibrated_material,
    bool& calibrated_material_enabled)
{
    CalibratedBeamMaterialSerializationTransition transition;
    if (allow_calibrated_material &&
        authored != nullptr &&
        authored->_is_user_defined)
    {
        transition.authored_state_valid =
            TryFormatCalibratedBeamMaterialDirectiveArguments(
                *authored,
                transition.arguments);
        if (transition.authored_state_valid)
        {
            transition.emit_directive = true;
            calibrated_material_enabled = authored->enabled;
            return transition;
        }
    }

    if (calibrated_material_enabled)
    {
        transition.emit_directive = true;
        transition.arguments = "1, off";
        calibrated_material_enabled = false;
    }
    return transition;
}

} // namespace RigDef
