/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Validated Ogre-Next input to legacy InputEngine semantic adapter.

#pragma once

#include "RendererGameInputTarget.h"
#include "render/InputEventTransport.h"

#include <array>
#include <cstdint>
#include <vector>

namespace RoR {

constexpr std::uint32_t kRendererOgre14InputAdapterContractVersion = 1U;

enum class RendererOgre14InputApplyStatus : std::uint8_t {
  APPLIED = 0U,
  REJECTED_INVALID_MESSAGE,
  REJECTED_SEQUENCE,
  REJECTED_DEVICE_CAPACITY,
  FAILED_TARGET,
  FAILED_ALLOCATION,
};

struct RendererOgre14InputApplyResult final {
  std::uint32_t version = kRendererOgre14InputAdapterContractVersion;
  RendererOgre14InputApplyStatus status =
      RendererOgre14InputApplyStatus::REJECTED_INVALID_MESSAGE;
  Render::RenderTransportStatus transport_status =
      Render::RenderTransportStatus::INVALID_ARGUMENT;
  std::uint64_t reverse_sequence = 0U;
  std::uint64_t issued_first_event_id = 0U;
  std::uint64_t issued_last_event_id = 0U;
  std::uint64_t resolved_through_event_id = 0U;
  std::uint64_t applied_through_event_id = 0U;
  std::uint32_t ignored_unmapped_scancodes = 0U;
  bool accepted = false;

  [[nodiscard]] bool ok() const noexcept { return accepted; }
  explicit operator bool() const noexcept { return ok(); }
};

/// Converts SDL2 physical scan positions to the OIS/DirectInput key identity
/// used by existing RoR mappings. Keys which OIS cannot represent are returned
/// as KC_UNASSIGNED and remain available through TEXT_INPUT.
[[nodiscard]] RendererGameKey TranslateRendererSdl2ScancodeToGame(
    Render::Sdl2PhysicalScancode scancode) noexcept;
[[nodiscard]] RendererGameMouseButton
TranslateRendererSdl2MouseButtonToGame(
    Render::Sdl2MouseButton button) noexcept;

/// Main-thread adapter. The decoded-message entry point consumes only the
/// immutable object published by InputEventTransportDecoder. ApplyValidatedBatch
/// exists for focused tests and independently revalidates its public input.
class RendererOgre14InputAdapter final {
public:
  explicit RendererOgre14InputAdapter(
      IRendererGameInputTarget &target) noexcept;

  /// Make the renderer transport authoritative before the first gameplay
  /// frame. Idempotent so product startup and the first decoded batch share
  /// one activation edge.
  [[nodiscard]] bool ActivateTarget() noexcept;
  [[nodiscard]] RendererOgre14InputApplyResult Apply(
      const Render::DecodedInputEventTransportMessage &message);
  [[nodiscard]] RendererOgre14InputApplyResult ApplyValidatedBatch(
      std::uint64_t reverse_sequence,
      const Render::InputTransportBatch &batch);

  [[nodiscard]] std::uint64_t last_reverse_sequence() const noexcept {
    return last_reverse_sequence_;
  }
  [[nodiscard]] std::uint64_t applied_through_event_id() const noexcept {
    return applied_through_event_id_;
  }

private:
  struct DeviceSlot final {
    bool active = false;
    bool raw = false;
    std::uint64_t device_id = 0U;
    std::uint64_t generation = 0U;
    std::vector<std::int32_t> axes;
  };

  [[nodiscard]] RendererOgre14InputApplyResult ApplyKnownValidBatch(
      std::uint64_t reverse_sequence,
      const Render::InputTransportBatch &batch);

  IRendererGameInputTarget &target_;
  std::array<DeviceSlot, kRendererGameJoystickSlots> slots_{};
  std::uint64_t last_reverse_sequence_ = 0U;
  std::uint64_t applied_through_event_id_ = 0U;
  std::int32_t mouse_x_pixels_ = 0;
  std::int32_t mouse_y_pixels_ = 0;
  bool activated_ = false;
};

[[nodiscard]] bool IsKnownRendererOgre14InputApplyStatus(
    RendererOgre14InputApplyStatus status) noexcept;
[[nodiscard]] const char *ToString(
    RendererOgre14InputApplyStatus status) noexcept;

} // namespace RoR
