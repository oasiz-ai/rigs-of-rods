#include "JBeamExpressionEvaluator.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

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

using RoR::BeamNG::EvaluateJBeamExpression;
using RoR::BeamNG::JBeamExpressionDiagnosticCode;
using RoR::BeamNG::JBeamExpressionEnvironment;
using RoR::BeamNG::JBeamExpressionLimits;
using RoR::BeamNG::JBeamExpressionResult;
using RoR::BeamNG::JBeamExpressionValue;
using RoR::BeamNG::JBeamExpressionValueType;
using RoR::BeamNG::JBeamExpressionVariable;
using RoR::BeamNG::SerializeCanonicalJBeamExpressionValue;

bool HasCode(
    const JBeamExpressionResult& result,
    JBeamExpressionDiagnosticCode code)
{
    for (std::size_t index = 0U;
         index < result.diagnostics.size();
         ++index)
    {
        if (result.diagnostics[index].code == code)
        {
            return true;
        }
    }
    return false;
}

void CheckNumber(const std::string& expression, double expected)
{
    const JBeamExpressionResult result =
        EvaluateJBeamExpression(expression);
    CHECK(result.IsValid());
    if (result.IsValid())
    {
        CHECK(result.value.type == JBeamExpressionValueType::NUMBER);
        CHECK(result.value.number_value == expected);
    }
}

void CheckBoolean(const std::string& expression, bool expected)
{
    const JBeamExpressionResult result =
        EvaluateJBeamExpression(expression);
    CHECK(result.IsValid());
    if (result.IsValid())
    {
        CHECK(result.value.type == JBeamExpressionValueType::BOOLEAN);
        CHECK(result.value.boolean_value == expected);
    }
}

void CheckString(
    const std::string& expression,
    const std::string& expected)
{
    const JBeamExpressionResult result =
        EvaluateJBeamExpression(expression);
    CHECK(result.IsValid());
    if (result.IsValid())
    {
        CHECK(result.value.type == JBeamExpressionValueType::STRING);
        CHECK(result.value.string_value == expected);
    }
}

void CheckCanonicalNumber(
    const std::string& expression,
    const std::string& expected)
{
    const JBeamExpressionResult result =
        EvaluateJBeamExpression(expression);
    CHECK(result.IsValid());
    if (result.IsValid())
    {
        CHECK(result.value.type == JBeamExpressionValueType::NUMBER);
        CHECK(
            SerializeCanonicalJBeamExpressionValue(result.value) ==
            expected);
    }
}

JBeamExpressionVariable Variable(
    const std::string& name,
    const JBeamExpressionValue& value)
{
    JBeamExpressionVariable variable;
    variable.name = name;
    variable.value = value;
    return variable;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    volatile double observed = value;
    return observed;
}

void TestArithmeticAndPrecedence()
{
    CheckNumber("$=2+3*4", 14.0);
    CheckNumber("$=(2+3)*4", 20.0);
    CheckNumber("$=20/5/2", 2.0);
    CheckNumber("$=8-3-2", 3.0);
    CheckNumber("$=2^3^2", 512.0);
    CheckNumber("$=-2^2", -4.0);
    CheckNumber("$=2^-2", 0.25);
    CheckNumber("$=--2", 2.0);
    CheckNumber("$=7%3", 1.0);
    CheckNumber("$=-7%3", 2.0);
    CheckNumber("$=7%-3", -2.0);
    CheckNumber("$=1. + .5 + 1e1", 11.5);
    CheckNumber("$=\n 2\t+\r3 ", 5.0);

    CheckBoolean("$=1+2*3 == 7", true);
    CheckBoolean("$=1 < 2", true);
    CheckBoolean("$=2 <= 2", true);
    CheckBoolean("$=3 > 2", true);
    CheckBoolean("$=3 >= 3", true);
    CheckBoolean("$=3 ~= 4", true);
    CheckBoolean("$=nil == nil", true);
    CheckBoolean("$=nil ~= false", true);
    CheckBoolean("$='abc' == 'abc'", true);
    CheckBoolean("$='abc' < 'abd'", true);
    CheckBoolean("$='abc' == 3", false);

    const JBeamExpressionResult mismatch =
        EvaluateJBeamExpression("$=true < false");
    CHECK(!mismatch.IsValid());
    CHECK(HasCode(
        mismatch, JBeamExpressionDiagnosticCode::TYPE_MISMATCH));
    const JBeamExpressionResult chained =
        EvaluateJBeamExpression("$=1 < 2 < 3");
    CHECK(!chained.IsValid());
    CHECK(HasCode(
        chained, JBeamExpressionDiagnosticCode::TYPE_MISMATCH));
}

void TestDeterministicPowerAndModuloIdentities()
{
    CheckCanonicalNumber(
        "$=2^3^2",
        "jbeam-expression-value-v1:number:4080000000000000");
    CheckCanonicalNumber(
        "$=2^-2",
        "jbeam-expression-value-v1:number:3fd0000000000000");
    CheckCanonicalNumber(
        "$=1.5^3",
        "jbeam-expression-value-v1:number:400b000000000000");
    CheckCanonicalNumber(
        "$=-7%3",
        "jbeam-expression-value-v1:number:4000000000000000");
    CheckCanonicalNumber(
        "$=7%-3",
        "jbeam-expression-value-v1:number:c000000000000000");
    CheckCanonicalNumber(
        "$=9007199254740992%3",
        "jbeam-expression-value-v1:number:4000000000000000");

    const char* rejected[] = {
        "$=5.5%2",
        "$=5%2.5",
        "$=9007199254740994%3",
        "$=2^0.5",
        "$=2^1025",
        "$=2^-1025"
    };
    const std::size_t rejected_count =
        sizeof(rejected) / sizeof(rejected[0]);
    for (std::size_t index = 0U; index < rejected_count; ++index)
    {
        const JBeamExpressionResult result =
            EvaluateJBeamExpression(rejected[index]);
        CHECK(!result.IsValid());
        CHECK(HasCode(
            result,
            JBeamExpressionDiagnosticCode::
                NON_DETERMINISTIC_OPERAND));
        if (!result.diagnostics.empty())
        {
            CHECK(
                std::string(
                    RoR::BeamNG::
                        JBeamExpressionDiagnosticCodeToString(
                            result.diagnostics[0].code)) ==
                "non-deterministic-operand");
        }
    }
}

void TestLogicalTernaryAndCase()
{
    CheckBoolean("$=not nil", true);
    CheckBoolean("$=not false", true);
    CheckBoolean("$=not 0", false);
    CheckBoolean("$=false and 1/0", false);
    CheckBoolean("$=true or 1/0", true);
    CheckNumber("$=nil or 4", 4.0);
    CheckNumber("$=0 and 5", 5.0);
    CheckBoolean("$=false or true and false", false);
    CheckNumber("$=true and 7 or 9", 7.0);
    CheckNumber("$=false and 7 or 9", 9.0);
    // This is the documented Lua idiom, including its false-value caveat.
    CheckNumber("$=true and false or 9", 9.0);

    JBeamExpressionEnvironment environment;
    environment.variables.push_back(
        Variable("$fallback", JBeamExpressionValue::Number(7.0)));
    const JBeamExpressionResult ternary = EvaluateJBeamExpression(
        "$=$missing == nil and $fallback or 2", environment);
    CHECK(ternary.IsValid());
    CHECK(ternary.value.type == JBeamExpressionValueType::NUMBER);
    CHECK(ternary.value.number_value == 7.0);

    CheckNumber("$=case(true, 3, 4)", 3.0);
    CheckNumber("$=case(false, 3, 4)", 4.0);
    CheckString("$=case(1 < 2, 'yes', 'no')", "yes");

    // BeamNG documents that case cannot protect arithmetic on nil operands:
    // it is eager, unlike and/or.
    const JBeamExpressionResult eager =
        EvaluateJBeamExpression("$=case(true, 1, 1/0)");
    CHECK(!eager.IsValid());
    CHECK(HasCode(
        eager, JBeamExpressionDiagnosticCode::DIVISION_BY_ZERO));

    const JBeamExpressionResult numeric_case =
        EvaluateJBeamExpression("$=case(1, 'a', 'b')");
    CHECK(!numeric_case.IsValid());
    CHECK(HasCode(
        numeric_case,
        JBeamExpressionDiagnosticCode::UNSUPPORTED_CASE_SIGNATURE));
}

void TestStrings()
{
    CheckString("$='my_'..'group'", "my_group");
    CheckString("$='a'..'b'..'c'", "abc");
    CheckString("$='can\\'t'..'\\\\'", "can't\\");
    CheckString("$='a\\nb'", "a\nb");
    CheckNumber("$=#'abc'", 3.0);
    CheckNumber("$=#'\xcf\x80'", 2.0);
    CheckBoolean("$='z' > 'a'", true);

    JBeamExpressionEnvironment environment;
    environment.variables.push_back(
        Variable("$prefix", JBeamExpressionValue::String("row1_")));
    environment.variables.push_back(
        Variable("$suffix", JBeamExpressionValue::String("_part1")));
    const JBeamExpressionResult namespaced = EvaluateJBeamExpression(
        "$=$prefix..'node1'..$suffix", environment);
    CHECK(namespaced.IsValid());
    CHECK(namespaced.value.type == JBeamExpressionValueType::STRING);
    CHECK(namespaced.value.string_value == "row1_node1_part1");

    const JBeamExpressionResult numeric_concat =
        EvaluateJBeamExpression("$='n='..(2)");
    CHECK(!numeric_concat.IsValid());
    CHECK(HasCode(
        numeric_concat, JBeamExpressionDiagnosticCode::TYPE_MISMATCH));
}

void TestTypedEnvironment()
{
    JBeamExpressionEnvironment environment;
    environment.variables.push_back(
        Variable("$n", JBeamExpressionValue::Number(2.5)));
    environment.variables.push_back(
        Variable("$flag", JBeamExpressionValue::Boolean(true)));
    environment.variables.push_back(
        Variable("$text", JBeamExpressionValue::String("front")));
    environment.variables.push_back(
        Variable("$none", JBeamExpressionValue::Nil()));
    environment.variables.push_back(
        Variable("$n", JBeamExpressionValue::Number(4.0)));

    JBeamExpressionResult result =
        EvaluateJBeamExpression("$=$n*2", environment);
    CHECK(result.IsValid());
    CHECK(result.value.number_value == 8.0);
    result = EvaluateJBeamExpression("$=$flag and $text or 'rear'", environment);
    CHECK(result.IsValid());
    CHECK(result.value.type == JBeamExpressionValueType::STRING);
    CHECK(result.value.string_value == "front");
    result = EvaluateJBeamExpression("$=$none == nil", environment);
    CHECK(result.IsValid());
    CHECK(result.value.boolean_value);
    result = EvaluateJBeamExpression("$=$absent == nil", environment);
    CHECK(result.IsValid());
    CHECK(result.value.boolean_value);

    result = EvaluateJBeamExpression(
        "$=$components.lightbarMaterials.glass_L_dmg == nil "
        "and 'policeparts_glass_white_dmg' "
        "or $components.lightbarMaterials.glass_L_dmg",
        environment);
    CHECK(result.IsValid());
    CHECK(result.value.type == JBeamExpressionValueType::STRING);
    CHECK(
        result.value.string_value ==
        "policeparts_glass_white_dmg");
    environment.variables.push_back(Variable(
        "$components.lightbarMaterials.glass_L_dmg",
        JBeamExpressionValue::String("formula_glass_dmg")));
    result = EvaluateJBeamExpression(
        "$=$components.lightbarMaterials.glass_L_dmg == nil "
        "and 'policeparts_glass_white_dmg' "
        "or $components.lightbarMaterials.glass_L_dmg",
        environment);
    CHECK(result.IsValid());
    CHECK(result.value.string_value == "formula_glass_dmg");

    JBeamExpressionEnvironment invalid_name;
    invalid_name.variables.push_back(
        Variable("n", JBeamExpressionValue::Number(1.0)));
    result = EvaluateJBeamExpression("$=1", invalid_name);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result,
        JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VARIABLE));

    invalid_name.variables[0].name = "$bad-name";
    result = EvaluateJBeamExpression("$=1", invalid_name);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result,
        JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VARIABLE));

    JBeamExpressionEnvironment invalid_type;
    JBeamExpressionValue bad_type;
    bad_type.type = static_cast<JBeamExpressionValueType>(99);
    invalid_type.variables.push_back(Variable("$bad", bad_type));
    result = EvaluateJBeamExpression("$=1", invalid_type);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result,
        JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VALUE));

    JBeamExpressionEnvironment invalid_utf8;
    invalid_utf8.variables.push_back(Variable(
        "$bad",
        JBeamExpressionValue::String(std::string("\xc0\x80", 2U))));
    result = EvaluateJBeamExpression("$=1", invalid_utf8);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result,
        JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VALUE));
}

void TestMalformedAndHostileSyntax()
{
    struct Failure
    {
        const char* source;
        JBeamExpressionDiagnosticCode code;
    };
    const Failure failures[] = {
        {"2+2", JBeamExpressionDiagnosticCode::MISSING_EXPRESSION_PREFIX},
        {"$=", JBeamExpressionDiagnosticCode::EXPECTED_VALUE},
        {"$=+", JBeamExpressionDiagnosticCode::EXPECTED_VALUE},
        {"$=+1", JBeamExpressionDiagnosticCode::EXPECTED_VALUE},
        {"$=1 2", JBeamExpressionDiagnosticCode::TRAILING_CONTENT},
        {"$=(1", JBeamExpressionDiagnosticCode::EXPECTED_TOKEN},
        {"$='abc", JBeamExpressionDiagnosticCode::UNTERMINATED_STRING},
        {"$='\\x'", JBeamExpressionDiagnosticCode::INVALID_ESCAPE},
        {"$=foo", JBeamExpressionDiagnosticCode::EXPECTED_VALUE},
        {"$=print(1)", JBeamExpressionDiagnosticCode::UNSUPPORTED_FUNCTION},
        {"$=random()", JBeamExpressionDiagnosticCode::UNSUPPORTED_FUNCTION},
        {"$=os.execute()", JBeamExpressionDiagnosticCode::INVALID_CHARACTER},
        {"$=$ordinary.path", JBeamExpressionDiagnosticCode::INVALID_CHARACTER},
        {"$=$components..path",
         JBeamExpressionDiagnosticCode::EXPECTED_VALUE},
        {"$=1?2:3", JBeamExpressionDiagnosticCode::INVALID_CHARACTER},
        {"$=a=1", JBeamExpressionDiagnosticCode::INVALID_CHARACTER},
        {"$=\"text\"", JBeamExpressionDiagnosticCode::INVALID_CHARACTER},
        {"$={}", JBeamExpressionDiagnosticCode::INVALID_CHARACTER},
        {"$=1e", JBeamExpressionDiagnosticCode::INVALID_NUMBER},
        {"$=.", JBeamExpressionDiagnosticCode::INVALID_CHARACTER},
        {"$=case(true,1)", JBeamExpressionDiagnosticCode::EXPECTED_TOKEN},
        {"$=case(true,1,2,3)",
         JBeamExpressionDiagnosticCode::EXPECTED_TOKEN},
        {"$=#1", JBeamExpressionDiagnosticCode::TYPE_MISMATCH},
        {"$=nil+1", JBeamExpressionDiagnosticCode::TYPE_MISMATCH}
    };
    const std::size_t failure_count =
        sizeof(failures) / sizeof(failures[0]);
    for (std::size_t index = 0U; index < failure_count; ++index)
    {
        const JBeamExpressionResult result =
            EvaluateJBeamExpression(failures[index].source);
        CHECK(!result.IsValid());
        CHECK(!result.has_value);
        CHECK(result.diagnostics.size() == 1U);
        CHECK(HasCode(result, failures[index].code));
    }

    const std::string invalid_utf8 =
        std::string("$='", 3U) + std::string("\xed\xa0\x80", 3U) + "'";
    const JBeamExpressionResult utf8 =
        EvaluateJBeamExpression(invalid_utf8);
    CHECK(!utf8.IsValid());
    CHECK(HasCode(
        utf8, JBeamExpressionDiagnosticCode::INVALID_UTF8));
}

void TestFiniteNumberEnforcement()
{
    const char* expressions[] = {
        "$=1e999",
        "$=1e308*1e308",
        "$=1e308+1e308",
        "$=2^1024"
    };
    const std::size_t count =
        sizeof(expressions) / sizeof(expressions[0]);
    for (std::size_t index = 0U; index < count; ++index)
    {
        const JBeamExpressionResult result =
            EvaluateJBeamExpression(expressions[index]);
        CHECK(!result.IsValid());
        CHECK(
            HasCode(
                result,
                JBeamExpressionDiagnosticCode::NON_FINITE_NUMBER) ||
            HasCode(
                result,
                JBeamExpressionDiagnosticCode::NON_FINITE_RESULT));
    }

    const JBeamExpressionResult zero_division =
        EvaluateJBeamExpression("$=1/0");
    CHECK(!zero_division.IsValid());
    CHECK(HasCode(
        zero_division,
        JBeamExpressionDiagnosticCode::DIVISION_BY_ZERO));
    const JBeamExpressionResult negative_zero_division =
        EvaluateJBeamExpression("$=1/-0");
    CHECK(!negative_zero_division.IsValid());
    CHECK(HasCode(
        negative_zero_division,
        JBeamExpressionDiagnosticCode::DIVISION_BY_ZERO));
    const JBeamExpressionResult negative_power_of_zero =
        EvaluateJBeamExpression("$=0^-1");
    CHECK(!negative_power_of_zero.IsValid());
    CHECK(HasCode(
        negative_power_of_zero,
        JBeamExpressionDiagnosticCode::DIVISION_BY_ZERO));

    const std::uint64_t nonfinite_bits[] = {
        UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff8000000000001)
    };
    for (std::size_t index = 0U; index < 3U; ++index)
    {
        JBeamExpressionEnvironment environment;
        environment.variables.push_back(Variable(
            "$bad",
            JBeamExpressionValue::Number(
                DoubleFromBits(nonfinite_bits[index]))));
        const JBeamExpressionResult result =
            EvaluateJBeamExpression("$=$bad", environment);
        CHECK(!result.IsValid());
        CHECK(HasCode(
            result,
            JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VALUE));
    }
}

void TestResourceLimits()
{
    JBeamExpressionLimits limits;
    limits.max_expression_bytes = 3U;
    JBeamExpressionResult result =
        EvaluateJBeamExpression("$=12", JBeamExpressionEnvironment(), limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result,
        JBeamExpressionDiagnosticCode::EXPRESSION_SIZE_LIMIT));

    limits = JBeamExpressionLimits();
    limits.max_tokens = 2U;
    result =
        EvaluateJBeamExpression("$=1+2", JBeamExpressionEnvironment(), limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result, JBeamExpressionDiagnosticCode::TOKEN_LIMIT));

    limits = JBeamExpressionLimits();
    limits.max_depth = 0U;
    result =
        EvaluateJBeamExpression("$=(1)", JBeamExpressionEnvironment(), limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result, JBeamExpressionDiagnosticCode::DEPTH_LIMIT));

    limits = JBeamExpressionLimits();
    limits.max_depth = 3U;
    result = EvaluateJBeamExpression(
        "$=-----1", JBeamExpressionEnvironment(), limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result, JBeamExpressionDiagnosticCode::DEPTH_LIMIT));

    limits = JBeamExpressionLimits();
    limits.max_work_units = 1U;
    result =
        EvaluateJBeamExpression("$=1", JBeamExpressionEnvironment(), limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result, JBeamExpressionDiagnosticCode::WORK_LIMIT));

    limits = JBeamExpressionLimits();
    limits.max_string_bytes = 2U;
    result = EvaluateJBeamExpression(
        "$='abc'", JBeamExpressionEnvironment(), limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result, JBeamExpressionDiagnosticCode::STRING_SIZE_LIMIT));

    limits = JBeamExpressionLimits();
    limits.max_output_string_bytes = 3U;
    result = EvaluateJBeamExpression(
        "$='ab'..'cd'", JBeamExpressionEnvironment(), limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result,
        JBeamExpressionDiagnosticCode::OUTPUT_STRING_SIZE_LIMIT));

    JBeamExpressionEnvironment environment;
    environment.variables.push_back(
        Variable("$a", JBeamExpressionValue::Number(1.0)));
    limits = JBeamExpressionLimits();
    limits.max_variables = 0U;
    result = EvaluateJBeamExpression("$=1", environment, limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result, JBeamExpressionDiagnosticCode::ENVIRONMENT_LIMIT));

    limits = JBeamExpressionLimits();
    limits.max_variable_name_bytes = 1U;
    result = EvaluateJBeamExpression("$=1", environment, limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result,
        JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VARIABLE));

    environment.variables[0].value =
        JBeamExpressionValue::String("abcd");
    limits = JBeamExpressionLimits();
    limits.max_environment_string_bytes = 3U;
    result = EvaluateJBeamExpression("$=1", environment, limits);
    CHECK(!result.IsValid());
    CHECK(HasCode(
        result, JBeamExpressionDiagnosticCode::ENVIRONMENT_LIMIT));
}

void TestCanonicalResults()
{
    const JBeamExpressionResult four_a =
        EvaluateJBeamExpression("$=2+2");
    const JBeamExpressionResult four_b =
        EvaluateJBeamExpression("$=8/2");
    CHECK(four_a.IsValid());
    CHECK(four_b.IsValid());
    CHECK(
        SerializeCanonicalJBeamExpressionValue(four_a.value) ==
        SerializeCanonicalJBeamExpressionValue(four_b.value));
    CHECK(
        SerializeCanonicalJBeamExpressionValue(four_a.value) ==
        "jbeam-expression-value-v1:number:4010000000000000");

    const JBeamExpressionResult zero =
        EvaluateJBeamExpression("$=0");
    const JBeamExpressionResult negative_zero =
        EvaluateJBeamExpression("$=-0");
    CHECK(zero.IsValid());
    CHECK(negative_zero.IsValid());
    CHECK(
        SerializeCanonicalJBeamExpressionValue(zero.value) ==
        SerializeCanonicalJBeamExpressionValue(negative_zero.value));

    CHECK(
        SerializeCanonicalJBeamExpressionValue(
            JBeamExpressionValue::String("a:b")) ==
        "jbeam-expression-value-v1:string:3:a:b");
    CHECK(
        SerializeCanonicalJBeamExpressionValue(
            JBeamExpressionValue::Boolean(true)) ==
        "jbeam-expression-value-v1:boolean:1");
    CHECK(
        SerializeCanonicalJBeamExpressionValue(
            JBeamExpressionValue::Nil()) ==
        "jbeam-expression-value-v1:nil");
    CHECK(
        SerializeCanonicalJBeamExpressionValue(
            JBeamExpressionValue::Number(
                DoubleFromBits(UINT64_C(0x7ff8000000000001)))) ==
        "jbeam-expression-value-v1:invalid");
}

void TestDeterminismAndBoundedMalformedCorpus()
{
    const std::string baseline =
        "$=($a == nil and 2^3 or $a)..'x'";
    JBeamExpressionEnvironment environment;
    environment.variables.push_back(
        Variable("$unused", JBeamExpressionValue::Number(9.0)));
    environment.variables.push_back(
        Variable("$a", JBeamExpressionValue::String("v")));
    const JBeamExpressionResult expected =
        EvaluateJBeamExpression(baseline, environment);
    CHECK(expected.IsValid());
    const std::string expected_canonical =
        SerializeCanonicalJBeamExpressionValue(expected.value);
    for (std::size_t iteration = 0U; iteration < 100U; ++iteration)
    {
        const JBeamExpressionResult repeated =
            EvaluateJBeamExpression(baseline, environment);
        CHECK(repeated.IsValid());
        CHECK(repeated.token_count == expected.token_count);
        CHECK(repeated.work_units == expected.work_units);
        CHECK(
            SerializeCanonicalJBeamExpressionValue(repeated.value) ==
            expected_canonical);
    }

    const std::string complete =
        "$=(case($x == nil, 'a', 'b')..'c')";
    for (std::size_t length = 0U; length <= complete.size(); ++length)
    {
        const JBeamExpressionResult prefix =
            EvaluateJBeamExpression(complete.substr(0U, length));
        CHECK(prefix.diagnostics.size() <= 1U);
        CHECK(prefix.IsValid() == (length == complete.size()));
    }

    for (unsigned int byte = 0U; byte <= 0xffU; ++byte)
    {
        std::string expression = "$=";
        expression.push_back(static_cast<char>(byte));
        const JBeamExpressionResult first =
            EvaluateJBeamExpression(expression);
        const JBeamExpressionResult second =
            EvaluateJBeamExpression(expression);
        CHECK(first.IsValid() == second.IsValid());
        CHECK(first.diagnostics.size() <= 1U);
        CHECK(first.diagnostics.size() == second.diagnostics.size());
        if (!first.diagnostics.empty())
        {
            CHECK(
                first.diagnostics[0].code ==
                second.diagnostics[0].code);
        }
    }

    // Deterministic clean-room hostile corpus: no parser state or diagnostic
    // count may depend on prior evaluations.
    std::uint32_t state = UINT32_C(0x6a09e667);
    for (std::size_t sample = 0U; sample < 2000U; ++sample)
    {
        std::string expression = "$=";
        const std::size_t length =
            static_cast<std::size_t>((state >> 24U) % 31U);
        for (std::size_t index = 0U; index < length; ++index)
        {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            expression.push_back(
                static_cast<char>((state >> 16U) & 0xffU));
        }
        const JBeamExpressionResult first =
            EvaluateJBeamExpression(expression);
        const JBeamExpressionResult second =
            EvaluateJBeamExpression(expression);
        CHECK(first.IsValid() == second.IsValid());
        CHECK(first.token_count == second.token_count);
        CHECK(first.work_units == second.work_units);
        CHECK(first.diagnostics.size() <= 1U);
        CHECK(first.diagnostics.size() == second.diagnostics.size());
        if (first.IsValid())
        {
            CHECK(
                SerializeCanonicalJBeamExpressionValue(first.value) ==
                SerializeCanonicalJBeamExpressionValue(second.value));
        }
        else if (!first.diagnostics.empty())
        {
            CHECK(
                first.diagnostics[0].code ==
                second.diagnostics[0].code);
            CHECK(
                first.diagnostics[0].byte_offset ==
                second.diagnostics[0].byte_offset);
        }
    }
}

} // namespace

int main()
{
    TestArithmeticAndPrecedence();
    TestDeterministicPowerAndModuloIdentities();
    TestLogicalTernaryAndCase();
    TestStrings();
    TestTypedEnvironment();
    TestMalformedAndHostileSyntax();
    TestFiniteNumberEnforcement();
    TestResourceLimits();
    TestCanonicalResults();
    TestDeterminismAndBoundedMalformedCorpus();
    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " JBeam expression evaluator test(s) failed\n";
        return 1;
    }
    std::cout << "JBeam expression evaluator tests passed\n";
    return 0;
}
