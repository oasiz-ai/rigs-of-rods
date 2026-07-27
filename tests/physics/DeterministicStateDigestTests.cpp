#include "DeterministicStateDigest.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

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
    actor.flags = UINT32_C(0x21);
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
        "694aa61f24f54d3c33bd06f67a2d93fb534dfa853ea85230fc6274edcf314f1e");
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
    changed.material_schema_version = 2;
    CHECK(BuildMaterialFixture(changed) != baseline);
    changed = beam;
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

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " deterministic state-digest test(s) failed\n";
        return 1;
    }

    std::cout << "deterministic state-digest tests passed\n";
    return 0;
}
