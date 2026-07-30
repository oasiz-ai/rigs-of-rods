/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PostProcessRuntimeContract.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr
            << "post-process runtime contract test failed: "
            << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

RoR::PostProcessLifecycleEvent Event(
    RoR::PostProcessLifecycleEventType type,
    std::uint32_t width = 0U,
    std::uint32_t height = 0U,
    bool enabled = false)
{
    RoR::PostProcessLifecycleEvent event;
    event.type = type;
    event.effective_mode_enabled = enabled;
    event.backing_width = width;
    event.backing_height = height;
    return event;
}

RoR::PostProcessLifecycleTransition Step(
    const RoR::PostProcessLifecycleState& state,
    const RoR::PostProcessLifecycleEvent& event,
    RoR::PostProcessLifecycleStage expected_stage,
    RoR::PostProcessLifecycleAction expected_action,
    const char* message)
{
    const RoR::PostProcessLifecycleTransition transition =
        RoR::ResolvePostProcessLifecycleTransition(state, event);
    Require(transition.next.stage == expected_stage, message);
    Require(transition.action == expected_action, message);
    return transition;
}

void TestRendererClassification()
{
    using RoR::PostProcessBackend;

    Require(
        RoR::ClassifyPostProcessBackend(
            "OpenGL 3+ Rendering Subsystem",
            true,
            false,
            false) == PostProcessBackend::GL3PLUS_GLSL,
        "canonical GL3Plus renderer was rejected");
    Require(
        RoR::ClassifyPostProcessBackend(
            "  OPENGL   3+ Rendering Subsystem\t",
            true,
            true,
            true) == PostProcessBackend::GL3PLUS_GLSL,
        "safe GL3Plus name normalization changed");
    Require(
        RoR::ClassifyPostProcessBackend(
            "Direct3D11 Rendering Subsystem",
            false,
            true,
            true) == PostProcessBackend::D3D11_HLSL,
        "canonical D3D11 renderer was rejected");

    Require(
        RoR::ClassifyPostProcessBackend(
            "OpenGL 3+ Rendering Subsystem",
            false,
            true,
            true) == PostProcessBackend::UNSUPPORTED,
        "GL3Plus enabled without GLSL 330");
    Require(
        RoR::ClassifyPostProcessBackend(
            "Direct3D11 Rendering Subsystem",
            true,
            true,
            false) == PostProcessBackend::UNSUPPORTED,
        "D3D11 enabled without both SM4 stages");

    const char* rejected_names[] = {
        "",
        "OpenGL Rendering Subsystem",
        "OpenGL 3+ Rendering Subsystem compatibility",
        "Direct3D11x Rendering Subsystem",
        "prefix Direct3D11 Rendering Subsystem",
        "Metal Rendering Subsystem",
        "Vulkan Rendering Subsystem",
    };
    for (const char* name : rejected_names)
    {
        Require(
            RoR::ClassifyPostProcessBackend(
                name,
                true,
                true,
                true) == PostProcessBackend::UNSUPPORTED,
            "unsupported renderer name was accepted");
    }
}

void TestDefaultOffAndCompleteSceneLifecycle()
{
    using RoR::PostProcessLifecycleAction;
    using RoR::PostProcessLifecycleEventType;
    using RoR::PostProcessLifecycleStage;

    RoR::PostProcessLifecycleState state;
    RoR::PostProcessLifecycleTransition transition = Step(
        state,
        Event(
            PostProcessLifecycleEventType::SCENE_READY,
            1920U,
            1080U,
            false),
        PostProcessLifecycleStage::BYPASSED,
        PostProcessLifecycleAction::NONE,
        "default-off scene did not bypass attachment");
    state = transition.next;

    transition = Step(
        state,
        Event(
            PostProcessLifecycleEventType::VIEWPORT_RESIZED,
            2560U,
            1440U),
        PostProcessLifecycleStage::BYPASSED,
        PostProcessLifecycleAction::NONE,
        "disabled resize acquired renderer state");
    state = transition.next;
    Require(
        state.backing_width == 2560U &&
            state.backing_height == 1440U,
        "disabled resize did not retain backing extent");

    transition = Step(
        state,
        Event(PostProcessLifecycleEventType::SCENE_END),
        PostProcessLifecycleStage::INACTIVE,
        PostProcessLifecycleAction::NONE,
        "disabled scene end attempted a detach");
    Require(
        transition.next.backing_width == 0U &&
            transition.next.backing_height == 0U,
        "scene end retained stale backing extent");
}

void TestEnabledResizeReadbackAndUnloadLifecycle()
{
    using RoR::PostProcessLifecycleAction;
    using RoR::PostProcessLifecycleEventType;
    using RoR::PostProcessLifecycleStage;

    RoR::PostProcessLifecycleState state;
    RoR::PostProcessLifecycleTransition transition = Step(
        state,
        Event(
            PostProcessLifecycleEventType::SCENE_READY,
            1920U,
            1080U,
            true),
        PostProcessLifecycleStage::ATTACHED,
        PostProcessLifecycleAction::ATTACH,
        "enabled scene did not attach");
    state = transition.next;

    transition = Step(
        state,
        Event(PostProcessLifecycleEventType::MAIN_WINDOW_READBACK),
        PostProcessLifecycleStage::ATTACHED,
        PostProcessLifecycleAction::VERIFY_ATTACHED_LAST,
        "screenshot did not preserve and verify the compositor");

    transition = Step(
        state,
        Event(
            PostProcessLifecycleEventType::
                SCENE_COMPOSITOR_CHAIN_CHANGED),
        PostProcessLifecycleStage::ATTACHED,
        PostProcessLifecycleAction::RECREATE,
        "changed scene compositor chain did not re-append V0A");

    transition = Step(
        state,
        Event(
            PostProcessLifecycleEventType::VIEWPORT_RESIZED,
            1920U,
            1080U),
        PostProcessLifecycleStage::ATTACHED,
        PostProcessLifecycleAction::NONE,
        "same backing extent recreated resources");

    transition = Step(
        state,
        Event(
            PostProcessLifecycleEventType::VIEWPORT_RESIZED,
            2880U,
            1800U),
        PostProcessLifecycleStage::ATTACHED,
        PostProcessLifecycleAction::RECREATE,
        "changed backing extent did not recreate resources");
    state = transition.next;

    transition = Step(
        state,
        Event(
            PostProcessLifecycleEventType::VIEWPORT_RESIZED,
            0U,
            1800U),
        PostProcessLifecycleStage::SUSPENDED_ZERO_EXTENT,
        PostProcessLifecycleAction::DETACH,
        "zero backing extent did not suspend and detach");
    state = transition.next;

    transition = Step(
        state,
        Event(PostProcessLifecycleEventType::MAIN_WINDOW_READBACK),
        PostProcessLifecycleStage::SUSPENDED_ZERO_EXTENT,
        PostProcessLifecycleAction::NONE,
        "suspended readback touched renderer state");

    transition = Step(
        state,
        Event(
            PostProcessLifecycleEventType::VIEWPORT_RESIZED,
            1280U,
            720U),
        PostProcessLifecycleStage::ATTACHED,
        PostProcessLifecycleAction::ATTACH,
        "nonzero backing extent did not resume attachment");
    state = transition.next;

    transition = Step(
        state,
        Event(PostProcessLifecycleEventType::SCENE_END),
        PostProcessLifecycleStage::INACTIVE,
        PostProcessLifecycleAction::DETACH,
        "scene end did not detach before teardown");
}

void TestAdapterFailureIsStickyWithinScene()
{
    using RoR::PostProcessLifecycleAction;
    using RoR::PostProcessLifecycleEventType;
    using RoR::PostProcessLifecycleStage;

    RoR::PostProcessLifecycleState state;
    state = Step(
        state,
        Event(
            PostProcessLifecycleEventType::SCENE_READY,
            1920U,
            1080U,
            true),
        PostProcessLifecycleStage::ATTACHED,
        PostProcessLifecycleAction::ATTACH,
        "failure fixture did not begin attached").next;

    state = Step(
        state,
        Event(PostProcessLifecycleEventType::ADAPTER_FAILED),
        PostProcessLifecycleStage::FAILED,
        PostProcessLifecycleAction::DETACH,
        "adapter failure did not fail closed").next;

    state = Step(
        state,
        Event(
            PostProcessLifecycleEventType::VIEWPORT_RESIZED,
            2560U,
            1440U),
        PostProcessLifecycleStage::FAILED,
        PostProcessLifecycleAction::NONE,
        "failed scene retried on resize").next;
    Step(
        state,
        Event(
            PostProcessLifecycleEventType::
                SCENE_COMPOSITOR_CHAIN_CHANGED),
        PostProcessLifecycleStage::FAILED,
        PostProcessLifecycleAction::NONE,
        "failed scene retried after a compositor-chain change");
    Step(
        state,
        Event(PostProcessLifecycleEventType::MAIN_WINDOW_READBACK),
        PostProcessLifecycleStage::FAILED,
        PostProcessLifecycleAction::NONE,
        "failed scene retried on readback");

    Step(
        state,
        Event(PostProcessLifecycleEventType::SHUTDOWN),
        PostProcessLifecycleStage::INACTIVE,
        PostProcessLifecycleAction::NONE,
        "failed scene shutdown did not become inactive");
}

void TestUnknownInputsFailClosed()
{
    using RoR::PostProcessLifecycleAction;
    using RoR::PostProcessLifecycleEventType;
    using RoR::PostProcessLifecycleStage;

    RoR::PostProcessLifecycleState attached;
    attached.stage = PostProcessLifecycleStage::ATTACHED;
    attached.backing_width = 1U;
    attached.backing_height = 1U;

    Step(
        attached,
        Event(static_cast<PostProcessLifecycleEventType>(
            std::numeric_limits<std::uint8_t>::max())),
        PostProcessLifecycleStage::FAILED,
        PostProcessLifecycleAction::DETACH,
        "unknown lifecycle event did not fail closed");

    RoR::PostProcessLifecycleState unknown = attached;
    unknown.stage = static_cast<PostProcessLifecycleStage>(
        std::numeric_limits<std::uint8_t>::max());
    Step(
        unknown,
        Event(PostProcessLifecycleEventType::VIEWPORT_RESIZED, 2U, 2U),
        PostProcessLifecycleStage::FAILED,
        PostProcessLifecycleAction::DETACH,
        "unknown lifecycle stage did not request conservative detach");
}

void TestOneLineDiagnosticBound()
{
    const std::string normalized =
        RoR::BoundPostProcessDiagnosticDetail(
            " first\n\tsecond\r\nthird\x01 fourth ");
    Require(
        normalized == "first second third fourth",
        "diagnostic control characters were not normalized");

    const std::string long_detail(300U, 'x');
    const std::string bounded =
        RoR::BoundPostProcessDiagnosticDetail(long_detail, 16U);
    Require(bounded.size() == 16U, "diagnostic bound changed");
    Require(
        bounded.substr(13U) == "...",
        "bounded diagnostic did not mark truncation");
    Require(
        RoR::BoundPostProcessDiagnosticDetail("abcdef", 3U) == "abc",
        "small diagnostic bound overflowed");
    Require(
        RoR::BoundPostProcessDiagnosticDetail("abcdef", 0U).empty(),
        "zero diagnostic bound emitted text");
}

} // namespace

int main()
{
    TestRendererClassification();
    TestDefaultOffAndCompleteSceneLifecycle();
    TestEnabledResizeReadbackAndUnloadLifecycle();
    TestAdapterFailureIsStickyWithinScene();
    TestUnknownInputsFailClosed();
    TestOneLineDiagnosticBound();

    std::cout
        << "cross-platform V0A post-process runtime contract verified\n";
    return EXIT_SUCCESS;
}
