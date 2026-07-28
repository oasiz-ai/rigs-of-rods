/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Renderer-independent policy for replacing unusable explicit shaders.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace RoR
{

struct ExplicitGpuProgramState
{
    bool is_bound;
    bool is_available;
    bool is_supported;
    bool has_compile_error;
};

struct ScriptArchiveState
{
    std::size_t matching_script_count;
    bool is_package_owned;
};

struct ExplicitGraphicsProgramBindings
{
    bool is_programmable;
    bool has_vertex;
    bool has_fragment;
    bool has_geometry;
    bool has_mesh;
    bool has_compute;
};

struct ShaderTechniqueCompatibility
{
    std::string scheme_name;
    bool is_compatible;
};

/// ResourceGroupListener identifies scripts only by filename. Resolve repeated
/// filenames using the same ordered archive occurrences OGRE uses while
/// parsing, so shared engine scripts and appended package archives retain
/// distinct ownership.
inline bool IsPackageOwnedScriptOccurrence(
    const std::vector<ScriptArchiveState>& archive_states,
    std::size_t wanted_occurrence)
{
    std::size_t matching_occurrence = 0;
    for (const ScriptArchiveState& archive_state : archive_states)
    {
        if (archive_state.matching_script_count >
            wanted_occurrence - matching_occurrence)
        {
            return archive_state.is_package_owned;
        }
        matching_occurrence += archive_state.matching_script_count;
    }
    return false;
}

/// An authored pass needs a generated replacement only when it explicitly
/// binds a program which the active renderer cannot execute. Fixed-function
/// passes remain untouched so OGRE can translate them through RTShaderSystem
/// in the normal way.
inline bool NeedsGeneratedShaderFallback(
    const ExplicitGpuProgramState& state)
{
    return state.is_bound &&
        (!state.is_available ||
         !state.is_supported ||
         state.has_compile_error);
}

/// Programmable-only renderers reject a graphics pass unless it provides a
/// complete vertex/mesh-to-fragment pipeline. Geometry is an optional
/// pre-rasterization stage, not a replacement for fragment shading. A package
/// can therefore be blank even when every stage it did bind compiled
/// successfully. Preserve genuinely fixed-function and compute-only passes;
/// RTShaderSystem can replace only the incomplete graphics pipeline.
inline bool NeedsGeneratedShaderFallbackForIncompletePipeline(
    bool renderer_requires_complete_graphics_pipeline,
    const ExplicitGraphicsProgramBindings& bindings)
{
    return renderer_requires_complete_graphics_pipeline &&
        bindings.is_programmable &&
        !bindings.has_compute &&
        ((!bindings.has_vertex && !bindings.has_mesh) ||
         !bindings.has_fragment);
}

/// An alternate technique can suppress repair only within the same material
/// scheme. OGRE's RTShader resolver generates its active viewport technique
/// from the Default scheme; a compatible shadow/depth/custom scheme therefore
/// cannot make an incompatible Default source usable.
inline bool HasCompatibleShaderTechniqueForScheme(
    const std::vector<ShaderTechniqueCompatibility>& techniques,
    const std::string& scheme_name)
{
    for (const ShaderTechniqueCompatibility& technique : techniques)
    {
        if (technique.scheme_name == scheme_name &&
            technique.is_compatible)
        {
            return true;
        }
    }
    return false;
}

/// Do not replace a renderer-specific technique when the same material scheme
/// already provides another technique the active renderer can execute.
inline bool ShouldRepairIncompatibleShaderPass(
    bool scheme_has_compatible_technique,
    bool pass_has_incompatible_program)
{
    return !scheme_has_compatible_technique &&
        pass_has_incompatible_program;
}

} // namespace RoR
