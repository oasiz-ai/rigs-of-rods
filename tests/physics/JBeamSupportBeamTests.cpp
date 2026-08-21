#include "CompressionOnlySupportBeam.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "line " << line << ": check failed: "
                  << expression << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

float FloatFromBits(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void TestCompressionOnlyResponse()
{
    using RoR::CompressionOnlySupportBeam::Evaluate;

    const auto compressed =
        Evaluate(0.70f, 0.75f, 1.0f, 1.0f, 5000.0f, 250.0f);
    CHECK(compressed.valid);
    CHECK(compressed.compression_active);
    CHECK(!compressed.break_now);
    CHECK(compressed.spring == 5000.0f);
    CHECK(compressed.damping == 250.0f);

    const auto at_activation =
        Evaluate(0.75f, 0.75f, 1.0f, 1.0f, 5000.0f, 250.0f);
    CHECK(at_activation.valid);
    CHECK(!at_activation.compression_active);
    CHECK(at_activation.spring == 0.0f);
    CHECK(at_activation.damping == 0.0f);

    const auto extended =
        Evaluate(1.25f, 0.75f, 1.0f, 1.0f, 5000.0f, 250.0f);
    CHECK(extended.valid);
    CHECK(!extended.compression_active);
    CHECK(!extended.break_now);
    CHECK(extended.spring == 0.0f);
    CHECK(extended.damping == 0.0f);
}

void TestBreakRatioUsesSpawnedLength()
{
    using RoR::CompressionOnlySupportBeam::Evaluate;

    // beamLongBound=0.5 breaks beyond 150% of the geometric spawned length,
    // independent of the 0.71 precompressed activation length.
    const auto at_limit =
        Evaluate(1.5f, 0.71f, 1.0f, 0.5f, 1.0f, 1.0f);
    CHECK(at_limit.valid);
    CHECK(!at_limit.break_now);

    const auto beyond_limit =
        Evaluate(1.5001f, 0.71f, 1.0f, 0.5f, 1.0f, 1.0f);
    CHECK(beyond_limit.valid);
    CHECK(beyond_limit.break_now);

    // Zero is a documented value, not a request for RoR's legacy default.
    const auto zero_at_spawn =
        Evaluate(1.0f, 0.71f, 1.0f, 0.0f, 1.0f, 1.0f);
    const auto zero_beyond_spawn =
        Evaluate(1.0001f, 0.71f, 1.0f, 0.0f, 1.0f, 1.0f);
    CHECK(zero_at_spawn.valid);
    CHECK(!zero_at_spawn.break_now);
    CHECK(zero_beyond_spawn.valid);
    CHECK(zero_beyond_spawn.break_now);
}

void TestExtremeFiniteRatio()
{
    using RoR::CompressionOnlySupportBeam::Evaluate;

    const auto result = Evaluate(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::min(),
        std::numeric_limits<float>::min(),
        std::numeric_limits<float>::max(),
        1.0f,
        1.0f);
    CHECK(result.valid);
    CHECK(result.break_now);
    CHECK(result.spring == 0.0f);
    CHECK(result.damping == 0.0f);
}

void TestInvalidInputsFailClosed()
{
    using RoR::CompressionOnlySupportBeam::Evaluate;

    const float infinity = FloatFromBits(UINT32_C(0x7f800000));
    const float nan = FloatFromBits(UINT32_C(0x7fc00001));
    const float invalid[] = {infinity, nan, 0.0f, -1.0f};
    for (float value : invalid)
    {
        CHECK(!Evaluate(value, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f).valid);
        CHECK(!Evaluate(1.0f, value, 1.0f, 1.0f, 1.0f, 1.0f).valid);
        CHECK(!Evaluate(1.0f, 1.0f, value, 1.0f, 1.0f, 1.0f).valid);
    }
    CHECK(!Evaluate(1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f).valid);
    CHECK(!Evaluate(1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f).valid);
    CHECK(!Evaluate(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f).valid);
    CHECK(!Evaluate(1.0f, 1.0f, 1.0f, infinity, 1.0f, 1.0f).valid);
    CHECK(!Evaluate(1.0f, 1.0f, 1.0f, 1.0f, nan, 1.0f).valid);
}

} // namespace

int main()
{
    TestCompressionOnlyResponse();
    TestBreakRatioUsesSpawnedLength();
    TestExtremeFiniteRatio();
    TestInvalidInputsFailClosed();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " JBeam SUPPORT checks failed\n";
        return 1;
    }
    std::cout << "JBeam SUPPORT response checks passed\n";
    return 0;
}
