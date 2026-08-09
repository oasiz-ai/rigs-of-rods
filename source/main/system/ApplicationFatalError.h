/*
    This source file is part of Rigs of Rods
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <exception>

namespace RoR
{

/// A process-fatal game error which must cross in-process recovery boundaries.
///
/// The reason must have static storage duration. Keeping only a pointer makes
/// the exception object itself allocation-free even when the process is
/// already handling a fatal resource or network failure.
class ApplicationFatalError final : public std::exception
{
public:
    ApplicationFatalError(int exit_code, const char* reason) noexcept:
        m_exit_code(exit_code),
        m_reason(reason != nullptr ? reason : "fatal application error")
    {
    }

    int exit_code() const noexcept
    {
        return m_exit_code;
    }

    const char* what() const noexcept override
    {
        return m_reason;
    }

private:
    int m_exit_code;
    const char* m_reason;
};

enum class ApplicationFatalShutdownDisposition
{
    RETURN_FROM_MAIN,
    FAIL_STOP
};

/// Runs one shutdown operation at most once and retains its proven result.
/// The opaque context makes active-scene, partial-scene, and join-failure
/// behavior independently injectable without linking the game runtime.
class ApplicationFatalShutdownGate final
{
public:
    using ReleaseFunction = bool (*)(void*) noexcept;

    ApplicationFatalShutdownGate(
        ReleaseFunction release_function,
        void* context = nullptr) noexcept:
        m_release_function(release_function),
        m_context(context)
    {
    }

    bool Release() noexcept
    {
        if (!m_attempted)
        {
            m_attempted = true;
            m_succeeded =
                m_release_function != nullptr &&
                m_release_function(m_context);
        }
        return m_succeeded;
    }

    bool attempted() const noexcept
    {
        return m_attempted;
    }

private:
    ReleaseFunction m_release_function;
    void* m_context;
    bool m_attempted = false;
    bool m_succeeded = false;
};

/// Converts an idempotent runtime-release proof into the only two legal scope
/// exit outcomes. Guard destructors must fail-stop on FAIL_STOP before a later
/// renderer guard is allowed to unwind.
inline ApplicationFatalShutdownDisposition
ResolveApplicationRuntimeShutdownGate(
    ApplicationFatalShutdownGate& release_gate) noexcept
{
    return release_gate.Release()
        ? ApplicationFatalShutdownDisposition::RETURN_FROM_MAIN
        : ApplicationFatalShutdownDisposition::FAIL_STOP;
}

template <typename CaptureStep,
          typename PresentationStep,
          typename WorkerStep,
          typename SceneStep>
ApplicationFatalShutdownDisposition RunApplicationFatalShutdownSequence(
    CaptureStep&& capture_step,
    PresentationStep&& presentation_step,
    WorkerStep&& worker_step,
    SceneStep&& scene_step) noexcept
{
    try
    {
        if (!capture_step() ||
            !presentation_step() ||
            !worker_step() ||
            !scene_step())
        {
            return ApplicationFatalShutdownDisposition::FAIL_STOP;
        }
    }
    catch (...)
    {
        return ApplicationFatalShutdownDisposition::FAIL_STOP;
    }
    return ApplicationFatalShutdownDisposition::RETURN_FROM_MAIN;
}

} // namespace RoR
