/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Opt-in owner of one live native world-model capture episode.

#pragma once

#include <memory>
#include <string>

namespace RoR {
namespace WorldModel {

class LiveCaptureController
{
public:
    LiveCaptureController();
    ~LiveCaptureController();

    LiveCaptureController(const LiveCaptureController&) = delete;
    LiveCaptureController& operator=(const LiveCaptureController&) = delete;

    /// Arms/waits/aborts according to the explicit capture CVar. Called at a
    /// joined main-loop boundary after queued game messages are handled.
    void UpdateRequestedState();

    bool IsActive() const;
    bool OwnsSimulationLoop() const;

    /// Polling occurs in main immediately before this call. Exactly one
    /// transition is captured and durably appended per controlled frame.
    bool CaptureControlledFrame();

    /// Aborts the open sink and preserves only the `.partial` evidence.
    void Abort(const std::string& reason);
    void Shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace WorldModel
} // namespace RoR
