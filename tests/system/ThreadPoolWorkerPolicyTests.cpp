/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ThreadPoolWorkerPolicy.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* detail)
{
    if (!condition)
    {
        std::cerr << detail << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void TestExplicitBoundedWorkerCountsAreHonored()
{
    Require(
        RoR::ResolveThreadPoolWorkerCount(1, 4) == 1,
        "explicit single-worker setting changed");
    Require(
        RoR::ResolveThreadPoolWorkerCount(4, 4) == 4,
        "explicit hardware-sized worker setting changed");
    Require(
        RoR::ResolveThreadPoolWorkerCount(8, 4) == 8,
        "explicit eight-worker CI oversubscription was clamped");
    Require(
        RoR::ResolveThreadPoolWorkerCount(8, 1) == 8,
        "explicit eight-worker setting depended on hardware concurrency");
}

void TestAutomaticAndInvalidSettingsUseHardwareSizing()
{
    Require(
        RoR::ResolveThreadPoolWorkerCount(0, 0) == 1,
        "unknown hardware concurrency did not retain one worker");
    Require(
        RoR::ResolveThreadPoolWorkerCount(-1, 1) == 1,
        "single-core automatic sizing did not retain one worker");
    Require(
        RoR::ResolveThreadPoolWorkerCount(0, 4) == 3,
        "four-core automatic sizing did not reserve the main thread");
    Require(
        RoR::ResolveThreadPoolWorkerCount(0, 64) == 8,
        "automatic sizing exceeded the eight-worker cap");
    Require(
        RoR::ResolveThreadPoolWorkerCount(9, 4) == 3,
        "out-of-range setting bypassed automatic sizing");
}

} // namespace

int main()
{
    TestExplicitBoundedWorkerCountsAreHonored();
    TestAutomaticAndInvalidSettingsUseHardwareSizing();
    return EXIT_SUCCESS;
}
