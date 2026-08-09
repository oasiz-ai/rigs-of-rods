/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "FlexMeshTopology.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "flex mesh topology test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class FakeTopologySource final : public RoR::IFlexMeshTopologySource
{
public:
    std::vector<RoR::FlexMeshTopologySourceSection> descriptions;
    std::vector<std::vector<std::uint32_t>> backing_indices;
    mutable std::size_t read_calls = 0U;
    bool throw_from_description = false;
    bool throw_from_read = false;

    std::size_t SectionCount() const noexcept override
    {
        return descriptions.size();
    }

    bool DescribeSection(
        std::size_t section_index,
        RoR::FlexMeshTopologySourceSection& description) const override
    {
        if (throw_from_description)
            throw std::runtime_error("injected description failure");
        if (section_index >= descriptions.size())
            return false;
        description = descriptions[section_index];
        return true;
    }

    bool ReadShadowedIndices(
        std::size_t section_index,
        std::size_t index_start,
        std::size_t index_count,
        RoR::FlexMeshTopologySection::IndexFormat,
        std::vector<std::uint32_t>& indices) const override
    {
        ++read_calls;
        if (throw_from_read)
            throw std::runtime_error("forbidden/private read trap");
        if (section_index >= backing_indices.size() ||
            index_start > backing_indices[section_index].size() ||
            index_count >
                backing_indices[section_index].size() - index_start)
        {
            return false;
        }
        indices.assign(
            backing_indices[section_index].begin() + index_start,
            backing_indices[section_index].begin() +
                index_start + index_count);
        return true;
    }
};

RoR::FlexMeshTopologySourceSection Section(
    std::size_t start,
    std::size_t count,
    bool shadowed = true)
{
    RoR::FlexMeshTopologySourceSection section;
    section.index_format =
        RoR::FlexMeshTopologySection::IndexFormat::UINT16;
    section.vertex_count = 4U;
    section.buffer_index_count = 8U;
    section.index_start = start;
    section.index_count = count;
    section.has_cpu_shadow = shadowed;
    return section;
}

void TestUnshadowedPrivateSourceIsNeverRead()
{
    FakeTopologySource source;
    source.descriptions.push_back(Section(1U, 3U, false));
    source.backing_indices.push_back({99U, 0U, 1U, 2U});
    source.throw_from_read = true;
    std::vector<RoR::FlexMeshTopologySection> output(1U);
    output.front().vertex_count = 77U;

    Require(!RoR::CaptureFlexMeshCpuTopology(source, output),
            "unshadowed source was accepted");
    Require(source.read_calls == 0U,
            "unshadowed/private source received a forbidden read call");
    Require(output.size() == 1U && output.front().vertex_count == 77U,
            "unshadowed failure mutated caller output");
}

void TestSharedBackingRangesUseExactNonzeroStarts()
{
    const std::vector<std::uint32_t> shared{
        99U, 0U, 1U, 2U, 2U, 3U, 0U, 98U};
    FakeTopologySource source;
    source.descriptions = {Section(1U, 3U), Section(4U, 3U)};
    source.backing_indices = {shared, shared};
    std::vector<RoR::FlexMeshTopologySection> output;

    Require(RoR::CaptureFlexMeshCpuTopology(source, output),
            "valid shared backing ranges were rejected");
    Require(source.read_calls == 2U && output.size() == 2U,
            "shared ranges were not captured exactly once each");
    Require(output[0U].indices ==
                std::vector<std::uint32_t>({0U, 1U, 2U}) &&
            output[1U].indices ==
                std::vector<std::uint32_t>({2U, 3U, 0U}),
            "nonzero indexStart slices copied unrelated backing bytes");
}

void TestHostileRangeAndExceptionLeaveOutputUntouched()
{
    FakeTopologySource source;
    source.descriptions.push_back(Section(7U, 3U));
    source.backing_indices.push_back(std::vector<std::uint32_t>(8U, 0U));
    std::vector<RoR::FlexMeshTopologySection> output(1U);
    output.front().revision = 91U;
    Require(!RoR::CaptureFlexMeshCpuTopology(source, output) &&
                source.read_calls == 0U && output.front().revision == 91U,
            "out-of-range capture read or mutated state");

    source.descriptions.front() = Section(1U, 3U);
    source.throw_from_description = true;
    Require(!RoR::CaptureFlexMeshCpuTopology(source, output) &&
                output.front().revision == 91U,
            "description exception escaped or mutated output");

    source.throw_from_description = false;
    source.descriptions.front() = Section(0U, 3U);
    source.descriptions.front().buffer_index_count =
        (std::numeric_limits<std::size_t>::max)();
    source.read_calls = 0U;
    Require(!RoR::CaptureFlexMeshCpuTopology(source, output) &&
                source.read_calls == 0U &&
                output.front().revision == 91U,
            "overflowing byte range reached the source or mutated output");
}

void TestCpuRemapIsTransactional()
{
    RoR::FlexMeshTopologySection source;
    source.index_format =
        RoR::FlexMeshTopologySection::IndexFormat::UINT16;
    source.revision = 4U;
    source.vertex_count = 4U;
    source.indices = {0U, 1U, 2U, 2U, 3U, 0U};
    RoR::FlexMeshTopologySection output;
    Require(RoR::RemapFlexMeshCpuTopology(
                source, {2U, 0U, 3U, 1U}, output) &&
                output.revision == 5U &&
                output.indices ==
                    std::vector<std::uint32_t>(
                        {2U, 0U, 3U, 3U, 1U, 2U}),
            "valid CPU index permutation was not preserved");

    const RoR::FlexMeshTopologySection accepted = output;
    Require(!RoR::RemapFlexMeshCpuTopology(
                source, {0U, 0U, 2U, 3U}, output) &&
                output.indices == accepted.indices &&
                output.revision == accepted.revision,
            "non-permutation remap changed accepted output");
}

void TestInstallationPreparesEverythingBeforeCommit()
{
    struct Prepared
    {
        std::size_t section = 0U;
    };
    std::size_t prepared_count = 0U;
    std::size_t committed_count = 0U;
    const bool failed =
        RoR::InstallFlexMeshCpuTopologyTransaction<Prepared>(
            3U,
            [&prepared_count](std::size_t section, Prepared& prepared)
            {
                ++prepared_count;
                prepared.section = section;
                return section != 1U; // injected upload failure
            },
            [&committed_count](std::size_t, Prepared&) noexcept
            {
                ++committed_count;
            });
    Require(!failed && prepared_count == 2U && committed_count == 0U,
            "failed preparation partially committed native ranges");

    prepared_count = 0U;
    committed_count = 0U;
    const bool threw =
        RoR::InstallFlexMeshCpuTopologyTransaction<Prepared>(
            3U,
            [&prepared_count](std::size_t section, Prepared& prepared)
                -> bool
            {
                ++prepared_count;
                prepared.section = section;
                if (section == 1U)
                    throw std::bad_alloc();
                return true;
            },
            [&committed_count](std::size_t, Prepared&) noexcept
            {
                ++committed_count;
            });
    Require(!threw && prepared_count == 2U && committed_count == 0U,
            "allocation exception partially committed native ranges");

    prepared_count = 0U;
    committed_count = 0U;
    const bool succeeded =
        RoR::InstallFlexMeshCpuTopologyTransaction<Prepared>(
            3U,
            [&prepared_count](std::size_t section, Prepared& prepared)
            {
                ++prepared_count;
                prepared.section = section;
                return true;
            },
            [&committed_count](std::size_t section,
                               Prepared& prepared) noexcept
            {
                if (prepared.section == section)
                    ++committed_count;
            });
    Require(succeeded && prepared_count == 3U && committed_count == 3U,
            "complete preparation did not commit every native range");
}

} // namespace

int main()
{
    TestUnshadowedPrivateSourceIsNeverRead();
    TestSharedBackingRangesUseExactNonzeroStarts();
    TestHostileRangeAndExceptionLeaveOutputUntouched();
    TestCpuRemapIsTransactional();
    TestInstallationPreparesEverythingBeforeCommit();
    return EXIT_SUCCESS;
}
