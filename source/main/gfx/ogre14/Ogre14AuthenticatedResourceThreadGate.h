/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Allocation-free process-lifetime thread-affinity gate.

#pragma once

#include <atomic>
#include <cstdint>

namespace RoR::Render {

/// Authenticated OGRE resource lifecycle code has lock ordering with OGRE's
/// group and manager mutexes that is safe only on one serialized resource /
/// render thread. This gate is allocation-free and can reject a foreign thread
/// before either lock domain is entered.
class Ogre14AuthenticatedResourceThreadGate final {
public:
  Ogre14AuthenticatedResourceThreadGate() noexcept = default;
  ~Ogre14AuthenticatedResourceThreadGate() = default;
  Ogre14AuthenticatedResourceThreadGate(
      const Ogre14AuthenticatedResourceThreadGate &) = delete;
  Ogre14AuthenticatedResourceThreadGate &operator=(
      const Ogre14AuthenticatedResourceThreadGate &) = delete;

  [[nodiscard]] bool BindCurrentThread() noexcept {
    const std::uintptr_t current = CurrentThreadToken();
    std::uintptr_t expected = 0U;
    return bound_thread_token_.compare_exchange_strong(
               expected, current, std::memory_order_acq_rel,
               std::memory_order_acquire) ||
           expected == current;
  }

  [[nodiscard]] bool IsCurrentThreadOrUnbound() const noexcept {
    const std::uintptr_t bound =
        bound_thread_token_.load(std::memory_order_acquire);
    return bound == 0U || bound == CurrentThreadToken();
  }

  [[nodiscard]] bool is_bound() const noexcept {
    return bound_thread_token_.load(std::memory_order_acquire) != 0U;
  }

private:
  static std::uintptr_t CurrentThreadToken() noexcept {
    static thread_local unsigned char token = 0U;
    return reinterpret_cast<std::uintptr_t>(&token);
  }

  std::atomic<std::uintptr_t> bound_thread_token_{0U};
};

} // namespace RoR::Render
