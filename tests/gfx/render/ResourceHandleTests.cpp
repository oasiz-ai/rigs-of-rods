/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ResourceHandle.h"

#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <unordered_set>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "resource handle test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void TestCompactStableRepresentation() {
  using RoR::Render::ResourceHandle;
  using RoR::Render::ResourceHandlePool;
  using RoR::Render::ResourceKind;

  static_assert(sizeof(ResourceHandle) == sizeof(std::uint64_t),
                "resource handle must remain a compact 64-bit token");
  static_assert(std::is_trivially_copyable_v<ResourceHandle>,
                "resource handles must cross queues by value");
  static_assert(std::is_standard_layout_v<ResourceHandle>,
                "resource handle storage must be portable");
  static_assert(!std::is_copy_constructible_v<ResourceHandlePool> &&
                    !std::is_move_constructible_v<ResourceHandlePool>,
                "live registries must not duplicate or move handle identity");

  const ResourceHandle handle =
      ResourceHandle::Create(ResourceKind::TEXTURE, 19U, 41U, 7U);
  Require(handle.valid(), "valid fields did not create a handle");
  Require(handle.kind() == ResourceKind::TEXTURE, "kind did not round trip");
  Require(handle.domain() == 19U, "domain did not round trip");
  Require(handle.slot() == 41U, "slot did not round trip");
  Require(handle.generation() == 7U, "generation did not round trip");
  Require(ResourceHandle::FromRaw(handle.raw()) == handle,
          "stable raw representation did not round trip");

  Require(!ResourceHandle::Create(ResourceKind::INVALID, 1U, 0U, 1U),
          "invalid kind created a handle");
  Require(!ResourceHandle::Create(ResourceKind::MESH, 0U, 0U, 1U),
          "zero domain created a handle");
  Require(!ResourceHandle::Create(ResourceKind::MESH, 1U, 0U, 0U),
          "zero generation created a handle");
  Require(!ResourceHandle::Create(ResourceKind::MESH, 1U,
                                  ResourceHandle::kMaxSlot + 1U, 1U),
          "out-of-range slot created a handle");
  Require(!ResourceHandle::Create(ResourceKind::MESH, 1U, 0U,
                                  ResourceHandle::kMaxGeneration + 1U),
          "out-of-range generation created a handle");
  Require(!ResourceHandle::FromRaw(1U), "malformed raw value created a handle");
}

void TestGenerationRejectsStaleHandles() {
  using RoR::Render::ResourceHandle;
  using RoR::Render::ResourceHandlePool;
  using RoR::Render::ResourceKind;

  ResourceHandlePool pool(ResourceKind::MESH);
  const ResourceHandle first = pool.Allocate();
  const ResourceHandle second = pool.Allocate();
  Require(pool.live_count() == 2U && pool.IsLive(first) && pool.IsLive(second),
          "allocated handles were not live");

  Require(pool.Release(first), "live handle could not be released");
  Require(!pool.IsLive(first), "released handle remained live");
  Require(!pool.Release(first), "stale handle was released twice");

  const ResourceHandle replacement = pool.Allocate();
  Require(replacement.slot() == first.slot(), "free slot was not reused");
  Require(replacement.generation() != first.generation(),
          "reused slot did not advance its generation");
  Require(pool.IsLive(replacement), "replacement handle was not live");
  Require(!pool.IsLive(first), "old generation became live again");
  Require(!pool.Release(first),
          "stale generation destroyed the replacement resource");

  const ResourceHandle wrong_kind =
      ResourceHandle::Create(ResourceKind::TEXTURE, replacement.domain(),
                             replacement.slot(), replacement.generation());
  Require(!pool.IsLive(wrong_kind) && !pool.Release(wrong_kind),
          "wrong-kind handle was accepted by a pool");
  Require(pool.live_count() == 2U,
          "failed stale operations changed the live count");
}

void TestPoolsHaveIndependentDomainsAndNeverWrap() {
  using RoR::Render::ResourceHandle;
  using RoR::Render::ResourceHandlePool;
  using RoR::Render::ResourceKind;

  ResourceHandlePool first_pool(ResourceKind::MESH);
  ResourceHandlePool second_pool(ResourceKind::MESH);
  const ResourceHandle first = first_pool.Allocate();
  const ResourceHandle second = second_pool.Allocate();
  Require(first.domain() != second.domain() && first.raw() != second.raw(),
          "same-kind pools issued aliasing handles");
  Require(!first_pool.IsLive(second) && !second_pool.IsLive(first),
          "a pool accepted another pool's handle");

  ResourceHandlePool terminal_pool(ResourceKind::TEXTURE,
                                   ResourceHandle::kMaxGeneration);
  const ResourceHandle terminal = terminal_pool.Allocate();
  Require(terminal.generation() == ResourceHandle::kMaxGeneration,
          "terminal-generation test pool did not use requested generation");
  Require(terminal_pool.Release(terminal),
          "terminal-generation resource could not be released");
  const ResourceHandle replacement = terminal_pool.Allocate();
  Require(replacement.slot() != terminal.slot(),
          "exhausted generation slot was reused");
  Require(!terminal_pool.IsLive(terminal),
          "terminal stale handle was resurrected");
}

void TestInvalidPoolAndHashingFailClosed() {
  using RoR::Render::ResourceHandle;
  using RoR::Render::ResourceHandleHash;
  using RoR::Render::ResourceHandlePool;
  using RoR::Render::ResourceKind;

  ResourceHandlePool invalid_pool(ResourceKind::INVALID);
  Require(!invalid_pool.Allocate(), "invalid pool allocated a resource");

  const ResourceHandle material =
      ResourceHandle::Create(ResourceKind::MATERIAL, 7U, 3U, 5U);
  std::unordered_set<ResourceHandle, ResourceHandleHash> handles;
  handles.insert(material);
  handles.insert(ResourceHandle::FromRaw(material.raw()));
  Require(handles.size() == 1U, "equal stable handles hashed differently");
}

} // namespace

int main() {
  TestCompactStableRepresentation();
  TestGenerationRejectsStaleHandles();
  TestPoolsHaveIndependentDomainsAndNeverWrap();
  TestInvalidPoolAndHashingFailClosed();
  std::cout << "resource handle tests passed\n";
  return EXIT_SUCCESS;
}
