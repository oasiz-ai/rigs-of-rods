/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "GfxActorCaptureInventory.h"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "GfxActor capture inventory test failed: " << message
                  << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Owner
{
};

struct AllocationFailureState
{
    bool enabled = false;
    std::size_t allocations_before_failure =
        (std::numeric_limits<std::size_t>::max)();
};

template <typename T>
class InjectedFailureAllocator
{
public:
    using value_type = T;

    InjectedFailureAllocator() noexcept = default;
    explicit InjectedFailureAllocator(
        AllocationFailureState* state) noexcept
        : m_state(state)
    {
    }

    template <typename U>
    InjectedFailureAllocator(
        const InjectedFailureAllocator<U>& other) noexcept
        : m_state(other.State())
    {
    }

    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (m_state != nullptr && m_state->enabled)
        {
            if (m_state->allocations_before_failure == 0U)
                throw std::bad_alloc();
            --m_state->allocations_before_failure;
        }
        return std::allocator<T>{}.allocate(count);
    }

    void deallocate(T* allocation, std::size_t count) noexcept
    {
        std::allocator<T>{}.deallocate(allocation, count);
    }

    [[nodiscard]] AllocationFailureState* State() const noexcept
    {
        return m_state;
    }

    template <typename U>
    bool operator==(const InjectedFailureAllocator<U>& other) const noexcept
    {
        return m_state == other.State();
    }
    template <typename U>
    bool operator!=(const InjectedFailureAllocator<U>& other) const noexcept
    {
        return !(*this == other);
    }

private:
    template <typename>
    friend class InjectedFailureAllocator;
    AllocationFailureState* m_state = nullptr;
};

void TestDurableLifecycleAndVisibility()
{
    RoR::BasicGfxActorCaptureInventory<Owner> inventory;
    Owner owner;
    Require(inventory.Register(41, &owner) ==
                RoR::GfxActorCaptureMutation::APPLIED &&
                inventory.Active().size() == 1U &&
                inventory.Records().at(41).lifecycle ==
                    RoR::GfxActorCaptureLifecycle::LIVE,
            "live registration was not committed consistently");
    Require(RoR::IsGfxActorCaptureEffectivelyVisible(
                inventory.Records().at(41).lifecycle, true, true, true),
            "fully visible live actor was hidden");
    Require(!RoR::IsGfxActorCaptureEffectivelyVisible(
                inventory.Records().at(41).lifecycle, false, true, true),
            "detached/hidden parent SceneNode remained visible");

    Require(inventory.Hide(41, &owner) ==
                RoR::GfxActorCaptureMutation::APPLIED &&
                inventory.Active().empty() &&
                inventory.Records().at(41).owner == &owner &&
                inventory.Records().at(41).lifecycle ==
                    RoR::GfxActorCaptureLifecycle::HIDDEN &&
                !RoR::IsGfxActorCaptureEffectivelyVisible(
                    inventory.Records().at(41).lifecycle, true, true, true),
            "hidden identity was erased or emitted visible");

    Require(inventory.Register(41, &owner) ==
                RoR::GfxActorCaptureMutation::APPLIED &&
                inventory.Active().size() == 1U,
            "hidden identity did not resume live state");
    Require(inventory.Destroy(41, &owner) ==
                RoR::GfxActorCaptureMutation::APPLIED &&
                inventory.Active().empty() &&
                inventory.Records().size() == 1U &&
                inventory.Records().at(41).owner == nullptr &&
                inventory.Records().at(41).lifecycle ==
                    RoR::GfxActorCaptureLifecycle::DESTROYED,
            "destroyed identity did not retain a null-owner tombstone");
    Require(inventory.Register(41, &owner) ==
                RoR::GfxActorCaptureMutation::ALREADY_DESTROYED &&
                inventory.Destroy(41, &owner) ==
                    RoR::GfxActorCaptureMutation::ALREADY_DESTROYED,
            "destroyed identity was resurrected or double-mutated");
}

void TestRegistrationAllocationFailureIsAtomic()
{
    using Allocator = InjectedFailureAllocator<std::byte>;
    AllocationFailureState failure;
    RoR::BasicGfxActorCaptureInventory<Owner, Allocator> inventory{
        Allocator{&failure}};
    Owner owner;

    // A vector-reserve failure occurs before either inventory changes.
    failure.enabled = true;
    failure.allocations_before_failure = 0U;
    bool threw = false;
    try
    {
        (void)inventory.Register(72, &owner);
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    failure.enabled = false;
    Require(threw && inventory.Active().empty() &&
                inventory.Records().empty(),
            "active reserve failure mutated either inventory");

    // Active-vector reserve succeeds; the following map-node allocation is
    // injected to fail. No durable record or active pointer may be published.
    failure.enabled = true;
    failure.allocations_before_failure = 1U;
    threw = false;
    try
    {
        (void)inventory.Register(73, &owner);
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    failure.enabled = false;
    Require(threw && inventory.Active().empty() &&
                inventory.Records().empty(),
            "map allocation failure split record and active inventories");

    Require(inventory.Register(73, &owner) ==
                RoR::GfxActorCaptureMutation::APPLIED &&
                inventory.Active().size() == 1U &&
                inventory.Records().size() == 1U,
            "inventory did not recover after injected allocation failure");
}

void TestNetworkVisibilityMutationOrder(const char* main_source_path)
{
    std::ifstream input(main_source_path, std::ios::binary);
    Require(input.good(), "could not open main.cpp visibility integration");
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const std::size_t hide_case =
        source.find("case MSG_SIM_HIDE_NET_ACTOR_REQUESTED:");
    const std::size_t unhide_case =
        source.find("case MSG_SIM_UNHIDE_NET_ACTOR_REQUESTED:");
    Require(hide_case != std::string::npos &&
                unhide_case != std::string::npos &&
                hide_case < unhide_case,
            "network visibility cases are missing");
    const std::string hide =
        source.substr(hide_case, unhide_case - hide_case);
    const std::size_t hide_inventory = hide.find("HideGfxActor(");
    const std::size_t hide_state =
        hide.find("actor->ar_state = ActorState::NETWORKED_HIDDEN");
    Require(hide_inventory != std::string::npos &&
                hide_state != std::string::npos &&
                hide_inventory < hide_state,
            "actor becomes hidden before inventory mutation succeeds");

    const std::size_t unhide_end = source.find(
        "case MSG_SIM_MUTE_NET_ACTOR_REQUESTED:", unhide_case);
    Require(unhide_end != std::string::npos,
            "network unhide case end is missing");
    const std::string unhide =
        source.substr(unhide_case, unhide_end - unhide_case);
    const std::size_t unhide_inventory = unhide.find("UnhideGfxActor(");
    const std::size_t unhide_state =
        unhide.find("actor->ar_state = ActorState::NETWORKED_OK");
    Require(unhide_inventory != std::string::npos &&
                unhide_state != std::string::npos &&
                unhide_inventory < unhide_state,
            "actor becomes live before inventory registration succeeds");
}

} // namespace

int main(int argc, char** argv)
{
    Require(argc == 2, "expected main.cpp source path");
    TestDurableLifecycleAndVisibility();
    TestRegistrationAllocationFailureIsAtomic();
    TestNetworkVisibilityMutationOrder(argv[1]);
    return EXIT_SUCCESS;
}
