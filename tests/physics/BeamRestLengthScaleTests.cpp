#include "BeamRestLengthScale.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
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

void CheckRejected(float length, float scale)
{
    float output = 123.0f;
    CHECK(!RoR::Physics::TryScaleBeamRestLength(
        length, scale, &output));
    CHECK(output == 0.0f);
}

void TestExactSupportedScales()
{
    float output = 0.0f;
    CHECK(RoR::Physics::TryScaleBeamRestLength(
        2.0f, 1.0f, &output));
    CHECK(output == 2.0f);
    CHECK(RoR::Physics::TryScaleBeamRestLength(
        2.0f, 0.75f, &output));
    CHECK(output == 1.5f);
    CHECK(RoR::Physics::TryScaleBeamRestLength(
        2.0f, 1.25f, &output));
    CHECK(output == 2.5f);
    CHECK(RoR::Physics::TryScaleBeamRestLength(
        std::numeric_limits<float>::min(), 1.0f, &output));
    CHECK(output == std::numeric_limits<float>::min());
}

void TestInvalidInputsAndOverflow()
{
    const float infinity = FloatFromBits(UINT32_C(0x7f800000));
    const float negative_infinity =
        FloatFromBits(UINT32_C(0xff800000));
    const float nan = FloatFromBits(UINT32_C(0x7fc00001));
    const float denormal = std::numeric_limits<float>::denorm_min();

    CheckRejected(0.0f, 1.0f);
    CheckRejected(-1.0f, 1.0f);
    CheckRejected(denormal, 1.0f);
    CheckRejected(1.0f, 0.0f);
    CheckRejected(1.0f, -1.0f);
    CheckRejected(1.0f, denormal);
    CheckRejected(infinity, 1.0f);
    CheckRejected(negative_infinity, 1.0f);
    CheckRejected(nan, 1.0f);
    CheckRejected(1.0f, infinity);
    CheckRejected(1.0f, negative_infinity);
    CheckRejected(1.0f, nan);
    CheckRejected(std::numeric_limits<float>::max(), 2.0f);
    CheckRejected(std::numeric_limits<float>::min(), 0.5f);

    CHECK(!RoR::Physics::TryScaleBeamRestLength(
        1.0f, 1.0f, NULL));
}

} // namespace

int main()
{
    TestExactSupportedScales();
    TestInvalidInputsAndOverflow();
    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " beam rest-length scaling test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Beam rest-length scaling tests passed\n";
    return EXIT_SUCCESS;
}
