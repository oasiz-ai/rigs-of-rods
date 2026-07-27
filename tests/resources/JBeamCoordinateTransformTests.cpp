#include "JBeamCoordinateTransform.h"

#include <cmath>
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

using RoR::BeamNG::JBeamPoint3;
using RoR::BeamNG::JBeamTransformHandedness;
using RoR::BeamNG::JBeamVector3;

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool Equal(double left, double right)
{
    return left == right;
}

bool Equal(const JBeamPoint3& left, const JBeamPoint3& right)
{
    return Equal(left.x, right.x) &&
        Equal(left.y, right.y) &&
        Equal(left.z, right.z);
}

bool Equal(const JBeamVector3& left, const JBeamVector3& right)
{
    return Equal(left.x, right.x) &&
        Equal(left.y, right.y) &&
        Equal(left.z, right.z);
}

JBeamVector3 Subtract(const JBeamPoint3& left, const JBeamPoint3& right)
{
    return JBeamVector3(
        left.x - right.x,
        left.y - right.y,
        left.z - right.z);
}

double Dot(const JBeamVector3& left, const JBeamVector3& right)
{
    return left.x * right.x +
        left.y * right.y +
        left.z * right.z;
}

JBeamVector3 Cross(const JBeamVector3& left, const JBeamVector3& right)
{
    return JBeamVector3(
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x);
}

double DistanceSquared(const JBeamPoint3& left, const JBeamPoint3& right)
{
    return Dot(Subtract(left, right), Subtract(left, right));
}

void CheckZero(const JBeamPoint3& point)
{
    CHECK(Equal(point, JBeamPoint3()));
}

void CheckZero(const JBeamVector3& vector)
{
    CHECK(Equal(vector, JBeamVector3()));
}

void TestBasisAndTransformSemantics()
{
    CHECK(RoR::BeamNG::GetBeamNGToRoRTransformDeterminant() == 1.0);
    CHECK(RoR::BeamNG::GetRoRToBeamNGTransformDeterminant() == 1.0);
    CHECK(RoR::BeamNG::GetBeamNGToRoRTransformHandedness() ==
        JBeamTransformHandedness::PRESERVED);
    CHECK(RoR::BeamNG::GetRoRToBeamNGTransformHandedness() ==
        JBeamTransformHandedness::PRESERVED);

    JBeamVector3 transformed;
    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        JBeamVector3(1.0, 0.0, 0.0), &transformed));
    CHECK(Equal(transformed, JBeamVector3(0.0, 0.0, 1.0)));

    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        JBeamVector3(0.0, 1.0, 0.0), &transformed));
    CHECK(Equal(transformed, JBeamVector3(1.0, 0.0, 0.0)));

    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        JBeamVector3(0.0, 0.0, 1.0), &transformed));
    CHECK(Equal(transformed, JBeamVector3(0.0, 1.0, 0.0)));
}

void TestInverseRoundTripsAndInPlaceConversion()
{
    const JBeamPoint3 beamng_point(-123.5, 0.25, 987.0);
    JBeamPoint3 ror_point;
    JBeamPoint3 round_trip_point;
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        beamng_point, &ror_point));
    CHECK(Equal(ror_point, JBeamPoint3(0.25, 987.0, -123.5)));
    CHECK(RoR::BeamNG::TryTransformRoRPointToBeamNG(
        ror_point, &round_trip_point));
    CHECK(Equal(round_trip_point, beamng_point));

    const JBeamVector3 beamng_vector(6.5, -7.25, 8.75);
    JBeamVector3 ror_vector;
    JBeamVector3 round_trip_vector;
    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        beamng_vector, &ror_vector));
    CHECK(RoR::BeamNG::TryTransformRoRVectorToBeamNG(
        ror_vector, &round_trip_vector));
    CHECK(Equal(round_trip_vector, beamng_vector));

    JBeamPoint3 in_place_point = beamng_point;
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        in_place_point, &in_place_point));
    CHECK(Equal(in_place_point, ror_point));
    CHECK(RoR::BeamNG::TryTransformRoRPointToBeamNG(
        in_place_point, &in_place_point));
    CHECK(Equal(in_place_point, beamng_point));

    JBeamVector3 in_place_vector = beamng_vector;
    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        in_place_vector, &in_place_vector));
    CHECK(Equal(in_place_vector, ror_vector));
    CHECK(RoR::BeamNG::TryTransformRoRVectorToBeamNG(
        in_place_vector, &in_place_vector));
    CHECK(Equal(in_place_vector, beamng_vector));
}

void CheckRejectedPoint(const JBeamPoint3& invalid)
{
    CHECK(!RoR::BeamNG::IsFiniteJBeamPoint(invalid));

    JBeamPoint3 output(9.0, 8.0, 7.0);
    CHECK(!RoR::BeamNG::TryTransformBeamNGPointToRoR(
        invalid, &output));
    CheckZero(output);

    output = JBeamPoint3(9.0, 8.0, 7.0);
    CHECK(!RoR::BeamNG::TryTransformRoRPointToBeamNG(
        invalid, &output));
    CheckZero(output);
}

void CheckRejectedVector(const JBeamVector3& invalid)
{
    CHECK(!RoR::BeamNG::IsFiniteJBeamVector(invalid));

    JBeamVector3 output(9.0, 8.0, 7.0);
    CHECK(!RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        invalid, &output));
    CheckZero(output);

    output = JBeamVector3(9.0, 8.0, 7.0);
    CHECK(!RoR::BeamNG::TryTransformRoRVectorToBeamNG(
        invalid, &output));
    CheckZero(output);
}

void TestFiniteValidationAndFailClosedResults()
{
    // Bit construction keeps these adversarial inputs observable even when
    // the test itself is compiled with finite-math-only assumptions.
    const double positive_infinity =
        DoubleFromBits(UINT64_C(0x7ff0000000000000));
    const double negative_infinity =
        DoubleFromBits(UINT64_C(0xfff0000000000000));
    const double quiet_nan =
        DoubleFromBits(UINT64_C(0x7ff8000000000001));

    CHECK(RoR::BeamNG::IsFiniteJBeamPoint(
        JBeamPoint3(-std::numeric_limits<double>::max(),
            std::numeric_limits<double>::denorm_min(),
            0.0)));
    CHECK(RoR::BeamNG::IsFiniteJBeamVector(
        JBeamVector3(std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::denorm_min(),
            -0.0)));

    const double invalid_values[] = {
        positive_infinity,
        negative_infinity,
        quiet_nan
    };
    for (std::size_t value_index = 0;
         value_index < sizeof(invalid_values) / sizeof(invalid_values[0]);
         ++value_index)
    {
        const double value = invalid_values[value_index];
        CheckRejectedPoint(JBeamPoint3(value, 2.0, 3.0));
        CheckRejectedPoint(JBeamPoint3(1.0, value, 3.0));
        CheckRejectedPoint(JBeamPoint3(1.0, 2.0, value));
        CheckRejectedVector(JBeamVector3(value, 2.0, 3.0));
        CheckRejectedVector(JBeamVector3(1.0, value, 3.0));
        CheckRejectedVector(JBeamVector3(1.0, 2.0, value));
    }

    CHECK(!RoR::BeamNG::TryTransformBeamNGPointToRoR(
        JBeamPoint3(), NULL));
    CHECK(!RoR::BeamNG::TryTransformRoRPointToBeamNG(
        JBeamPoint3(), NULL));
    CHECK(!RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        JBeamVector3(), NULL));
    CHECK(!RoR::BeamNG::TryTransformRoRVectorToBeamNG(
        JBeamVector3(), NULL));

    JBeamPoint3 invalid_in_place(1.0, positive_infinity, 3.0);
    CHECK(!RoR::BeamNG::TryTransformBeamNGPointToRoR(
        invalid_in_place, &invalid_in_place));
    CheckZero(invalid_in_place);

    JBeamVector3 invalid_vector_in_place(quiet_nan, 2.0, 3.0);
    CHECK(!RoR::BeamNG::TryTransformRoRVectorToBeamNG(
        invalid_vector_in_place, &invalid_vector_in_place));
    CheckZero(invalid_vector_in_place);
}

void TestMetricAndOrientationPreservation()
{
    const JBeamPoint3 point_a(3.25, -4.5, 9.75);
    const JBeamPoint3 point_b(-8.0, 5.5, 1.25);
    JBeamPoint3 transformed_a;
    JBeamPoint3 transformed_b;
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        point_a, &transformed_a));
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        point_b, &transformed_b));
    CHECK(DistanceSquared(point_a, point_b) ==
        DistanceSquared(transformed_a, transformed_b));

    const JBeamVector3 vector_a(2.0, -3.0, 5.0);
    const JBeamVector3 vector_b(-7.0, 11.0, 13.0);
    JBeamVector3 transformed_vector_a;
    JBeamVector3 transformed_vector_b;
    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        vector_a, &transformed_vector_a));
    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        vector_b, &transformed_vector_b));
    CHECK(Dot(vector_a, vector_b) ==
        Dot(transformed_vector_a, transformed_vector_b));

    const JBeamVector3 beamng_cross = Cross(vector_a, vector_b);
    JBeamVector3 transformed_cross;
    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        beamng_cross, &transformed_cross));
    CHECK(Equal(
        transformed_cross,
        Cross(transformed_vector_a, transformed_vector_b)));

    const JBeamPoint3 triangle_a(2.0, 3.0, 4.0);
    const JBeamPoint3 triangle_b(7.0, 4.0, 1.0);
    const JBeamPoint3 triangle_c(-1.0, 9.0, 6.0);
    const JBeamVector3 beamng_normal = Cross(
        Subtract(triangle_b, triangle_a),
        Subtract(triangle_c, triangle_a));
    JBeamPoint3 transformed_triangle_a;
    JBeamPoint3 transformed_triangle_b;
    JBeamPoint3 transformed_triangle_c;
    JBeamVector3 transformed_normal;
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        triangle_a, &transformed_triangle_a));
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        triangle_b, &transformed_triangle_b));
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        triangle_c, &transformed_triangle_c));
    CHECK(RoR::BeamNG::TryTransformBeamNGVectorToRoR(
        beamng_normal, &transformed_normal));
    CHECK(Equal(
        transformed_normal,
        Cross(
            Subtract(transformed_triangle_b, transformed_triangle_a),
            Subtract(transformed_triangle_c, transformed_triangle_a))));
}

void TestVehicleLandmarkFrame()
{
    const JBeamPoint3 beamng_reference(10.0, 20.0, 30.0);
    const JBeamPoint3 beamng_back(10.0, 21.0, 30.0);
    const JBeamPoint3 beamng_left(11.0, 20.0, 30.0);
    const JBeamPoint3 beamng_up(10.0, 20.0, 31.0);

    JBeamPoint3 ror_reference;
    JBeamPoint3 ror_back;
    JBeamPoint3 ror_left;
    JBeamPoint3 ror_up;
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        beamng_reference, &ror_reference));
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        beamng_back, &ror_back));
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        beamng_left, &ror_left));
    CHECK(RoR::BeamNG::TryTransformBeamNGPointToRoR(
        beamng_up, &ror_up));

    CHECK(Equal(ror_reference, JBeamPoint3(20.0, 30.0, 10.0)));
    CHECK(Equal(Subtract(ror_back, ror_reference),
        JBeamVector3(1.0, 0.0, 0.0)));
    CHECK(Equal(Subtract(ror_left, ror_reference),
        JBeamVector3(0.0, 0.0, 1.0)));
    CHECK(Equal(Subtract(ror_up, ror_reference),
        JBeamVector3(0.0, 1.0, 0.0)));
}

} // namespace

int main()
{
    TestBasisAndTransformSemantics();
    TestInverseRoundTripsAndInPlaceConversion();
    TestFiniteValidationAndFailClosedResults();
    TestMetricAndOrientationPreservation();
    TestVehicleLandmarkFrame();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " coordinate transform checks failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "JBeam coordinate transform tests passed\n";
    return EXIT_SUCCESS;
}
