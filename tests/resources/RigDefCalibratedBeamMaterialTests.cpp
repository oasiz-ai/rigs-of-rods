/*
    Dependency-light contract tests for the authored calibrated beam directive.
*/

#include "RigDef_CalibratedBeamMaterial.h"

#include <cstring>
#include <iostream>
#include <locale>
#include <string>
#include <vector>

namespace {

int g_failed_checks = 0;

void Check(
    bool condition,
    const char* expression,
    const char* file,
    int line)
{
    if (!condition)
    {
        std::cerr << file << ':' << line
            << ": check failed: " << expression << '\n';
        ++g_failed_checks;
    }
}

#define CHECK(expression) \
    Check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

bool SameBits(double first, double second)
{
    return std::memcmp(&first, &second, sizeof(first)) == 0;
}

bool SameDefaults(
    const RigDef::CalibratedBeamMaterialDefaults& first,
    const RigDef::CalibratedBeamMaterialDefaults& second)
{
    return
        first.schema_version == second.schema_version &&
        first.enabled == second.enabled &&
        SameBits(
            first.cross_section_area_m2,
            second.cross_section_area_m2) &&
        SameBits(
            first.elastic_modulus_pa,
            second.elastic_modulus_pa) &&
        SameBits(
            first.yield_stress_pa,
            second.yield_stress_pa) &&
        SameBits(
            first.hardening_modulus_pa,
            second.hardening_modulus_pa) &&
        SameBits(
            first.damage_onset_plastic_strain,
            second.damage_onset_plastic_strain) &&
        SameBits(
            first.damage_driver_capacity_density_j_m3,
            second.damage_driver_capacity_density_j_m3) &&
        first._is_user_defined == second._is_user_defined;
}

bool SameRuntime(
    const RoR::CalibratedBeamMaterialAdapter::Runtime& first,
    const RoR::CalibratedBeamMaterialAdapter::Runtime& second)
{
    return
        first.enabled == second.enabled &&
        first.faulted == second.faulted &&
        first.configuration.schema_version ==
            second.configuration.schema_version &&
        SameBits(
            first.configuration.cross_section_area_m2,
            second.configuration.cross_section_area_m2) &&
        first.configuration.material.schema_version ==
            second.configuration.material.schema_version &&
        SameBits(
            first.configuration.material.elastic_modulus,
            second.configuration.material.elastic_modulus) &&
        SameBits(
            first.configuration.material.yield_stress,
            second.configuration.material.yield_stress) &&
        SameBits(
            first.configuration.material.hardening_modulus,
            second.configuration.material.hardening_modulus) &&
        SameBits(
            first.configuration.material.damage_onset_plastic_strain,
            second.configuration.material.damage_onset_plastic_strain) &&
        SameBits(
            first.configuration.material.
                damage_driver_capacity_density,
            second.configuration.material.
                damage_driver_capacity_density) &&
        SameBits(
            first.state.plastic_strain,
            second.state.plastic_strain) &&
        SameBits(
            first.state.accumulated_plastic_strain,
            second.state.accumulated_plastic_strain) &&
        SameBits(first.state.damage, second.state.damage) &&
        SameBits(
            first.state.damage_driver_density,
            second.state.damage_driver_density) &&
        SameBits(
            first.state.last_total_strain,
            second.state.last_total_strain) &&
        first.state.fractured == second.state.fractured &&
        first.last_error == second.last_error &&
        first.last_material_error == second.last_material_error;
}

struct TestBeamDefaults
{
    int legacy_marker = 0;
    RigDef::CalibratedBeamMaterialDefaults calibrated_material;
};

class CommaDecimalPunctuation : public std::numpunct<char>
{
protected:
    char do_decimal_point() const override
    {
        return ',';
    }
};

void TestLegacyDefaultIsInert()
{
    RigDef::CalibratedBeamMaterialDefaults defaults;
    CHECK(!defaults.enabled);
    CHECK(!defaults._is_user_defined);

    std::string arguments = "untouched";
    CHECK(RigDef::TryFormatCalibratedBeamMaterialDirectiveArguments(
        defaults,
        arguments));
    CHECK(arguments.empty());
}

void TestValidDirectiveRoundTrip()
{
    const std::vector<std::string> fields = {
        "1", "on", "0.00012345678901234567", "210000000000",
        "355000000", "1200000000", "0.0175", "85000000"
    };
    RigDef::CalibratedBeamMaterialDefaults first;
    const RigDef::CalibratedBeamMaterialDirectiveResult parsed =
        RigDef::TryParseCalibratedBeamMaterialDirective(fields, first);
    CHECK(parsed.IsValid());
    CHECK(first.enabled);
    CHECK(first._is_user_defined);

    RoR::CalibratedBeamMaterialAdapter::Configuration configuration;
    CHECK(RigDef::TryBuildCalibratedBeamMaterialConfiguration(
        first,
        configuration));

    std::string arguments;
    CHECK(RigDef::TryFormatCalibratedBeamMaterialDirectiveArguments(
        first,
        arguments));

    std::vector<std::string> serialized_fields;
    std::size_t start = 0;
    while (start <= arguments.size())
    {
        const std::size_t delimiter = arguments.find(',', start);
        std::string field = arguments.substr(
            start,
            delimiter == std::string::npos
                ? std::string::npos
                : delimiter - start);
        while (!field.empty() && field.front() == ' ')
            field.erase(field.begin());
        while (!field.empty() && field.back() == ' ')
            field.pop_back();
        serialized_fields.push_back(field);
        if (delimiter == std::string::npos)
            break;
        start = delimiter + 1;
    }

    RigDef::CalibratedBeamMaterialDefaults second;
    CHECK(RigDef::TryParseCalibratedBeamMaterialDirective(
        serialized_fields,
        second).IsValid());
    CHECK(SameBits(first.cross_section_area_m2,
        second.cross_section_area_m2));
    CHECK(SameBits(first.elastic_modulus_pa,
        second.elastic_modulus_pa));
    CHECK(SameBits(first.yield_stress_pa,
        second.yield_stress_pa));
    CHECK(SameBits(first.hardening_modulus_pa,
        second.hardening_modulus_pa));
    CHECK(SameBits(first.damage_onset_plastic_strain,
        second.damage_onset_plastic_strain));
    CHECK(SameBits(first.damage_driver_capacity_density_j_m3,
        second.damage_driver_capacity_density_j_m3));
}

void TestDisableAndInvalidInputAreExplicit()
{
    RigDef::CalibratedBeamMaterialDefaults disabled;
    CHECK(RigDef::TryParseCalibratedBeamMaterialDirective(
        {"1", "OFF"},
        disabled).IsValid());
    CHECK(!disabled.enabled);
    CHECK(disabled._is_user_defined);

    std::string arguments;
    CHECK(RigDef::TryFormatCalibratedBeamMaterialDirectiveArguments(
        disabled,
        arguments));
    CHECK(arguments == "1, off");

    RigDef::CalibratedBeamMaterialDefaults unchanged;
    unchanged.enabled = true;
    const RigDef::CalibratedBeamMaterialDefaults snapshot = unchanged;
    CHECK(!RigDef::TryParseCalibratedBeamMaterialDirective(
        {"2", "off"},
        unchanged).IsValid());
    CHECK(SameDefaults(unchanged, snapshot));
    CHECK(!RigDef::TryParseCalibratedBeamMaterialDirective(
        {"1", "on", "nan", "2e11", "3e8", "0", "0.01", "5e7"},
        unchanged).IsValid());
    CHECK(SameDefaults(unchanged, snapshot));
    CHECK(!RigDef::TryParseCalibratedBeamMaterialDirective(
        {"1", "on", "1e-4junk", "2e11", "3e8", "0", "0.01", "5e7"},
        unchanged).IsValid());
    CHECK(SameDefaults(unchanged, snapshot));
    CHECK(!RigDef::TryParseCalibratedBeamMaterialDirective(
        {"1", "on", "-1e-4", "2e11", "3e8", "0", "0.01", "5e7"},
        unchanged).IsValid());
    CHECK(SameDefaults(unchanged, snapshot));
    CHECK(!RigDef::TryParseCalibratedBeamMaterialDirective(
        {"1", "on", "1e-4", "0", "3e8", "0", "0.01", "5e7"},
        unchanged).IsValid());
    CHECK(SameDefaults(unchanged, snapshot));
}

void TestConfigurationDiagnosticsIdentifyAuthoredFields()
{
    struct InvalidFixture
    {
        std::vector<std::string> fields;
        std::size_t expected_field_index;
    };
    const std::vector<InvalidFixture> fixtures = {
        {{"1", "on", "-1", "2e11", "3e8", "0", "0.01", "5e7"}, 2},
        {{"1", "on", "1e-4", "0", "3e8", "0", "0.01", "5e7"}, 3},
        {{"1", "on", "1e-4", "2e11", "0", "0", "0.01", "5e7"}, 4},
        {{"1", "on", "1e-4", "2e11", "3e8", "-1", "0.01", "5e7"}, 5},
        {{"1", "on", "1e-4", "2e11", "3e8", "0", "-1", "5e7"}, 6},
        {{"1", "on", "1e-4", "2e11", "3e8", "0", "0.01", "0"}, 7}
    };

    for (const InvalidFixture& fixture : fixtures)
    {
        RigDef::CalibratedBeamMaterialDefaults authored;
        const RigDef::CalibratedBeamMaterialDirectiveResult result =
            RigDef::TryParseCalibratedBeamMaterialDirective(
                fixture.fields,
                authored);
        CHECK(!result.IsValid());
        CHECK(result.error ==
            RigDef::CalibratedBeamMaterialDirectiveError::
                INVALID_CONFIGURATION);
        CHECK(result.HasField());
        CHECK(result.field_index == fixture.expected_field_index);
    }

    RigDef::CalibratedBeamMaterialDefaults authored;
    const RigDef::CalibratedBeamMaterialDirectiveResult wrong_count =
        RigDef::TryParseCalibratedBeamMaterialDirective(
            {"1", "on"},
            authored);
    CHECK(!wrong_count.IsValid());
    CHECK(!wrong_count.HasField());
}

void TestParserCopyOnWriteTransition()
{
    std::shared_ptr<TestBeamDefaults> active(new TestBeamDefaults());
    active->legacy_marker = 73;
    const std::shared_ptr<TestBeamDefaults> legacy_snapshot = active;

    const RigDef::CalibratedBeamMaterialDirectiveResult enabled =
        RigDef::TryApplyCalibratedBeamMaterialDirective(
            {"1", "on", "1e-4", "2e11", "3e8", "0", "0.01", "5e7"},
            active);
    CHECK(enabled.IsValid());
    CHECK(active != legacy_snapshot);
    CHECK(active->legacy_marker == 73);
    CHECK(active->calibrated_material.enabled);
    CHECK(!legacy_snapshot->calibrated_material.enabled);

    const std::shared_ptr<TestBeamDefaults> enabled_snapshot = active;
    const RigDef::CalibratedBeamMaterialDefaults enabled_material =
        active->calibrated_material;
    const RigDef::CalibratedBeamMaterialDirectiveResult invalid =
        RigDef::TryApplyCalibratedBeamMaterialDirective(
            {"2", "off"},
            active);
    CHECK(!invalid.IsValid());
    CHECK(active == enabled_snapshot);
    CHECK(SameDefaults(
        active->calibrated_material,
        enabled_material));

    const RigDef::CalibratedBeamMaterialDirectiveResult disabled =
        RigDef::TryApplyCalibratedBeamMaterialDirective(
            {"1", "off"},
            active);
    CHECK(disabled.IsValid());
    CHECK(active != enabled_snapshot);
    CHECK(!active->calibrated_material.enabled);
    CHECK(active->calibrated_material._is_user_defined);
    CHECK(enabled_snapshot->calibrated_material.enabled);
}

void TestActorSpawnerPreparationIsAtomicAndFailClosed()
{
    RigDef::CalibratedBeamMaterialDefaults authored;
    CHECK(RigDef::TryParseCalibratedBeamMaterialDirective(
        {"1", "on", "1e-4", "2e11", "3e8", "0", "0.01", "5e7"},
        authored).IsValid());

    RoR::CalibratedBeamMaterialAdapter::Runtime runtime;
    RoR::CalibratedBeamMaterialAdapter::Error adapter_error =
        RoR::CalibratedBeamMaterialAdapter::Error::NONE;
    RoR::CalibratedBeamMaterial::Error material_error =
        RoR::CalibratedBeamMaterial::Error::NONE;
    CHECK(RigDef::TryPrepareCalibratedBeamMaterialForSpawn(
        authored,
        true,
        runtime,
        &adapter_error,
        &material_error));
    CHECK(adapter_error ==
        RoR::CalibratedBeamMaterialAdapter::Error::NONE);
    CHECK(material_error == RoR::CalibratedBeamMaterial::Error::NONE);
    CHECK(runtime.enabled);
    CHECK(runtime.configuration.cross_section_area_m2 ==
        authored.cross_section_area_m2);

    const RoR::CalibratedBeamMaterialAdapter::Runtime configured = runtime;
    CHECK(!RigDef::TryPrepareCalibratedBeamMaterialForSpawn(
        authored,
        false,
        runtime,
        &adapter_error,
        &material_error));
    CHECK(adapter_error ==
        RoR::CalibratedBeamMaterialAdapter::Error::
            UNSUPPORTED_BEAM_ROLE);
    CHECK(SameRuntime(runtime, configured));

    RigDef::CalibratedBeamMaterialDefaults invalid = authored;
    invalid.elastic_modulus_pa = 0.0;
    CHECK(!RigDef::TryPrepareCalibratedBeamMaterialForSpawn(
        invalid,
        true,
        runtime,
        &adapter_error,
        &material_error));
    CHECK(adapter_error ==
        RoR::CalibratedBeamMaterialAdapter::Error::MATERIAL_FAILURE);
    CHECK(material_error ==
        RoR::CalibratedBeamMaterial::Error::INVALID_ELASTIC_MODULUS);
    CHECK(SameRuntime(runtime, configured));

    RigDef::CalibratedBeamMaterialDefaults legacy;
    CHECK(!RigDef::TryPrepareCalibratedBeamMaterialForSpawn(
        legacy,
        true,
        runtime,
        &adapter_error,
        &material_error));
    CHECK(adapter_error ==
        RoR::CalibratedBeamMaterialAdapter::Error::DISABLED);
    CHECK(SameRuntime(runtime, configured));
}

void TestSerializerRoleTransitionsFailClosed()
{
    RigDef::CalibratedBeamMaterialDefaults authored_on;
    CHECK(RigDef::TryParseCalibratedBeamMaterialDirective(
        {"1", "on", "1e-4", "2e11", "3e8", "0", "0.01", "5e7"},
        authored_on).IsValid());

    RigDef::CalibratedBeamMaterialDefaults authored_off;
    CHECK(RigDef::TryParseCalibratedBeamMaterialDirective(
        {"1", "off"},
        authored_off).IsValid());

    bool enabled = false;
    const RigDef::CalibratedBeamMaterialDefaults legacy;
    RigDef::CalibratedBeamMaterialSerializationTransition transition =
        RigDef::AdvanceCalibratedBeamMaterialSerialization(
            &legacy,
            true,
            enabled);
    CHECK(!transition.emit_directive);
    CHECK(transition.authored_state_valid);
    CHECK(!enabled);

    transition =
        RigDef::AdvanceCalibratedBeamMaterialSerialization(
            &authored_on,
            true,
            enabled);
    CHECK(transition.emit_directive);
    CHECK(transition.authored_state_valid);
    CHECK(transition.arguments.find("1, on, ") == 0);
    CHECK(enabled);

    // A support/rope/specialized role must close the global opt-in.
    transition = RigDef::AdvanceCalibratedBeamMaterialSerialization(
        &authored_on,
        false,
        enabled);
    CHECK(transition.emit_directive);
    CHECK(transition.arguments == "1, off");
    CHECK(!enabled);

    // A later plain beam using the same defaults must explicitly opt in again.
    transition = RigDef::AdvanceCalibratedBeamMaterialSerialization(
        &authored_on,
        true,
        enabled);
    CHECK(transition.emit_directive);
    CHECK(transition.arguments.find("1, on, ") == 0);
    CHECK(enabled);

    transition = RigDef::AdvanceCalibratedBeamMaterialSerialization(
        &authored_off,
        true,
        enabled);
    CHECK(transition.emit_directive);
    CHECK(transition.arguments == "1, off");
    CHECK(!enabled);

    // Explicit authored off remains serializable even when already disabled.
    transition = RigDef::AdvanceCalibratedBeamMaterialSerialization(
        &authored_off,
        true,
        enabled);
    CHECK(transition.emit_directive);
    CHECK(transition.arguments == "1, off");
    CHECK(!enabled);

    RigDef::CalibratedBeamMaterialDefaults invalid = authored_on;
    invalid.schema_version = 2;
    enabled = true;
    transition = RigDef::AdvanceCalibratedBeamMaterialSerialization(
        &invalid,
        true,
        enabled);
    CHECK(!transition.authored_state_valid);
    CHECK(transition.emit_directive);
    CHECK(transition.arguments == "1, off");
    CHECK(!enabled);
}

void TestFiniteDecimalParsingUsesTheClassicLocale()
{
    const std::locale original_locale = std::locale();
    std::locale::global(std::locale(
        std::locale::classic(),
        new CommaDecimalPunctuation));

    double parsed = 0.0;
    CHECK(RigDef::TryParseFiniteDouble("1234.5", parsed));
    CHECK(parsed == 1234.5);
    CHECK(!RigDef::TryParseFiniteDouble("1234,5", parsed));

    std::locale::global(original_locale);
}

} // namespace

int main()
{
    TestLegacyDefaultIsInert();
    TestValidDirectiveRoundTrip();
    TestDisableAndInvalidInputAreExplicit();
    TestConfigurationDiagnosticsIdentifyAuthoredFields();
    TestParserCopyOnWriteTransition();
    TestActorSpawnerPreparationIsAtomicAndFailClosed();
    TestSerializerRoleTransitionsFailClosed();
    TestFiniteDecimalParsingUsesTheClassicLocale();

    if (g_failed_checks != 0)
    {
        std::cerr << "RigDef calibrated beam material tests failed: "
            << g_failed_checks << " check(s)\n";
        return 1;
    }

    std::cout << "RigDef calibrated beam material tests passed\n";
    return 0;
}
