/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "LocalLightBudget.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "local-light budget test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::size_t Select(
    const std::vector<RoR::LocalLightCandidate>& candidates,
    const RoR::LocalLightPosition& camera,
    std::size_t budget,
    std::vector<std::uint8_t>* selected)
{
    selected->assign(candidates.size(), 0);
    std::vector<RoR::LocalLightRank> scratch(candidates.size());
    return RoR::SelectLocalLights(
        candidates.data(),
        candidates.size(),
        camera,
        budget,
        selected->data(),
        scratch.data());
}

void TestLockedPolicyAndSparseScene()
{
    Require(
        RoR::LOCAL_LIGHT_ACTIVE_BUDGET == 64u,
        "the cross-renderer scene budget changed unexpectedly");

    const std::vector<RoR::LocalLightCandidate> candidates = {
        {{10.0, 2.0, 0.0}, 0},
        {{20.0, 3.0, 0.0}, 1},
        {{30.0, 4.0, 0.0}, 2},
    };
    std::vector<std::uint8_t> selected;
    Require(
        Select(
            candidates,
            {1000.0, 1000.0, 1000.0},
            RoR::LOCAL_LIGHT_ACTIVE_BUDGET,
            &selected) == candidates.size(),
        "a sparse scene must retain every valid light");
    Require(
        selected == std::vector<std::uint8_t>({1, 1, 1}),
        "a sparse scene changed legacy light visibility");
}

void TestNearestBudget()
{
    std::vector<RoR::LocalLightCandidate> candidates;
    for (std::uint64_t index = 0; index < 70; ++index)
    {
        candidates.push_back(
            {{static_cast<double>(index), 0.0, 0.0}, index});
    }

    std::vector<std::uint8_t> selected;
    Require(
        Select(
            candidates,
            {0.0, 0.0, 0.0},
            RoR::LOCAL_LIGHT_ACTIVE_BUDGET,
            &selected) == RoR::LOCAL_LIGHT_ACTIVE_BUDGET,
        "an over-budget scene did not enforce the fixed cap");
    for (std::size_t index = 0; index < selected.size(); ++index)
    {
        Require(
            selected[index] == (index < RoR::LOCAL_LIGHT_ACTIVE_BUDGET),
            "the nearest candidates were not selected");
    }
}

void TestStableTieBreak()
{
    const std::vector<RoR::LocalLightCandidate> candidates = {
        {{1.0, 0.0, 0.0}, 30},
        {{-1.0, 0.0, 0.0}, 10},
        {{0.0, 1.0, 0.0}, 20},
        {{0.0, -1.0, 0.0}, 40},
    };
    std::vector<std::uint8_t> selected;
    Require(
        Select(candidates, {0.0, 0.0, 0.0}, 2, &selected) == 2,
        "equal-distance candidates did not fill the budget");
    Require(
        selected == std::vector<std::uint8_t>({0, 1, 1, 0}),
        "equal distances did not use stable registration IDs");

    std::vector<std::uint8_t> repeated;
    Select(candidates, {0.0, 0.0, 0.0}, 2, &repeated);
    Require(
        repeated == selected,
        "repeated selection changed an equal-distance result");
}

void TestMalformedCoordinatesFailClosed()
{
    const double nan = DoubleFromBits(UINT64_C(0x7ff8000000000001));
    const double infinity =
        DoubleFromBits(UINT64_C(0x7ff0000000000000));
    const std::vector<RoR::LocalLightCandidate> candidates = {
        {{1.0, 0.0, 0.0}, 0},
        {{nan, 0.0, 0.0}, 1},
        {{0.0, infinity, 0.0}, 2},
    };
    Require(
        !RoR::IsFiniteLocalLightValue(nan),
        "NaN bit inspection failed");
    Require(
        !RoR::IsFiniteLocalLightValue(infinity),
        "infinity bit inspection failed");
    Require(
        !RoR::IsFiniteLocalLightPosition(candidates[1].position),
        "NaN position bit inspection failed");

    std::vector<std::uint8_t> selected;
    const std::size_t malformed_active =
        Select(candidates, {0.0, 0.0, 0.0}, 3, &selected);
    Require(
        malformed_active == 1,
        "malformed candidate positions counted as active");
    Require(
        selected == std::vector<std::uint8_t>({1, 0, 0}),
        "malformed candidate positions did not stay hidden");

    Require(
        Select(candidates, {nan, 0.0, 0.0}, 3, &selected) == 0,
        "a malformed camera position did not fail closed");
    Require(
        selected == std::vector<std::uint8_t>({0, 0, 0}),
        "a malformed camera left local lights active");
}

void TestOverflowAndZeroBudgetFailClosed()
{
    const double maximum_finite =
        DoubleFromBits(UINT64_C(0x7fefffffffffffff));
    const std::vector<RoR::LocalLightCandidate> candidates = {
        {{0.0, 0.0, 0.0}, 0},
        {{maximum_finite, maximum_finite, maximum_finite}, 1},
    };

    std::vector<std::uint8_t> selected;
    Require(
        Select(
            candidates,
            {-maximum_finite, 0.0, 0.0},
            2,
            &selected) == 0,
        "overflowing distance arithmetic did not fail closed");
    Require(
        selected == std::vector<std::uint8_t>({0, 0}),
        "overflowing distance arithmetic left a light active");

    Require(
        Select(candidates, {0.0, 0.0, 0.0}, 0, &selected) == 0,
        "a zero budget selected a light");
    Require(
        selected == std::vector<std::uint8_t>({0, 0}),
        "a zero budget left a light active");
}

void TestMalformedStorageFailsClosed()
{
    const std::vector<RoR::LocalLightCandidate> candidates = {
        {{0.0, 0.0, 0.0}, 0},
        {{1.0, 0.0, 0.0}, 1},
    };
    std::vector<RoR::LocalLightRank> scratch(candidates.size());
    std::vector<std::uint8_t> selected(candidates.size(), 1);

    Require(
        RoR::SelectLocalLights(
            nullptr,
            candidates.size(),
            {0.0, 0.0, 0.0},
            candidates.size(),
            selected.data(),
            scratch.data()) == 0,
        "missing candidate storage did not fail closed");
    Require(
        selected == std::vector<std::uint8_t>({0, 0}),
        "missing candidate storage left a light active");

    selected.assign(candidates.size(), 1);
    Require(
        RoR::SelectLocalLights(
            candidates.data(),
            candidates.size(),
            {0.0, 0.0, 0.0},
            candidates.size(),
            selected.data(),
            nullptr) == 0,
        "missing scratch storage did not fail closed");
    Require(
        selected == std::vector<std::uint8_t>({0, 0}),
        "missing scratch storage left a light active");
}

} // namespace

int main()
{
    TestLockedPolicyAndSparseScene();
    TestNearestBudget();
    TestStableTieBreak();
    TestMalformedCoordinatesFailClosed();
    TestOverflowAndZeroBudgetFailClosed();
    TestMalformedStorageFailsClosed();
    std::cout << "cross-platform local-light budget verified\n";
    return EXIT_SUCCESS;
}
