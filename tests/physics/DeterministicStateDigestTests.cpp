#include "DeterministicStateDigest.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
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

float FloatFromBits(std::uint32_t bits)
{
    float value = 0.f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void StoreDoubleBits(std::uint64_t bits, double* value)
{
    const unsigned char* const source =
        reinterpret_cast<const unsigned char*>(&bits);
    volatile unsigned char* const destination =
        reinterpret_cast<volatile unsigned char*>(value);
    for (std::size_t index = 0; index < sizeof(bits); ++index)
        destination[index] = source[index];
}

struct FakeSnapshotActor
{
    RoR::DeterministicStateDigest::SnapshotActor snapshot;
    std::vector<RoR::DeterministicStateDigest::NodeRecord> nodes;
    std::vector<RoR::DeterministicStateDigest::BeamRecord> beams;
};

class FakeSnapshotSource final :
    public RoR::DeterministicStateDigest::SnapshotSource
{
public:
    std::vector<FakeSnapshotActor> actors;
    std::vector<RoR::DeterministicStateDigest::ContactRecord> contacts;
    std::size_t reported_actor_count =
        std::numeric_limits<std::size_t>::max();
    std::size_t reported_contact_count =
        std::numeric_limits<std::size_t>::max();
    std::size_t failing_actor =
        std::numeric_limits<std::size_t>::max();
    std::size_t failing_node_actor =
        std::numeric_limits<std::size_t>::max();
    std::size_t failing_beam_actor =
        std::numeric_limits<std::size_t>::max();
    std::size_t failing_contact =
        std::numeric_limits<std::size_t>::max();

    std::size_t GetActorCount() const override
    {
        return reported_actor_count ==
                std::numeric_limits<std::size_t>::max()
            ? actors.size()
            : reported_actor_count;
    }

    bool ReadActor(
        std::size_t source_actor_index,
        RoR::DeterministicStateDigest::SnapshotActor& actor)
        const override
    {
        if (source_actor_index == failing_actor ||
            source_actor_index >= actors.size())
        {
            return false;
        }
        actor = actors[source_actor_index].snapshot;
        return true;
    }

    bool ReadNode(
        std::size_t source_actor_index,
        std::uint32_t node_index,
        RoR::DeterministicStateDigest::NodeRecord& node)
        const override
    {
        if (source_actor_index == failing_node_actor ||
            source_actor_index >= actors.size() ||
            node_index >= actors[source_actor_index].nodes.size())
        {
            return false;
        }
        node = actors[source_actor_index].nodes[node_index];
        return true;
    }

    bool ReadBeam(
        std::size_t source_actor_index,
        std::uint32_t beam_index,
        RoR::DeterministicStateDigest::BeamRecord& beam)
        const override
    {
        if (source_actor_index == failing_beam_actor ||
            source_actor_index >= actors.size() ||
            beam_index >= actors[source_actor_index].beams.size())
        {
            return false;
        }
        beam = actors[source_actor_index].beams[beam_index];
        return true;
    }

    std::size_t GetContactCount() const override
    {
        return reported_contact_count ==
                std::numeric_limits<std::size_t>::max()
            ? contacts.size()
            : reported_contact_count;
    }

    bool ReadContact(
        std::size_t source_contact_index,
        RoR::DeterministicStateDigest::ContactRecord& contact)
        const override
    {
        if (source_contact_index == failing_contact ||
            source_contact_index >= contacts.size())
        {
            return false;
        }
        contact = contacts[source_contact_index];
        return true;
    }
};

FakeSnapshotSource MakeSnapshotSourceFixture()
{
    using namespace RoR::DeterministicStateDigest;

    FakeSnapshotSource source;
    FakeSnapshotActor actor;
    actor.snapshot.actor.actor_id = 9;
    actor.snapshot.actor.state = ACTOR_STATE_LOCAL_SLEEPING;
    actor.snapshot.actor.flags = ACTOR_FLAG_PHYSICS_PAUSED;
    actor.snapshot.actor.deterministic_seed =
        UINT64_C(0x9988776655443322);
    actor.snapshot.actor.actor_physics_step = 45;
    actor.snapshot.actor.engine_update_step = 47;
    actor.snapshot.actor.origin = {{90.f, 9.f, -9.f}};
    actor.snapshot.node_count = 2;
    actor.snapshot.beam_count = 1;
    actor.snapshot.surface_contact_count = 3;

    NodeRecord node;
    node.actor_id = 9;
    node.node_id = 0;
    node.position = {{9.f, 0.f, 1.f}};
    node.velocity = {{0.9f, 0.f, 0.1f}};
    actor.nodes.push_back(node);
    node.node_id = 1;
    node.position = {{9.f, 2.f, 3.f}};
    node.velocity = {{0.9f, 0.2f, 0.3f}};
    actor.nodes.push_back(node);

    BeamRecord beam;
    beam.actor_id = 9;
    beam.beam_id = 0;
    beam.rest_length = 2.5f;
    beam.stress = -12.f;
    beam.state_flags = BEAM_STATE_DISABLED;
    actor.beams.push_back(beam);
    source.actors.push_back(actor);

    actor = FakeSnapshotActor();
    actor.snapshot.actor.actor_id = 3;
    actor.snapshot.actor.state = ACTOR_STATE_LOCAL_SIMULATED;
    actor.snapshot.actor.flags =
        ACTOR_FLAG_UPDATE_PHYSICS |
        ACTOR_FLAG_COLLISION_RELEVANT;
    actor.snapshot.actor.deterministic_seed =
        UINT64_C(0x123456789abcdef0);
    actor.snapshot.actor.actor_physics_step = 50;
    actor.snapshot.actor.engine_update_step = 53;
    actor.snapshot.actor.origin = {{30.f, 3.f, -3.f}};
    actor.snapshot.node_count = 1;
    actor.snapshot.beam_count = 1;
    actor.snapshot.surface_contact_count = 5;

    node = NodeRecord();
    node.actor_id = 3;
    node.node_id = 0;
    node.position = {{3.f, 4.f, 5.f}};
    node.velocity = {{0.3f, 0.4f, 0.5f}};
    actor.nodes.push_back(node);

    beam = BeamRecord();
    beam.actor_id = 3;
    beam.beam_id = 0;
    beam.rest_length = 1.25f;
    beam.stress = 24.f;
    beam.state_flags = BEAM_STATE_BROKEN;
    actor.beams.push_back(beam);
    source.actors.push_back(actor);

    ContactRecord contact;
    contact.surface_actor = 9;
    contact.surface_contact = 2;
    contact.hit_actor = 3;
    contact.hit_node = 0;
    source.contacts.push_back(contact);
    contact.surface_actor = 3;
    contact.surface_contact = 4;
    contact.hit_actor = 9;
    contact.hit_node = 1;
    source.contacts.push_back(contact);
    return source;
}

RoR::DeterministicStateDigest::Digest BuildFixture(
    std::uint32_t first_position_bits,
    float beam_stress,
    std::uint32_t beam_flags,
    std::uint64_t physics_step = 73)
{
    using namespace RoR::DeterministicStateDigest;

    Builder builder(physics_step, UINT64_C(0x1122334455667788));
    CHECK(builder.BeginActors(2));

    ActorRecord actor;
    actor.actor_id = 3;
    actor.state = 1;
    actor.flags =
        ACTOR_FLAG_UPDATE_PHYSICS |
        ACTOR_FLAG_COLLISION_RELEVANT;
    actor.deterministic_seed = UINT64_C(0xabcdef0123456789);
    actor.actor_physics_step = UINT64_C(73);
    actor.engine_update_step = UINT64_C(81);
    actor.origin = {{10.f, -2.f, 7.5f}};
    CHECK(builder.AddActor(actor));

    actor.actor_id = 9;
    actor.state = 4;
    actor.flags = UINT32_C(0x02);
    actor.deterministic_seed = UINT64_C(0x1020304050607080);
    actor.actor_physics_step = UINT64_C(70);
    actor.engine_update_step = UINT64_C(79);
    actor.origin = {{-8.f, 0.25f, 100.f}};
    CHECK(builder.AddActor(actor));

    CHECK(builder.BeginNodes(3));
    NodeRecord node;
    node.actor_id = 3;
    node.node_id = 0;
    node.position = {{0.f, 2.f, 3.f}};
    std::memcpy(
        &node.position[0],
        &first_position_bits,
        sizeof(first_position_bits));
    node.velocity = {{4.f, 5.f, 6.f}};
    CHECK(builder.AddNode(node));

    node.node_id = 4;
    node.position = {{-1.f, -2.f, -3.f}};
    node.velocity = {{-4.f, -5.f, -6.f}};
    CHECK(builder.AddNode(node));

    node.actor_id = 9;
    node.node_id = 1;
    node.position = {{0.5f, 0.25f, 0.125f}};
    node.velocity = {{8.f, 16.f, 32.f}};
    CHECK(builder.AddNode(node));

    CHECK(builder.BeginBeams(2));
    BeamRecord beam;
    beam.actor_id = 3;
    beam.beam_id = 2;
    beam.rest_length = 1.25f;
    beam.stress = beam_stress;
    beam.material_schema_version = 1;
    beam.plastic_strain = -0.00625;
    beam.accumulated_plastic_strain = 0.0125;
    beam.damage = 0.2;
    beam.damage_driver_density = 125000.0;
    beam.last_total_strain = -0.003;
    beam.state_flags = beam_flags;
    CHECK(builder.AddBeam(beam));

    beam.actor_id = 9;
    beam.beam_id = 0;
    beam.rest_length = 2.5f;
    beam.stress = -1200.f;
    beam.material_schema_version = 1;
    beam.plastic_strain = 0.25;
    beam.accumulated_plastic_strain = 0.5;
    beam.damage = 1.0;
    beam.damage_driver_density = 750000.0;
    beam.last_total_strain = 0.75;
    beam.state_flags = UINT32_C(0x05);
    CHECK(builder.AddBeam(beam));

    CHECK(builder.BeginContacts(2));
    ContactRecord contact;
    contact.surface_actor = 3;
    contact.surface_contact = 7;
    contact.hit_actor = 9;
    contact.hit_node = 1;
    CHECK(builder.AddContact(contact));

    contact.surface_actor = 9;
    contact.surface_contact = 0;
    contact.hit_actor = 3;
    contact.hit_node = 4;
    CHECK(builder.AddContact(contact));

    Digest digest;
    CHECK(builder.Finish(digest));
    CHECK(builder.GetError() == Error::NONE);
    return digest;
}

RoR::DeterministicStateDigest::Digest BuildActorFixture(
    std::uint64_t seed,
    std::uint64_t actor_physics_step,
    std::uint64_t engine_update_step)
{
    using namespace RoR::DeterministicStateDigest;

    Builder builder(100, 200);
    CHECK(builder.BeginActors(1));
    ActorRecord actor;
    actor.actor_id = 1;
    actor.deterministic_seed = seed;
    actor.actor_physics_step = actor_physics_step;
    actor.engine_update_step = engine_update_step;
    CHECK(builder.AddActor(actor));
    CHECK(builder.BeginNodes(0));
    CHECK(builder.BeginBeams(0));
    CHECK(builder.BeginContacts(0));
    Digest digest;
    CHECK(builder.Finish(digest));
    return digest;
}

RoR::DeterministicStateDigest::Digest BuildMaterialFixture(
    const RoR::DeterministicStateDigest::BeamRecord& beam)
{
    using namespace RoR::DeterministicStateDigest;

    Builder builder(9, 10);
    CHECK(builder.BeginActors(1));
    ActorRecord actor;
    actor.actor_id = beam.actor_id;
    CHECK(builder.AddActor(actor));
    CHECK(builder.BeginNodes(0));
    CHECK(builder.BeginBeams(1));
    CHECK(builder.AddBeam(beam));
    CHECK(builder.BeginContacts(0));
    Digest digest;
    CHECK(builder.Finish(digest));
    return digest;
}

void TestGoldenAndSensitivity()
{
    using namespace RoR::DeterministicStateDigest;

    const Digest baseline =
        BuildFixture(UINT32_C(0x3f800000), 4500.f, UINT32_C(0x03));
    CHECK(baseline.ToHex() ==
        "d569dfba124a6a07344e49edeb3416da287d1552dd6c8a64be3e68ac690fa470");
    CHECK(baseline.ToHex().size() == 64);
    CHECK(BuildFixture(
        UINT32_C(0x3f800000), 4500.f, UINT32_C(0x03)) == baseline);
    CHECK(BuildFixture(
        UINT32_C(0x3f800001), 4500.f, UINT32_C(0x03)) != baseline);
    CHECK(BuildFixture(
        UINT32_C(0x3f800000), 4500.5f, UINT32_C(0x03)) != baseline);
    CHECK(BuildFixture(
        UINT32_C(0x3f800000), 4500.f, UINT32_C(0x07)) != baseline);
    CHECK(BuildFixture(
        UINT32_C(0x3f800000), 4500.f, UINT32_C(0x03), 74) != baseline);

    const Digest positive_zero =
        BuildFixture(UINT32_C(0x00000000), 4500.f, UINT32_C(0x03));
    const Digest negative_zero =
        BuildFixture(UINT32_C(0x80000000), 4500.f, UINT32_C(0x03));
    CHECK(positive_zero != negative_zero);
}

void TestActorNoiseStateSensitivity()
{
    using namespace RoR::DeterministicStateDigest;

    const Digest baseline =
        BuildActorFixture(UINT64_C(0x0000000012345678), 10, 20);
    CHECK(BuildActorFixture(
        UINT64_C(0x1000000012345678), 10, 20) != baseline);
    CHECK(BuildActorFixture(
        UINT64_C(0x0000000012345678), 11, 20) != baseline);
    CHECK(BuildActorFixture(
        UINT64_C(0x0000000012345678), 10, 21) != baseline);
}

void TestCompleteMaterialHistorySensitivity()
{
    using namespace RoR::DeterministicStateDigest;

    BeamRecord beam;
    beam.actor_id = 1;
    beam.beam_id = 2;
    beam.rest_length = 3.f;
    beam.stress = 4.f;
    beam.material_schema_version = 1;
    beam.plastic_strain = -0.1;
    beam.accumulated_plastic_strain = 0.2;
    beam.damage = 0.3;
    beam.damage_driver_density = 400.0;
    beam.last_total_strain = -0.5;
    beam.state_flags = UINT32_C(0x06);
    const Digest baseline = BuildMaterialFixture(beam);

    BeamRecord changed = beam;
    changed.plastic_strain = -0.11;
    CHECK(BuildMaterialFixture(changed) != baseline);
    changed = beam;
    changed.accumulated_plastic_strain = 0.21;
    CHECK(BuildMaterialFixture(changed) != baseline);
    changed = beam;
    changed.damage = 0.31;
    CHECK(BuildMaterialFixture(changed) != baseline);
    changed = beam;
    changed.damage_driver_density = 401.0;
    CHECK(BuildMaterialFixture(changed) != baseline);
    changed = beam;
    changed.last_total_strain = -0.51;
    CHECK(BuildMaterialFixture(changed) != baseline);
}

void TestEmptySnapshot()
{
    using namespace RoR::DeterministicStateDigest;

    Builder builder(0, 0);
    CHECK(builder.BeginActors(0));
    CHECK(builder.BeginNodes(0));
    CHECK(builder.BeginBeams(0));
    CHECK(builder.BeginContacts(0));
    Digest digest;
    CHECK(builder.Finish(digest));
    CHECK(digest.ToHex() ==
        "3f4db870f55ea2c58163463b9b040afc38e470e45a89e26689b7c71a958b1d8c");

    Digest second;
    CHECK(!builder.Finish(second));
    CHECK(builder.GetError() == Error::ALREADY_FINISHED);
}

void TestOrderAndCountFailures()
{
    using namespace RoR::DeterministicStateDigest;

    {
        Builder builder(1, 2);
        CHECK(!builder.BeginNodes(0));
        CHECK(builder.GetError() == Error::INVALID_SECTION_ORDER);
    }
    {
        Builder builder(1, 2);
        CHECK(builder.BeginActors(1));
        CHECK(!builder.BeginNodes(0));
        CHECK(builder.GetError() == Error::COUNT_MISMATCH);
        CHECK(builder.GetErrorRecordIndex() == 0);
    }
    {
        Builder builder(1, 2);
        CHECK(builder.BeginActors(1));
        ActorRecord actor;
        actor.actor_id = 4;
        CHECK(builder.AddActor(actor));
        CHECK(!builder.AddActor(actor));
        CHECK(builder.GetError() == Error::COUNT_MISMATCH);
        CHECK(builder.GetErrorRecordIndex() == 1);
    }
    {
        Builder builder(1, 2);
        CHECK(builder.BeginActors(2));
        ActorRecord actor;
        actor.actor_id = 4;
        CHECK(builder.AddActor(actor));
        actor.actor_id = 3;
        CHECK(!builder.AddActor(actor));
        CHECK(builder.GetError() == Error::NON_CANONICAL_KEY);
        CHECK(builder.GetErrorRecordIndex() == 1);
    }
    {
        Builder builder(1, 2);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(2));
        NodeRecord node;
        node.actor_id = 1;
        node.node_id = 8;
        CHECK(builder.AddNode(node));
        node.node_id = 8;
        CHECK(!builder.AddNode(node));
        CHECK(builder.GetError() == Error::NON_CANONICAL_KEY);
    }
    {
        Builder builder(1, 2);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(0));
        CHECK(builder.BeginContacts(2));
        ContactRecord contact;
        contact.surface_actor = 2;
        contact.surface_contact = 0;
        contact.hit_actor = 3;
        contact.hit_node = 0;
        CHECK(builder.AddContact(contact));
        contact.surface_actor = 1;
        CHECK(!builder.AddContact(contact));
        CHECK(builder.GetError() == Error::NON_CANONICAL_KEY);
    }
}

void TestHardLimits()
{
    using namespace RoR::DeterministicStateDigest;

    {
        Builder builder(0, 0);
        CHECK(!builder.BeginActors(MAX_ACTORS + 1));
        CHECK(builder.GetError() == Error::COUNT_LIMIT_EXCEEDED);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(!builder.BeginNodes(MAX_NODES + 1));
        CHECK(builder.GetError() == Error::COUNT_LIMIT_EXCEEDED);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(!builder.BeginBeams(MAX_BEAMS + 1));
        CHECK(builder.GetError() == Error::COUNT_LIMIT_EXCEEDED);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(0));
        CHECK(!builder.BeginContacts(MAX_CONTACTS + 1));
        CHECK(builder.GetError() == Error::COUNT_LIMIT_EXCEEDED);
    }
}

void TestInvalidRecordsAndFastMathFiniteCheck()
{
    using namespace RoR::DeterministicStateDigest;

    const float quiet_nan = FloatFromBits(UINT32_C(0x7fc01234));
    const float positive_infinity = FloatFromBits(UINT32_C(0x7f800000));
    const double double_infinity =
        DoubleFromBits(UINT64_C(0x7ff0000000000000));
    const double double_nan =
        DoubleFromBits(UINT64_C(0x7ff8000000001234));

    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(1));
        ActorRecord actor;
        actor.actor_id = 1;
        actor.origin[2] = quiet_nan;
        CHECK(!builder.AddActor(actor));
        CHECK(builder.GetError() == Error::NON_FINITE_VALUE);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(1));
        NodeRecord node;
        node.actor_id = 1;
        node.velocity[0] = positive_infinity;
        CHECK(!builder.AddNode(node));
        CHECK(builder.GetError() == Error::NON_FINITE_VALUE);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(1));
        ActorRecord actor;
        actor.actor_id = 1;
        actor.state = ACTOR_STATE_DISPOSED + 1U;
        CHECK(!builder.AddActor(actor));
        CHECK(builder.GetError() == Error::INVALID_RECORD);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(1));
        ActorRecord actor;
        actor.actor_id = 1;
        actor.flags = UINT32_C(1) << 31;
        CHECK(!builder.AddActor(actor));
        CHECK(builder.GetError() == Error::INVALID_RECORD);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 0.f;
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::INVALID_RECORD);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 1.f;
        beam.stress = quiet_nan;
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::NON_FINITE_VALUE);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 1.f;
        beam.damage = 1.01;
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::INVALID_RECORD);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 1.f;
        beam.damage_driver_density = double_infinity;
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::NON_FINITE_VALUE);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 1.f;
        beam.plastic_strain = double_nan;
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::NON_FINITE_VALUE);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 1.f;
        beam.material_schema_version =
            BEAM_MATERIAL_SCHEMA_CALIBRATED_V1 + 1U;
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::INVALID_RECORD);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 1.f;
        beam.state_flags = UINT32_C(1) << 31;
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::INVALID_RECORD);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 1.f;
        StoreDoubleBits(
            UINT64_C(0x8000000000000000),
            &beam.plastic_strain);
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::INVALID_RECORD);
    }
    {
        Builder builder(0, 0);
        CHECK(builder.BeginActors(0));
        CHECK(builder.BeginNodes(0));
        CHECK(builder.BeginBeams(1));
        BeamRecord beam;
        beam.actor_id = 1;
        beam.rest_length = 1.f;
        beam.state_flags = BEAM_STATE_MATERIAL_FRACTURED;
        CHECK(!builder.AddBeam(beam));
        CHECK(builder.GetError() == Error::INVALID_RECORD);
    }
}

void TestSnapshotAdapterCanonicalExtraction()
{
    using namespace RoR::DeterministicStateDigest;

    FakeSnapshotSource source = MakeSnapshotSourceFixture();
    SnapshotStatus status;
    status.error = SnapshotError::SOURCE_READ_FAILED;
    status.digest_error = Error::INVALID_RECORD;
    status.source_index = 4;
    status.record_index = 5;
    Digest baseline;
    CHECK(BuildSnapshotDigest(73, 91, source, baseline, &status));
    CHECK(baseline.ToHex() ==
        "aa66a9ab55a0e7c89fb62eeb0209897d921fd5b7f7fde709d4eeeaee6940026e");
    CHECK(status.error == SnapshotError::NONE);
    CHECK(status.digest_error == Error::NONE);
    CHECK(
        status.source_index ==
        std::numeric_limits<std::size_t>::max());
    CHECK(status.record_index == 0);

    FakeSnapshotSource reordered = source;
    std::reverse(reordered.actors.begin(), reordered.actors.end());
    std::reverse(reordered.contacts.begin(), reordered.contacts.end());
    Digest reordered_digest;
    CHECK(BuildSnapshotDigest(
        73, 91, reordered, reordered_digest, &status));
    CHECK(reordered_digest == baseline);

    FakeSnapshotSource changed = source;
    changed.actors[0].snapshot.actor.deterministic_seed++;
    Digest changed_digest;
    CHECK(BuildSnapshotDigest(
        73, 91, changed, changed_digest, &status));
    CHECK(changed_digest != baseline);

    changed = source;
    changed.actors[0].snapshot.actor.actor_physics_step++;
    CHECK(BuildSnapshotDigest(
        73, 91, changed, changed_digest, &status));
    CHECK(changed_digest != baseline);

    changed = source;
    changed.actors[0].snapshot.actor.engine_update_step++;
    CHECK(BuildSnapshotDigest(
        73, 91, changed, changed_digest, &status));
    CHECK(changed_digest != baseline);

    FakeSnapshotSource minimum_id = source;
    minimum_id.actors[1].snapshot.actor.actor_id = 1;
    minimum_id.actors[1].nodes[0].actor_id = 1;
    minimum_id.actors[1].beams[0].actor_id = 1;
    for (ContactRecord& contact : minimum_id.contacts)
    {
        if (contact.surface_actor == 3)
            contact.surface_actor = 1;
        if (contact.hit_actor == 3)
            contact.hit_actor = 1;
    }
    CHECK(BuildSnapshotDigest(
        73, 91, minimum_id, changed_digest, &status));
    CHECK(status.error == SnapshotError::NONE);
}

void TestSnapshotAdapterFailsClosed()
{
    using namespace RoR::DeterministicStateDigest;

    FakeSnapshotSource source = MakeSnapshotSourceFixture();
    SnapshotStatus status;
    Digest sentinel;
    sentinel.bytes.fill(UINT8_C(0xa5));
    const Digest unchanged = sentinel;

    source.reported_actor_count =
        static_cast<std::size_t>(MAX_ACTORS) + 1U;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::COUNT_LIMIT_EXCEEDED);
    CHECK(sentinel == unchanged);

    source = MakeSnapshotSourceFixture();
    source.reported_contact_count =
        static_cast<std::size_t>(MAX_CONTACTS) + 1U;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::COUNT_LIMIT_EXCEEDED);

    source = MakeSnapshotSourceFixture();
    source.actors[0].snapshot.node_count = MAX_NODES;
    source.actors[1].snapshot.node_count = 1;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::COUNT_LIMIT_EXCEEDED);

    source = MakeSnapshotSourceFixture();
    source.actors[1].snapshot.actor.actor_id = 0;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::INVALID_ACTOR_ID);

    source = MakeSnapshotSourceFixture();
    source.actors[1].snapshot.actor.actor_id =
        source.actors[0].snapshot.actor.actor_id;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::DUPLICATE_ACTOR_ID);

    source = MakeSnapshotSourceFixture();
    source.actors[0].snapshot.actor.state =
        ACTOR_STATE_DISPOSED + 1U;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::DIGEST_REJECTED);
    CHECK(status.digest_error == Error::INVALID_RECORD);

    source = MakeSnapshotSourceFixture();
    source.actors[0].nodes[0].node_id = 1;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::INVALID_CROSS_REFERENCE);

    source = MakeSnapshotSourceFixture();
    source.contacts[0].hit_node = 1;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::INVALID_CROSS_REFERENCE);

    source = MakeSnapshotSourceFixture();
    source.contacts[0].surface_contact = 3;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::INVALID_CROSS_REFERENCE);

    source = MakeSnapshotSourceFixture();
    source.contacts.push_back(source.contacts[0]);
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::DIGEST_REJECTED);
    CHECK(status.digest_error == Error::NON_CANONICAL_KEY);

    source = MakeSnapshotSourceFixture();
    source.contacts[0].hit_actor = source.contacts[0].surface_actor;
    source.contacts[0].hit_node = 0;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::INVALID_CROSS_REFERENCE);

    source = MakeSnapshotSourceFixture();
    source.actors[0].snapshot.actor.origin[0] =
        FloatFromBits(UINT32_C(0x7f800000));
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::DIGEST_REJECTED);
    CHECK(status.digest_error == Error::NON_FINITE_VALUE);
    CHECK(sentinel == unchanged);

    source = MakeSnapshotSourceFixture();
    const volatile double wider_than_binary32 =
        std::numeric_limits<double>::max();
    source.actors[0].nodes[0].position[0] =
        static_cast<float>(wider_than_binary32);
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::DIGEST_REJECTED);
    CHECK(status.digest_error == Error::NON_FINITE_VALUE);
    CHECK(sentinel == unchanged);

    source = MakeSnapshotSourceFixture();
    source.failing_actor = 1;
    CHECK(!BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::SOURCE_READ_FAILED);
    CHECK(status.source_index == 1);

    source = MakeSnapshotSourceFixture();
    CHECK(BuildSnapshotDigest(1, 2, source, sentinel, &status));
    CHECK(status.error == SnapshotError::NONE);
    CHECK(status.digest_error == Error::NONE);
}

} // namespace

int main()
{
    TestGoldenAndSensitivity();
    TestActorNoiseStateSensitivity();
    TestCompleteMaterialHistorySensitivity();
    TestEmptySnapshot();
    TestOrderAndCountFailures();
    TestHardLimits();
    TestInvalidRecordsAndFastMathFiniteCheck();
    TestSnapshotAdapterCanonicalExtraction();
    TestSnapshotAdapterFailsClosed();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " deterministic state-digest test(s) failed\n";
        return 1;
    }

    std::cout << "deterministic state-digest tests passed\n";
    return 0;
}
