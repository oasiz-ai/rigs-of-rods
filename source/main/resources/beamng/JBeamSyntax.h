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

/// @file JBeamSyntax.h
/// @brief Dependency-free relaxed-JBeam lexer, parser, and table normalizer.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

/// Source positions are one-based. byte_offset and end positions are
/// half-open; columns count source bytes so diagnostics are locale-independent.
struct JBeamSourcePosition
{
    std::uint64_t byte_offset;
    std::uint64_t line;
    std::uint64_t column;

    JBeamSourcePosition();
};

struct JBeamSourceSpan
{
    std::string source_name;
    JBeamSourcePosition begin;
    JBeamSourcePosition end;

    JBeamSourceSpan();
};

enum class JBeamDiagnosticSeverity
{
    WARNING,
    ERROR
};

enum class JBeamDiagnosticCode
{
    SOURCE_SIZE_LIMIT,
    TOKEN_LIMIT,
    NODE_LIMIT,
    DEPTH_LIMIT,
    STRING_SIZE_LIMIT,
    DIAGNOSTIC_LIMIT,
    INVALID_CHARACTER,
    UNTERMINATED_BLOCK_COMMENT,
    UNTERMINATED_STRING,
    INVALID_STRING_CHARACTER,
    INVALID_UTF8,
    INVALID_ESCAPE,
    INVALID_UNICODE_ESCAPE,
    INVALID_UNICODE_SURROGATE,
    INVALID_NUMBER,
    NON_FINITE_NUMBER,
    EXPECTED_VALUE,
    EXPECTED_OBJECT_KEY,
    EXPECTED_COLON,
    EXPECTED_OBJECT_END,
    EXPECTED_ARRAY_END,
    UNEXPECTED_COMMA,
    TRAILING_CONTENT,
    DUPLICATE_OBJECT_KEY,
    DUPLICATE_TABLE_HEADER,
    TABLE_ROW_TOO_SHORT,
    TABLE_ROW_TOO_LONG,
    TABLE_INVALID_ENTRY
};

struct JBeamDiagnostic
{
    JBeamDiagnosticCode code;
    JBeamDiagnosticSeverity severity;
    JBeamSourceSpan span;
    std::string message;

    JBeamDiagnostic();
};

struct JBeamParseLimits
{
    std::size_t max_source_bytes;
    std::size_t max_tokens;
    std::size_t max_nodes;
    std::size_t max_depth;
    std::size_t max_string_bytes;
    std::size_t max_diagnostics;

    JBeamParseLimits();
};

enum class JBeamTokenKind
{
    LEFT_BRACE,
    RIGHT_BRACE,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    COLON,
    COMMA,
    STRING,
    NUMBER,
    TRUE_VALUE,
    FALSE_VALUE,
    NULL_VALUE,
    END_OF_INPUT,
    INVALID
};

struct JBeamToken
{
    JBeamTokenKind kind;
    /// Decoded UTF-8 for STRING, the exact source spelling for NUMBER, and an
    /// empty string for punctuation and keywords.
    std::string text;
    JBeamSourceSpan span;

    JBeamToken();
};

struct JBeamLexResult
{
    std::vector<JBeamToken> tokens;
    std::vector<JBeamDiagnostic> diagnostics;

    bool IsValid() const;
};

struct JBeamValue;

/// Object fields remain in source order. They intentionally are not stored in
/// a map: duplicate keys are meaningful input and BeamNG-compatible
/// last-assignment behavior must remain observable to later resolution stages.
struct JBeamObjectField
{
    std::string key;
    JBeamSourceSpan key_span;
    std::shared_ptr<const JBeamValue> value;

    JBeamObjectField();
};

enum class JBeamValueType
{
    NULL_VALUE,
    BOOLEAN,
    NUMBER,
    STRING,
    ARRAY,
    OBJECT
};

struct JBeamValue
{
    JBeamValueType type;
    JBeamSourceSpan span;
    bool boolean_value;
    double number_value;
    /// Exact token spelling for NUMBER and decoded UTF-8 for STRING.
    std::string scalar_text;
    std::vector<JBeamValue> array_values;
    std::vector<JBeamObjectField> object_fields;

    JBeamValue();
};

struct JBeamParseResult
{
    JBeamValue root;
    std::vector<JBeamDiagnostic> diagnostics;
    bool has_root;

    JBeamParseResult();
    bool IsValid() const;
};

/// Lexes strict JSON tokens plus JBeam's line/block comments. Comments are not
/// emitted because their source ranges are not semantically observable.
JBeamLexResult LexJBeam(
    const std::string& source,
    const std::string& source_name,
    const JBeamParseLimits& limits = JBeamParseLimits());

/// Parses JSON values with JBeam's optional commas. Leading commas, doubled
/// commas, bare keys, NaN, and Infinity remain errors. Variable references and
/// strings beginning with "$=" remain inert strings: the allowlisted,
/// resource-bounded expression evaluator is a separate resolver stage.
JBeamParseResult ParseJBeam(
    const std::string& source,
    const std::string& source_name,
    const JBeamParseLimits& limits = JBeamParseLimits());

/// Returns the last field with this exact case-sensitive key, matching JBeam's
/// assignment behavior while retaining every earlier duplicate in the AST.
const JBeamObjectField* FindLastJBeamObjectField(
    const JBeamValue& object,
    const std::string& key);

enum class JBeamFieldOrigin
{
    INHERITED_DEFAULT,
    POSITIONAL_CELL,
    ROW_LOCAL_OVERRIDE
};

struct JBeamTableColumn
{
    std::string name;
    JBeamSourceSpan span;
};

struct JBeamFieldAssignment
{
    std::string name;
    JBeamFieldOrigin origin;
    JBeamSourceSpan span;
    std::shared_ptr<const JBeamValue> value;
};

struct JBeamNormalizedDataRow
{
    JBeamSourceSpan span;
    /// The original row, including a trailing row-local dictionary.
    JBeamValue raw_row;
    /// All defaults for the containing table share one immutable-after-build
    /// backing store. Only the prefix ending at inherited_assignment_count was
    /// active for this row. This keeps duplicate history without copying an
    /// ever-growing defaults vector into every subsequent row.
    std::shared_ptr<const std::vector<JBeamFieldAssignment> >
        inherited_assignment_storage;
    std::size_t inherited_assignment_count = 0;
    /// Header/cell assignments in column order, including duplicate headers.
    std::vector<JBeamFieldAssignment> positional_assignments;
    /// Trailing dictionary assignments in source order, including duplicates.
    std::vector<JBeamFieldAssignment> row_local_assignments;
};

enum class JBeamNormalizedTableEntryKind
{
    DEFAULT_MODIFIER,
    DATA_ROW,
    INVALID_ENTRY
};

struct JBeamNormalizedTableEntry
{
    JBeamNormalizedTableEntryKind kind;
    JBeamValue raw_value;
    JBeamNormalizedDataRow data_row;
};

struct JBeamNormalizedTable
{
    /// Stable path with duplicate object-field occurrences encoded as #N.
    std::string path;
    JBeamSourceSpan span;
    std::vector<JBeamTableColumn> columns;
    std::vector<JBeamNormalizedTableEntry> entries;
};

struct JBeamNormalizeResult
{
    std::vector<JBeamNormalizedTable> tables;
    std::vector<JBeamDiagnostic> diagnostics;

    bool IsValid() const;
};

/// Discovers table-shaped arrays recursively and preserves their raw entries.
/// An array is treated as a table only when its first item is a non-empty array
/// consisting entirely of strings. This deliberate syntactic rule avoids
/// section-name allowlists; semantic section validation belongs to J1 resolver
/// stages. A final object in a data row is treated as the documented row-local
/// override dictionary only when it appears after all header columns; an
/// object in a declared positional column remains an ordinary cell.
JBeamNormalizeResult NormalizeJBeamTables(const JBeamValue& root);

/// Applies JBeam precedence (defaults, positional cells, row-local overrides)
/// and returns the final exact-case field assignment without discarding any
/// source history from the normalized row.
const JBeamFieldAssignment* FindEffectiveJBeamField(
    const JBeamNormalizedDataRow& row,
    const std::string& name);

const char* JBeamDiagnosticCodeToString(JBeamDiagnosticCode code);

} // namespace BeamNG
} // namespace RoR
