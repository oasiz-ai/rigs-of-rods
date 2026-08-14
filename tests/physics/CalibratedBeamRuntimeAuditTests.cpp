#include "CalibratedBeamRuntimeAudit.h"

#include <cstdint>
#include <cstring>
#include <iostream>

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

RoR::CalibratedBeamMaterialAdapter::Configuration Configuration()
{
    RoR::CalibratedBeamMaterialAdapter::Configuration configuration;
    configuration.cross_section_area_m2 = 0.1;
    configuration.material.elastic_modulus = 1.0e8;
    configuration.material.yield_stress = 1.0e9;
    configuration.material.hardening_modulus = 0.0;
    configuration.material.damage_onset_plastic_strain = 1.0;
    configuration.material.damage_driver_capacity_density = 1.0e12;
    return configuration;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&bits);
    unsigned char* destination = reinterpret_cast<unsigned char*>(&value);
    for (std::size_t index = 0U; index < sizeof(value); ++index)
        destination[index] = source[index];
    return value;
}

void TestCountsAndMaxima()
{
    using namespace RoR::CalibratedBeamRuntimeAudit;
    const RoR::CalibratedBeamMaterialAdapter::Configuration configuration =
        Configuration();
    RoR::CalibratedBeamMaterial::State first;
    first.last_total_strain = -0.125;
    RoR::CalibratedBeamMaterialAdapter::Configuration plastic_configuration =
        configuration;
    plastic_configuration.material.elastic_modulus = 100.0;
    plastic_configuration.material.yield_stress = 10.0;
    const RoR::CalibratedBeamMaterial::Response plastic_response =
        RoR::CalibratedBeamMaterial::Update(
            0.2,
            plastic_configuration.material,
            RoR::CalibratedBeamMaterial::State());
    CHECK(plastic_response.IsValid());
    const RoR::CalibratedBeamMaterial::State second = plastic_response.state;

    Builder builder;
    builder.Add(Sample(), 0U);
    Sample one;
    one.enabled = true;
    one.configuration = &configuration;
    one.state = &first;
    builder.Add(one, 7U);
    Sample two;
    two.enabled = true;
    two.faulted = true;
    two.fractured = true;
    two.disabled = true;
    two.configuration = &plastic_configuration;
    two.state = &second;
    builder.Add(two, 9U);

    const Result& result = builder.Get();
    CHECK(result.schema_version == AUDIT_SCHEMA_VERSION);
    CHECK(result.calibrated_count == 2U);
    CHECK(result.fault_count == 1U);
    CHECK(result.fracture_count == 1U);
    CHECK(result.disabled_count == 1U);
    CHECK(result.active_history_count == 2U);
    CHECK(result.finite);
    CHECK(result.state_valid);
    CHECK(result.first_invalid_index == UINT32_MAX);
    CHECK(result.max_abs_total_strain == 0.2);
    CHECK(result.max_accumulated_plastic_strain ==
        second.accumulated_plastic_strain);
    CHECK(result.max_damage == second.damage);
}

void TestHostileStateFailsClosed()
{
    using namespace RoR::CalibratedBeamRuntimeAudit;
    const RoR::CalibratedBeamMaterialAdapter::Configuration configuration =
        Configuration();

    RoR::CalibratedBeamMaterial::State invalid_range;
    invalid_range.damage = 1.5;
    Sample invalid;
    invalid.enabled = true;
    invalid.configuration = &configuration;
    invalid.state = &invalid_range;

    RoR::CalibratedBeamMaterial::State nonfinite;
    nonfinite.last_total_strain =
        DoubleFromBits(UINT64_C(0x7ff8000000000042));
    Sample nan;
    nan.enabled = true;
    nan.configuration = &configuration;
    nan.state = &nonfinite;

    Builder builder;
    builder.Add(invalid, 12U);
    builder.Add(nan, 13U);
    const Result& result = builder.Get();
    CHECK(result.calibrated_count == 2U);
    CHECK(!result.finite);
    CHECK(!result.state_valid);
    CHECK(result.first_invalid_index == 12U);
    CHECK(result.active_history_count == 0U);
    CHECK(result.max_abs_total_strain == 0.0);
}

void TestMissingBindingsFailClosed()
{
    using namespace RoR::CalibratedBeamRuntimeAudit;
    Sample sample;
    sample.enabled = true;
    Builder builder;
    builder.Add(sample, 42U);
    CHECK(builder.Get().calibrated_count == 1U);
    CHECK(builder.Get().finite);
    CHECK(!builder.Get().state_valid);
    CHECK(builder.Get().first_invalid_index == 42U);

    RoR::CalibratedBeamMaterialAdapter::Configuration invalid_configuration =
        Configuration();
    invalid_configuration.cross_section_area_m2 = 0.0;
    RoR::CalibratedBeamMaterial::State state;
    Sample invalid;
    invalid.enabled = true;
    invalid.configuration = &invalid_configuration;
    invalid.state = &state;
    Builder invalid_builder;
    invalid_builder.Add(invalid, 43U);
    CHECK(invalid_builder.Get().finite);
    CHECK(!invalid_builder.Get().state_valid);
    CHECK(invalid_builder.Get().first_invalid_index == 43U);
}

} // namespace

int main()
{
    TestCountsAndMaxima();
    TestHostileStateFailsClosed();
    TestMissingBindingsFailClosed();
    if (g_failures != 0)
        return 1;
    std::cout << "calibrated beam runtime audit tests passed\n";
    return 0;
}
