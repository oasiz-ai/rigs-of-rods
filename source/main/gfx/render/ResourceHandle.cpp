/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ResourceHandle.h"

#include <atomic>
#include <functional>
#include <stdexcept>

namespace RoR::Render {
namespace {

std::atomic<std::uint32_t> g_next_resource_domain{1U};

std::uint32_t AcquireResourceDomain() {
  std::uint32_t domain = g_next_resource_domain.load(std::memory_order_relaxed);
  for (;;) {
    if (domain == 0U || domain > ResourceHandle::kMaxDomain) {
      throw std::length_error("renderer resource domain space exhausted");
    }
    if (g_next_resource_domain.compare_exchange_weak(
            domain, domain + 1U, std::memory_order_relaxed,
            std::memory_order_relaxed)) {
      return domain;
    }
  }
}

} // namespace

ResourceHandlePool::ResourceHandlePool(ResourceKind kind,
                                       std::uint32_t initial_generation)
    : kind_(kind), initial_generation_(initial_generation) {
  if (!IsKnownResourceKind(kind_)) {
    initial_generation_ = 0U;
    return;
  }
  if (initial_generation_ == 0U ||
      initial_generation_ > ResourceHandle::kMaxGeneration) {
    kind_ = ResourceKind::INVALID;
    initial_generation_ = 0U;
    return;
  }
  domain_ = AcquireResourceDomain();
}

std::size_t
ResourceHandleHash::operator()(ResourceHandle handle) const noexcept {
  return std::hash<std::uint64_t>{}(handle.raw());
}

ResourceHandle ResourceHandlePool::Allocate() {
  if (!IsKnownResourceKind(kind_)) {
    return {};
  }

  std::uint32_t slot_index = 0U;
  if (!free_slots_.empty()) {
    slot_index = free_slots_.back();
    free_slots_.pop_back();
  } else {
    if (slots_.size() > ResourceHandle::kMaxSlot) {
      throw std::length_error("renderer resource handle pool exhausted");
    }
    // Guarantee Release() can return a slot without allocating. This keeps
    // the stale-handle rejection path safe during device-loss teardown.
    free_slots_.reserve(slots_.size() + 1U);
    slot_index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back({initial_generation_, false, false});
  }

  Slot &slot = slots_[slot_index];
  if (slot.retired) {
    throw std::logic_error("retired renderer resource slot was reused");
  }
  slot.live = true;
  ++live_count_;
  return ResourceHandle::Create(kind_, domain_, slot_index, slot.generation);
}

bool ResourceHandlePool::Release(ResourceHandle handle) noexcept {
  if (!IsLive(handle)) {
    return false;
  }

  Slot &slot = slots_[handle.slot()];
  slot.live = false;
  if (slot.generation == ResourceHandle::kMaxGeneration) {
    // Never wrap generations: wrapping would eventually resurrect a stale
    // token. A retired slot consumes no free-list storage and is never reused.
    slot.retired = true;
  } else {
    ++slot.generation;
    free_slots_.push_back(handle.slot());
  }
  --live_count_;
  return true;
}

bool ResourceHandlePool::IsLive(ResourceHandle handle) const noexcept {
  if (!handle.valid() || handle.kind() != kind_ || handle.domain() != domain_ ||
      handle.slot() >= slots_.size()) {
    return false;
  }
  const Slot &slot = slots_[handle.slot()];
  return slot.live && !slot.retired && slot.generation == handle.generation();
}

} // namespace RoR::Render
