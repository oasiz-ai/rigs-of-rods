/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Combined-runtime-only application argument selection.

#pragma once

#include <cstdint>
#include <vector>

namespace RoR {

constexpr char kRendererCombinedNativeVisualShowcaseOption[] =
    "--native-visual-showcase";
constexpr char kRendererCombinedNativeVisualShowcaseA0Option[] =
    "--native-visual-showcase-a0";

/// The unqualified private showcase option always selects the newest reviewed
/// project-owned native scene. The explicit A0 option remains available as a
/// stable regression coupon after newer course packages are admitted.
enum class RendererCombinedNativeVisualScene : std::uint8_t {
    JOINED_GAME = 0U,
    A0_LIGHTING_COUPON,
    A1_NATIVE_COURSE,
};

enum class RendererCombinedApplicationArgumentsStatus : std::uint8_t {
    READY = 0U,
    INVALID_ARGUMENT_VECTOR,
    DUPLICATE_NATIVE_VISUAL_SHOWCASE_OPTION,
    OUT_OF_MEMORY,
};

struct RendererCombinedApplicationArguments {
    RendererCombinedApplicationArgumentsStatus status =
        RendererCombinedApplicationArgumentsStatus::INVALID_ARGUMENT_VECTOR;
    RendererCombinedNativeVisualScene native_visual_scene =
        RendererCombinedNativeVisualScene::JOINED_GAME;
    std::vector<char*> forwarded_arguments;

    [[nodiscard]] bool ok() const noexcept {
        return status == RendererCombinedApplicationArgumentsStatus::READY &&
            !forwarded_arguments.empty() &&
            forwarded_arguments.back() == nullptr;
    }

    [[nodiscard]] int argc() const noexcept {
        return ok()
            ? static_cast<int>(forwarded_arguments.size() - 1U)
            : 0;
    }

    [[nodiscard]] char** argv() noexcept {
        return ok() ? forwarded_arguments.data() : nullptr;
    }

    [[nodiscard]] bool native_visual_showcase() const noexcept {
        return native_visual_scene !=
            RendererCombinedNativeVisualScene::JOINED_GAME;
    }
};

/// Consumes the private showcase option before Console sees the ordinary RoR
/// command line. Every other caller-owned pointer is retained in exact order.
/// The unqualified option selects A1; the explicit A0 option retains the
/// original lighting coupon. Any second private scene option is rejected
/// instead of being silently folded or allowed to override an earlier choice.
[[nodiscard]] RendererCombinedApplicationArguments
ResolveRendererCombinedApplicationArguments(
    int argc,
    char* const argv[]) noexcept;

[[nodiscard]] const char* ToString(
    RendererCombinedApplicationArgumentsStatus status) noexcept;

} // namespace RoR
