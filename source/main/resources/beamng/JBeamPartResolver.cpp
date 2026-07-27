/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

#include "JBeamPartResolver.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace RoR {
namespace BeamNG {

namespace {

bool HasErrors(const std::vector<JBeamResolveDiagnostic>& diagnostics)
{
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        if (diagnostics[i].severity == JBeamResolveSeverity::ERROR)
        {
            return true;
        }
    }
    return false;
}

JBeamResolveDiagnostic MakeDiagnostic(
    JBeamResolveDiagnosticCode code,
    JBeamResolveSeverity severity,
    const JBeamSourceSpan& span,
    const std::string& part_name,
    const std::string& slot_name,
    const std::string& detail)
{
    JBeamResolveDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.span = span;
    diagnostic.part_name = part_name;
    diagnostic.slot_name = slot_name;
    diagnostic.detail = detail;
    return diagnostic;
}

bool IsDiagnosticLimit(
    const JBeamResolveDiagnostic& diagnostic)
{
    return diagnostic.code ==
           JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT;
}

JBeamResolveDiagnostic MakeDiagnosticLimit()
{
    return MakeDiagnostic(
        JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT,
        JBeamResolveSeverity::ERROR,
        JBeamSourceSpan(),
        std::string(),
        std::string(),
        "Additional diagnostics were deterministically suppressed");
}

void PushDiagnostic(
    std::vector<JBeamResolveDiagnostic>& diagnostics,
    std::size_t configured_limit,
    const JBeamResolveDiagnostic& diagnostic)
{
    if (!diagnostics.empty() &&
        IsDiagnosticLimit(diagnostics.back()))
    {
        return;
    }

    if (configured_limit == 0U)
    {
        diagnostics.push_back(MakeDiagnosticLimit());
        return;
    }

    const std::size_t retained_limit =
        configured_limit;
    if (diagnostics.size() < retained_limit)
    {
        diagnostics.push_back(diagnostic);
        return;
    }

    // The terminal entry consumes the last retained slot. Replacing the last
    // ordinary entry avoids a transient allocation beyond the configured
    // bound and makes the result independent of allocator capacity.
    diagnostics[retained_limit - 1U] = MakeDiagnosticLimit();
}

bool DiagnosticLess(
    const JBeamResolveDiagnostic& left,
    const JBeamResolveDiagnostic& right)
{
    if (IsDiagnosticLimit(left) != IsDiagnosticLimit(right))
    {
        return !IsDiagnosticLimit(left);
    }
    if (left.span.source_name != right.span.source_name)
    {
        return left.span.source_name < right.span.source_name;
    }
    if (left.span.begin.byte_offset != right.span.begin.byte_offset)
    {
        return left.span.begin.byte_offset < right.span.begin.byte_offset;
    }
    if (left.span.end.byte_offset != right.span.end.byte_offset)
    {
        return left.span.end.byte_offset < right.span.end.byte_offset;
    }
    if (left.code != right.code)
    {
        return static_cast<int>(left.code) < static_cast<int>(right.code);
    }
    if (left.part_name != right.part_name)
    {
        return left.part_name < right.part_name;
    }
    if (left.slot_name != right.slot_name)
    {
        return left.slot_name < right.slot_name;
    }
    return left.detail < right.detail;
}

void SortDiagnostics(std::vector<JBeamResolveDiagnostic>& diagnostics)
{
    std::stable_sort(
        diagnostics.begin(), diagnostics.end(), DiagnosticLess);
}

std::string LengthPrefixed(const std::string& value)
{
    std::ostringstream output;
    output << value.size() << ':' << value;
    return output.str();
}

void AppendCanonicalValue(
    const JBeamValue& value,
    std::ostringstream& output)
{
    switch (value.type)
    {
    case JBeamValueType::NULL_VALUE:
        output << 'n';
        return;
    case JBeamValueType::BOOLEAN:
        output << (value.boolean_value ? "b1" : "b0");
        return;
    case JBeamValueType::NUMBER:
        output << 'd' << LengthPrefixed(value.scalar_text);
        return;
    case JBeamValueType::STRING:
        output << 's' << LengthPrefixed(value.scalar_text);
        return;
    case JBeamValueType::ARRAY:
        output << 'a' << value.array_values.size() << '[';
        for (std::size_t i = 0; i < value.array_values.size(); ++i)
        {
            AppendCanonicalValue(value.array_values[i], output);
        }
        output << ']';
        return;
    case JBeamValueType::OBJECT:
        output << 'o' << value.object_fields.size() << '{';
        for (std::size_t i = 0; i < value.object_fields.size(); ++i)
        {
            const JBeamObjectField& field = value.object_fields[i];
            output << 'k' << LengthPrefixed(field.key);
            if (field.value)
            {
                AppendCanonicalValue(*field.value, output);
            }
            else
            {
                output << 'x';
            }
        }
        output << '}';
        return;
    }
}

std::string CanonicalValue(const JBeamValue& value)
{
    std::ostringstream output;
    AppendCanonicalValue(value, output);
    return output.str();
}

void AppendSpan(
    const JBeamSourceSpan& span,
    std::ostringstream& output)
{
    output
        << LengthPrefixed(span.source_name) << '\t'
        << span.begin.byte_offset << '\t'
        << span.begin.line << '\t'
        << span.begin.column << '\t'
        << span.end.byte_offset << '\t'
        << span.end.line << '\t'
        << span.end.column;
}

bool IsScalarVariableValue(const JBeamValue& value)
{
    return value.type == JBeamValueType::BOOLEAN ||
           value.type == JBeamValueType::NUMBER ||
           value.type == JBeamValueType::STRING;
}

bool IsVariableName(const std::string& name)
{
    return !name.empty() && name[0] == '$';
}

bool IsStructuralString(const JBeamValue& value)
{
    if (value.type != JBeamValueType::STRING ||
        value.scalar_text.empty())
    {
        return false;
    }
    return value.scalar_text.compare(0, 2, "$=") != 0 &&
           value.scalar_text[0] != '$';
}

const JBeamNormalizedTable* FindRootTable(
    const JBeamNormalizeResult& normalized)
{
    for (std::size_t i = 0; i < normalized.tables.size(); ++i)
    {
        if (normalized.tables[i].path == "$")
        {
            return &normalized.tables[i];
        }
    }
    return NULL;
}

const JBeamFieldAssignment* RowField(
    const JBeamNormalizedDataRow& row,
    const std::string& name)
{
    return FindEffectiveJBeamField(row, name);
}

bool ReadStringArray(
    const JBeamValue& value,
    std::vector<std::string>& output)
{
    if (value.type != JBeamValueType::ARRAY ||
        value.array_values.empty())
    {
        return false;
    }
    for (std::size_t i = 0; i < value.array_values.size(); ++i)
    {
        if (!IsStructuralString(value.array_values[i]))
        {
            return false;
        }
        output.push_back(value.array_values[i].scalar_text);
    }
    return true;
}

void ReadSlotVariables(
    const JBeamFieldAssignment* variables_field,
    const std::string& part_name,
    const std::string& slot_name,
    const JBeamResolverLimits& limits,
    std::vector<JBeamVariableAssignment>& output,
    std::vector<JBeamResolveDiagnostic>& diagnostics)
{
    if (variables_field == NULL)
    {
        return;
    }
    if (variables_field->value->type != JBeamValueType::OBJECT)
    {
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            JBeamResolveDiagnosticCode::INVALID_SLOT_VARIABLE,
            JBeamResolveSeverity::ERROR,
            variables_field->span,
            part_name,
            slot_name,
            "Slot variables must be a dictionary"));
        return;
    }

    const JBeamValue& object = *variables_field->value;
    if (object.object_fields.size() >
        limits.max_variables_per_node)
    {
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
                JBeamResolveDiagnosticCode::RESOLVED_VARIABLE_LIMIT,
                JBeamResolveSeverity::ERROR,
                variables_field->span,
                part_name,
                slot_name,
                "Slot variables exceed the configured per-node limit"));
        return;
    }
    for (std::size_t i = 0; i < object.object_fields.size(); ++i)
    {
        const JBeamObjectField& field = object.object_fields[i];
        if (!IsVariableName(field.key) ||
            !field.value ||
            !IsScalarVariableValue(*field.value))
        {
            PushDiagnostic(
                diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::INVALID_SLOT_VARIABLE,
                JBeamResolveSeverity::ERROR,
                field.key_span,
                part_name,
                slot_name,
                "Slot variable names must begin with '$' and values must "
                "be number, string, or Boolean scalars"));
            continue;
        }
        JBeamVariableAssignment assignment;
        assignment.name = field.key;
        assignment.value = *field.value;
        assignment.span = field.value->span;
        assignment.origin = JBeamVariableOrigin::SLOT;
        output.push_back(assignment);
    }
}

bool ReadCoreSlot(
    const JBeamFieldAssignment* field,
    const std::string& part_name,
    const std::string& slot_name,
    bool& core_slot,
    const JBeamResolverLimits& limits,
    std::vector<JBeamResolveDiagnostic>& diagnostics)
{
    core_slot = false;
    if (field == NULL)
    {
        return true;
    }
    if (field->value->type != JBeamValueType::BOOLEAN)
    {
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            JBeamResolveDiagnosticCode::INVALID_SLOT_FIELD,
            JBeamResolveSeverity::ERROR,
            field->span,
            part_name,
            slot_name,
            "coreSlot must be Boolean"));
        return false;
    }
    core_slot = field->value->boolean_value;
    return true;
}

bool ReadRequiredString(
    const JBeamFieldAssignment* field,
    const std::string& field_name,
    const std::string& part_name,
    const std::string& slot_name,
    std::string& output,
    const JBeamResolverLimits& limits,
    std::vector<JBeamResolveDiagnostic>& diagnostics,
    bool allow_empty)
{
    if (field == NULL)
    {
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            JBeamResolveDiagnosticCode::MISSING_SLOT_FIELD,
            JBeamResolveSeverity::ERROR,
            JBeamSourceSpan(),
            part_name,
            slot_name,
            "Missing required slot field '" + field_name + "'"));
        return false;
    }
    if (field->value->type != JBeamValueType::STRING ||
        (!allow_empty && !IsStructuralString(*field->value)) ||
        (allow_empty &&
         !field->value->scalar_text.empty() &&
         !IsStructuralString(*field->value)))
    {
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            JBeamResolveDiagnosticCode::INVALID_SLOT_FIELD,
            JBeamResolveSeverity::ERROR,
            field->span,
            part_name,
            slot_name,
            "Slot field '" + field_name +
                "' must be a literal string"));
        return false;
    }
    output = field->value->scalar_text;
    return true;
}

void ParseSlotTable(
    const JBeamValue& value,
    JBeamSlotKind kind,
    const std::string& part_name,
    const JBeamResolverLimits& limits,
    std::vector<JBeamSlotDefinition>& output,
    std::vector<JBeamResolveDiagnostic>& diagnostics)
{
    const JBeamNormalizeResult normalized = NormalizeJBeamTables(value);
    const JBeamNormalizedTable* table = FindRootTable(normalized);
    if (table == NULL)
    {
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            JBeamResolveDiagnosticCode::INVALID_SLOT_TABLE,
            JBeamResolveSeverity::ERROR,
            value.span,
            part_name,
            std::string(),
            kind == JBeamSlotKind::SLOTS
                ? "slots must be a header-row table"
                : "slots2 must be a header-row table"));
        return;
    }
    for (std::size_t diagnostic_index = 0;
         diagnostic_index < normalized.diagnostics.size();
         ++diagnostic_index)
    {
        const JBeamDiagnostic& syntax_diagnostic =
            normalized.diagnostics[diagnostic_index];
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            syntax_diagnostic.code ==
                    JBeamDiagnosticCode::DUPLICATE_TABLE_HEADER
                ? JBeamResolveDiagnosticCode::INVALID_SLOT_TABLE
                : JBeamResolveDiagnosticCode::INVALID_SLOT_ROW,
            JBeamResolveSeverity::ERROR,
            syntax_diagnostic.span,
            part_name,
            std::string(),
            "Malformed slot table: " + syntax_diagnostic.message));
    }

    for (std::size_t i = 0; i < table->entries.size(); ++i)
    {
        const JBeamNormalizedTableEntry& entry = table->entries[i];
        if (entry.kind ==
            JBeamNormalizedTableEntryKind::DEFAULT_MODIFIER)
        {
            continue;
        }
        if (entry.kind != JBeamNormalizedTableEntryKind::DATA_ROW)
        {
            PushDiagnostic(
                diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::INVALID_SLOT_ROW,
                JBeamResolveSeverity::ERROR,
                entry.raw_value.span,
                part_name,
                std::string(),
                "Slot table entries must be row arrays or inherited "
                "default dictionaries"));
            continue;
        }
        if (output.size() >= limits.max_slots_per_part)
        {
            PushDiagnostic(
                diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::INVALID_SLOT_TABLE,
                JBeamResolveSeverity::ERROR,
                entry.raw_value.span,
                part_name,
                std::string(),
                "Part exceeds the configured slot-count limit"));
            return;
        }

        const JBeamNormalizedDataRow& row = entry.data_row;
        JBeamSlotDefinition slot;
        slot.kind = kind;
        slot.span = row.span;
        const char* name_field =
            kind == JBeamSlotKind::SLOTS ? "type" : "name";
        bool valid = ReadRequiredString(
            RowField(row, name_field),
            name_field,
            part_name,
            std::string(),
            slot.name,
            limits,
            diagnostics,
            false);
        valid = ReadRequiredString(
                    RowField(row, "default"),
                    "default",
                    part_name,
                    slot.name,
                    slot.default_part,
                    limits,
                    diagnostics,
                    true) &&
                valid;
        valid = ReadRequiredString(
                    RowField(row, "description"),
                    "description",
                    part_name,
                    slot.name,
                    slot.description,
                    limits,
                    diagnostics,
                    true) &&
                valid;
        valid = ReadCoreSlot(
                    RowField(row, "coreSlot"),
                    part_name,
                    slot.name,
                    slot.core_slot,
                    limits,
                    diagnostics) &&
                valid;

        if (kind == JBeamSlotKind::SLOTS)
        {
            if (!slot.name.empty())
            {
                slot.allow_types.push_back(slot.name);
            }
        }
        else
        {
            const JBeamFieldAssignment* allow =
                RowField(row, "allowTypes");
            const JBeamFieldAssignment* deny =
                RowField(row, "denyTypes");
            if (allow == NULL ||
                !ReadStringArray(*allow->value, slot.allow_types))
            {
                PushDiagnostic(
                    diagnostics,
                    limits.max_diagnostics,
                    MakeDiagnostic(
                    allow == NULL
                        ? JBeamResolveDiagnosticCode::MISSING_SLOT_FIELD
                        : JBeamResolveDiagnosticCode::INVALID_SLOT_FIELD,
                    JBeamResolveSeverity::ERROR,
                    allow == NULL ? row.span : allow->span,
                    part_name,
                    slot.name,
                    "slots2 allowTypes must be a non-empty array of "
                    "literal strings"));
                valid = false;
            }
            if (deny == NULL ||
                deny->value->type != JBeamValueType::ARRAY)
            {
                PushDiagnostic(
                    diagnostics,
                    limits.max_diagnostics,
                    MakeDiagnostic(
                    deny == NULL
                        ? JBeamResolveDiagnosticCode::MISSING_SLOT_FIELD
                        : JBeamResolveDiagnosticCode::INVALID_SLOT_FIELD,
                    JBeamResolveSeverity::ERROR,
                    deny == NULL ? row.span : deny->span,
                    part_name,
                    slot.name,
                    "slots2 denyTypes must be an array of literal strings"));
                valid = false;
            }
            else
            {
                bool deny_valid = true;
                for (std::size_t deny_index = 0;
                     deny_index < deny->value->array_values.size();
                     ++deny_index)
                {
                    const JBeamValue& deny_value =
                        deny->value->array_values[deny_index];
                    if (!IsStructuralString(deny_value))
                    {
                        deny_valid = false;
                        break;
                    }
                    slot.deny_types.push_back(deny_value.scalar_text);
                }
                if (!deny_valid)
                {
                    PushDiagnostic(
                        diagnostics,
                        limits.max_diagnostics,
                        MakeDiagnostic(
                        JBeamResolveDiagnosticCode::INVALID_SLOT_FIELD,
                        JBeamResolveSeverity::ERROR,
                        deny->span,
                        part_name,
                        slot.name,
                        "slots2 denyTypes must contain only literal "
                        "strings"));
                    valid = false;
                }
            }
        }

        ReadSlotVariables(
            RowField(row, "variables"),
            part_name,
            slot.name,
            limits,
            slot.variables,
            diagnostics);
        if (valid)
        {
            output.push_back(slot);
        }
    }
}

void ParsePartSlotTypes(
    JBeamPartDefinition& part,
    const JBeamResolverLimits& limits,
    std::vector<JBeamResolveDiagnostic>& diagnostics)
{
    const JBeamObjectField* effective = NULL;
    for (std::size_t i = 0; i < part.body.object_fields.size(); ++i)
    {
        if (part.body.object_fields[i].key == "slotType")
        {
            effective = &part.body.object_fields[i];
        }
    }
    if (effective == NULL || !effective->value)
    {
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            JBeamResolveDiagnosticCode::MISSING_SLOT_TYPE,
            JBeamResolveSeverity::ERROR,
            part.name_span,
            part.name,
            std::string(),
            "Part has no slotType"));
        return;
    }

    const JBeamValue& value = *effective->value;
    if (IsStructuralString(value))
    {
        part.slot_types.push_back(value.scalar_text);
    }
    else if (value.type == JBeamValueType::ARRAY &&
             !value.array_values.empty())
    {
        for (std::size_t i = 0; i < value.array_values.size(); ++i)
        {
            if (!IsStructuralString(value.array_values[i]))
            {
                PushDiagnostic(
                    diagnostics,
                    limits.max_diagnostics,
                    MakeDiagnostic(
                    JBeamResolveDiagnosticCode::INVALID_SLOT_TYPE,
                    JBeamResolveSeverity::ERROR,
                    value.array_values[i].span,
                    part.name,
                    std::string(),
                    "slotType arrays must contain literal non-empty "
                    "strings"));
                continue;
            }
            if (std::find(
                    part.slot_types.begin(),
                    part.slot_types.end(),
                    value.array_values[i].scalar_text) !=
                part.slot_types.end())
            {
                PushDiagnostic(
                    diagnostics,
                    limits.max_diagnostics,
                    MakeDiagnostic(
                    JBeamResolveDiagnosticCode::DUPLICATE_SLOT_TYPE,
                    JBeamResolveSeverity::WARNING,
                    value.array_values[i].span,
                    part.name,
                    std::string(),
                    "Duplicate slotType is preserved"));
            }
            part.slot_types.push_back(value.array_values[i].scalar_text);
        }
    }
    else
    {
        PushDiagnostic(
            diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            JBeamResolveDiagnosticCode::INVALID_SLOT_TYPE,
            JBeamResolveSeverity::ERROR,
            value.span,
            part.name,
            std::string(),
            "slotType must be a literal string or non-empty array of "
            "literal strings"));
    }
}

void ParsePartSlots(
    JBeamPartDefinition& part,
    const JBeamResolverLimits& limits,
    std::vector<JBeamResolveDiagnostic>& diagnostics)
{
    const JBeamObjectField* slots = NULL;
    const JBeamObjectField* slots2 = NULL;
    for (std::size_t i = 0; i < part.body.object_fields.size(); ++i)
    {
        const JBeamObjectField& field = part.body.object_fields[i];
        if (field.key != "slots" && field.key != "slots2")
        {
            continue;
        }
        const JBeamObjectField*& effective =
            field.key == "slots" ? slots : slots2;
        if (effective != NULL)
        {
            PushDiagnostic(
                diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::DUPLICATE_SLOT_SECTION,
                JBeamResolveSeverity::WARNING,
                field.key_span,
                part.name,
                field.key,
                "Only the last duplicate slot section is effective; all "
                "source fields remain in the part AST"));
        }
        effective = &field;
    }

    if (slots != NULL && slots->value)
    {
        ParseSlotTable(
            *slots->value,
            JBeamSlotKind::SLOTS,
            part.name,
            limits,
            part.slots,
            diagnostics);
    }
    if (slots2 != NULL && slots2->value)
    {
        ParseSlotTable(
            *slots2->value,
            JBeamSlotKind::SLOTS2,
            part.name,
            limits,
            part.slots,
            diagnostics);
    }

    std::stable_sort(
        part.slots.begin(),
        part.slots.end(),
        [](const JBeamSlotDefinition& left,
           const JBeamSlotDefinition& right)
        {
            if (left.span.source_name != right.span.source_name)
            {
                return left.span.source_name < right.span.source_name;
            }
            if (left.span.begin.byte_offset !=
                right.span.begin.byte_offset)
            {
                return left.span.begin.byte_offset <
                       right.span.begin.byte_offset;
            }
            if (left.kind != right.kind)
            {
                return static_cast<int>(left.kind) <
                       static_cast<int>(right.kind);
            }
            return left.name < right.name;
        });

    std::map<std::string, JBeamSourceSpan> names;
    for (std::size_t i = 0; i < part.slots.size(); ++i)
    {
        const JBeamSlotDefinition& slot = part.slots[i];
        if (names.find(slot.name) != names.end())
        {
            PushDiagnostic(
                diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::DUPLICATE_SLOT,
                JBeamResolveSeverity::ERROR,
                slot.span,
                part.name,
                slot.name,
                "Duplicate slot name is ambiguous across slots/slots2"));
        }
        else
        {
            names.insert(std::make_pair(slot.name, slot.span));
        }
    }
}

bool HasSlotType(
    const JBeamPartDefinition& part,
    const std::string& slot_type)
{
    return std::find(
               part.slot_types.begin(),
               part.slot_types.end(),
               slot_type) != part.slot_types.end();
}

bool PartLess(
    const JBeamPartDefinition& left,
    const JBeamPartDefinition& right)
{
    if (left.name != right.name)
    {
        return left.name < right.name;
    }
    if (left.package_path != right.package_path)
    {
        return left.package_path < right.package_path;
    }
    if (left.name_span.source_name != right.name_span.source_name)
    {
        return left.name_span.source_name < right.name_span.source_name;
    }
    if (left.name_span.begin.byte_offset !=
        right.name_span.begin.byte_offset)
    {
        return left.name_span.begin.byte_offset <
               right.name_span.begin.byte_offset;
    }
    return CanonicalValue(left.body) < CanonicalValue(right.body);
}

const char* SeverityToString(JBeamResolveSeverity severity)
{
    return severity == JBeamResolveSeverity::ERROR ? "error" : "warning";
}

const char* SlotKindToString(JBeamSlotKind kind)
{
    return kind == JBeamSlotKind::SLOTS ? "slots" : "slots2";
}

const char* VariableOriginToString(JBeamVariableOrigin origin)
{
    return origin == JBeamVariableOrigin::CONFIGURATION
        ? "configuration"
        : "slot";
}

const char* SlotStatusToString(JBeamResolvedSlotStatus status)
{
    switch (status)
    {
    case JBeamResolvedSlotStatus::RESOLVED: return "resolved";
    case JBeamResolvedSlotStatus::EMPTY: return "empty";
    case JBeamResolvedSlotStatus::MISSING: return "missing";
    case JBeamResolvedSlotStatus::NOT_ALLOWED: return "not-allowed";
    case JBeamResolvedSlotStatus::CYCLE: return "cycle";
    case JBeamResolvedSlotStatus::LIMIT_REJECTED:
        return "limit-rejected";
    }
    return "unknown";
}

void AppendDiagnostic(
    const JBeamResolveDiagnostic& diagnostic,
    std::ostringstream& output)
{
    output
        << "D\t"
        << JBeamResolveDiagnosticCodeToString(diagnostic.code) << '\t'
        << SeverityToString(diagnostic.severity) << '\t'
        << LengthPrefixed(diagnostic.part_name) << '\t'
        << LengthPrefixed(diagnostic.slot_name) << '\t'
        << LengthPrefixed(diagnostic.detail) << '\t';
    AppendSpan(diagnostic.span, output);
    output << '\n';
}

void AppendVariable(
    const JBeamVariableAssignment& variable,
    std::ostringstream& output)
{
    output
        << "V\t" << LengthPrefixed(variable.name) << '\t'
        << VariableOriginToString(variable.origin) << '\t';
    AppendSpan(variable.span, output);
    const std::string value = CanonicalValue(variable.value);
    output << '\t' << LengthPrefixed(value) << '\n';
}

void AppendSlotDefinition(
    const JBeamSlotDefinition& slot,
    std::ostringstream& output)
{
    output
        << SlotKindToString(slot.kind) << '\t'
        << LengthPrefixed(slot.name) << '\t'
        << LengthPrefixed(slot.default_part) << '\t'
        << LengthPrefixed(slot.description) << '\t'
        << (slot.core_slot ? 1 : 0) << '\t';
    AppendSpan(slot.span, output);
    output << "\tallow\t" << slot.allow_types.size();
    for (std::size_t i = 0; i < slot.allow_types.size(); ++i)
    {
        output << '\t' << LengthPrefixed(slot.allow_types[i]);
    }
    output << "\tdeny\t" << slot.deny_types.size();
    for (std::size_t i = 0; i < slot.deny_types.size(); ++i)
    {
        output << '\t' << LengthPrefixed(slot.deny_types[i]);
    }
    output << "\tslot-vars\t" << slot.variables.size() << '\n';
    for (std::size_t i = 0; i < slot.variables.size(); ++i)
    {
        AppendVariable(slot.variables[i], output);
    }
}

bool IsPartAllowed(
    const JBeamSlotDefinition& slot,
    const JBeamPartDefinition& part)
{
    for (std::size_t i = 0; i < slot.deny_types.size(); ++i)
    {
        if (HasSlotType(part, slot.deny_types[i]))
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < slot.allow_types.size(); ++i)
    {
        if (HasSlotType(part, slot.allow_types[i]))
        {
            return true;
        }
    }
    return false;
}

class GraphResolver
{
public:
    GraphResolver(
        const JBeamPackageIndex& index,
        const JBeamResolveRequest& request,
        const JBeamResolverLimits& limits)
        : m_index(index)
        , m_request(request)
        , m_limits(limits)
    {
    }

    JBeamResolvedGraph Run()
    {
        if (!m_index.IsValid())
        {
            Emit(MakeDiagnostic(
                JBeamResolveDiagnosticCode::INDEX_INVALID,
                JBeamResolveSeverity::ERROR,
                JBeamSourceSpan(),
                std::string(),
                std::string(),
                "Cannot resolve a package index containing errors"));
            return Finish();
        }
        if (!PreflightRequest())
        {
            return Finish();
        }
        PrepareLookups();
        ValidateRequest();
        if (HasErrors(m_graph.diagnostics))
        {
            return Finish();
        }
        m_graph.request = m_request;

        const JBeamPartDefinition* root = SelectRoot();
        if (root == NULL)
        {
            return Finish();
        }
        std::vector<JBeamVariableAssignment> variables;
        variables.reserve(m_request.variables.size());
        variables.insert(
            variables.end(),
            m_request.variables.begin(),
            m_request.variables.end());
        m_graph.root = ResolveNode(*root, variables, 0);

        for (std::size_t i = 0;
             i < m_request.part_selections.size();
             ++i)
        {
            if (!m_selection_used[i])
            {
                const JBeamPartSelection& selection =
                    m_request.part_selections[i];
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::UNUSED_PART_SELECTION,
                    JBeamResolveSeverity::WARNING,
                    selection.span,
                    std::string(),
                    selection.slot_name,
                    "Configuration selection did not match a slot in the "
                    "resolved graph"));
            }
        }
        return Finish();
    }

private:
    struct SelectionLookup
    {
        std::size_t effective_index;
        std::vector<std::size_t> occurrence_indices;
        bool marked_used;

        SelectionLookup()
            : effective_index(0U)
            , marked_used(false)
        {
        }
    };

    void Emit(const JBeamResolveDiagnostic& diagnostic)
    {
        PushDiagnostic(
            m_graph.diagnostics,
            m_limits.max_diagnostics,
            diagnostic);
    }

    JBeamResolvedGraph Finish()
    {
        SortDiagnostics(m_graph.diagnostics);
        return m_graph;
    }

    bool PreflightRequest()
    {
        if (m_request.part_selections.size() >
            m_limits.max_request_selections)
        {
            const std::size_t rejected_index =
                m_limits.max_request_selections;
            const JBeamSourceSpan span =
                rejected_index < m_request.part_selections.size()
                    ? m_request.part_selections[rejected_index].span
                    : JBeamSourceSpan();
            Emit(MakeDiagnostic(
                JBeamResolveDiagnosticCode::PART_SELECTION_LIMIT,
                JBeamResolveSeverity::ERROR,
                span,
                std::string(),
                std::string(),
                "Resolve request exceeds the configured part-selection "
                "limit"));
            return false;
        }
        if (m_request.variables.size() >
            m_limits.max_request_variables)
        {
            const std::size_t rejected_index =
                m_limits.max_request_variables;
            const JBeamSourceSpan span =
                rejected_index < m_request.variables.size()
                    ? m_request.variables[rejected_index].span
                    : JBeamSourceSpan();
            Emit(MakeDiagnostic(
                JBeamResolveDiagnosticCode::REQUEST_VARIABLE_LIMIT,
                JBeamResolveSeverity::ERROR,
                span,
                std::string(),
                std::string(),
                "Resolve request exceeds the configured variable limit"));
            return false;
        }
        if (m_request.variables.size() >
            m_limits.max_variables_per_node)
        {
            const std::size_t rejected_index =
                m_limits.max_variables_per_node;
            const JBeamSourceSpan span =
                rejected_index < m_request.variables.size()
                    ? m_request.variables[rejected_index].span
                    : JBeamSourceSpan();
            Emit(MakeDiagnostic(
                JBeamResolveDiagnosticCode::RESOLVED_VARIABLE_LIMIT,
                JBeamResolveSeverity::ERROR,
                span,
                std::string(),
                std::string(),
                "Root variables exceed the configured per-node limit"));
            return false;
        }
        return true;
    }

    void PrepareLookups()
    {
        m_selection_used.assign(
            m_request.part_selections.size(), false);
        for (std::size_t i = 0; i < m_index.parts.size(); ++i)
        {
            m_parts[m_index.parts[i].name].push_back(i);
        }
        for (std::size_t i = 0;
             i < m_request.part_selections.size();
             ++i)
        {
            SelectionLookup& lookup =
                m_selections[
                    m_request.part_selections[i].slot_name];
            lookup.effective_index = i;
            lookup.occurrence_indices.push_back(i);
        }
    }

    void ValidateRequest()
    {
        for (std::size_t i = 0;
             i < m_request.part_selections.size();
             ++i)
        {
            const JBeamPartSelection& selection =
                m_request.part_selections[i];
            if (selection.slot_name.empty())
            {
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::INVALID_PART_SELECTION,
                    JBeamResolveSeverity::ERROR,
                    selection.span,
                    std::string(),
                    selection.slot_name,
                    "Part-selection slot name cannot be empty"));
            }
            if (!selection.part_name.empty() &&
                (selection.part_name.compare(0, 2, "$=") == 0 ||
                 selection.part_name[0] == '$'))
            {
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::INVALID_PART_SELECTION,
                    JBeamResolveSeverity::ERROR,
                    selection.span,
                    std::string(),
                    selection.slot_name,
                    "Structural part selections cannot be unresolved "
                    "variable or expression strings"));
            }
        }
        for (std::map<std::string, SelectionLookup>::const_iterator
                 lookup = m_selections.begin();
             lookup != m_selections.end();
             ++lookup)
        {
            for (std::size_t occurrence = 1U;
                 occurrence <
                     lookup->second.occurrence_indices.size();
                 ++occurrence)
            {
                const std::size_t selection_index =
                    lookup->second.occurrence_indices[occurrence];
                const JBeamPartSelection& selection =
                    m_request.part_selections[selection_index];
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::
                        DUPLICATE_PART_SELECTION,
                    JBeamResolveSeverity::WARNING,
                    selection.span,
                    std::string(),
                    selection.slot_name,
                    "Duplicate selection uses the last request entry"));
            }
        }

        std::map<std::string, JBeamSourceSpan> variables;
        for (std::size_t i = 0; i < m_request.variables.size(); ++i)
        {
            const JBeamVariableAssignment& variable =
                m_request.variables[i];
            if (!IsVariableName(variable.name) ||
                !IsScalarVariableValue(variable.value))
            {
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::
                        INVALID_CONFIGURATION_VARIABLE,
                    JBeamResolveSeverity::ERROR,
                    variable.span,
                    std::string(),
                    std::string(),
                    "Configuration variable names must begin with '$' and "
                    "values must be number, string, or Boolean scalars"));
            }
            if (variables.find(variable.name) != variables.end())
            {
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::
                        DUPLICATE_CONFIGURATION_VARIABLE,
                    JBeamResolveSeverity::WARNING,
                    variable.span,
                    std::string(),
                    std::string(),
                    "Duplicate configuration variable uses "
                    "last-assignment semantics"));
            }
            else
            {
                variables.insert(
                    std::make_pair(variable.name, variable.span));
            }
        }
    }

    const JBeamPartDefinition* SelectRoot()
    {
        if (!m_request.root_part_name.empty())
        {
            const std::map<std::string, std::vector<std::size_t> >::
                const_iterator found =
                    m_parts.find(m_request.root_part_name);
            if (found == m_parts.end())
            {
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::ROOT_PART_NOT_FOUND,
                    JBeamResolveSeverity::ERROR,
                    JBeamSourceSpan(),
                    m_request.root_part_name,
                    std::string(),
                    "Requested root part is not present in the package"));
                return NULL;
            }
            if (found->second.size() != 1)
            {
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::AMBIGUOUS_PART,
                    JBeamResolveSeverity::ERROR,
                    JBeamSourceSpan(),
                    m_request.root_part_name,
                    std::string(),
                    "Requested root part has duplicate definitions"));
                return NULL;
            }
            const JBeamPartDefinition& part =
                m_index.parts[found->second[0]];
            if (!HasSlotType(part, "main"))
            {
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::ROOT_PART_NOT_MAIN,
                    JBeamResolveSeverity::ERROR,
                    part.name_span,
                    part.name,
                    std::string(),
                    "Requested root does not declare slotType 'main'"));
                return NULL;
            }
            return &part;
        }

        const JBeamPartDefinition* root = NULL;
        for (std::size_t i = 0; i < m_index.parts.size(); ++i)
        {
            if (!HasSlotType(m_index.parts[i], "main"))
            {
                continue;
            }
            if (root != NULL)
            {
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::MULTIPLE_MAIN_PARTS,
                    JBeamResolveSeverity::ERROR,
                    m_index.parts[i].name_span,
                    m_index.parts[i].name,
                    std::string(),
                    "Package has more than one slotType 'main' part"));
                return NULL;
            }
            root = &m_index.parts[i];
        }
        if (root == NULL)
        {
            Emit(MakeDiagnostic(
                JBeamResolveDiagnosticCode::ROOT_PART_NOT_FOUND,
                JBeamResolveSeverity::ERROR,
                JBeamSourceSpan(),
                std::string(),
                std::string(),
                "Package has no slotType 'main' part"));
        }
        return root;
    }

    const JBeamPartSelection* FindSelection(const std::string& slot_name)
    {
        const std::map<std::string, SelectionLookup>::iterator found =
            m_selections.find(slot_name);
        if (found == m_selections.end())
        {
            return NULL;
        }
        SelectionLookup& lookup = found->second;
        if (!lookup.marked_used)
        {
            for (std::size_t i = 0;
                 i < lookup.occurrence_indices.size();
                 ++i)
            {
                m_selection_used[lookup.occurrence_indices[i]] = true;
            }
            lookup.marked_used = true;
        }
        return &m_request.part_selections[lookup.effective_index];
    }

    bool IsOnStack(const std::string& part_name) const
    {
        return std::find(
                   m_stack.begin(),
                   m_stack.end(),
                   part_name) != m_stack.end();
    }

    std::shared_ptr<JBeamResolvedPartNode> ResolveNode(
        const JBeamPartDefinition& part,
        const std::vector<JBeamVariableAssignment>& variables,
        std::size_t depth)
    {
        if (depth > m_limits.max_depth)
        {
            Emit(MakeDiagnostic(
                JBeamResolveDiagnosticCode::RESOLVE_DEPTH_LIMIT,
                JBeamResolveSeverity::ERROR,
                part.name_span,
                part.name,
                std::string(),
                "Resolved part depth exceeds the configured limit"));
            return std::shared_ptr<JBeamResolvedPartNode>();
        }
        if (m_graph.resolved_part_count >=
            m_limits.max_resolved_parts)
        {
            Emit(MakeDiagnostic(
                JBeamResolveDiagnosticCode::RESOLVED_PART_LIMIT,
                JBeamResolveSeverity::ERROR,
                part.name_span,
                part.name,
                std::string(),
                "Resolved graph exceeds the configured part-count limit"));
            return std::shared_ptr<JBeamResolvedPartNode>();
        }
        ++m_graph.resolved_part_count;

        std::shared_ptr<JBeamResolvedPartNode> node(
            new JBeamResolvedPartNode());
        node->definition = part;
        node->inherited_variables = variables;
        m_stack.push_back(part.name);

        for (std::size_t i = 0; i < part.slots.size(); ++i)
        {
            const JBeamSlotDefinition& slot = part.slots[i];
            JBeamResolvedSlot edge;
            edge.definition = slot;
            const JBeamPartSelection* selection =
                FindSelection(slot.name);
            edge.explicitly_selected = selection != NULL;
            edge.selected_part =
                selection != NULL
                    ? selection->part_name
                    : slot.default_part;

            if (edge.selected_part.empty())
            {
                if (slot.core_slot)
                {
                    edge.status = JBeamResolvedSlotStatus::MISSING;
                    Emit(MakeDiagnostic(
                        JBeamResolveDiagnosticCode::MISSING_REQUIRED_PART,
                        JBeamResolveSeverity::ERROR,
                        slot.span,
                        part.name,
                        slot.name,
                        "Required coreSlot resolved to an empty selection"));
                }
                else
                {
                    edge.status = JBeamResolvedSlotStatus::EMPTY;
                }
                node->slots.push_back(edge);
                continue;
            }

            const std::map<std::string, std::vector<std::size_t> >::
                const_iterator found = m_parts.find(edge.selected_part);
            if (found == m_parts.end())
            {
                edge.status = JBeamResolvedSlotStatus::MISSING;
                Emit(MakeDiagnostic(
                    slot.core_slot
                        ? JBeamResolveDiagnosticCode::MISSING_REQUIRED_PART
                        : JBeamResolveDiagnosticCode::MISSING_OPTIONAL_PART,
                    slot.core_slot
                        ? JBeamResolveSeverity::ERROR
                        : JBeamResolveSeverity::WARNING,
                    slot.span,
                    part.name,
                    slot.name,
                    "Selected part '" + edge.selected_part +
                        "' is not present in the package"));
                node->slots.push_back(edge);
                continue;
            }
            if (found->second.size() != 1)
            {
                edge.status = JBeamResolvedSlotStatus::MISSING;
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::AMBIGUOUS_PART,
                    JBeamResolveSeverity::ERROR,
                    slot.span,
                    edge.selected_part,
                    slot.name,
                    "Selected part has duplicate package definitions"));
                node->slots.push_back(edge);
                continue;
            }

            const JBeamPartDefinition& child_part =
                m_index.parts[found->second[0]];
            if (!IsPartAllowed(slot, child_part))
            {
                edge.status = JBeamResolvedSlotStatus::NOT_ALLOWED;
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::PART_NOT_ALLOWED_IN_SLOT,
                    JBeamResolveSeverity::ERROR,
                    slot.span,
                    child_part.name,
                    slot.name,
                    "Selected part slotType is not allowed, or is denied "
                    "by slots2"));
                node->slots.push_back(edge);
                continue;
            }
            if (IsOnStack(child_part.name))
            {
                edge.status = JBeamResolvedSlotStatus::CYCLE;
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::SLOT_CYCLE,
                    JBeamResolveSeverity::ERROR,
                    slot.span,
                    child_part.name,
                    slot.name,
                    "Recursive slot selection forms a part cycle"));
                node->slots.push_back(edge);
                continue;
            }

            // Reject graph-shape limits before constructing the child's
            // inherited-variable vector. A part with many rejected slots must
            // not repeatedly allocate and copy variables after the graph is
            // already known to be at its depth or part-count bound.
            if (depth >= m_limits.max_depth)
            {
                edge.status = JBeamResolvedSlotStatus::LIMIT_REJECTED;
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::RESOLVE_DEPTH_LIMIT,
                    JBeamResolveSeverity::ERROR,
                    slot.span,
                    child_part.name,
                    slot.name,
                    "Child would exceed the configured resolve depth"));
                node->slots.push_back(edge);
                continue;
            }
            if (m_graph.resolved_part_count >=
                m_limits.max_resolved_parts)
            {
                edge.status = JBeamResolvedSlotStatus::LIMIT_REJECTED;
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::RESOLVED_PART_LIMIT,
                    JBeamResolveSeverity::ERROR,
                    slot.span,
                    child_part.name,
                    slot.name,
                    "Child would exceed the configured resolved-part "
                    "limit"));
                node->slots.push_back(edge);
                continue;
            }
            if (variables.size() >
                    m_limits.max_variables_per_node ||
                slot.variables.size() >
                    m_limits.max_variables_per_node -
                        variables.size())
            {
                edge.status = JBeamResolvedSlotStatus::LIMIT_REJECTED;
                Emit(MakeDiagnostic(
                    JBeamResolveDiagnosticCode::RESOLVED_VARIABLE_LIMIT,
                    JBeamResolveSeverity::ERROR,
                    slot.span,
                    child_part.name,
                    slot.name,
                    "Inherited variables exceed the configured per-node "
                    "limit"));
                node->slots.push_back(edge);
                continue;
            }
            std::vector<JBeamVariableAssignment> child_variables;
            const std::size_t child_variable_count =
                variables.size() + slot.variables.size();
            child_variables.reserve(child_variable_count);
            child_variables.insert(
                child_variables.end(),
                variables.begin(),
                variables.end());
            child_variables.insert(
                child_variables.end(),
                slot.variables.begin(),
                slot.variables.end());

            edge.child =
                ResolveNode(child_part, child_variables, depth + 1);
            edge.status = edge.child
                ? JBeamResolvedSlotStatus::RESOLVED
                : JBeamResolvedSlotStatus::LIMIT_REJECTED;
            node->slots.push_back(edge);
        }
        m_stack.pop_back();
        return node;
    }

    const JBeamPackageIndex& m_index;
    const JBeamResolveRequest& m_request;
    const JBeamResolverLimits& m_limits;
    std::map<std::string, std::vector<std::size_t> > m_parts;
    std::map<std::string, SelectionLookup> m_selections;
    std::vector<bool> m_selection_used;
    std::vector<std::string> m_stack;
    JBeamResolvedGraph m_graph;
};

void AppendResolvedNode(
    const JBeamResolvedPartNode& node,
    std::size_t depth,
    std::ostringstream& output)
{
    output
        << "N\t" << depth << '\t'
        << LengthPrefixed(node.definition.name) << '\t'
        << LengthPrefixed(node.definition.package_path) << '\t';
    AppendSpan(node.definition.name_span, output);
    const std::string body = CanonicalValue(node.definition.body);
    output << "\tbody\t" << LengthPrefixed(body) << '\n';

    output << "node-vars\t" << node.inherited_variables.size() << '\n';
    for (std::size_t i = 0; i < node.inherited_variables.size(); ++i)
    {
        AppendVariable(node.inherited_variables[i], output);
    }
    output << "node-slots\t" << node.slots.size() << '\n';
    for (std::size_t i = 0; i < node.slots.size(); ++i)
    {
        const JBeamResolvedSlot& edge = node.slots[i];
        output
            << "E\t" << depth << '\t'
            << SlotStatusToString(edge.status) << '\t'
            << (edge.explicitly_selected ? 1 : 0) << '\t'
            << LengthPrefixed(edge.selected_part) << '\t';
        AppendSlotDefinition(edge.definition, output);
        output << "child\t" << (edge.child ? 1 : 0) << '\n';
        if (edge.child)
        {
            AppendResolvedNode(*edge.child, depth + 1, output);
        }
    }
}

} // namespace

JBeamResolveDiagnostic::JBeamResolveDiagnostic()
    : code(JBeamResolveDiagnosticCode::PACKAGE_DOCUMENT_NOT_OBJECT)
    , severity(JBeamResolveSeverity::ERROR)
{
}

JBeamResolverLimits::JBeamResolverLimits()
    : max_input_entries(200000U)
    , max_indexed_parts(100000U)
    , max_resolved_parts(4096U)
    , max_depth(128U)
    , max_slots_per_part(4096U)
    , max_request_selections(4096U)
    , max_request_variables(4096U)
    , max_variables_per_node(4096U)
    , max_diagnostics(4096U)
{
}

JBeamSlotDefinition::JBeamSlotDefinition()
    : kind(JBeamSlotKind::SLOTS)
    , core_slot(false)
{
}

bool JBeamPackageIndex::IsValid() const
{
    return !HasErrors(diagnostics);
}

JBeamPackageIndex BuildJBeamPackageIndex(
    const std::vector<JBeamPackageSource>& sources,
    const JBeamResolverLimits& limits)
{
    struct OrderedSource
    {
        const JBeamPackageSource* source;
        std::string body_key;
    };
    std::vector<OrderedSource> ordered;
    ordered.reserve(sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i)
    {
        OrderedSource entry;
        entry.source = &sources[i];
        entry.body_key = CanonicalValue(sources[i].document);
        ordered.push_back(entry);
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const OrderedSource& left, const OrderedSource& right)
        {
            if (left.source->package_path != right.source->package_path)
            {
                return left.source->package_path <
                       right.source->package_path;
            }
            if (left.source->document.span.source_name !=
                right.source->document.span.source_name)
            {
                return left.source->document.span.source_name <
                       right.source->document.span.source_name;
            }
            return left.body_key < right.body_key;
        });

    JBeamPackageIndex index;
    bool limit_reached = false;
    std::size_t input_entry_count = 0U;
    for (std::size_t source_index = 0;
         source_index < ordered.size() && !limit_reached;
         ++source_index)
    {
        const JBeamPackageSource& source = *ordered[source_index].source;
        if (source.document.type != JBeamValueType::OBJECT)
        {
            PushDiagnostic(
                index.diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::PACKAGE_DOCUMENT_NOT_OBJECT,
                JBeamResolveSeverity::ERROR,
                source.document.span,
                std::string(),
                std::string(),
                "JBeam package documents must be top-level part "
                "dictionaries"));
            continue;
        }
        for (std::size_t field_index = 0;
             field_index < source.document.object_fields.size();
             ++field_index)
        {
            const JBeamObjectField& field =
                source.document.object_fields[field_index];
            if (input_entry_count >= limits.max_input_entries)
            {
                PushDiagnostic(
                    index.diagnostics,
                    limits.max_diagnostics,
                    MakeDiagnostic(
                        JBeamResolveDiagnosticCode::
                            INDEX_INPUT_ENTRY_LIMIT,
                        JBeamResolveSeverity::ERROR,
                        field.key_span,
                        field.key,
                        std::string(),
                        "Package exceeds the configured top-level "
                        "input-entry limit"));
                limit_reached = true;
                break;
            }
            ++input_entry_count;
            if (field.key.empty())
            {
                PushDiagnostic(
                    index.diagnostics,
                    limits.max_diagnostics,
                    MakeDiagnostic(
                    JBeamResolveDiagnosticCode::EMPTY_PART_NAME,
                    JBeamResolveSeverity::ERROR,
                    field.key_span,
                    field.key,
                    std::string(),
                    "Part name cannot be empty"));
                continue;
            }
            if (!field.value ||
                field.value->type != JBeamValueType::OBJECT)
            {
                PushDiagnostic(
                    index.diagnostics,
                    limits.max_diagnostics,
                    MakeDiagnostic(
                    JBeamResolveDiagnosticCode::PART_NOT_OBJECT,
                    JBeamResolveSeverity::ERROR,
                    field.value ? field.value->span : field.key_span,
                    field.key,
                    std::string(),
                    "Top-level JBeam part values must be dictionaries"));
                continue;
            }
            if (index.parts.size() >= limits.max_indexed_parts)
            {
                PushDiagnostic(
                    index.diagnostics,
                    limits.max_diagnostics,
                    MakeDiagnostic(
                    JBeamResolveDiagnosticCode::INDEX_PART_LIMIT,
                    JBeamResolveSeverity::ERROR,
                    field.key_span,
                    field.key,
                    std::string(),
                    "Package exceeds the configured indexed-part limit"));
                limit_reached = true;
                break;
            }

            JBeamPartDefinition part;
            part.name = field.key;
            part.package_path = source.package_path;
            part.name_span = field.key_span;
            part.body = *field.value;
            ParsePartSlotTypes(part, limits, index.diagnostics);
            ParsePartSlots(part, limits, index.diagnostics);
            index.parts.push_back(part);
        }
    }

    std::sort(index.parts.begin(), index.parts.end(), PartLess);
    for (std::size_t i = 1; i < index.parts.size(); ++i)
    {
        if (index.parts[i - 1].name == index.parts[i].name)
        {
            PushDiagnostic(
                index.diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::DUPLICATE_PART,
                JBeamResolveSeverity::ERROR,
                index.parts[i].name_span,
                index.parts[i].name,
                std::string(),
                "Package contains multiple definitions of the same "
                "case-sensitive part name"));
        }
    }

    SortDiagnostics(index.diagnostics);
    return index;
}

std::string SerializeCanonicalJBeamPackageIndex(
    const JBeamPackageIndex& index)
{
    std::vector<JBeamPartDefinition> parts = index.parts;
    std::sort(parts.begin(), parts.end(), PartLess);
    std::vector<JBeamResolveDiagnostic> diagnostics = index.diagnostics;
    SortDiagnostics(diagnostics);

    std::ostringstream output;
    output << "ror-beamng-package-index-v1\n";
    output << "parts\t" << parts.size() << '\n';
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        const JBeamPartDefinition& part = parts[i];
        output
            << "P\t" << LengthPrefixed(part.name) << '\t'
            << LengthPrefixed(part.package_path) << '\t';
        AppendSpan(part.name_span, output);
        output << "\tslot-types\t" << part.slot_types.size();
        for (std::size_t type_index = 0;
             type_index < part.slot_types.size();
             ++type_index)
        {
            output << '\t'
                   << LengthPrefixed(part.slot_types[type_index]);
        }
        const std::string body = CanonicalValue(part.body);
        output << "\tbody\t" << LengthPrefixed(body) << '\n';
        output << "part-slots\t" << part.slots.size() << '\n';
        for (std::size_t slot_index = 0;
             slot_index < part.slots.size();
             ++slot_index)
        {
            output << "S\t";
            AppendSlotDefinition(part.slots[slot_index], output);
        }
    }
    output << "diagnostics\t" << diagnostics.size() << '\n';
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        AppendDiagnostic(diagnostics[i], output);
    }
    return output.str();
}

bool JBeamConfigurationResult::IsValid() const
{
    return !HasErrors(diagnostics);
}

JBeamConfigurationResult ParseJBeamConfiguration(
    const JBeamValue& configuration)
{
    return ParseJBeamConfiguration(
        configuration, JBeamResolverLimits());
}

JBeamConfigurationResult ParseJBeamConfiguration(
    const JBeamValue& configuration,
    const JBeamResolverLimits& limits)
{
    JBeamConfigurationResult result;
    if (configuration.type != JBeamValueType::OBJECT)
    {
        PushDiagnostic(
            result.diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
            JBeamResolveDiagnosticCode::CONFIGURATION_NOT_OBJECT,
            JBeamResolveSeverity::ERROR,
            configuration.span,
            std::string(),
            std::string(),
            "BeamNG configuration must be a dictionary"));
        return result;
    }

    const JBeamObjectField* parts = NULL;
    const JBeamObjectField* variables = NULL;
    for (std::size_t i = 0;
         i < configuration.object_fields.size();
         ++i)
    {
        const JBeamObjectField& field =
            configuration.object_fields[i];
        if (field.key != "parts" && field.key != "vars")
        {
            continue;
        }
        const JBeamObjectField*& effective =
            field.key == "parts" ? parts : variables;
        if (effective != NULL)
        {
            PushDiagnostic(
                result.diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::
                    DUPLICATE_CONFIGURATION_SECTION,
                JBeamResolveSeverity::WARNING,
                field.key_span,
                std::string(),
                field.key,
                "Only the last duplicate configuration section is "
                "effective"));
        }
        effective = &field;
    }

    bool resource_limit_reached = false;
    if (parts != NULL && parts->value &&
        parts->value->type == JBeamValueType::OBJECT &&
        parts->value->object_fields.size() >
            limits.max_request_selections)
    {
        const std::size_t rejected_index =
            limits.max_request_selections;
        const JBeamSourceSpan span =
            rejected_index < parts->value->object_fields.size()
                ? parts->value->object_fields[rejected_index].key_span
                : parts->key_span;
        PushDiagnostic(
            result.diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
                JBeamResolveDiagnosticCode::PART_SELECTION_LIMIT,
                JBeamResolveSeverity::ERROR,
                span,
                std::string(),
                "parts",
                "Configuration exceeds the configured part-selection "
                "limit"));
        resource_limit_reached = true;
    }
    if (variables != NULL && variables->value &&
        variables->value->type == JBeamValueType::OBJECT &&
        variables->value->object_fields.size() >
            limits.max_request_variables)
    {
        const std::size_t rejected_index =
            limits.max_request_variables;
        const JBeamSourceSpan span =
            rejected_index < variables->value->object_fields.size()
                ? variables->value->object_fields[rejected_index].key_span
                : variables->key_span;
        PushDiagnostic(
            result.diagnostics,
            limits.max_diagnostics,
            MakeDiagnostic(
                JBeamResolveDiagnosticCode::REQUEST_VARIABLE_LIMIT,
                JBeamResolveSeverity::ERROR,
                span,
                std::string(),
                "vars",
                "Configuration exceeds the configured variable limit"));
        resource_limit_reached = true;
    }
    if (resource_limit_reached)
    {
        SortDiagnostics(result.diagnostics);
        return result;
    }

    if (parts != NULL)
    {
        if (!parts->value ||
            parts->value->type != JBeamValueType::OBJECT)
        {
            PushDiagnostic(
                result.diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::
                    INVALID_CONFIGURATION_SECTION,
                JBeamResolveSeverity::ERROR,
                parts->value ? parts->value->span : parts->key_span,
                std::string(),
                "parts",
                "Configuration parts must be a dictionary"));
        }
        else
        {
            std::set<std::string> seen;
            for (std::size_t i = 0;
                 i < parts->value->object_fields.size();
                 ++i)
            {
                const JBeamObjectField& field =
                    parts->value->object_fields[i];
                if (!field.value ||
                    field.value->type != JBeamValueType::STRING ||
                    (!field.value->scalar_text.empty() &&
                     !IsStructuralString(*field.value)))
                {
                    PushDiagnostic(
                        result.diagnostics,
                        limits.max_diagnostics,
                        MakeDiagnostic(
                        JBeamResolveDiagnosticCode::
                            INVALID_PART_SELECTION,
                        JBeamResolveSeverity::ERROR,
                        field.value
                            ? field.value->span
                            : field.key_span,
                        std::string(),
                        field.key,
                        "Configuration part selections must be strings"));
                    continue;
                }
                if (!seen.insert(field.key).second)
                {
                    PushDiagnostic(
                        result.diagnostics,
                        limits.max_diagnostics,
                        MakeDiagnostic(
                        JBeamResolveDiagnosticCode::
                            DUPLICATE_PART_SELECTION,
                        JBeamResolveSeverity::WARNING,
                        field.key_span,
                        std::string(),
                        field.key,
                        "Duplicate selection uses last-assignment "
                        "semantics"));
                }
                JBeamPartSelection selection;
                selection.slot_name = field.key;
                selection.part_name = field.value->scalar_text;
                selection.span = field.value->span;
                result.request.part_selections.push_back(selection);
            }
        }
    }

    if (variables != NULL)
    {
        if (!variables->value ||
            variables->value->type != JBeamValueType::OBJECT)
        {
            PushDiagnostic(
                result.diagnostics,
                limits.max_diagnostics,
                MakeDiagnostic(
                JBeamResolveDiagnosticCode::
                    INVALID_CONFIGURATION_SECTION,
                JBeamResolveSeverity::ERROR,
                variables->value
                    ? variables->value->span
                    : variables->key_span,
                std::string(),
                "vars",
                "Configuration vars must be a dictionary"));
        }
        else
        {
            std::set<std::string> seen;
            for (std::size_t i = 0;
                 i < variables->value->object_fields.size();
                 ++i)
            {
                const JBeamObjectField& field =
                    variables->value->object_fields[i];
                if (!IsVariableName(field.key) ||
                    !field.value ||
                    !IsScalarVariableValue(*field.value))
                {
                    PushDiagnostic(
                        result.diagnostics,
                        limits.max_diagnostics,
                        MakeDiagnostic(
                        JBeamResolveDiagnosticCode::
                            INVALID_CONFIGURATION_VARIABLE,
                        JBeamResolveSeverity::ERROR,
                        field.value
                            ? field.value->span
                            : field.key_span,
                        std::string(),
                        std::string(),
                        "Configuration variable names must begin with '$' "
                        "and values must be number, string, or Boolean "
                        "scalars"));
                    continue;
                }
                if (!seen.insert(field.key).second)
                {
                    PushDiagnostic(
                        result.diagnostics,
                        limits.max_diagnostics,
                        MakeDiagnostic(
                        JBeamResolveDiagnosticCode::
                            DUPLICATE_CONFIGURATION_VARIABLE,
                        JBeamResolveSeverity::WARNING,
                        field.key_span,
                        std::string(),
                        std::string(),
                        "Duplicate variable uses last-assignment "
                        "semantics"));
                }
                JBeamVariableAssignment assignment;
                assignment.name = field.key;
                assignment.value = *field.value;
                assignment.span = field.value->span;
                assignment.origin = JBeamVariableOrigin::CONFIGURATION;
                result.request.variables.push_back(assignment);
            }
        }
    }
    SortDiagnostics(result.diagnostics);
    if (!result.diagnostics.empty() &&
        IsDiagnosticLimit(result.diagnostics.back()))
    {
        // A capped report cannot prove which later entries were valid. Do not
        // expose a partially parsed request that a caller could accidentally
        // resolve after ignoring IsValid().
        result.request = JBeamResolveRequest();
    }
    return result;
}

JBeamResolvedSlot::JBeamResolvedSlot()
    : explicitly_selected(false)
    , status(JBeamResolvedSlotStatus::EMPTY)
{
}

JBeamResolvedGraph::JBeamResolvedGraph()
    : resolved_part_count(0)
{
}

bool JBeamResolvedGraph::IsValid() const
{
    return root && !HasErrors(diagnostics);
}

JBeamResolvedGraph ResolveJBeamPartGraph(
    const JBeamPackageIndex& index,
    const JBeamResolveRequest& request,
    const JBeamResolverLimits& limits)
{
    return GraphResolver(index, request, limits).Run();
}

std::string SerializeCanonicalJBeamResolvedGraph(
    const JBeamResolvedGraph& graph)
{
    std::vector<JBeamResolveDiagnostic> diagnostics = graph.diagnostics;
    SortDiagnostics(diagnostics);
    std::ostringstream output;
    output << "ror-beamng-resolved-graph-v1\n";
    output
        << "request-root\t"
        << LengthPrefixed(graph.request.root_part_name) << '\n';
    output
        << "request-selections\t"
        << graph.request.part_selections.size() << '\n';
    for (std::size_t i = 0;
         i < graph.request.part_selections.size();
         ++i)
    {
        const JBeamPartSelection& selection =
            graph.request.part_selections[i];
        output
            << "Q\t" << LengthPrefixed(selection.slot_name) << '\t'
            << LengthPrefixed(selection.part_name) << '\t';
        AppendSpan(selection.span, output);
        output << '\n';
    }
    output << "request-vars\t" << graph.request.variables.size() << '\n';
    for (std::size_t i = 0; i < graph.request.variables.size(); ++i)
    {
        AppendVariable(graph.request.variables[i], output);
    }
    output << "resolved-parts\t" << graph.resolved_part_count << '\n';
    output << "root\t" << (graph.root ? 1 : 0) << '\n';
    if (graph.root)
    {
        AppendResolvedNode(*graph.root, 0, output);
    }
    output << "diagnostics\t" << diagnostics.size() << '\n';
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        AppendDiagnostic(diagnostics[i], output);
    }
    return output.str();
}

const JBeamVariableAssignment* FindEffectiveJBeamVariable(
    const JBeamResolvedPartNode& node,
    const std::string& name)
{
    for (std::size_t i = node.inherited_variables.size(); i > 0; --i)
    {
        if (node.inherited_variables[i - 1].name == name)
        {
            return &node.inherited_variables[i - 1];
        }
    }
    return NULL;
}

const char* JBeamResolveDiagnosticCodeToString(
    JBeamResolveDiagnosticCode code)
{
    switch (code)
    {
    case JBeamResolveDiagnosticCode::PACKAGE_DOCUMENT_NOT_OBJECT:
        return "package-document-not-object";
    case JBeamResolveDiagnosticCode::PART_NOT_OBJECT:
        return "part-not-object";
    case JBeamResolveDiagnosticCode::EMPTY_PART_NAME:
        return "empty-part-name";
    case JBeamResolveDiagnosticCode::MISSING_SLOT_TYPE:
        return "missing-slot-type";
    case JBeamResolveDiagnosticCode::INVALID_SLOT_TYPE:
        return "invalid-slot-type";
    case JBeamResolveDiagnosticCode::DUPLICATE_SLOT_TYPE:
        return "duplicate-slot-type";
    case JBeamResolveDiagnosticCode::DUPLICATE_PART:
        return "duplicate-part";
    case JBeamResolveDiagnosticCode::MULTIPLE_MAIN_PARTS:
        return "multiple-main-parts";
    case JBeamResolveDiagnosticCode::DUPLICATE_SLOT_SECTION:
        return "duplicate-slot-section";
    case JBeamResolveDiagnosticCode::INVALID_SLOT_TABLE:
        return "invalid-slot-table";
    case JBeamResolveDiagnosticCode::INVALID_SLOT_ROW:
        return "invalid-slot-row";
    case JBeamResolveDiagnosticCode::MISSING_SLOT_FIELD:
        return "missing-slot-field";
    case JBeamResolveDiagnosticCode::INVALID_SLOT_FIELD:
        return "invalid-slot-field";
    case JBeamResolveDiagnosticCode::DUPLICATE_SLOT:
        return "duplicate-slot";
    case JBeamResolveDiagnosticCode::INVALID_SLOT_VARIABLE:
        return "invalid-slot-variable";
    case JBeamResolveDiagnosticCode::INDEX_INPUT_ENTRY_LIMIT:
        return "index-input-entry-limit";
    case JBeamResolveDiagnosticCode::INDEX_PART_LIMIT:
        return "index-part-limit";
    case JBeamResolveDiagnosticCode::CONFIGURATION_NOT_OBJECT:
        return "configuration-not-object";
    case JBeamResolveDiagnosticCode::INVALID_CONFIGURATION_SECTION:
        return "invalid-configuration-section";
    case JBeamResolveDiagnosticCode::DUPLICATE_CONFIGURATION_SECTION:
        return "duplicate-configuration-section";
    case JBeamResolveDiagnosticCode::INVALID_PART_SELECTION:
        return "invalid-part-selection";
    case JBeamResolveDiagnosticCode::DUPLICATE_PART_SELECTION:
        return "duplicate-part-selection";
    case JBeamResolveDiagnosticCode::PART_SELECTION_LIMIT:
        return "part-selection-limit";
    case JBeamResolveDiagnosticCode::INVALID_CONFIGURATION_VARIABLE:
        return "invalid-configuration-variable";
    case JBeamResolveDiagnosticCode::DUPLICATE_CONFIGURATION_VARIABLE:
        return "duplicate-configuration-variable";
    case JBeamResolveDiagnosticCode::REQUEST_VARIABLE_LIMIT:
        return "request-variable-limit";
    case JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT:
        return "diagnostic-limit";
    case JBeamResolveDiagnosticCode::INDEX_INVALID:
        return "index-invalid";
    case JBeamResolveDiagnosticCode::ROOT_PART_NOT_FOUND:
        return "root-part-not-found";
    case JBeamResolveDiagnosticCode::ROOT_PART_NOT_MAIN:
        return "root-part-not-main";
    case JBeamResolveDiagnosticCode::AMBIGUOUS_PART:
        return "ambiguous-part";
    case JBeamResolveDiagnosticCode::MISSING_REQUIRED_PART:
        return "missing-required-part";
    case JBeamResolveDiagnosticCode::MISSING_OPTIONAL_PART:
        return "missing-optional-part";
    case JBeamResolveDiagnosticCode::PART_NOT_ALLOWED_IN_SLOT:
        return "part-not-allowed-in-slot";
    case JBeamResolveDiagnosticCode::SLOT_CYCLE:
        return "slot-cycle";
    case JBeamResolveDiagnosticCode::RESOLVE_DEPTH_LIMIT:
        return "resolve-depth-limit";
    case JBeamResolveDiagnosticCode::RESOLVED_PART_LIMIT:
        return "resolved-part-limit";
    case JBeamResolveDiagnosticCode::RESOLVED_VARIABLE_LIMIT:
        return "resolved-variable-limit";
    case JBeamResolveDiagnosticCode::UNUSED_PART_SELECTION:
        return "unused-part-selection";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
