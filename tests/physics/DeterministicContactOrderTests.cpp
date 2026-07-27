#include "DeterministicContactOrder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
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

class FixedRandom
{
public:
    explicit FixedRandom(std::uint32_t state): m_state(state) {}

    std::uint32_t Next()
    {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 17;
        m_state ^= m_state << 5;
        return m_state;
    }

    int Uniform(int limit)
    {
        return static_cast<int>(
            Next() % static_cast<std::uint32_t>(limit));
    }

private:
    std::uint32_t m_state;
};

struct PointFixture
{
    RoR::DeterministicContactOrder::PointKey key;
    std::array<int, 3> position;
};

void Shuffle(std::vector<int>& values, FixedRandom& random)
{
    for (std::size_t remaining = values.size(); remaining > 1; --remaining)
    {
        const std::size_t selected = static_cast<std::size_t>(
            random.Next() % static_cast<std::uint32_t>(remaining));
        std::swap(values[remaining - 1], values[selected]);
    }
}

void TestPointKeyOrdering()
{
    using namespace RoR::DeterministicContactOrder;

    const std::array<PointKey, 5> keys = {{
        {2, 4, 0},
        {1, 9, 1},
        {1, 3, 2},
        {2, 4, 3},
        {1, 3, 0}
    }};
    std::vector<int> candidates = {0, 1, 2, 3, 4};

    SortByKey(
        candidates,
        [&keys](int candidate)
        {
            return keys[static_cast<std::size_t>(candidate)];
        });

    const std::array<int, 5> expected = {{4, 2, 1, 0, 3}};
    CHECK(std::equal(candidates.begin(), candidates.end(), expected.begin()));
}

void TestRandomizedBroadPhaseOrder()
{
    using namespace RoR::DeterministicContactOrder;

    FixedRandom random(UINT32_C(0x6d2b79f5));
    for (int fixture_index = 0; fixture_index < 10000; ++fixture_index)
    {
        const int point_count = 1 + random.Uniform(64);
        std::vector<PointFixture> points;
        points.reserve(static_cast<std::size_t>(point_count));

        for (int point_index = 0; point_index < point_count; ++point_index)
        {
            PointFixture point;
            point.key.actor = 1 + random.Uniform(8);
            point.key.node = static_cast<std::uint32_t>(point_index);
            point.key.candidate = point_index;
            for (int axis = 0; axis < 3; ++axis)
                point.position[axis] = random.Uniform(2001) - 1000;
            points.push_back(point);
        }

        std::array<int, 3> box_min;
        std::array<int, 3> box_max;
        for (int axis = 0; axis < 3; ++axis)
        {
            const int center = random.Uniform(1601) - 800;
            const int radius = random.Uniform(401);
            box_min[axis] = center - radius;
            box_max[axis] = center + radius;
        }

        std::vector<int> discovered;
        std::vector<PointKey> oracle;
        for (int point_index = 0; point_index < point_count; ++point_index)
        {
            const PointFixture& point =
                points[static_cast<std::size_t>(point_index)];
            bool inside = true;
            for (int axis = 0; axis < 3; ++axis)
            {
                inside = inside &&
                    point.position[axis] >= box_min[axis] &&
                    point.position[axis] <= box_max[axis];
            }
            if (inside)
            {
                discovered.push_back(point_index);
                oracle.push_back(point.key);
            }
        }

        Shuffle(discovered, random);
        SortByKey(
            discovered,
            [&points](int candidate)
            {
                return points[static_cast<std::size_t>(candidate)].key;
            });
        std::sort(oracle.begin(), oracle.end());

        CHECK(discovered.size() == oracle.size());
        for (std::size_t item_index = 0;
                item_index < discovered.size() &&
                item_index < oracle.size();
                ++item_index)
        {
            const PointKey actual =
                points[static_cast<std::size_t>(
                    discovered[item_index])].key;
            CHECK(actual == oracle[item_index]);
        }
    }
}

struct ForceContact
{
    RoR::DeterministicContactOrder::InterActorKey key;
    std::uint32_t target_node = 0;
    float force = 0.f;
};

struct FailingCopy
{
    FailingCopy() {}
    FailingCopy(const FailingCopy&)
    {
        throw std::bad_alloc();
    }
};

void TestQuotaAllocation()
{
    using namespace RoR::DeterministicContactOrder;

    CHECK(AllocateTaskQuotas(0, 10).empty());

    const std::vector<std::size_t> uneven =
        AllocateTaskQuotas(3, 10);
    CHECK(uneven.size() == 3);
    CHECK(uneven[0] == 4);
    CHECK(uneven[1] == 3);
    CHECK(uneven[2] == 3);

    const std::vector<std::size_t> sparse =
        AllocateTaskQuotas(8, 3);
    CHECK(sparse.size() == 8);
    for (std::size_t index = 0; index < sparse.size(); ++index)
        CHECK(sparse[index] == (index < 3 ? 1u : 0u));

    std::size_t allocated = 0;
    const std::vector<std::size_t> production =
        AllocateTaskQuotas(127, INTER_ACTOR_CONTACT_BUDGET);
    for (std::size_t quota : production)
        allocated += quota;
    CHECK(allocated == INTER_ACTOR_CONTACT_BUDGET);
    CHECK(production.front() - production.back() <= 1);
}

void TestBoundedBufferOverflow()
{
    using namespace RoR::DeterministicContactOrder;

    BoundedTaskBuffer<int> buffer(2);
    CHECK(buffer.GetQuota() == 2);
    CHECK(buffer.TryPush(11));
    CHECK(buffer.TryPush(22));
    CHECK(!buffer.TryPush(33));
    CHECK(!buffer.TryPush(44));
    CHECK(buffer.HasOverflowed());
    CHECK(buffer.GetItems().size() == 2);
    CHECK(buffer.GetItems()[0] == 11);
    CHECK(buffer.GetItems()[1] == 22);

    buffer.Reset(1);
    CHECK(buffer.GetQuota() == 1);
    CHECK(!buffer.HasOverflowed());
    CHECK(!buffer.HasAllocationFailed());
    CHECK(buffer.GetItems().empty());
    CHECK(buffer.TryPush(44));
    CHECK(!buffer.TryPush(55));
    CHECK(buffer.GetItems().size() == 1);
    CHECK(buffer.GetItems()[0] == 44);

    BoundedTaskBuffer<FailingCopy> allocation_failure(1);
    const FailingCopy item;
    CHECK(!allocation_failure.TryPush(item));
    CHECK(allocation_failure.HasOverflowed());
    CHECK(allocation_failure.HasAllocationFailed());
    CHECK(allocation_failure.GetItems().empty());
    CHECK(!allocation_failure.TryPush(item));
}

std::array<float, 16> ReduceForces(
    const std::vector<ForceContact>& contacts,
    int worker_count)
{
    using namespace RoR::DeterministicContactOrder;

    const std::vector<std::size_t> quotas = AllocateTaskQuotas(
        static_cast<std::size_t>(worker_count),
        contacts.size());
    std::vector<BoundedTaskBuffer<ForceContact>> task_buffers;
    task_buffers.reserve(quotas.size());
    for (std::size_t quota : quotas)
        task_buffers.emplace_back(quota);

    std::vector<ForceContact> partitioned = contacts;
    SortByKey(
        partitioned,
        [](const ForceContact& contact)
        {
            return contact.key;
        });
    std::size_t partition_begin = 0;
    for (std::size_t buffer_index = 0;
            buffer_index < task_buffers.size();
            ++buffer_index)
    {
        const std::size_t partition_size = quotas[buffer_index];
        for (std::size_t reverse_index = partition_size;
                reverse_index > 0;
                --reverse_index)
        {
            CHECK(task_buffers[buffer_index].TryPush(
                partitioned[partition_begin + reverse_index - 1]));
        }
        partition_begin += partition_size;
    }
    CHECK(partition_begin == contacts.size());

    std::array<float, 16> totals = {};
    bool fallback_called = false;
    const bool used_fast_path = ProcessTaskBuffersOrFallback(
        task_buffers,
        [](const ForceContact& contact)
        {
            return contact.key;
        },
        [&totals](
            const std::vector<BoundedTaskBuffer<ForceContact>>& buffers)
        {
            CanonicalOrderValidator<InterActorKey> order;
            for (const BoundedTaskBuffer<ForceContact>& buffer : buffers)
            {
                for (const ForceContact& contact : buffer.GetItems())
                {
                    CHECK(order.Observe(contact.key));
                    totals[contact.target_node] += contact.force;
                }
            }
        },
        [&fallback_called]()
        {
            fallback_called = true;
        });

    CHECK(used_fast_path);
    CHECK(!fallback_called);
    return totals;
}

void TestOverflowUsesCanonicalFallback()
{
    using namespace RoR::DeterministicContactOrder;

    std::vector<ForceContact> all_contacts(6);
    for (std::uint32_t index = 0; index < all_contacts.size(); ++index)
    {
        ForceContact& contact = all_contacts[index];
        contact.key.surface_actor =
            1 + static_cast<std::int32_t>(index / 3);
        contact.key.surface_contact = index % 3;
        contact.key.hit_actor = 20;
        contact.key.hit_node = index;
        contact.target_node = index % 2;
        contact.force = static_cast<float>(index + 1);
    }

    // This models the production fallback's actor/surface/hit traversal. It
    // must already be canonical because the fallback intentionally holds no
    // complete contact set that it could sort.
    CanonicalOrderValidator<InterActorKey> fallback_order;
    for (const ForceContact& contact : all_contacts)
        CHECK(fallback_order.Observe(contact.key));

    std::vector<BoundedTaskBuffer<ForceContact>> buffers;
    buffers.reserve(2);
    buffers.emplace_back(1);
    buffers.emplace_back(1);
    CHECK(buffers[0].TryPush(all_contacts[0]));
    CHECK(!buffers[0].TryPush(all_contacts[1]));
    CHECK(buffers[1].TryPush(all_contacts[2]));

    bool fast_path_called = false;
    std::vector<InterActorKey> applied_keys;
    std::array<float, 2> fallback_totals = {};
    const bool used_fast_path = ProcessTaskBuffersOrFallback(
        buffers,
        [](const ForceContact& contact)
        {
            return contact.key;
        },
        [&fast_path_called](
            const std::vector<BoundedTaskBuffer<ForceContact>>&)
        {
            fast_path_called = true;
        },
        [&all_contacts, &applied_keys, &fallback_totals]()
        {
            CanonicalOrderValidator<InterActorKey> emitted_order;
            for (const ForceContact& contact : all_contacts)
            {
                CHECK(emitted_order.Observe(contact.key));
                applied_keys.push_back(contact.key);
                fallback_totals[contact.target_node] += contact.force;
            }
        });

    CHECK(!used_fast_path);
    CHECK(!fast_path_called);
    CHECK(applied_keys.size() == all_contacts.size());
    for (std::size_t index = 0; index < all_contacts.size(); ++index)
        CHECK(applied_keys[index] == all_contacts[index].key);

    std::array<float, 2> expected_totals = {};
    for (const ForceContact& contact : all_contacts)
        expected_totals[contact.target_node] += contact.force;
    CHECK(std::memcmp(
        fallback_totals.data(),
        expected_totals.data(),
        sizeof(fallback_totals)) == 0);
}

void TestCanonicalOrderValidatorRejectsRegression()
{
    using namespace RoR::DeterministicContactOrder;

    CanonicalOrderValidator<InterActorKey> validator;
    CHECK(validator.Observe({1, 0, 2, 0}));
    CHECK(validator.Observe({1, 0, 2, 1}));
    CHECK(validator.Observe({1, 1, 2, 0}));
    CHECK(!validator.Observe({1, 0, 2, 9}));
}

void TestTaskBufferReduction()
{
    std::vector<ForceContact> contacts;
    contacts.reserve(512);
    FixedRandom random(UINT32_C(0xa341316c));

    const std::array<float, 8> forces = {{
        1000000.f,
        0.25f,
        -1000000.f,
        0.125f,
        12.5f,
        -3.75f,
        0.5f,
        -0.0625f
    }};
    for (std::uint32_t index = 0; index < 512; ++index)
    {
        ForceContact contact;
        contact.key.surface_actor = 1 + static_cast<std::int32_t>(index % 5);
        contact.key.surface_contact = (index * 37) % 113;
        contact.key.hit_actor = 10 + static_cast<std::int32_t>(index % 7);
        contact.key.hit_node = index;
        contact.target_node = index % 16;
        contact.force = forces[index % forces.size()];
        contacts.push_back(contact);
    }

    std::vector<int> shuffled_indices(contacts.size());
    for (std::size_t index = 0; index < shuffled_indices.size(); ++index)
        shuffled_indices[index] = static_cast<int>(index);
    Shuffle(shuffled_indices, random);

    std::vector<ForceContact> shuffled;
    shuffled.reserve(contacts.size());
    for (int index : shuffled_indices)
        shuffled.push_back(contacts[static_cast<std::size_t>(index)]);

    const std::array<float, 16> one_worker = ReduceForces(shuffled, 1);
    const std::array<float, 16> two_workers = ReduceForces(shuffled, 2);
    const std::array<float, 16> eight_workers = ReduceForces(shuffled, 8);

    CHECK(std::memcmp(
        one_worker.data(),
        two_workers.data(),
        sizeof(one_worker)) == 0);
    CHECK(std::memcmp(
        one_worker.data(),
        eight_workers.data(),
        sizeof(one_worker)) == 0);
}

} // namespace

int main()
{
    TestPointKeyOrdering();
    TestRandomizedBroadPhaseOrder();
    TestQuotaAllocation();
    TestBoundedBufferOverflow();
    TestOverflowUsesCanonicalFallback();
    TestCanonicalOrderValidatorRejectsRegression();
    TestTaskBufferReduction();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " deterministic contact-order test(s) failed\n";
        return 1;
    }

    std::cout << "deterministic contact-order tests passed\n";
    return 0;
}
