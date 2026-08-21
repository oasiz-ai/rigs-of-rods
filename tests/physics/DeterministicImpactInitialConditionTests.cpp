#include "DeterministicImpactInitialCondition.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            std::cerr << __FILE__ << ':' << __LINE__                           \
                      << ": CHECK failed: " #condition << '\n';               \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double NextPositiveDouble(double value)
{
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    ++bits;
    return DoubleFromBits(bits);
}

RoR::DeterministicImpactInitialCondition::Request MakeRequest(
    double x,
    double y,
    double z)
{
    RoR::DeterministicImpactInitialCondition::Request request;
    request.velocity_meters_per_second = {{x, y, z}};
    return request;
}

RoR::DeterministicImpactInitialCondition::PlacementRequest MakePlacement(
    double x,
    double y,
    double z)
{
    RoR::DeterministicImpactInitialCondition::PlacementRequest request;
    request.translation_offset_meters = {{x, y, z}};
    return request;
}

void TestAcceptedVelocity()
{
    using namespace RoR::DeterministicImpactInitialCondition;
    const Result result = Validate(MakeRequest(-12.0, 0.0, 5.0));
    CHECK(result.IsValid());
    CHECK(result.error == Error::NONE);
    CHECK(result.speed_squared_meters2_per_second2 == 169.0);

    const Result boundary = Validate(
        MakeRequest(MAXIMUM_SPEED_METERS_PER_SECOND, 0.0, 0.0));
    CHECK(boundary.IsValid());
    CHECK(boundary.speed_squared_meters2_per_second2 == 10000.0);
}

void TestInvalidVelocity()
{
    using namespace RoR::DeterministicImpactInitialCondition;
    Request unsupported = MakeRequest(1.0, 0.0, 0.0);
    unsupported.schema_version = SCHEMA_VERSION + 1U;
    CHECK(Validate(unsupported).error == Error::UNSUPPORTED_SCHEMA);
    CHECK(Validate(MakeRequest(0.0, 0.0, 0.0)).error == Error::ZERO_SPEED);

    const double infinity = DoubleFromBits(UINT64_C(0x7ff0000000000000));
    const double nan = DoubleFromBits(UINT64_C(0x7ff8000000000001));
    CHECK(Validate(MakeRequest(infinity, 0.0, 0.0)).error ==
        Error::NONFINITE_VELOCITY);
    CHECK(Validate(MakeRequest(0.0, nan, 0.0)).error ==
        Error::NONFINITE_VELOCITY);
    CHECK(Validate(MakeRequest(std::numeric_limits<double>::max(), 0.0, 0.0)).error ==
        Error::NUMERIC_OVERFLOW);

    const double over = NextPositiveDouble(MAXIMUM_SPEED_METERS_PER_SECOND);
    CHECK(Validate(MakeRequest(over, 0.0, 0.0)).error ==
        Error::SPEED_OUT_OF_RANGE);
    CHECK(Validate(MakeRequest(60.0, 60.0, 60.0)).error ==
        Error::SPEED_OUT_OF_RANGE);
}

void TestPlacement()
{
    using namespace RoR::DeterministicImpactInitialCondition;
    const PlacementResult accepted = ValidatePlacement(
        MakePlacement(0.0, 2.0, 0.0));
    CHECK(accepted.IsValid());
    CHECK(accepted.error == PlacementError::NONE);
    CHECK(accepted.translation_squared_meters2 == 4.0);

    const PlacementResult boundary = ValidatePlacement(
        MakePlacement(MAXIMUM_TRANSLATION_METERS, 0.0, 0.0));
    CHECK(boundary.IsValid());
    CHECK(boundary.translation_squared_meters2 == 10000.0);

    PlacementRequest unsupported = MakePlacement(0.0, 2.0, 0.0);
    unsupported.schema_version = PLACEMENT_SCHEMA_VERSION + 1U;
    CHECK(ValidatePlacement(unsupported).error ==
        PlacementError::UNSUPPORTED_SCHEMA);

    const double infinity = DoubleFromBits(UINT64_C(0x7ff0000000000000));
    const double nan = DoubleFromBits(UINT64_C(0x7ff8000000000001));
    CHECK(ValidatePlacement(MakePlacement(infinity, 0.0, 0.0)).error ==
        PlacementError::NONFINITE_TRANSLATION);
    CHECK(ValidatePlacement(MakePlacement(0.0, nan, 0.0)).error ==
        PlacementError::NONFINITE_TRANSLATION);
    CHECK(ValidatePlacement(MakePlacement(
        std::numeric_limits<double>::max(), 0.0, 0.0)).error ==
        PlacementError::NUMERIC_OVERFLOW);

    const double over = NextPositiveDouble(MAXIMUM_TRANSLATION_METERS);
    CHECK(ValidatePlacement(MakePlacement(over, 0.0, 0.0)).error ==
        PlacementError::TRANSLATION_OUT_OF_RANGE);
    CHECK(ValidatePlacement(MakePlacement(60.0, 60.0, 60.0)).error ==
        PlacementError::TRANSLATION_OUT_OF_RANGE);
}

} // namespace

int main()
{
    TestAcceptedVelocity();
    TestInvalidVelocity();
    TestPlacement();
    if (failures != 0)
    {
        std::cerr << failures
                  << " deterministic impact initial-condition checks failed\n";
        return 1;
    }
    std::cout << "deterministic impact initial-condition checks passed\n";
    return 0;
}
