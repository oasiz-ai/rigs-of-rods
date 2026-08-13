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

enum class RendererCombinedApplicationArgumentsStatus : std::uint8_t {
    READY = 0U,
    INVALID_ARGUMENT_VECTOR,
    DUPLICATE_NATIVE_VISUAL_SHOWCASE_OPTION,
    OUT_OF_MEMORY,
};

struct RendererCombinedApplicationArguments {
    RendererCombinedApplicationArgumentsStatus status =
        RendererCombinedApplicationArgumentsStatus::INVALID_ARGUMENT_VECTOR;
    bool native_visual_showcase = false;
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
};

/// Consumes the private showcase option before Console sees the ordinary RoR
/// command line. Every other caller-owned pointer is retained in exact order.
/// Duplicate showcase options are rejected instead of being silently folded.
[[nodiscard]] RendererCombinedApplicationArguments
ResolveRendererCombinedApplicationArguments(
    int argc,
    char* const argv[]) noexcept;

[[nodiscard]] const char* ToString(
    RendererCombinedApplicationArgumentsStatus status) noexcept;

} // namespace RoR
