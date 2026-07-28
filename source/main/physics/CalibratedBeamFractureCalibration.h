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
/// @brief Crack-band calibration for the version-1 beam material convention.

#pragma once

#include <cstdint>
#include <cstring>
#include <limits>

namespace RoR {
namespace CalibratedBeamFractureCalibration {

static const std::uint32_t CALIBRATION_SCHEMA_VERSION = 1;

/// The only convention implemented here is the one declared by
/// CalibratedBeamMaterial::Parameters:
///
///   integral sigma_nominal d epsilon_total = G_f / l_char
///
/// integrated monotonically from damage onset to complete fracture. This is
/// deliberately not labelled total fracture dissipation. It excludes all work
/// before damage onset and does not establish cyclic mesh objectivity.
enum class Convention : std::uint32_t
{
    MONOTONIC_POST_ONSET_NOMINAL_WORK = 1
};

struct Inputs
{
    std::uint32_t schema_version = CALIBRATION_SCHEMA_VERSION;
    Convention convention =
        Convention::MONOTONIC_POST_ONSET_NOMINAL_WORK;
    double fracture_energy_per_area = 0.0;  //!< G_f, J/m^2
    double characteristic_length = 0.0;     //!< l_char, m
    double elastic_modulus = 0.0;           //!< E, Pa
    double hardening_modulus = 0.0;         //!< H, Pa
};

struct Calibration
{
    double post_onset_work_density = 0.0;        //!< G_f/l_char, J/m^3
    double hardening_ratio = 0.0;                //!< H/E
    double damage_driver_capacity_density = 0.0; //!< C, J/m^3
};

enum class Error
{
    NONE,
    UNSUPPORTED_SCHEMA,
    UNSUPPORTED_CONVENTION,
    NONFINITE_INPUT,
    INVALID_FRACTURE_ENERGY,
    INVALID_CHARACTERISTIC_LENGTH,
    INVALID_ELASTIC_MODULUS,
    INVALID_HARDENING_MODULUS,
    NUMERIC_RANGE
};

/// Bit-level finiteness is intentional: callers and tests compile under the
/// game's fast-math flags, where floating-point classification predicates may
/// otherwise be optimized under an assumed-finite model.
inline bool IsFiniteScalar(double value)
{
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "64-bit IEEE-754 double required");
    static_assert(
        std::numeric_limits<double>::is_iec559,
        "IEEE-754 double required");

    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const volatile std::uint64_t observed_bits = bits;
    return (observed_bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

/// Maps a crack energy to the local capacity used by
/// CalibratedBeamMaterial under the declared convention:
///
///   C = 2 (G_f / l_char) / (1 + H/E).
///
/// Validation order is part of the contract. Schema and convention errors take
/// precedence, then non-finite values, individual physical ranges, and finally
/// derived numeric range. On any error, `output` is left byte-for-byte
/// untouched so an invalid authored value cannot partially replace a validated
/// configuration.
inline Error TryCalibrate(
    const Inputs& inputs,
    Calibration& output)
{
    if (inputs.schema_version != CALIBRATION_SCHEMA_VERSION)
        return Error::UNSUPPORTED_SCHEMA;
    if (inputs.convention !=
        Convention::MONOTONIC_POST_ONSET_NOMINAL_WORK)
    {
        return Error::UNSUPPORTED_CONVENTION;
    }
    if (!IsFiniteScalar(inputs.fracture_energy_per_area) ||
        !IsFiniteScalar(inputs.characteristic_length) ||
        !IsFiniteScalar(inputs.elastic_modulus) ||
        !IsFiniteScalar(inputs.hardening_modulus))
    {
        return Error::NONFINITE_INPUT;
    }
    if (inputs.fracture_energy_per_area <= 0.0)
        return Error::INVALID_FRACTURE_ENERGY;
    if (inputs.characteristic_length <= 0.0)
        return Error::INVALID_CHARACTERISTIC_LENGTH;
    if (inputs.elastic_modulus <= 0.0)
        return Error::INVALID_ELASTIC_MODULUS;
    if (inputs.hardening_modulus < 0.0)
        return Error::INVALID_HARDENING_MODULUS;

    Calibration candidate;
    candidate.hardening_ratio =
        inputs.hardening_modulus / inputs.elastic_modulus;
    candidate.post_onset_work_density =
        inputs.fracture_energy_per_area /
        inputs.characteristic_length;
    if (!IsFiniteScalar(candidate.hardening_ratio) ||
        candidate.hardening_ratio < 0.0 ||
        !IsFiniteScalar(candidate.post_onset_work_density) ||
        candidate.post_onset_work_density <= 0.0)
    {
        return Error::NUMERIC_RANGE;
    }

    const double stiffness_factor =
        1.0 + candidate.hardening_ratio;
    const double capacity_factor =
        2.0 / stiffness_factor;
    candidate.damage_driver_capacity_density =
        capacity_factor * candidate.post_onset_work_density;
    if (!IsFiniteScalar(stiffness_factor) ||
        stiffness_factor <= 0.0 ||
        !IsFiniteScalar(capacity_factor) ||
        capacity_factor <= 0.0 ||
        !IsFiniteScalar(
            candidate.damage_driver_capacity_density) ||
        candidate.damage_driver_capacity_density <= 0.0)
    {
        return Error::NUMERIC_RANGE;
    }

    output = candidate;
    return Error::NONE;
}

} // namespace CalibratedBeamFractureCalibration
} // namespace RoR
