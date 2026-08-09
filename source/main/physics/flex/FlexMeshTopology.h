/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace RoR {

/// Immutable CPU-owned topology for one deformable material section. The
/// renderer bridge consumes this copy instead of trying to read a write-only
/// hardware index buffer back from the GPU.
struct FlexMeshTopologySection
{
    enum class IndexFormat : std::uint8_t
    {
        UINT16 = 0U,
        UINT32 = 1U,
    };

    IndexFormat index_format = IndexFormat::UINT32;
    std::uint64_t revision = 1U;
    std::uint32_t vertex_count = 0U;
    std::vector<std::uint32_t> indices;
};

/// Native range metadata copied without mapping or reading the underlying
/// buffer. The source explicitly reports whether a CPU shadow owns the bytes.
struct FlexMeshTopologySourceSection
{
    FlexMeshTopologySection::IndexFormat index_format =
        FlexMeshTopologySection::IndexFormat::UINT32;
    std::size_t vertex_count = 0U;
    std::size_t buffer_index_count = 0U;
    std::size_t index_start = 0U;
    std::size_t index_count = 0U;
    bool has_cpu_shadow = false;
};

/// Construction-time adapter used to retain imported flexbody topology before
/// the renderer bridge can observe it. Implementations must read only the CPU
/// shadow identified by DescribeSection(); unshadowed GPU/private resources
/// are deliberately rejected before ReadShadowedIndices() is invoked.
class IFlexMeshTopologySource
{
public:
    virtual ~IFlexMeshTopologySource() = default;

    [[nodiscard]] virtual std::size_t SectionCount() const noexcept = 0;
    [[nodiscard]] virtual bool DescribeSection(
        std::size_t section_index,
        FlexMeshTopologySourceSection& description) const = 0;
    [[nodiscard]] virtual bool ReadShadowedIndices(
        std::size_t section_index,
        std::size_t index_start,
        std::size_t index_count,
        FlexMeshTopologySection::IndexFormat index_format,
        std::vector<std::uint32_t>& indices) const = 0;
};

/// Captures every exact draw range transactionally. Failure leaves output
/// untouched and, critically, never asks an unshadowed source to read.
[[nodiscard]] inline bool CaptureFlexMeshCpuTopology(
    const IFlexMeshTopologySource& source,
    std::vector<FlexMeshTopologySection>& output) noexcept
{
    try
    {
        const std::size_t section_count = source.SectionCount();
        if (section_count == 0U ||
            section_count > (std::numeric_limits<std::uint32_t>::max)())
        {
            return false;
        }

        std::vector<FlexMeshTopologySection> candidate;
        candidate.reserve(section_count);
        for (std::size_t section_index = 0U;
             section_index < section_count; ++section_index)
        {
            FlexMeshTopologySourceSection description;
            if (!source.DescribeSection(section_index, description))
                return false;
            const std::size_t index_size =
                description.index_format ==
                        FlexMeshTopologySection::IndexFormat::UINT16
                ? sizeof(std::uint16_t)
                : description.index_format ==
                          FlexMeshTopologySection::IndexFormat::UINT32
                    ? sizeof(std::uint32_t)
                    : 0U;
            const std::size_t max_index_count = index_size == 0U
                ? 0U
                : (std::numeric_limits<std::size_t>::max)() / index_size;
            if (index_size == 0U ||
                description.vertex_count == 0U ||
                description.vertex_count >
                    (std::numeric_limits<std::uint32_t>::max)() ||
                description.index_count == 0U ||
                description.index_count % 3U != 0U ||
                description.index_count >
                    (std::numeric_limits<std::uint32_t>::max)() ||
                description.index_start > description.buffer_index_count ||
                description.index_count >
                    description.buffer_index_count - description.index_start ||
                description.buffer_index_count > max_index_count ||
                description.index_start > max_index_count ||
                description.index_count >
                    max_index_count - description.index_start ||
                !description.has_cpu_shadow)
            {
                return false;
            }

            FlexMeshTopologySection section;
            section.index_format = description.index_format;
            section.vertex_count =
                static_cast<std::uint32_t>(description.vertex_count);
            if (!source.ReadShadowedIndices(
                    section_index, description.index_start,
                    description.index_count, description.index_format,
                    section.indices) ||
                section.indices.size() != description.index_count ||
                std::any_of(
                    section.indices.begin(), section.indices.end(),
                    [&section](std::uint32_t index)
                    {
                        return index >= section.vertex_count ||
                            (section.index_format ==
                                 FlexMeshTopologySection::IndexFormat::UINT16 &&
                             index >
                                 (std::numeric_limits<std::uint16_t>::max)());
                    }))
            {
                return false;
            }
            candidate.push_back(std::move(section));
        }
        output = std::move(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

/// Applies an old-vertex to new-vertex permutation to one CPU topology owner.
/// The source and output may alias; failure leaves output untouched.
[[nodiscard]] inline bool RemapFlexMeshCpuTopology(
    const FlexMeshTopologySection& source,
    const std::vector<std::uint32_t>& old_to_new_vertex,
    FlexMeshTopologySection& output) noexcept
{
    try
    {
        if (source.revision == 0U ||
            source.revision == (std::numeric_limits<std::uint64_t>::max)() ||
            source.vertex_count == 0U ||
            old_to_new_vertex.size() != source.vertex_count ||
            source.indices.empty() || source.indices.size() % 3U != 0U)
        {
            return false;
        }

        std::vector<std::uint8_t> seen(source.vertex_count, 0U);
        for (std::uint32_t mapped : old_to_new_vertex)
        {
            if (mapped >= source.vertex_count || seen[mapped] != 0U)
                return false;
            seen[mapped] = 1U;
        }

        FlexMeshTopologySection candidate = source;
        candidate.revision = source.revision + 1U;
        for (std::uint32_t& index : candidate.indices)
        {
            if (index >= source.vertex_count)
                return false;
            index = old_to_new_vertex[index];
            if (candidate.index_format ==
                    FlexMeshTopologySection::IndexFormat::UINT16 &&
                index > (std::numeric_limits<std::uint16_t>::max)())
            {
                return false;
            }
        }
        output = std::move(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

/// Builds every native replacement before invoking the non-throwing commit
/// callback. This keeps a failed allocation/upload from rebinding even the
/// first section of a shared or multi-section mesh.
template <typename PreparedSection, typename Prepare, typename Commit>
[[nodiscard]] inline bool InstallFlexMeshCpuTopologyTransaction(
    std::size_t section_count,
    Prepare&& prepare,
    Commit&& commit) noexcept
{
    static_assert(
        noexcept(std::declval<Commit&>()(
            std::size_t{}, std::declval<PreparedSection&>())),
        "Flex topology commit must be non-throwing");
    try
    {
        if (section_count == 0U)
            return false;
        std::vector<PreparedSection> prepared;
        prepared.reserve(section_count);
        for (std::size_t section_index = 0U;
             section_index < section_count; ++section_index)
        {
            PreparedSection section;
            if (!prepare(section_index, section))
                return false;
            prepared.push_back(std::move(section));
        }
        for (std::size_t section_index = 0U;
             section_index < section_count; ++section_index)
        {
            commit(section_index, prepared[section_index]);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace RoR
