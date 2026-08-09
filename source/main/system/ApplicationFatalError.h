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

} // namespace RoR
