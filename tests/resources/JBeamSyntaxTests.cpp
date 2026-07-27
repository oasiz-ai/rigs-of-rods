#include "JBeamSyntax.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

using RoR::BeamNG::JBeamDiagnostic;
using RoR::BeamNG::JBeamDiagnosticCode;
using RoR::BeamNG::JBeamFieldAssignment;
using RoR::BeamNG::JBeamFieldLookupMetrics;
using RoR::BeamNG::JBeamNormalizeLimits;
using RoR::BeamNG::JBeamNormalizedTable;
using RoR::BeamNG::JBeamNormalizedTableEntryKind;
using RoR::BeamNG::JBeamObjectField;
using RoR::BeamNG::JBeamParseLimits;
using RoR::BeamNG::JBeamParseResult;
using RoR::BeamNG::JBeamValue;
using RoR::BeamNG::JBeamValueType;

const JBeamDiagnostic* FindDiagnostic(
    const std::vector<JBeamDiagnostic>& diagnostics,
    JBeamDiagnosticCode code)
{
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        if (diagnostics[i].code == code)
        {
            return &diagnostics[i];
        }
    }
    return NULL;
}

const JBeamValue* LastField(
    const JBeamValue& object,
    const std::string& key)
{
    const JBeamObjectField* field =
        RoR::BeamNG::FindLastJBeamObjectField(object, key);
    return field != NULL ? field->value.get() : NULL;
}

JBeamValue StringValue(const std::string& value)
{
    JBeamValue output;
    output.type = JBeamValueType::STRING;
    output.scalar_text = value;
    return output;
}

JBeamValue NumberValue(double value)
{
    JBeamValue output;
    output.type = JBeamValueType::NUMBER;
    output.number_value = value;
    return output;
}

std::shared_ptr<const JBeamValue> SharedValue(const JBeamValue& value)
{
    return std::shared_ptr<const JBeamValue>(new JBeamValue(value));
}

void TestCommentsOptionalCommasAndScalars()
{
    const std::string source =
        "{\r\n"
        "  // all separators below are deliberately omitted\r\n"
        "  \"part\": {\r\n"
        "    \"slotType\": \"main\"\r\n"
        "    /* expressions remain inert strings for the resolver */\r\n"
        "    \"spring\": \"$=$base*2\"\r\n"
        "    \"enabled\": true\r\n"
        "    \"none\": null\r\n"
        "    \"numbers\": [0 -2.5 6.02e23]\r\n"
        "  }\r\n"
        "}\r\n";
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, "vehicles/test/main.jbeam");
    CHECK(parsed.IsValid());
    CHECK(parsed.diagnostics.empty());
    CHECK(parsed.root.type == JBeamValueType::OBJECT);
    CHECK(parsed.root.span.begin.line == 1);
    CHECK(parsed.root.span.begin.column == 1);
    CHECK(parsed.root.span.end.line == 11);
    CHECK(parsed.root.span.end.column == 2);

    const JBeamValue* part = LastField(parsed.root, "part");
    CHECK(part != NULL);
    CHECK(part->type == JBeamValueType::OBJECT);
    const JBeamValue* slot_type = LastField(*part, "slotType");
    const JBeamValue* spring = LastField(*part, "spring");
    const JBeamValue* enabled = LastField(*part, "enabled");
    const JBeamValue* none = LastField(*part, "none");
    const JBeamValue* numbers = LastField(*part, "numbers");
    CHECK(slot_type != NULL && slot_type->scalar_text == "main");
    CHECK(spring != NULL && spring->scalar_text == "$=$base*2");
    CHECK(
        enabled != NULL && enabled->type == JBeamValueType::BOOLEAN &&
        enabled->boolean_value);
    CHECK(none != NULL && none->type == JBeamValueType::NULL_VALUE);
    CHECK(numbers != NULL && numbers->array_values.size() == 3);
    CHECK(numbers->array_values[0].scalar_text == "0");
    CHECK(numbers->array_values[1].scalar_text == "-2.5");
    CHECK(numbers->array_values[2].scalar_text == "6.02e23");
}

void TestUnicodeEscapesAndByteSpans()
{
    const std::string source =
        "{\"escaped\":\"A\\u00df\\u6771\\ud834\\udd1e\","
        "\"raw\":\"caf\xc3\xa9\"}";
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, "unicode.jbeam");
    CHECK(parsed.IsValid());
    const JBeamValue* escaped = LastField(parsed.root, "escaped");
    const JBeamValue* raw = LastField(parsed.root, "raw");
    CHECK(escaped != NULL);
    CHECK(escaped->scalar_text == "A\xc3\x9f\xe6\x9d\xb1\xf0\x9d\x84\x9e");
    CHECK(raw != NULL && raw->scalar_text == "caf\xc3\xa9");
    CHECK(raw->span.begin.column == 46);
    CHECK(raw->span.end.column == 53);
}

void TestDuplicateKeysAreOrderedAndLastWins()
{
    const std::string source =
        "{\"Part\":{\"Mass\":1,\"mass\":2,\"Mass\":3}}";
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, "duplicates.jbeam");
    CHECK(parsed.IsValid());
    const JBeamDiagnostic* duplicate = FindDiagnostic(
        parsed.diagnostics,
        JBeamDiagnosticCode::DUPLICATE_OBJECT_KEY);
    CHECK(duplicate != NULL);
    CHECK(duplicate->span.begin.line == 1);
    CHECK(duplicate->span.begin.column == 28);

    const JBeamValue* part = LastField(parsed.root, "Part");
    CHECK(part != NULL && part->object_fields.size() == 3);
    CHECK(part->object_fields[0].key == "Mass");
    CHECK(part->object_fields[1].key == "mass");
    CHECK(part->object_fields[2].key == "Mass");
    const JBeamValue* upper = LastField(*part, "Mass");
    const JBeamValue* lower = LastField(*part, "mass");
    CHECK(upper != NULL && upper->number_value == 3.0);
    CHECK(lower != NULL && lower->number_value == 2.0);
}

void TestTableNormalizationPreservesSemantics()
{
    const std::string source =
        "{\n"
        " \"vehicle\": {\n"
        "  \"nodes\": [\n"
        "   [\"id\", \"posX\", \"posX\", \"posZ\"],\n"
        "   {\"nodeWeight\": 2, \"group\": \"body\"},\n"
        "   {\"nodeWeight\": 3, \"nodeWeight\": 4},\n"
        "   [\"n1\", 1, 2, 3, {\"group\":\"local\","
        "\"nodeWeight\":5,\"nodeWeight\":6}],\n"
        "   [\"short\", 0],\n"
        "   [\"long\", 0, 1, 2, 3],\n"
        "   false\n"
        "  ]\n"
        " }\n"
        "}\n";
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, "vehicles/test/main.jbeam");
    CHECK(parsed.IsValid());

    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(parsed.root);
    CHECK(normalized.IsValid());
    CHECK(normalized.tables.size() == 1);
    CHECK(
        normalized.tables[0].path ==
        "$/vehicle#0/nodes#0");
    const JBeamNormalizedTable& table = normalized.tables[0];
    CHECK(table.columns.size() == 4);
    CHECK(table.columns[1].name == "posX");
    CHECK(table.columns[2].name == "posX");
    CHECK(table.entries.size() == 6);
    CHECK(
        table.entries[0].kind ==
        JBeamNormalizedTableEntryKind::DEFAULT_MODIFIER);
    CHECK(
        table.entries[1].kind ==
        JBeamNormalizedTableEntryKind::DEFAULT_MODIFIER);
    CHECK(
        table.entries[2].kind ==
        JBeamNormalizedTableEntryKind::DATA_ROW);

    const RoR::BeamNG::JBeamNormalizedDataRow& row =
        table.entries[2].data_row;
    CHECK(row.raw_row.array_values.size() == 5);
    CHECK(row.inherited_assignment_storage != NULL);
    CHECK(row.inherited_assignment_count == 4);
    CHECK(row.positional_assignments.size() == 4);
    CHECK(row.positional_assignments[1].name == "posX");
    CHECK(row.positional_assignments[2].name == "posX");
    CHECK(row.row_local_assignments.size() == 3);

    const JBeamFieldAssignment* weight =
        RoR::BeamNG::FindEffectiveJBeamField(row, "nodeWeight");
    const JBeamFieldAssignment* group =
        RoR::BeamNG::FindEffectiveJBeamField(row, "group");
    const JBeamFieldAssignment* pos_x =
        RoR::BeamNG::FindEffectiveJBeamField(row, "posX");
    CHECK(weight != NULL && weight->value->number_value == 6.0);
    CHECK(group != NULL && group->value->scalar_text == "local");
    CHECK(pos_x != NULL && pos_x->value->number_value == 2.0);

    CHECK(
        FindDiagnostic(
            normalized.diagnostics,
            JBeamDiagnosticCode::DUPLICATE_TABLE_HEADER) != NULL);
    CHECK(
        FindDiagnostic(
            normalized.diagnostics,
            JBeamDiagnosticCode::TABLE_ROW_TOO_SHORT) != NULL);
    CHECK(
        FindDiagnostic(
            normalized.diagnostics,
            JBeamDiagnosticCode::TABLE_ROW_TOO_LONG) != NULL);
    CHECK(
        FindDiagnostic(
            normalized.diagnostics,
            JBeamDiagnosticCode::TABLE_INVALID_ENTRY) != NULL);
}

void TestDefaultSnapshotsShareLinearStorage()
{
    const JBeamParseResult parsed = RoR::BeamNG::ParseJBeam(
        "{\"part\":{\"nodes\":[[\"id\"],"
        "{\"weight\":1},[\"first\"],"
        "{\"weight\":2},[\"second\"]]}}",
        "default-snapshots.jbeam");
    CHECK(parsed.IsValid());
    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(parsed.root);
    CHECK(normalized.IsValid());
    CHECK(normalized.tables.size() == 1);
    const JBeamNormalizedTable& table = normalized.tables[0];
    CHECK(table.entries.size() == 4);
    const RoR::BeamNG::JBeamNormalizedDataRow& first =
        table.entries[1].data_row;
    const RoR::BeamNG::JBeamNormalizedDataRow& second =
        table.entries[3].data_row;
    CHECK(first.inherited_assignment_storage != NULL);
    CHECK(
        first.inherited_assignment_storage ==
        second.inherited_assignment_storage);
    CHECK(first.inherited_assignment_index != NULL);
    CHECK(
        first.inherited_assignment_index ==
        second.inherited_assignment_index);
    CHECK(first.inherited_assignment_count == 1);
    CHECK(second.inherited_assignment_count == 2);
    CHECK(first.inherited_assignment_storage->size() == 2);

    const JBeamFieldAssignment* first_weight =
        RoR::BeamNG::FindEffectiveJBeamField(first, "weight");
    const JBeamFieldAssignment* second_weight =
        RoR::BeamNG::FindEffectiveJBeamField(second, "weight");
    CHECK(
        first_weight != NULL &&
        first_weight->value->number_value == 1.0);
    CHECK(
        second_weight != NULL &&
        second_weight->value->number_value == 2.0);
}

void TestStructuralScaleDefaultsAreNearLinearAndBudgeted()
{
    const std::size_t pair_count = 100000U;
    JBeamValue table;
    table.type = JBeamValueType::ARRAY;
    table.array_values.reserve(1U + pair_count * 2U);

    JBeamValue header;
    header.type = JBeamValueType::ARRAY;
    header.array_values.push_back(StringValue("id"));
    table.array_values.push_back(std::move(header));

    for (std::size_t i = 0; i < pair_count; ++i)
    {
        JBeamValue defaults;
        defaults.type = JBeamValueType::OBJECT;
        JBeamObjectField field;
        field.key = "nodeWeight";
        field.value = SharedValue(NumberValue(static_cast<double>(i)));
        defaults.object_fields.push_back(std::move(field));
        table.array_values.push_back(std::move(defaults));

        JBeamValue row;
        row.type = JBeamValueType::ARRAY;
        row.array_values.push_back(StringValue("node"));
        table.array_values.push_back(std::move(row));
    }

    JBeamNormalizeLimits limits;
    limits.max_work_units = pair_count * 24U;
    limits.max_retained_bytes = 384U * 1024U * 1024U;
    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(table, limits);
    CHECK(normalized.IsValid());
    CHECK(normalized.tables.size() == 1);
    CHECK(normalized.work_units <= limits.max_work_units);
    CHECK(normalized.retained_bytes <= limits.max_retained_bytes);
    if (normalized.tables.size() != 1)
    {
        return;
    }

    const JBeamNormalizedTable& normalized_table =
        normalized.tables[0];
    CHECK(normalized_table.entries.size() == pair_count * 2U);
    JBeamFieldLookupMetrics lookup_metrics;
    for (std::size_t i = 0; i < pair_count; ++i)
    {
        const RoR::BeamNG::JBeamNormalizedDataRow& row =
            normalized_table.entries[i * 2U + 1U].data_row;
        const JBeamFieldAssignment* assignment =
            RoR::BeamNG::FindEffectiveJBeamField(
                row, "nodeWeight", &lookup_metrics);
        CHECK(assignment != NULL);
        if (assignment != NULL)
        {
            CHECK(assignment->value != NULL);
            CHECK(
                assignment->value->number_value ==
                static_cast<double>(i));
        }
        // A missing field is the historical worst case: the old reverse
        // prefix scan inspected every same-key default before returning null.
        CHECK(
            RoR::BeamNG::FindEffectiveJBeamField(
                row, "unmapped", &lookup_metrics) == NULL);
    }
    // Positional checks plus indexed present/missing searches remain
    // logarithmic even when every inherited default uses the same key.
    CHECK(lookup_metrics.work_units <= pair_count * 24U);
}

void TestNormalizationBudgetsFailClosed()
{
    const JBeamParseResult parsed = RoR::BeamNG::ParseJBeam(
        "{\"part\":{\"nodes\":[[\"id\"],[\"n\"]]}}",
        "normalize-budget.jbeam");
    CHECK(parsed.IsValid());

    JBeamNormalizeLimits limits;
    limits.max_work_units = 1;
    const RoR::BeamNG::JBeamNormalizeResult work_limited =
        RoR::BeamNG::NormalizeJBeamTables(parsed.root, limits);
    CHECK(!work_limited.IsValid());
    CHECK(work_limited.tables.empty());
    CHECK(work_limited.work_units <= limits.max_work_units);
    CHECK(
        FindDiagnostic(
            work_limited.diagnostics,
            JBeamDiagnosticCode::NORMALIZE_WORK_LIMIT) != NULL);

    limits = JBeamNormalizeLimits();
    limits.max_retained_bytes = 1;
    const RoR::BeamNG::JBeamNormalizeResult bytes_limited =
        RoR::BeamNG::NormalizeJBeamTables(parsed.root, limits);
    CHECK(!bytes_limited.IsValid());
    CHECK(bytes_limited.tables.empty());
    CHECK(bytes_limited.retained_bytes <= limits.max_retained_bytes);
    CHECK(
        FindDiagnostic(
            bytes_limited.diagnostics,
            JBeamDiagnosticCode::NORMALIZE_RETAINED_BYTES_LIMIT) != NULL);
}

void TestNullObjectFieldsAreIgnoredSafely()
{
    JBeamValue table;
    table.type = JBeamValueType::ARRAY;
    JBeamValue header;
    header.type = JBeamValueType::ARRAY;
    header.array_values.push_back(StringValue("id"));
    table.array_values.push_back(std::move(header));

    JBeamValue defaults;
    defaults.type = JBeamValueType::OBJECT;
    JBeamObjectField null_default;
    null_default.key = "nodeWeight";
    defaults.object_fields.push_back(std::move(null_default));
    table.array_values.push_back(std::move(defaults));

    JBeamValue row;
    row.type = JBeamValueType::ARRAY;
    row.array_values.push_back(StringValue("node"));
    JBeamValue overrides;
    overrides.type = JBeamValueType::OBJECT;
    JBeamObjectField null_override;
    null_override.key = "group";
    overrides.object_fields.push_back(std::move(null_override));
    row.array_values.push_back(std::move(overrides));
    table.array_values.push_back(std::move(row));

    JBeamValue root;
    root.type = JBeamValueType::OBJECT;
    JBeamObjectField null_section;
    null_section.key = "broken";
    root.object_fields.push_back(std::move(null_section));
    JBeamObjectField table_section;
    table_section.key = "nodes";
    table_section.value = SharedValue(table);
    root.object_fields.push_back(std::move(table_section));

    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(root);
    CHECK(normalized.IsValid());
    CHECK(normalized.tables.size() == 1);
    CHECK(
        FindDiagnostic(
            normalized.diagnostics,
            JBeamDiagnosticCode::TABLE_INVALID_ENTRY) != NULL);
    if (normalized.tables.size() != 1)
    {
        return;
    }
    const JBeamNormalizedTable& normalized_table =
        normalized.tables[0];
    CHECK(normalized_table.entries.size() == 2);
    const RoR::BeamNG::JBeamNormalizedDataRow& normalized_row =
        normalized_table.entries[1].data_row;
    CHECK(normalized_row.inherited_assignment_count == 0);
    CHECK(normalized_row.row_local_assignments.empty());
    CHECK(
        RoR::BeamNG::FindEffectiveJBeamField(
            normalized_row, "nodeWeight") == NULL);
    const JBeamFieldAssignment* id =
        RoR::BeamNG::FindEffectiveJBeamField(normalized_row, "id");
    CHECK(id != NULL);
    CHECK(id != NULL && id->value != NULL);
}

void TestObjectValuedCellIsNotMistakenForRowOverrides()
{
    const JBeamParseResult parsed = RoR::BeamNG::ParseJBeam(
        "{\"part\":{\"section\":[[\"id\",\"settings\"],"
        "[\"row\",{\"x\":1}]]}}",
        "object-cell.jbeam");
    CHECK(parsed.IsValid());
    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(parsed.root);
    CHECK(normalized.tables.size() == 1);
    const RoR::BeamNG::JBeamNormalizedDataRow& row =
        normalized.tables[0].entries[0].data_row;
    CHECK(row.positional_assignments.size() == 2);
    CHECK(row.row_local_assignments.empty());
    const JBeamFieldAssignment* settings =
        RoR::BeamNG::FindEffectiveJBeamField(row, "settings");
    CHECK(settings != NULL);
    CHECK(settings->value->type == JBeamValueType::OBJECT);
}

void TestDuplicateSectionPathsAndNestedDiscovery()
{
    const std::string source =
        "{"
        "\"part\":{\"nodes\":[[\"id\"],[\"a\"]]},"
        "\"part\":{\"nodes\":[[\"id\"],[\"b\"]]},"
        "\"ordinary\":[[1,2],[3,4]]"
        "}";
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, "paths.jbeam");
    CHECK(parsed.IsValid());
    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(parsed.root);
    CHECK(normalized.tables.size() == 2);
    CHECK(normalized.tables[0].path == "$/part#0/nodes#0");
    CHECK(normalized.tables[1].path == "$/part#1/nodes#0");
}

void TestNestedRowArraysAreNotRediscoveredAsTables()
{
    const JBeamParseResult parsed = RoR::BeamNG::ParseJBeam(
        "{"
        "\"part\":{\"nodes\":[[\"id\",\"payload\"],"
        "[\"n\",[[\"fake\"],[\"nested-row\"]]]]},"
        "\"ordinary\":[{\"nested\":[[\"id\"],[\"not-a-section\"]]}]"
        "}",
        "nested-row-array.jbeam");
    CHECK(parsed.IsValid());
    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(parsed.root);
    CHECK(normalized.IsValid());
    CHECK(normalized.tables.size() == 1);
    CHECK(
        normalized.tables.size() == 1 &&
        normalized.tables[0].path == "$/part#0/nodes#0");
}

void TestNearTableHeaderInspectionIsBudgeted()
{
    JBeamValue root;
    root.type = JBeamValueType::ARRAY;
    JBeamValue almost_header;
    almost_header.type = JBeamValueType::ARRAY;
    for (std::size_t i = 0; i < 256U; ++i)
    {
        almost_header.array_values.push_back(StringValue("field"));
    }
    almost_header.array_values.push_back(NumberValue(1.0));
    root.array_values.push_back(std::move(almost_header));

    JBeamNormalizeLimits limits;
    limits.max_work_units = 32U;
    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(root, limits);
    CHECK(!normalized.IsValid());
    CHECK(normalized.tables.empty());
    CHECK(normalized.work_units <= limits.max_work_units);
    CHECK(
        FindDiagnostic(
            normalized.diagnostics,
            JBeamDiagnosticCode::NORMALIZE_WORK_LIMIT) != NULL);
}

void TestObjectPathConstructionIsLinearAndBudgeted()
{
    JBeamValue root;
    root.type = JBeamValueType::OBJECT;
    JBeamValue* current = &root;
    const std::string segment(4096U, '/');
    for (std::size_t depth = 0; depth < 32U; ++depth)
    {
        JBeamObjectField field;
        field.key = segment;
        std::shared_ptr<JBeamValue> child(new JBeamValue());
        child->type = JBeamValueType::OBJECT;
        field.value = child;
        current->object_fields.push_back(field);
        current = child.get();
    }

    JBeamNormalizeLimits limits;
    limits.max_work_units = 1000U;
    limits.max_retained_bytes = 1U;
    const RoR::BeamNG::JBeamNormalizeResult limited =
        RoR::BeamNG::NormalizeJBeamTables(root, limits);
    CHECK(!limited.IsValid());
    CHECK(limited.tables.empty());
    CHECK(limited.work_units <= limits.max_work_units);
    CHECK(
        FindDiagnostic(
            limited.diagnostics,
            JBeamDiagnosticCode::NORMALIZE_WORK_LIMIT) != NULL);

    limits.max_work_units = 1024U * 1024U;
    const RoR::BeamNG::JBeamNormalizeResult complete =
        RoR::BeamNG::NormalizeJBeamTables(root, limits);
    CHECK(complete.IsValid());
    CHECK(complete.tables.empty());
    // Each segment is scanned/escaped once. Re-copying every cumulative
    // ancestor path would make this grow quadratically with depth.
    CHECK(complete.work_units < 500000U);
}

void TestObjectKeyIsChargedBeforeOccurrenceBookkeeping()
{
    JBeamValue root;
    root.type = JBeamValueType::OBJECT;
    JBeamObjectField field;
    field.key.assign(4096U, 'k');
    // A null value deliberately exercises the early diagnostic path. The key
    // must be rejected by the work budget before duplicate bookkeeping copies
    // it into the occurrence map.
    root.object_fields.push_back(std::move(field));

    JBeamNormalizeLimits limits;
    limits.max_work_units = 32U;
    const RoR::BeamNG::JBeamNormalizeResult normalized =
        RoR::BeamNG::NormalizeJBeamTables(root, limits);
    CHECK(!normalized.IsValid());
    CHECK(normalized.tables.empty());
    CHECK(normalized.work_units <= limits.max_work_units);
    CHECK(
        FindDiagnostic(
            normalized.diagnostics,
            JBeamDiagnosticCode::NORMALIZE_WORK_LIMIT) != NULL);
}

void TestSyntaxFailuresAndStableDiagnostics()
{
    struct FailureCase
    {
        const char* source;
        JBeamDiagnosticCode code;
    };
    const FailureCase cases[] = {
        {"{/*", JBeamDiagnosticCode::UNTERMINATED_BLOCK_COMMENT},
        {"{\"x\":\"\\q\"}", JBeamDiagnosticCode::INVALID_ESCAPE},
        {"{\"x\":\"\\ud800\"}",
         JBeamDiagnosticCode::INVALID_UNICODE_SURROGATE},
        {"{\"x\":\"\xc0\x80\"}", JBeamDiagnosticCode::INVALID_UTF8},
        {"{\"x\":\"\xed\xa0\x80\"}", JBeamDiagnosticCode::INVALID_UTF8},
        {"{\"x\":01}", JBeamDiagnosticCode::INVALID_NUMBER},
        {"{\"x\":1e9999}", JBeamDiagnosticCode::NON_FINITE_NUMBER},
        {"{\"x\" 1}", JBeamDiagnosticCode::EXPECTED_COLON},
        {"{,\"x\":1}", JBeamDiagnosticCode::UNEXPECTED_COMMA},
        {"[1,,2]", JBeamDiagnosticCode::UNEXPECTED_COMMA},
        {"{\"x\":1", JBeamDiagnosticCode::EXPECTED_OBJECT_END},
        {"[1", JBeamDiagnosticCode::EXPECTED_ARRAY_END},
        {"true false", JBeamDiagnosticCode::TRAILING_CONTENT}
    };

    for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const JBeamParseResult first =
            RoR::BeamNG::ParseJBeam(cases[i].source, "bad.jbeam");
        const JBeamParseResult second =
            RoR::BeamNG::ParseJBeam(cases[i].source, "bad.jbeam");
        CHECK(!first.IsValid());
        const JBeamDiagnostic* first_diagnostic =
            FindDiagnostic(first.diagnostics, cases[i].code);
        const JBeamDiagnostic* second_diagnostic =
            FindDiagnostic(second.diagnostics, cases[i].code);
        CHECK(first_diagnostic != NULL);
        CHECK(second_diagnostic != NULL);
        if (first_diagnostic != NULL && second_diagnostic != NULL)
        {
            CHECK(
                first_diagnostic->span.begin.byte_offset ==
                second_diagnostic->span.begin.byte_offset);
            CHECK(first_diagnostic->message == second_diagnostic->message);
        }
    }
}

void TestDeterministicNoiseCorpus()
{
    std::uint32_t state = 0x91e10da5U;
    for (std::size_t fixture = 0; fixture < 2000; ++fixture)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const std::size_t length = state % 129U;
        std::string source;
        source.reserve(length);
        for (std::size_t i = 0; i < length; ++i)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            source.push_back(static_cast<char>(state & 0xffU));
        }

        const JBeamParseResult first =
            RoR::BeamNG::ParseJBeam(source, "noise.jbeam");
        const JBeamParseResult second =
            RoR::BeamNG::ParseJBeam(source, "noise.jbeam");
        CHECK(first.IsValid() == second.IsValid());
        CHECK(first.diagnostics.size() == second.diagnostics.size());
        if (first.diagnostics.size() == second.diagnostics.size())
        {
            for (std::size_t i = 0; i < first.diagnostics.size(); ++i)
            {
                CHECK(
                    first.diagnostics[i].code ==
                    second.diagnostics[i].code);
                CHECK(
                    first.diagnostics[i].span.begin.byte_offset ==
                    second.diagnostics[i].span.begin.byte_offset);
                CHECK(
                    first.diagnostics[i].span.end.byte_offset ==
                    second.diagnostics[i].span.end.byte_offset);
                CHECK(
                    first.diagnostics[i].message ==
                    second.diagnostics[i].message);
            }
        }
    }
}

void TestResourceBudgets()
{
    JBeamParseLimits limits;
    limits.max_source_bytes = 2;
    CHECK(
        FindDiagnostic(
            RoR::BeamNG::ParseJBeam("null", "budget.jbeam", limits)
                .diagnostics,
            JBeamDiagnosticCode::SOURCE_SIZE_LIMIT) != NULL);

    limits = JBeamParseLimits();
    limits.max_tokens = 2;
    CHECK(
        FindDiagnostic(
            RoR::BeamNG::ParseJBeam("[1]", "budget.jbeam", limits)
                .diagnostics,
            JBeamDiagnosticCode::TOKEN_LIMIT) != NULL);

    limits = JBeamParseLimits();
    limits.max_nodes = 2;
    CHECK(
        FindDiagnostic(
            RoR::BeamNG::ParseJBeam("[1,2]", "budget.jbeam", limits)
                .diagnostics,
            JBeamDiagnosticCode::NODE_LIMIT) != NULL);

    limits = JBeamParseLimits();
    limits.max_depth = 1;
    CHECK(
        FindDiagnostic(
            RoR::BeamNG::ParseJBeam("[[0]]", "budget.jbeam", limits)
                .diagnostics,
            JBeamDiagnosticCode::DEPTH_LIMIT) != NULL);

    limits = JBeamParseLimits();
    limits.max_string_bytes = 3;
    CHECK(
        FindDiagnostic(
            RoR::BeamNG::ParseJBeam("\"four\"", "budget.jbeam", limits)
                .diagnostics,
            JBeamDiagnosticCode::STRING_SIZE_LIMIT) != NULL);

    limits = JBeamParseLimits();
    limits.max_diagnostics = 1;
    CHECK(
        FindDiagnostic(
            RoR::BeamNG::ParseJBeam(
                "{\"x\":0,\"x\":1,\"x\":2}",
                "budget.jbeam",
                limits)
                .diagnostics,
            JBeamDiagnosticCode::DIAGNOSTIC_LIMIT) != NULL);
}

} // namespace

int main()
{
    TestCommentsOptionalCommasAndScalars();
    TestUnicodeEscapesAndByteSpans();
    TestDuplicateKeysAreOrderedAndLastWins();
    TestTableNormalizationPreservesSemantics();
    TestDefaultSnapshotsShareLinearStorage();
    TestStructuralScaleDefaultsAreNearLinearAndBudgeted();
    TestNormalizationBudgetsFailClosed();
    TestNullObjectFieldsAreIgnoredSafely();
    TestObjectValuedCellIsNotMistakenForRowOverrides();
    TestDuplicateSectionPathsAndNestedDiscovery();
    TestNestedRowArraysAreNotRediscoveredAsTables();
    TestNearTableHeaderInspectionIsBudgeted();
    TestObjectPathConstructionIsLinearAndBudgeted();
    TestObjectKeyIsChargedBeforeOccurrenceBookkeeping();
    TestSyntaxFailuresAndStableDiagnostics();
    TestResourceBudgets();
    TestDeterministicNoiseCorpus();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " JBeam syntax test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JBeam syntax tests passed\n";
    return EXIT_SUCCESS;
}
