#include "FixedStepCaptureBridge.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace Bridge = RoR::FixedStepCaptureBridge;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

class CollectingObserver:
    public Bridge::AppliedInputObserver
{
public:
    bool ObserveAppliedInputAtFixedStepStart(
        const Bridge::StepStartIdentity& identity) override
    {
        identities.push_back(identity);
        return !reject;
    }

    bool reject = false;
    std::vector<Bridge::StepStartIdentity> identities;
};

class ThrowingObserver:
    public Bridge::AppliedInputObserver
{
public:
    bool ObserveAppliedInputAtFixedStepStart(
        const Bridge::StepStartIdentity&) override
    {
        throw std::runtime_error("capture failed");
    }
};

void TestBatchValidation()
{
    CHECK(
        Bridge::ValidateBatch(0U, 1U) ==
        Bridge::BatchResult::COMPLETED);
    CHECK(
        Bridge::ValidateBatch(17U, 42U) ==
        Bridge::BatchResult::COMPLETED);
    CHECK(
        Bridge::ValidateBatch(
            0U,
            RoR::DeterministicScenarioSchedule::
                MAX_FIXED_STEPS_PER_FRAME) ==
        Bridge::BatchResult::COMPLETED);
    CHECK(
        Bridge::ValidateBatch(0U, 0U) ==
        Bridge::BatchResult::INVALID_STEP_COUNT);
    CHECK(
        Bridge::ValidateBatch(
            0U,
            RoR::DeterministicScenarioSchedule::
                MAX_FIXED_STEPS_PER_FRAME + 1U) ==
        Bridge::BatchResult::INVALID_STEP_COUNT);

    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    CHECK(
        Bridge::ValidateBatch(maximum - 42U, 42U) ==
        Bridge::BatchResult::COMPLETED);
    CHECK(
        Bridge::ValidateBatch(maximum - 41U, 42U) ==
        Bridge::BatchResult::PHYSICS_COUNTER_EXHAUSTED);
}

void TestIdentitySequence()
{
    const std::uint64_t first = UINT64_C(9876543210);
    const std::uint32_t count = 42U;
    CollectingObserver observer;
    for (std::uint32_t index = 0U; index < count; ++index)
    {
        Bridge::StepStartIdentity identity;
        CHECK(
            Bridge::TryMakeStepStartIdentity(
                first,
                index,
                count,
                identity));
        CHECK(
            Bridge::NotifyAppliedInputObserver(
                observer,
                identity));
    }

    CHECK(observer.identities.size() == count);
    for (std::uint32_t index = 0U; index < count; ++index)
    {
        const Bridge::StepStartIdentity& identity =
            observer.identities[index];
        CHECK(
            identity.completed_physics_steps ==
            first + index);
        CHECK(
            identity.effective_input_tick ==
            identity.completed_physics_steps);
        CHECK(identity.batch_step_index == index);
        CHECK(identity.batch_step_count == count);
    }

    Bridge::StepStartIdentity unchanged;
    unchanged.completed_physics_steps = 99U;
    CHECK(
        !Bridge::TryMakeStepStartIdentity(
            first,
            count,
            count,
            unchanged));
    CHECK(unchanged.completed_physics_steps == 99U);
}

void TestObserverFailuresStayInsideBoundary()
{
    Bridge::StepStartIdentity identity;
    CHECK(
        Bridge::TryMakeStepStartIdentity(
            0U,
            0U,
            1U,
            identity));

    CollectingObserver rejecting;
    rejecting.reject = true;
    CHECK(
        !Bridge::NotifyAppliedInputObserver(
            rejecting,
            identity));
    CHECK(rejecting.identities.size() == 1U);

    ThrowingObserver throwing;
    CHECK(
        !Bridge::NotifyAppliedInputObserver(
            throwing,
            identity));
}

void TestObservationBatchDrainsAfterRejection()
{
    CollectingObserver observer;
    observer.reject = true;
    Bridge::ObservationBatch batch(125U, 3U, observer);
    CHECK(!batch.ObserveFixedStepStart(125U, 0U));
    CHECK(!batch.ObserveFixedStepStart(126U, 1U));
    CHECK(!batch.ObserveFixedStepStart(127U, 2U));
    CHECK(!batch.Succeeded());
    CHECK(observer.identities.size() == 3U);

    CollectingObserver mismatch_observer;
    Bridge::ObservationBatch mismatch(
        400U,
        2U,
        mismatch_observer);
    CHECK(!mismatch.ObserveFixedStepStart(401U, 0U));
    CHECK(mismatch.ObserveFixedStepStart(401U, 1U));
    CHECK(!mismatch.Succeeded());
    CHECK(mismatch_observer.identities.size() == 1U);

    CollectingObserver valid_observer;
    Bridge::ObservationBatch valid(
        900U,
        2U,
        valid_observer);
    CHECK(valid.ObserveFixedStepStart(900U, 0U));
    CHECK(valid.ObserveFixedStepStart(901U, 1U));
    CHECK(valid.Succeeded());
}

void TestResultNames()
{
    CHECK(
        std::string(
            Bridge::ToString(
                Bridge::BatchResult::COMPLETED)) ==
        "completed");
    CHECK(
        std::string(
            Bridge::ToString(
                Bridge::BatchResult::OBSERVER_REJECTED)) ==
        "applied-input observer rejected");
    CHECK(
        std::string(
            Bridge::ToString(
                Bridge::BatchResult::CAPTURE_OWNERSHIP_REQUIRED)) ==
        "capture runtime ownership required");
    CHECK(
        std::string(
            Bridge::ToString(
                Bridge::BatchResult::INPUT_RESOLUTION_REJECTED)) ==
        "capture input resolution rejected");
}

} // namespace

int main()
{
    TestBatchValidation();
    TestIdentitySequence();
    TestObserverFailuresStayInsideBoundary();
    TestObservationBatchDrainsAfterRejection();
    TestResultNames();
    return g_failures == 0 ? 0 : 1;
}
