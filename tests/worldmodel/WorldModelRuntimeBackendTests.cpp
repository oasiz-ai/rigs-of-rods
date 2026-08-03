#include "WorldModelRuntimeBackend.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

using namespace RoR;
using namespace RoR::WorldModel;

class FakeRuntime final : public FixedStepRuntime
{
public:
    std::uint64_t completed = 0U;
    unsigned int joins = 0U;
    bool owned = false;

    bool AcquireCaptureOwnership() override
    {
        if (owned)
            return false;
        owned = true;
        return true;
    }

    void ReleaseCaptureOwnership() noexcept override
    {
        owned = false;
    }

    std::uint64_t GetCompletedPhysicsSteps() const override
    {
        return completed;
    }

    FixedStepCaptureBridge::BatchResult AdvanceFixedSteps(
        std::uint32_t count,
        FixedStepCaptureBridge::AppliedInputObserver& observer) override
    {
        if (FixedStepCaptureBridge::ValidateBatch(
                completed,
                count) !=
            FixedStepCaptureBridge::BatchResult::COMPLETED)
        {
            return FixedStepCaptureBridge::
                BatchResult::INVALID_STEP_COUNT;
        }
        bool accepted = true;
        const std::uint64_t first = completed;
        for (std::uint32_t index = 0U; index < count; ++index)
        {
            FixedStepCaptureBridge::StepStartIdentity identity;
            accepted =
                FixedStepCaptureBridge::TryMakeStepStartIdentity(
                    first,
                    index,
                    count,
                    identity) &&
                FixedStepCaptureBridge::NotifyAppliedInputObserver(
                    observer,
                    identity) &&
                accepted;
            ++completed;
        }
        return accepted
            ? FixedStepCaptureBridge::BatchResult::COMPLETED
            : FixedStepCaptureBridge::BatchResult::OBSERVER_REJECTED;
    }

    bool JoinPhysics() override
    {
        ++joins;
        return true;
    }
};

class FakeProvider final : public RuntimeCaptureProvider
{
public:
    bool reject_applied = false;
    unsigned int begins = 0U;
    std::vector<FixedStepCaptureBridge::StepStartIdentity> applied;

    bool CaptureResetBaseline(
        const ObservationId& id,
        ObservationSample& observation) override
    {
        observation.record.observation_id = id;
        return true;
    }

    bool BeginTransition(const TransitionId&) override
    {
        ++begins;
        return true;
    }

    bool ObserveAppliedInputAtFixedStepStart(
        const FixedStepCaptureBridge::StepStartIdentity& identity) override
    {
        applied.push_back(identity);
        return !reject_applied;
    }

    bool CaptureCompletedTransition(
        const TransitionId& id,
        TransitionSample& transition,
        ObservationSample& observation) override
    {
        transition.record.transition_id = id;
        observation.record.observation_id = id.target;
        return true;
    }
};

void TestExactRuntimeSequence()
{
    const EpisodeId episode(1U, 2U);
    TransitionId id;
    CHECK(MakeTransitionId(episode, 0U, 0U, id));

    FakeRuntime runtime;
    FakeProvider provider;
    RuntimeCaptureBackend backend(runtime, provider);
    ObservationSample baseline;
    CHECK(backend.AcquireRuntimeOwnership());
    CHECK(backend.CaptureResetBaseline(id.source, baseline));
    CHECK(runtime.joins == 1U);
    CHECK(backend.BeginTransition(id));
    CHECK(provider.begins == 1U);
    CHECK(backend.AdvanceFixedSteps(41U));
    CHECK(backend.JoinPhysics());
    CHECK(runtime.completed == 41U);
    CHECK(provider.applied.size() == 41U);
    for (std::size_t index = 0U;
         index < provider.applied.size();
         ++index)
    {
        CHECK(provider.applied[index].effective_input_tick == index);
    }
    TransitionSample transition;
    ObservationSample observation;
    CHECK(backend.CaptureCompletedTransition(
        id,
        transition,
        observation));
    backend.ReleaseRuntimeOwnership();
    CHECK(!runtime.owned);
}

void TestObserverFailureStillDrains()
{
    const EpisodeId episode(1U, 2U);
    TransitionId id;
    CHECK(MakeTransitionId(episode, 0U, 0U, id));

    FakeRuntime runtime;
    FakeProvider provider;
    provider.reject_applied = true;
    RuntimeCaptureBackend backend(runtime, provider);
    CHECK(backend.AcquireRuntimeOwnership());
    CHECK(backend.BeginTransition(id));
    CHECK(!backend.AdvanceFixedSteps(41U));
    CHECK(backend.JoinPhysics());
    CHECK(runtime.completed == 41U);
    CHECK(provider.applied.size() == 41U);
    backend.ReleaseRuntimeOwnership();
}

void TestRejectsWrongBatch()
{
    const EpisodeId episode(1U, 2U);
    TransitionId id;
    CHECK(MakeTransitionId(episode, 0U, 0U, id));

    FakeRuntime runtime;
    FakeProvider provider;
    RuntimeCaptureBackend backend(runtime, provider);
    CHECK(backend.AcquireRuntimeOwnership());
    CHECK(backend.BeginTransition(id));
    CHECK(!backend.AdvanceFixedSteps(42U));
    CHECK(runtime.completed == 0U);
    CHECK(provider.applied.empty());
    backend.ReleaseRuntimeOwnership();
}

void TestOwnershipIsExclusiveAndReleasedByDestructor()
{
    FakeRuntime runtime;
    FakeProvider provider;
    {
        RuntimeCaptureBackend backend(runtime, provider);
        CHECK(backend.AcquireRuntimeOwnership());
        CHECK(!backend.AcquireRuntimeOwnership());
        CHECK(runtime.owned);
    }
    CHECK(!runtime.owned);
}

} // namespace

int main()
{
    TestExactRuntimeSequence();
    TestObserverFailureStillDrains();
    TestRejectsWrongBatch();
    TestOwnershipIsExclusiveAndReleasedByDestructor();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " runtime backend test(s) failed\n";
        return 1;
    }
    std::cout << "world-model runtime backend tests passed\n";
    return 0;
}
