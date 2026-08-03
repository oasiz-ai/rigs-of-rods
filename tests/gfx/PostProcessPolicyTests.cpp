/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PostProcessPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "post-process policy test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

RoR::PostProcessPolicyInput MakeInput(
    RoR::PostProcessMode requested_mode,
    RoR::PostProcessBackend backend,
    bool program_available,
    std::uint32_t viewport_width,
    std::uint32_t viewport_height)
{
    RoR::PostProcessPolicyInput input;
    input.requested_mode = requested_mode;
    input.backend = backend;
    input.program_available = program_available;
    input.viewport_width = viewport_width;
    input.viewport_height = viewport_height;
    return input;
}

void RequireDecision(
    const RoR::PostProcessPolicyInput& input,
    RoR::PostProcessMode effective_mode,
    RoR::PostProcessPolicyStatus status,
    const char* message)
{
    const RoR::PostProcessPolicyResult result =
        RoR::ResolvePostProcessPolicy(input);
    Require(result.requested_mode == input.requested_mode, message);
    Require(result.effective_mode == effective_mode, message);
    Require(result.status == status, message);
}

void TestClassifiers()
{
    using RoR::PostProcessBackend;
    using RoR::PostProcessMode;

    Require(RoR::IsKnownPostProcessMode(PostProcessMode::NONE),
        "NONE was not recognized");
    Require(RoR::IsKnownPostProcessMode(PostProcessMode::V0A_LDR_FXAA),
        "V0A_LDR_FXAA was not recognized");
    Require(!RoR::IsKnownPostProcessMode(
        static_cast<PostProcessMode>(-1)),
        "negative mode was recognized");
    Require(!RoR::IsKnownPostProcessMode(
        static_cast<PostProcessMode>(2)),
        "future mode was recognized");

    const unsigned int maximum_backend =
        std::numeric_limits<std::uint8_t>::max();
    for (unsigned int value = 0; value <= maximum_backend; ++value)
    {
        const PostProcessBackend backend =
            static_cast<PostProcessBackend>(value);
        const bool expected =
            backend == PostProcessBackend::GL3PLUS_GLSL ||
            backend == PostProcessBackend::D3D11_HLSL;
        Require(
            RoR::IsSupportedPostProcessBackend(backend) == expected,
            "backend classifier accepted an unknown enum value");
    }
}

void TestNoneIsUnconditional()
{
    using RoR::PostProcessBackend;
    using RoR::PostProcessMode;
    using RoR::PostProcessPolicyStatus;

    const bool program_states[] = {false, true};
    const std::uint32_t extents[] = {
        0,
        1,
        std::numeric_limits<std::uint32_t>::max(),
    };

    const unsigned int maximum_backend =
        std::numeric_limits<std::uint8_t>::max();
    for (unsigned int value = 0; value <= maximum_backend; ++value)
    {
        const PostProcessBackend backend =
            static_cast<PostProcessBackend>(value);
        for (const bool program_available : program_states)
        {
            for (const std::uint32_t width : extents)
            {
                for (const std::uint32_t height : extents)
                {
                    RequireDecision(
                        MakeInput(
                            PostProcessMode::NONE,
                            backend,
                            program_available,
                            width,
                            height),
                        PostProcessMode::NONE,
                        PostProcessPolicyStatus::REQUESTED_NONE,
                        "NONE depended on a runtime prerequisite");
                }
            }
        }
    }
}

RoR::PostProcessPolicyStatus ExpectedV0AStatus(
    RoR::PostProcessBackend backend,
    bool program_available,
    std::uint32_t viewport_width,
    std::uint32_t viewport_height)
{
    if (!RoR::IsSupportedPostProcessBackend(backend))
    {
        return RoR::PostProcessPolicyStatus::UNSUPPORTED_BACKEND;
    }
    if (!program_available)
    {
        return RoR::PostProcessPolicyStatus::PROGRAM_UNAVAILABLE;
    }
    if (viewport_width == 0 || viewport_height == 0)
    {
        return RoR::PostProcessPolicyStatus::ZERO_VIEWPORT;
    }
    return RoR::PostProcessPolicyStatus::ENABLED;
}

void TestV0ATruthTable()
{
    using RoR::PostProcessBackend;
    using RoR::PostProcessMode;
    using RoR::PostProcessPolicyStatus;

    const bool program_states[] = {false, true};
    const std::uint32_t widths[] = {0, 1920};
    const std::uint32_t heights[] = {0, 1080};

    const unsigned int maximum_backend =
        std::numeric_limits<std::uint8_t>::max();
    for (unsigned int value = 0; value <= maximum_backend; ++value)
    {
        const PostProcessBackend backend =
            static_cast<PostProcessBackend>(value);
        for (const bool program_available : program_states)
        {
            for (const std::uint32_t width : widths)
            {
                for (const std::uint32_t height : heights)
                {
                    const PostProcessPolicyStatus expected_status =
                        ExpectedV0AStatus(
                            backend,
                            program_available,
                            width,
                            height);
                    const PostProcessMode expected_mode =
                        expected_status == PostProcessPolicyStatus::ENABLED
                        ? PostProcessMode::V0A_LDR_FXAA
                        : PostProcessMode::NONE;
                    RequireDecision(
                        MakeInput(
                            PostProcessMode::V0A_LDR_FXAA,
                            backend,
                            program_available,
                            width,
                            height),
                        expected_mode,
                        expected_status,
                        "V0A prerequisite truth table changed");
                }
            }
        }
    }
}

void TestInvalidModesFailClosedFirst()
{
    using RoR::PostProcessBackend;
    using RoR::PostProcessMode;
    using RoR::PostProcessPolicyStatus;

    const PostProcessMode invalid_modes[] = {
        static_cast<PostProcessMode>(-1),
        static_cast<PostProcessMode>(2),
        static_cast<PostProcessMode>(
            std::numeric_limits<std::int32_t>::min()),
        static_cast<PostProcessMode>(
            std::numeric_limits<std::int32_t>::max()),
    };
    const PostProcessBackend backends[] = {
        PostProcessBackend::UNSUPPORTED,
        PostProcessBackend::GL3PLUS_GLSL,
        PostProcessBackend::D3D11_HLSL,
        static_cast<PostProcessBackend>(255),
    };
    const bool program_states[] = {false, true};
    const std::uint32_t extents[] = {0, 1};

    for (const PostProcessMode invalid_mode : invalid_modes)
    {
        for (const PostProcessBackend backend : backends)
        {
            for (const bool program_available : program_states)
            {
                for (const std::uint32_t width : extents)
                {
                    for (const std::uint32_t height : extents)
                    {
                        RequireDecision(
                            MakeInput(
                                invalid_mode,
                                backend,
                                program_available,
                                width,
                                height),
                            PostProcessMode::NONE,
                            PostProcessPolicyStatus::INVALID_MODE,
                            "invalid mode did not fail closed first");
                    }
                }
            }
        }
    }
}

} // namespace

int main()
{
    TestClassifiers();
    TestNoneIsUnconditional();
    TestV0ATruthTable();
    TestInvalidModesFailClosedFirst();

    std::cout
        << "cross-platform V0A post-process policy verified\n";
    return EXIT_SUCCESS;
}
