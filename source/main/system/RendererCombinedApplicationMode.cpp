/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererCombinedApplicationMode.h"

#include <cstring>
#include <limits>
#include <new>

namespace RoR {

RendererCombinedApplicationArguments
ResolveRendererCombinedApplicationArguments(
    int argc,
    char* const argv[]) noexcept
{
    RendererCombinedApplicationArguments result;
    if (argc < 1 || argv == nullptr || argv[0] == nullptr)
    {
        return result;
    }
    if (static_cast<unsigned long long>(argc) >=
        static_cast<unsigned long long>(
            (std::numeric_limits<std::size_t>::max)()))
    {
        return result;
    }

    try
    {
        result.forwarded_arguments.reserve(
            static_cast<std::size_t>(argc) + 1U);
        result.forwarded_arguments.push_back(argv[0]);
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] == nullptr)
            {
                result.forwarded_arguments.clear();
                return result;
            }
            const bool selects_current_showcase = std::strcmp(
                argv[index],
                kRendererCombinedNativeVisualShowcaseOption) == 0;
            const bool selects_a0_showcase = std::strcmp(
                argv[index],
                kRendererCombinedNativeVisualShowcaseA0Option) == 0;
            if (selects_current_showcase || selects_a0_showcase)
            {
                if (result.native_visual_showcase())
                {
                    result.status =
                        RendererCombinedApplicationArgumentsStatus::
                            DUPLICATE_NATIVE_VISUAL_SHOWCASE_OPTION;
                    result.forwarded_arguments.clear();
                    return result;
                }
                result.native_visual_scene = selects_current_showcase
                    ? RendererCombinedNativeVisualScene::A1_NATIVE_COURSE
                    : RendererCombinedNativeVisualScene::A0_LIGHTING_COUPON;
                continue;
            }
            result.forwarded_arguments.push_back(argv[index]);
        }
        result.forwarded_arguments.push_back(nullptr);
        result.status = RendererCombinedApplicationArgumentsStatus::READY;
        return result;
    }
    catch (const std::bad_alloc&)
    {
        result.status =
            RendererCombinedApplicationArgumentsStatus::OUT_OF_MEMORY;
        result.forwarded_arguments.clear();
        return result;
    }
    catch (...)
    {
        result.status =
            RendererCombinedApplicationArgumentsStatus::INVALID_ARGUMENT_VECTOR;
        result.forwarded_arguments.clear();
        return result;
    }
}

const char* ToString(
    RendererCombinedApplicationArgumentsStatus status) noexcept
{
    switch (status)
    {
    case RendererCombinedApplicationArgumentsStatus::READY:
        return "READY";
    case RendererCombinedApplicationArgumentsStatus::INVALID_ARGUMENT_VECTOR:
        return "INVALID_ARGUMENT_VECTOR";
    case RendererCombinedApplicationArgumentsStatus::
        DUPLICATE_NATIVE_VISUAL_SHOWCASE_OPTION:
        return "DUPLICATE_NATIVE_VISUAL_SHOWCASE_OPTION";
    case RendererCombinedApplicationArgumentsStatus::OUT_OF_MEMORY:
        return "OUT_OF_MEMORY";
    }
    return "UNKNOWN";
}

} // namespace RoR
