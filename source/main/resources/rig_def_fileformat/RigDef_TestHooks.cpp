/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Real Parser -> spawn preparation -> Serializer -> Parser test path.

#include "RigDef_TestHooks.h"

#include "RigDef_CalibratedBeamMaterial.h"
#include "RigDef_Parser.h"
#include "RigDef_Serializer.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool SameBits(double first, double second)
{
    return std::memcmp(&first, &second, sizeof(first)) == 0;
}

bool SameMaterial(
    const RigDef::CalibratedBeamMaterialDefaults& first,
    const RigDef::CalibratedBeamMaterialDefaults& second)
{
    return
        first.schema_version == second.schema_version &&
        first.enabled == second.enabled &&
        SameBits(
            first.cross_section_area_m2,
            second.cross_section_area_m2) &&
        SameBits(
            first.elastic_modulus_pa,
            second.elastic_modulus_pa) &&
        SameBits(
            first.yield_stress_pa,
            second.yield_stress_pa) &&
        SameBits(
            first.hardening_modulus_pa,
            second.hardening_modulus_pa) &&
        SameBits(
            first.damage_onset_plastic_strain,
            second.damage_onset_plastic_strain) &&
        SameBits(
            first.damage_driver_capacity_density_j_m3,
            second.damage_driver_capacity_density_j_m3) &&
        first._is_user_defined == second._is_user_defined;
}

bool ParseLines(
    std::istream& input,
    RigDef::DocumentPtr& document,
    std::string& error)
{
    RigDef::Parser parser;
    parser.Prepare();

    std::string line;
    while (std::getline(input, line))
        parser.ProcessRawLine(line.c_str());
    if (input.bad())
    {
        error = "failed while reading authored rig text";
        return false;
    }

    parser.Finalize();
    document = parser.GetFile();
    if (document == nullptr)
    {
        error = "production parser returned a null document";
        return false;
    }
    return true;
}

bool ValidateParsedFixture(
    const RigDef::DocumentPtr& document,
    std::string& error)
{
    if (document->name !=
        "P1 calibrated beam material round-trip fixture")
    {
        error = "actor name did not survive production parsing";
        return false;
    }
    if (document->root_module->nodes.size() != 4U)
    {
        error = "fixture did not produce four nodes";
        return false;
    }
    if (document->root_module->beams.size() != 3U)
    {
        error = "fixture did not produce three beams";
        return false;
    }

    // The directive follows the nodes. Copy-on-write parser state must keep
    // every earlier legacy snapshot disabled and inert.
    for (const RigDef::Node& node : document->root_module->nodes)
    {
        if (node.beam_defaults == nullptr ||
            node.beam_defaults->calibrated_material.enabled ||
            node.beam_defaults->calibrated_material._is_user_defined)
        {
            error =
                "authored material leaked backward into a legacy node "
                "defaults snapshot";
            return false;
        }
    }

    const RigDef::Beam& first = document->root_module->beams[0];
    const RigDef::Beam& rope = document->root_module->beams[1];
    const RigDef::Beam& third = document->root_module->beams[2];
    if (first.defaults == nullptr ||
        rope.defaults == nullptr ||
        third.defaults == nullptr)
    {
        error = "a parsed beam is missing its defaults snapshot";
        return false;
    }
    if (!first.defaults->calibrated_material.enabled ||
        !third.defaults->calibrated_material.enabled ||
        rope.defaults->calibrated_material.enabled ||
        !rope.defaults->calibrated_material._is_user_defined)
    {
        error = "on/off/on authored material state was not preserved";
        return false;
    }
    if ((rope.options & RigDef::Beam::OPTION_r_ROPE) == 0U)
    {
        error = "specialized rope role was not preserved";
        return false;
    }
    if (first.nodes[0].Str() != "0" ||
        first.nodes[1].Str() != "1" ||
        rope.nodes[0].Str() != "1" ||
        rope.nodes[1].Str() != "2" ||
        third.nodes[0].Str() != "2" ||
        third.nodes[1].Str() != "3")
    {
        error =
            "the parser did not reject the rope authored while calibrated "
            "material remained enabled";
        return false;
    }
    if (!SameMaterial(
            first.defaults->calibrated_material,
            third.defaults->calibrated_material))
    {
        error = "equivalent authored material snapshots differ";
        return false;
    }

    RoR::CalibratedBeamMaterialAdapter::Runtime runtime;
    RoR::CalibratedBeamMaterialAdapter::Error adapter_error =
        RoR::CalibratedBeamMaterialAdapter::Error::NONE;
    RoR::CalibratedBeamMaterial::Error material_error =
        RoR::CalibratedBeamMaterial::Error::NONE;
    if (!RigDef::TryPrepareCalibratedBeamMaterialForSpawn(
            first.defaults->calibrated_material,
            true,
            runtime,
            &adapter_error,
            &material_error) ||
        !runtime.enabled)
    {
        error = "production spawn preparation rejected a valid plain beam";
        return false;
    }

    RoR::CalibratedBeamMaterialAdapter::Runtime legacy_runtime;
    if (RigDef::TryPrepareCalibratedBeamMaterialForSpawn(
            rope.defaults->calibrated_material,
            false,
            legacy_runtime,
            &adapter_error,
            &material_error) ||
        adapter_error !=
            RoR::CalibratedBeamMaterialAdapter::Error::DISABLED ||
        legacy_runtime.enabled)
    {
        error =
            "explicit off did not preserve the specialized role's legacy "
            "spawn path";
        return false;
    }
    return true;
}

bool ValidateSerializedTransitions(
    const std::string& serialized,
    std::string& error)
{
    const std::string keyword = "set_calibrated_beam_material ";
    std::vector<std::string> transitions;
    std::istringstream lines(serialized);
    std::string line;
    while (std::getline(lines, line))
    {
        const std::size_t start = line.find(keyword);
        if (start != std::string::npos)
            transitions.push_back(line.substr(start + keyword.size()));
    }

    if (transitions.size() != 4U ||
        transitions[0].find("1, on, ") != 0U ||
        transitions[1] != "1, off" ||
        transitions[2].find("1, on, ") != 0U ||
        transitions[3] != "1, off")
    {
        error =
            "serializer did not emit the fail-closed on/off/on/off "
            "transition sequence";
        return false;
    }
    return true;
}

} // namespace

int RigDef::RunCalibratedBeamMaterialRoundTripIntegration(
    const std::string& fixture_path)
{
    std::ifstream fixture(fixture_path.c_str(), std::ios::binary);
    if (!fixture)
    {
        std::cerr << "RigDef integration: cannot open fixture '"
            << fixture_path << "'\n";
        return 1;
    }

    std::string error;
    RigDef::DocumentPtr authored;
    if (!ParseLines(fixture, authored, error) ||
        !ValidateParsedFixture(authored, error))
    {
        std::cerr << "RigDef integration: " << error << '\n';
        return 1;
    }

    RigDef::Serializer serializer(authored);
    serializer.Serialize();
    const std::string serialized = serializer.GetOutput();
    if (!ValidateSerializedTransitions(serialized, error))
    {
        std::cerr << "RigDef integration: " << error << '\n';
        return 1;
    }

    std::istringstream serialized_input(serialized);
    RigDef::DocumentPtr reparsed;
    if (!ParseLines(serialized_input, reparsed, error) ||
        !ValidateParsedFixture(reparsed, error))
    {
        std::cerr << "RigDef integration after serialization: "
            << error << '\n';
        return 1;
    }

    const RigDef::CalibratedBeamMaterialDefaults& authored_material =
        authored->root_module->beams[0].defaults->calibrated_material;
    const RigDef::CalibratedBeamMaterialDefaults& reparsed_material =
        reparsed->root_module->beams[0].defaults->calibrated_material;
    if (!SameMaterial(authored_material, reparsed_material))
    {
        std::cerr
            << "RigDef integration: binary64 authored material values "
               "changed during serialize/reparse\n";
        return 1;
    }

    std::cout
        << "RigDef calibrated beam authored round-trip integration passed\n";
    return 0;
}
