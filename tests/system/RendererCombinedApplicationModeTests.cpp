/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererCombinedApplicationMode.h"

#include <cstdlib>
#include <cstring>
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

void TestOrdinaryArgumentsArePointerExact()
{
    char executable[] = "RoR-Combined";
    char map_option[] = "-map";
    char map_name[] = "CityWorld.terrn2";
    char* arguments[] = {executable, map_option, map_name, nullptr};
    RoR::RendererCombinedApplicationArguments resolved =
        RoR::ResolveRendererCombinedApplicationArguments(3, arguments);

    Require(resolved.ok(), "ordinary argument vector was rejected");
    Require(!resolved.native_visual_showcase,
            "ordinary invocation selected native showcase");
    Require(resolved.argc() == 3, "ordinary argc changed");
    Require(resolved.argv()[0] == executable &&
                resolved.argv()[1] == map_option &&
                resolved.argv()[2] == map_name &&
                resolved.argv()[3] == nullptr,
            "ordinary arguments were copied, reordered, or unterminated");
}

void TestPrivateOptionIsConsumedAtAnyPosition()
{
    char executable[] = "RoR-Combined";
    char map_option[] = "-map";
    char showcase[] = "--native-visual-showcase";
    char map_name[] = "Example.terrn2";
    char* arguments[] = {
        executable, map_option, showcase, map_name, nullptr};
    RoR::RendererCombinedApplicationArguments resolved =
        RoR::ResolveRendererCombinedApplicationArguments(4, arguments);

    Require(resolved.ok(), "native showcase argument vector was rejected");
    Require(resolved.native_visual_showcase,
            "native showcase option was not selected");
    Require(resolved.argc() == 3, "private option was not consumed");
    Require(resolved.argv()[0] == executable &&
                resolved.argv()[1] == map_option &&
                resolved.argv()[2] == map_name &&
                resolved.argv()[3] == nullptr,
            "forwarded arguments lost pointer identity or ordering");

    char* showcase_only[] = {executable, showcase, nullptr};
    resolved =
        RoR::ResolveRendererCombinedApplicationArguments(2, showcase_only);
    Require(resolved.ok() && resolved.native_visual_showcase &&
                resolved.argc() == 1 && resolved.argv()[0] == executable,
            "showcase-only invocation did not become an ordinary argc=1 vector");
}

void TestNearMatchesRemainOrdinaryArguments()
{
    char executable[] = "RoR-Combined";
    char prefix[] = "--native-visual-showcase-extra";
    char different_case[] = "--Native-visual-showcase";
    char* arguments[] = {executable, prefix, different_case, nullptr};
    RoR::RendererCombinedApplicationArguments resolved =
        RoR::ResolveRendererCombinedApplicationArguments(3, arguments);

    Require(resolved.ok() && !resolved.native_visual_showcase &&
                resolved.argc() == 3 && resolved.argv()[1] == prefix &&
                resolved.argv()[2] == different_case,
            "non-exact showcase option was consumed");
}

void TestDuplicateAndInvalidVectorsFailClosed()
{
    char executable[] = "RoR-Combined";
    char first[] = "--native-visual-showcase";
    char second[] = "--native-visual-showcase";
    char* duplicate[] = {executable, first, second, nullptr};
    const RoR::RendererCombinedApplicationArguments duplicate_result =
        RoR::ResolveRendererCombinedApplicationArguments(3, duplicate);
    Require(!duplicate_result.ok() &&
                duplicate_result.status ==
                    RoR::RendererCombinedApplicationArgumentsStatus::
                        DUPLICATE_NATIVE_VISUAL_SHOWCASE_OPTION,
            "duplicate private option did not fail closed");

    char* null_member[] = {executable, nullptr};
    Require(!RoR::ResolveRendererCombinedApplicationArguments(
                 2, null_member).ok(),
            "null argument member was accepted");
    Require(!RoR::ResolveRendererCombinedApplicationArguments(
                 0, nullptr).ok(),
            "missing process argument vector was accepted");
}

} // namespace

int main()
{
    TestOrdinaryArgumentsArePointerExact();
    TestPrivateOptionIsConsumedAtAnyPosition();
    TestNearMatchesRemainOrdinaryArguments();
    TestDuplicateAndInvalidVectorsFailClosed();
    return EXIT_SUCCESS;
}
