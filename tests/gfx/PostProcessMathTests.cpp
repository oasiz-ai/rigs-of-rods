/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PostProcessMath.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "V0 post-process math test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void RequireNear(
    double actual,
    double expected,
    double tolerance,
    const char* message)
{
    Require(RoR::PostProcessDetail::IsFinite(actual), message);
    Require(RoR::PostProcessDetail::IsFinite(expected), message);
    Require(RoR::PostProcessDetail::IsFinite(tolerance), message);
    Require(
        RoR::PostProcessDetail::Abs(actual - expected) <= tolerance,
        message);
}

std::uint64_t NextRandom(std::uint64_t* state)
{
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return *state * UINT64_C(2685821657736338717);
}

double UnitRandom(std::uint64_t* state)
{
    return static_cast<double>(NextRandom(state) >> 11) *
        (1.0 / 9007199254740992.0);
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void RequireNormalized(
    const RoR::PostProcessColor& color,
    const char* message)
{
    Require(RoR::PostProcessDetail::IsNormalizedColor(color), message);
}

void TestLockedProfile()
{
    const RoR::PostProcessConfig config;
    Require(RoR::IsValidPostProcessConfig(config),
        "the locked high-quality profile must be valid");
    RequireNear(config.exposure, 1.08, 0.0,
        "exposure profile changed unexpectedly");
    RequireNear(config.contrast, 1.04, 0.0,
        "contrast profile changed unexpectedly");
    RequireNear(config.saturation, 1.03, 0.0,
        "saturation profile changed unexpectedly");
    RequireNear(config.bloom_threshold, 0.72, 0.0,
        "bloom threshold changed unexpectedly");
    RequireNear(config.bloom_soft_knee, 0.18, 0.0,
        "bloom knee changed unexpectedly");
    RequireNear(config.bloom_strength, 0.08, 0.0,
        "bloom strength changed unexpectedly");
    RequireNear(config.fxaa_edge_threshold, 1.0 / 8.0, 0.0,
        "FXAA relative threshold changed unexpectedly");
    RequireNear(config.fxaa_edge_threshold_min, 1.0 / 24.0, 0.0,
        "FXAA absolute threshold changed unexpectedly");
    RequireNear(config.fxaa_blend_limit, 0.75, 0.0,
        "FXAA blend limit changed unexpectedly");
}

void TestColorCurve()
{
    const RoR::PostProcessConfig config;
    RoR::PostProcessColor output = {9.0, 8.0, 7.0};
    Require(RoR::ApplyPostProcessColorCurve(
        {0.0, 0.0, 0.0}, config, &output),
        "black must be accepted");
    RequireNear(output.red, 0.0, 0.0, "black red must remain zero");
    RequireNear(output.green, 0.0, 0.0, "black green must remain zero");
    RequireNear(output.blue, 0.0, 0.0, "black blue must remain zero");

    Require(RoR::ApplyPostProcessColorCurve(
        {0.5, 0.5, 0.5}, config, &output),
        "neutral gray must be accepted");
    RequireNear(output.red, output.green, 1e-15,
        "neutral gray must not gain a tint");
    RequireNear(output.green, output.blue, 1e-15,
        "neutral gray must not gain a tint");
    Require(output.red > 0.5 && output.red < 0.65,
        "the fixed exposure curve must gently raise middle gray");

    Require(RoR::ApplyPostProcessColorCurve(
        {1.0, 1.0, 1.0}, config, &output),
        "white must be accepted");
    RequireNormalized(output, "white curve result must remain normalized");
    RequireNear(output.red, 1.0, 0.0,
        "the highlight shoulder must retain a bounded white point");

    Require(RoR::ApplyPostProcessColorCurve(
        {0.8, 0.2, 0.1}, config, &output),
        "colored sample must be accepted");
    Require(output.red > output.green && output.green > output.blue,
        "the color curve must preserve channel ordering");
}

void TestBloom()
{
    const RoR::PostProcessConfig config;
    RoR::PostProcessColor extracted = {4.0, 4.0, 4.0};
    Require(RoR::ExtractPostProcessBloom(
        {0.2, 0.2, 0.2}, config, &extracted),
        "dark sample must be accepted by bloom extraction");
    RequireNear(extracted.red, 0.0, 0.0,
        "dark samples must not bloom");
    RequireNear(extracted.green, 0.0, 0.0,
        "dark samples must not bloom");
    RequireNear(extracted.blue, 0.0, 0.0,
        "dark samples must not bloom");

    RoR::PostProcessColor at_threshold;
    Require(RoR::ExtractPostProcessBloom(
        {config.bloom_threshold, config.bloom_threshold,
         config.bloom_threshold},
        config,
        &at_threshold),
        "threshold sample must be accepted");
    Require(at_threshold.red > 0.0,
        "the soft knee must begin below the hard threshold");

    RoR::PostProcessColor bright;
    Require(RoR::ExtractPostProcessBloom(
        {1.0, 0.5, 0.25}, config, &bright),
        "bright sample must be accepted");
    RequireNormalized(bright, "bright extraction must remain normalized");
    Require(bright.red > at_threshold.red,
        "brighter samples must produce more bloom");
    RequireNear(bright.green / bright.red, 0.5, 1e-15,
        "bloom extraction must preserve hue");
    RequireNear(bright.blue / bright.red, 0.25, 1e-15,
        "bloom extraction must preserve hue");

    RoR::PostProcessColor without_bloom;
    RoR::PostProcessColor with_bloom;
    Require(RoR::ComposePostProcessBloom(
        {0.4, 0.4, 0.4}, {0.0, 0.0, 0.0}, config, &without_bloom),
        "zero-bloom composition must be accepted");
    Require(RoR::ComposePostProcessBloom(
        {0.4, 0.4, 0.4}, {1.0, 1.0, 1.0}, config, &with_bloom),
        "bounded bloom composition must be accepted");
    Require(with_bloom.red > without_bloom.red,
        "bloom must raise scene luminance");
    RequireNormalized(with_bloom, "bloom composition must remain normalized");
}

void TestFxaa()
{
    const RoR::PostProcessConfig config;
    RoR::FxaaNeighborhood flat;
    flat.center = {0.4, 0.4, 0.4};
    flat.north = flat.center;
    flat.south = flat.center;
    flat.east = flat.center;
    flat.west = flat.center;

    RoR::PostProcessColor output;
    Require(RoR::ResolvePostProcessFxaa(flat, config, &output),
        "flat neighborhood must be accepted");
    RequireNear(output.red, flat.center.red, 0.0,
        "flat red must remain pixel-identical");
    RequireNear(output.green, flat.center.green, 0.0,
        "flat green must remain pixel-identical");
    RequireNear(output.blue, flat.center.blue, 0.0,
        "flat blue must remain pixel-identical");

    RoR::FxaaNeighborhood vertical_edge;
    vertical_edge.center = {1.0, 1.0, 1.0};
    vertical_edge.north = vertical_edge.center;
    vertical_edge.south = vertical_edge.center;
    vertical_edge.east = vertical_edge.center;
    vertical_edge.west = {0.0, 0.0, 0.0};
    Require(RoR::ResolvePostProcessFxaa(
        vertical_edge, config, &output),
        "vertical edge must be accepted");
    Require(output.red < 1.0 && output.red > 0.5,
        "vertical edge must be blended but not over-blurred");
    RequireNear(output.red, output.green, 0.0,
        "neutral edge must remain neutral");
    RequireNear(output.green, output.blue, 0.0,
        "neutral edge must remain neutral");

    RoR::FxaaNeighborhood low_contrast = flat;
    low_contrast.west = {0.39, 0.39, 0.39};
    low_contrast.east = {0.41, 0.41, 0.41};
    Require(RoR::ResolvePostProcessFxaa(
        low_contrast, config, &output),
        "low-contrast neighborhood must be accepted");
    RequireNear(output.red, flat.center.red, 0.0,
        "sub-threshold detail must remain unchanged");
}

void TestMalformedInputs()
{
    const double nan = DoubleFromBits(UINT64_C(0x7ff8000000000001));
    const double infinity =
        DoubleFromBits(UINT64_C(0x7ff0000000000000));
    RoR::PostProcessConfig config;
    RoR::PostProcessColor sentinel = {9.0, 8.0, 7.0};
    RoR::PostProcessColor output = sentinel;

    Require(!RoR::ApplyPostProcessColorCurve(
        {nan, 0.0, 0.0}, config, &output),
        "NaN color must fail closed");
    Require(std::memcmp(&output, &sentinel, sizeof(output)) == 0,
        "failed color curve must be transactional");
    Require(!RoR::ExtractPostProcessBloom(
        {0.0, infinity, 0.0}, config, &output),
        "infinite color must fail closed");
    Require(std::memcmp(&output, &sentinel, sizeof(output)) == 0,
        "failed bloom extraction must be transactional");
    Require(!RoR::ApplyPostProcessColorCurve(
        {-0.01, 0.0, 0.0}, config, &output),
        "negative normalized input must fail closed");
    Require(!RoR::ApplyPostProcessColorCurve(
        {1.01, 0.0, 0.0}, config, &output),
        "out-of-range normalized input must fail closed");
    Require(!RoR::ApplyPostProcessColorCurve(
        {0.0, 0.0, 0.0}, config, nullptr),
        "null color output must fail closed");

    config.exposure = nan;
    Require(!RoR::IsValidPostProcessConfig(config),
        "NaN exposure must invalidate the profile");
    Require(!RoR::ComposePostProcessBloom(
        {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, config, &output),
        "invalid bloom profile must fail closed");

    config = RoR::PostProcessConfig();
    config.bloom_soft_knee = 0.0;
    Require(!RoR::IsValidPostProcessConfig(config),
        "zero bloom knee must be rejected");
    config = RoR::PostProcessConfig();
    config.fxaa_blend_limit = 0.9;
    Require(!RoR::IsValidPostProcessConfig(config),
        "excessive FXAA blending must be rejected");

    RoR::FxaaNeighborhood samples;
    samples.center = {0.5, 0.5, 0.5};
    samples.north = samples.center;
    samples.south = samples.center;
    samples.east = samples.center;
    samples.west = {nan, 0.5, 0.5};
    config = RoR::PostProcessConfig();
    Require(!RoR::ResolvePostProcessFxaa(samples, config, &output),
        "malformed FXAA neighborhood must fail closed");
    Require(std::memcmp(&output, &sentinel, sizeof(output)) == 0,
        "failed FXAA resolution must be transactional");
}

void TestFixedSeedProperties()
{
    const RoR::PostProcessConfig config;
    std::uint64_t random = UINT64_C(0x7e57a11ce5eed123);
    for (std::uint32_t index = 0; index < 50000; ++index)
    {
        const RoR::PostProcessColor scene = {
            UnitRandom(&random),
            UnitRandom(&random),
            UnitRandom(&random)};
        const RoR::PostProcessColor blur = {
            UnitRandom(&random),
            UnitRandom(&random),
            UnitRandom(&random)};
        RoR::PostProcessColor bloom;
        RoR::PostProcessColor composed;
        Require(RoR::ExtractPostProcessBloom(scene, config, &bloom),
            "fixed-seed bloom extraction must remain valid");
        Require(RoR::ComposePostProcessBloom(
            scene, blur, config, &composed),
            "fixed-seed bloom composition must remain valid");
        RequireNormalized(bloom,
            "fixed-seed bloom extraction must remain normalized");
        RequireNormalized(composed,
            "fixed-seed bloom composition must remain normalized");

        RoR::FxaaNeighborhood neighborhood;
        neighborhood.center = scene;
        neighborhood.north = {
            UnitRandom(&random),
            UnitRandom(&random),
            UnitRandom(&random)};
        neighborhood.south = {
            UnitRandom(&random),
            UnitRandom(&random),
            UnitRandom(&random)};
        neighborhood.east = {
            UnitRandom(&random),
            UnitRandom(&random),
            UnitRandom(&random)};
        neighborhood.west = {
            UnitRandom(&random),
            UnitRandom(&random),
            UnitRandom(&random)};
        RoR::PostProcessColor resolved;
        Require(RoR::ResolvePostProcessFxaa(
            neighborhood, config, &resolved),
            "fixed-seed FXAA resolution must remain valid");
        RequireNormalized(resolved,
            "fixed-seed FXAA result must remain normalized");

        RoR::PostProcessColor repeated;
        Require(RoR::ResolvePostProcessFxaa(
            neighborhood, config, &repeated),
            "repeated FXAA resolution must remain valid");
        Require(std::memcmp(
            &resolved, &repeated, sizeof(resolved)) == 0,
            "repeated FXAA resolution must be byte-identical");
    }
}

} // namespace

int main()
{
    TestLockedProfile();
    TestColorCurve();
    TestBloom();
    TestFxaa();
    TestMalformedInputs();
    TestFixedSeedProperties();
    std::cout << "V0 post-process math contract verified\n";
    return EXIT_SUCCESS;
}
