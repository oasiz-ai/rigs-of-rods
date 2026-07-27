/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#pragma once

#include "MacOSControllerContract.h"

#include <SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace RoR {

class MacOSControllerBackend
{
public:
    static constexpr const char* STANDARD_MAPPING_PROFILE =
        "SDL_GameController_v1";

    enum class UpdateKind
    {
        IGNORED,
        DEVICE_ADDED,
        DEVICE_REMOVED,
        DEVICE_REMAPPED,
        DEVICE_ERROR,
        STATE_CHANGED
    };

    struct Update
    {
        UpdateKind kind = UpdateKind::IGNORED;
        std::size_t slot = MacOSControllerContract::Registry::MAX_DEVICES;
        MacOSControllerContract::EventType component_type =
            MacOSControllerContract::EventType::AXIS;
        std::size_t component = 0;
    };

    MacOSControllerBackend() = default;
    ~MacOSControllerBackend();

    MacOSControllerBackend(const MacOSControllerBackend&) = delete;
    MacOSControllerBackend& operator=(const MacOSControllerBackend&) = delete;

    bool Initialize(
        const std::string& gamecontroller_mapping_file = std::string());
    void Shutdown();

    Update ProcessEvent(const SDL_Event& event);
    void ResetStates();
    void RefreshStates();

    bool IsReady() const { return m_ready; }
    bool IsConnected(std::size_t slot) const;
    std::size_t ConnectedCount() const;
    std::size_t SlotLimit() const;
    const MacOSControllerContract::Slot* GetSlot(std::size_t slot) const;
    const std::string& GetVendor(std::size_t slot) const;
    const std::string& GetMappingProfile(std::size_t slot) const;
    bool IsStandardGameController(std::size_t slot) const;
    const std::string& GetLastError() const { return m_last_error; }

    static bool IsControllerEvent(Uint32 event_type);

private:
    struct Device
    {
        // SDL_GameController owns and closes its underlying joystick. The
        // joystick pointer is borrowed whenever game_controller is non-null.
        SDL_Joystick* joystick = nullptr;
        SDL_GameController* game_controller = nullptr;
        bool standardized = false;
        std::string vendor;
        std::string mapping_profile;
    };

    bool OpenDevice(int device_index, std::size_t& slot);
    bool CloseDevice(std::int32_t instance_id, std::size_t& slot);
    void CloseHandle(Device& device);
    void PopulateCurrentState(std::size_t slot);
    void SetLastSDLError(const char* operation);

    bool m_ready = false;
    bool m_owns_gamecontroller_subsystem = false;
    MacOSControllerContract::Registry m_registry;
    std::array<
        Device,
        MacOSControllerContract::Registry::MAX_DEVICES> m_devices = {};
    std::string m_last_error;
};

} // namespace RoR
