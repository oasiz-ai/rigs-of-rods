/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RenderDisplayMetrics.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

int Fail(const char* message)
{
    std::cerr << "render display metrics test failed: " << message << '\n';
    return EXIT_FAILURE;
}

bool Near(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) <= 1e-6f;
}

float FloatFromBits(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

int main()
{
    if (!Near(RoR::SanitizeRequestedContentScale(1.0f), 1.0f) ||
        !Near(RoR::SanitizeRequestedContentScale(2.0f), 2.0f) ||
        !Near(RoR::SanitizeRequestedContentScale(4.0f), 4.0f))
    {
        return Fail("valid requested scales changed");
    }
    if (!Near(RoR::SanitizeRequestedContentScale(0.0f), 1.0f) ||
        !Near(RoR::SanitizeRequestedContentScale(5.0f), 1.0f) ||
        !Near(
            RoR::SanitizeRequestedContentScale(
                FloatFromBits(0x7f800000u)),
            1.0f) ||
        !Near(
            RoR::SanitizeRequestedContentScale(
                FloatFromBits(0x7fc00000u)),
            1.0f))
    {
        return Fail("invalid requested scale did not fail safe to 1x");
    }
    if (RoR::ShouldRequestHighPixelDensity(1.0f) ||
        !RoR::ShouldRequestHighPixelDensity(1.25f) ||
        RoR::ShouldRequestHighPixelDensity(
            FloatFromBits(0x7fc00000u)))
    {
        return Fail("high-pixel-density selection changed");
    }
    if (RoR::LogicalExtentForBacking(1280, 2.0f) != 640 ||
        RoR::LogicalExtentForBacking(1279, 2.0f) != 640 ||
        RoR::LogicalExtentForBacking(0, 2.0f) != 0 ||
        RoR::LogicalExtentForBacking(1280, 0.0f) != 1280)
    {
        return Fail("logical extent conversion changed");
    }

    const RoR::RenderDisplayMetrics one_x =
        RoR::ResolveRenderDisplayMetrics(1280, 720, 1280, 720);
    if (!one_x.valid ||
        one_x.logical_width != 1280 ||
        one_x.logical_height != 720 ||
        !Near(one_x.framebuffer_scale_x, 1.0f) ||
        !Near(one_x.framebuffer_scale_y, 1.0f) ||
        !Near(one_x.GetFontRasterScale(), 1.0f))
    {
        return Fail("1x display contract changed");
    }

    const RoR::RenderDisplayMetrics two_x =
        RoR::ResolveRenderDisplayMetrics(1280, 720, 640, 360);
    if (!two_x.valid ||
        two_x.backing_width != 1280 ||
        two_x.backing_height != 720 ||
        !Near(two_x.framebuffer_scale_x, 2.0f) ||
        !Near(two_x.framebuffer_scale_y, 2.0f) ||
        !Near(two_x.GetFontRasterScale(), 2.0f))
    {
        return Fail("2x display contract changed");
    }

    const RoR::RenderDisplayMetrics fractional =
        RoR::ResolveRenderDisplayMetrics(1500, 1000, 1000, 800);
    if (!fractional.valid ||
        !Near(fractional.framebuffer_scale_x, 1.5f) ||
        !Near(fractional.framebuffer_scale_y, 1.25f) ||
        !Near(fractional.GetFontRasterScale(), 1.5f))
    {
        return Fail("fractional/non-uniform backend scale changed");
    }

    const RoR::RenderDisplayMetrics missing_host =
        RoR::ResolveRenderDisplayMetrics(1280, 720, 0, 0);
    if (missing_host.valid ||
        missing_host.logical_width != 1280 ||
        missing_host.logical_height != 720 ||
        !Near(missing_host.framebuffer_scale_x, 1.0f) ||
        !Near(missing_host.framebuffer_scale_y, 1.0f))
    {
        return Fail("missing host metrics did not fail safe to backing pixels");
    }

    const RoR::RenderDisplayMetrics hostile_ratio =
        RoR::ResolveRenderDisplayMetrics(8000, 8000, 100, 100);
    if (hostile_ratio.valid ||
        hostile_ratio.logical_width != 8000 ||
        hostile_ratio.logical_height != 8000 ||
        !Near(hostile_ratio.framebuffer_scale_x, 1.0f) ||
        !Near(hostile_ratio.framebuffer_scale_y, 1.0f))
    {
        return Fail("implausible backend ratio did not fail safe");
    }

    std::cout << "cross-platform render display metrics verified\n";
    return EXIT_SUCCESS;
}
