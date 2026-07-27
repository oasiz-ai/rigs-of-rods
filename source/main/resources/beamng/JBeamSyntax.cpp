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

#include "JBeamSyntax.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>

namespace RoR {
namespace BeamNG {

namespace {

bool HasErrors(const std::vector<JBeamDiagnostic>& diagnostics)
{
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        if (diagnostics[i].severity == JBeamDiagnosticSeverity::ERROR)
        {
            return true;
        }
    }
    return false;
}

bool DiagnosticLess(
    const JBeamDiagnostic& left,
    const JBeamDiagnostic& right)
{
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
    if (left.severity != right.severity)
    {
        return static_cast<int>(left.severity) <
               static_cast<int>(right.severity);
    }
    if (left.code != right.code)
    {
        return static_cast<int>(left.code) < static_cast<int>(right.code);
    }
    return left.message < right.message;
}

void SortDiagnostics(std::vector<JBeamDiagnostic>& diagnostics)
{
    std::stable_sort(
        diagnostics.begin(), diagnostics.end(), DiagnosticLess);
}

JBeamDiagnostic MakeDiagnostic(
    JBeamDiagnosticCode code,
    JBeamDiagnosticSeverity severity,
    const JBeamSourceSpan& span,
    const std::string& message)
{
    JBeamDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.span = span;
    diagnostic.message = message;
    return diagnostic;
}

class Lexer
{
public:
    Lexer(
        const std::string& source,
        const std::string& source_name,
        const JBeamParseLimits& limits)
        : m_source(source)
        , m_source_name(source_name)
        , m_limits(limits)
        , m_offset(0)
        , m_line(1)
        , m_column(1)
        , m_stopped(false)
    {
    }

    JBeamLexResult Run()
    {
        if (m_source.size() > m_limits.max_source_bytes)
        {
            const JBeamSourcePosition begin = Position();
            JBeamSourcePosition end = begin;
            end.byte_offset = static_cast<std::uint64_t>(m_source.size());
            m_result.diagnostics.push_back(MakeDiagnostic(
                JBeamDiagnosticCode::SOURCE_SIZE_LIMIT,
                JBeamDiagnosticSeverity::ERROR,
                Span(begin, end),
                "JBeam source exceeds the configured byte limit"));
            return m_result;
        }

        while (!m_stopped)
        {
            SkipWhitespaceAndComments();
            if (m_stopped)
            {
                break;
            }
            if (AtEnd())
            {
                EmitSimple(JBeamTokenKind::END_OF_INPUT, Position(), Position());
                break;
            }
            LexOne();
        }
        return m_result;
    }

private:
    bool AtEnd() const
    {
        return m_offset >= m_source.size();
    }

    char Peek(std::size_t ahead = 0) const
    {
        const std::size_t index = m_offset + ahead;
        return index < m_source.size() ? m_source[index] : '\0';
    }

    JBeamSourcePosition Position() const
    {
        JBeamSourcePosition position;
        position.byte_offset = static_cast<std::uint64_t>(m_offset);
        position.line = static_cast<std::uint64_t>(m_line);
        position.column = static_cast<std::uint64_t>(m_column);
        return position;
    }

    JBeamSourceSpan Span(
        const JBeamSourcePosition& begin,
        const JBeamSourcePosition& end) const
    {
        JBeamSourceSpan span;
        span.source_name = m_source_name;
        span.begin = begin;
        span.end = end;
        return span;
    }

    char Advance()
    {
        const char value = Peek();
        if (AtEnd())
        {
            return '\0';
        }
        ++m_offset;
        if (value == '\r')
        {
            if (Peek() == '\n')
            {
                ++m_offset;
            }
            ++m_line;
            m_column = 1;
        }
        else if (value == '\n')
        {
            ++m_line;
            m_column = 1;
        }
        else
        {
            ++m_column;
        }
        return value;
    }

    void AddError(
        JBeamDiagnosticCode code,
        const JBeamSourcePosition& begin,
        const std::string& message)
    {
        if (m_result.diagnostics.size() < m_limits.max_diagnostics)
        {
            m_result.diagnostics.push_back(MakeDiagnostic(
                code,
                JBeamDiagnosticSeverity::ERROR,
                Span(begin, Position()),
                message));
        }
        else
        {
            m_result.diagnostics.push_back(MakeDiagnostic(
                JBeamDiagnosticCode::DIAGNOSTIC_LIMIT,
                JBeamDiagnosticSeverity::ERROR,
                Span(begin, Position()),
                "JBeam diagnostic count reached the configured limit"));
        }
        // A lexer error cannot be recovered without inventing token
        // boundaries. Failing at the first error is deterministic and keeps
        // hostile malformed sources within the diagnostic memory budget.
        m_stopped = true;
    }

    bool EnsureTokenBudget(const JBeamSourcePosition& begin)
    {
        if (m_result.tokens.size() < m_limits.max_tokens)
        {
            return true;
        }
        AddError(
            JBeamDiagnosticCode::TOKEN_LIMIT,
            begin,
            "JBeam token count exceeds the configured limit");
        m_stopped = true;
        return false;
    }

    void EmitSimple(
        JBeamTokenKind kind,
        const JBeamSourcePosition& begin,
        const JBeamSourcePosition& end)
    {
        if (!EnsureTokenBudget(begin))
        {
            return;
        }
        JBeamToken token;
        token.kind = kind;
        token.span = Span(begin, end);
        m_result.tokens.push_back(token);
    }

    void EmitText(
        JBeamTokenKind kind,
        const std::string& text,
        const JBeamSourcePosition& begin)
    {
        if (!EnsureTokenBudget(begin))
        {
            return;
        }
        JBeamToken token;
        token.kind = kind;
        token.text = text;
        token.span = Span(begin, Position());
        m_result.tokens.push_back(token);
    }

    void SkipWhitespaceAndComments()
    {
        for (;;)
        {
            while (!AtEnd())
            {
                const char value = Peek();
                if (value != ' ' && value != '\t' &&
                    value != '\r' && value != '\n')
                {
                    break;
                }
                Advance();
            }

            if (Peek() == '/' && Peek(1) == '/')
            {
                Advance();
                Advance();
                while (!AtEnd() && Peek() != '\r' && Peek() != '\n')
                {
                    Advance();
                }
                continue;
            }
            if (Peek() == '/' && Peek(1) == '*')
            {
                const JBeamSourcePosition begin = Position();
                Advance();
                Advance();
                bool terminated = false;
                while (!AtEnd())
                {
                    if (Peek() == '*' && Peek(1) == '/')
                    {
                        Advance();
                        Advance();
                        terminated = true;
                        break;
                    }
                    Advance();
                }
                if (!terminated)
                {
                    AddError(
                        JBeamDiagnosticCode::UNTERMINATED_BLOCK_COMMENT,
                        begin,
                        "Unterminated JBeam block comment");
                    m_stopped = true;
                    return;
                }
                continue;
            }
            return;
        }
    }

    static bool IsDigit(char value)
    {
        return value >= '0' && value <= '9';
    }

    static int HexValue(char value)
    {
        if (value >= '0' && value <= '9')
        {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f')
        {
            return 10 + value - 'a';
        }
        if (value >= 'A' && value <= 'F')
        {
            return 10 + value - 'A';
        }
        return -1;
    }

    static void AppendUtf8(std::uint32_t code_point, std::string& output)
    {
        if (code_point <= 0x7fU)
        {
            output.push_back(static_cast<char>(code_point));
        }
        else if (code_point <= 0x7ffU)
        {
            output.push_back(static_cast<char>(0xc0U | (code_point >> 6)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        }
        else if (code_point <= 0xffffU)
        {
            output.push_back(static_cast<char>(0xe0U | (code_point >> 12)));
            output.push_back(
                static_cast<char>(0x80U | ((code_point >> 6) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        }
        else
        {
            output.push_back(static_cast<char>(0xf0U | (code_point >> 18)));
            output.push_back(
                static_cast<char>(0x80U | ((code_point >> 12) & 0x3fU)));
            output.push_back(
                static_cast<char>(0x80U | ((code_point >> 6) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        }
    }

    bool ReadHexCodeUnit(std::uint32_t& code_unit)
    {
        code_unit = 0;
        for (int i = 0; i < 4; ++i)
        {
            const int digit = HexValue(Peek());
            if (digit < 0)
            {
                return false;
            }
            code_unit = (code_unit << 4) | static_cast<std::uint32_t>(digit);
            Advance();
        }
        return true;
    }

    bool CheckStringBudget(
        const std::string& output,
        const JBeamSourcePosition& begin)
    {
        if (output.size() <= m_limits.max_string_bytes)
        {
            return true;
        }
        AddError(
            JBeamDiagnosticCode::STRING_SIZE_LIMIT,
            begin,
            "Decoded JBeam string exceeds the configured byte limit");
        return false;
    }

    bool AppendRawUtf8(
        unsigned char first,
        const JBeamSourcePosition& character_begin,
        std::string& output)
    {
        std::size_t continuation_count = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2U && first <= 0xdfU)
        {
            continuation_count = 1;
            code_point = first & 0x1fU;
            minimum = 0x80U;
        }
        else if (first >= 0xe0U && first <= 0xefU)
        {
            continuation_count = 2;
            code_point = first & 0x0fU;
            minimum = 0x800U;
        }
        else if (first >= 0xf0U && first <= 0xf4U)
        {
            continuation_count = 3;
            code_point = first & 0x07U;
            minimum = 0x10000U;
        }
        else
        {
            AddError(
                JBeamDiagnosticCode::INVALID_UTF8,
                character_begin,
                "Invalid UTF-8 leading byte in JBeam string");
            return false;
        }

        for (std::size_t i = 0; i < continuation_count; ++i)
        {
            const unsigned char continuation =
                static_cast<unsigned char>(Peek(i));
            if ((continuation & 0xc0U) != 0x80U)
            {
                AddError(
                    JBeamDiagnosticCode::INVALID_UTF8,
                    character_begin,
                    "Invalid UTF-8 continuation byte in JBeam string");
                return false;
            }
            code_point =
                (code_point << 6) |
                static_cast<std::uint32_t>(continuation & 0x3fU);
        }
        if (code_point < minimum || code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU))
        {
            AddError(
                JBeamDiagnosticCode::INVALID_UTF8,
                character_begin,
                "Non-canonical Unicode scalar in JBeam UTF-8 string");
            return false;
        }

        output.push_back(static_cast<char>(first));
        for (std::size_t i = 0; i < continuation_count; ++i)
        {
            output.push_back(Advance());
        }
        return true;
    }

    void LexString()
    {
        const JBeamSourcePosition begin = Position();
        Advance();
        std::string output;
        bool terminated = false;
        while (!AtEnd())
        {
            const JBeamSourcePosition character_begin = Position();
            const char value = Advance();
            if (value == '"')
            {
                terminated = true;
                break;
            }
            const unsigned char unsigned_value =
                static_cast<unsigned char>(value);
            if (unsigned_value < 0x20U)
            {
                AddError(
                    JBeamDiagnosticCode::INVALID_STRING_CHARACTER,
                    begin,
                    "Unescaped control character in JBeam string");
                return;
            }
            if (value != '\\')
            {
                if (unsigned_value < 0x80U)
                {
                    output.push_back(value);
                }
                else if (!AppendRawUtf8(
                             unsigned_value, character_begin, output))
                {
                    return;
                }
                if (!CheckStringBudget(output, begin))
                {
                    return;
                }
                continue;
            }

            if (AtEnd())
            {
                break;
            }
            const char escaped = Advance();
            switch (escaped)
            {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
            {
                std::uint32_t first = 0;
                if (!ReadHexCodeUnit(first))
                {
                    AddError(
                        JBeamDiagnosticCode::INVALID_UNICODE_ESCAPE,
                        begin,
                        "Invalid four-digit Unicode escape in JBeam string");
                    return;
                }
                std::uint32_t code_point = first;
                if (first >= 0xd800U && first <= 0xdbffU)
                {
                    if (Peek() != '\\' || Peek(1) != 'u')
                    {
                        AddError(
                            JBeamDiagnosticCode::INVALID_UNICODE_SURROGATE,
                            begin,
                            "High Unicode surrogate has no low surrogate");
                        return;
                    }
                    Advance();
                    Advance();
                    std::uint32_t second = 0;
                    if (!ReadHexCodeUnit(second) ||
                        second < 0xdc00U || second > 0xdfffU)
                    {
                        AddError(
                            JBeamDiagnosticCode::INVALID_UNICODE_SURROGATE,
                            begin,
                            "Invalid low Unicode surrogate");
                        return;
                    }
                    code_point =
                        0x10000U + ((first - 0xd800U) << 10) +
                        (second - 0xdc00U);
                }
                else if (first >= 0xdc00U && first <= 0xdfffU)
                {
                    AddError(
                        JBeamDiagnosticCode::INVALID_UNICODE_SURROGATE,
                        begin,
                        "Unexpected low Unicode surrogate");
                    return;
                }
                AppendUtf8(code_point, output);
                break;
            }
            default:
                AddError(
                    JBeamDiagnosticCode::INVALID_ESCAPE,
                    begin,
                    "Invalid escape in JBeam string");
                return;
            }
            if (!CheckStringBudget(output, begin))
            {
                return;
            }
        }
        if (!terminated)
        {
            AddError(
                JBeamDiagnosticCode::UNTERMINATED_STRING,
                begin,
                "Unterminated JBeam string");
            return;
        }
        EmitText(JBeamTokenKind::STRING, output, begin);
    }

    void LexNumber()
    {
        const JBeamSourcePosition begin = Position();
        const std::size_t begin_offset = m_offset;
        if (Peek() == '-')
        {
            Advance();
        }
        if (Peek() == '0')
        {
            Advance();
            if (IsDigit(Peek()))
            {
                while (IsDigit(Peek()))
                {
                    Advance();
                }
                AddError(
                    JBeamDiagnosticCode::INVALID_NUMBER,
                    begin,
                    "Leading zero in JBeam number");
                return;
            }
        }
        else if (Peek() >= '1' && Peek() <= '9')
        {
            while (IsDigit(Peek()))
            {
                Advance();
            }
        }
        else
        {
            if (!AtEnd())
            {
                Advance();
            }
            AddError(
                JBeamDiagnosticCode::INVALID_NUMBER,
                begin,
                "JBeam number requires an integer component");
            return;
        }

        if (Peek() == '.')
        {
            Advance();
            if (!IsDigit(Peek()))
            {
                AddError(
                    JBeamDiagnosticCode::INVALID_NUMBER,
                    begin,
                    "JBeam number requires digits after the decimal point");
                return;
            }
            while (IsDigit(Peek()))
            {
                Advance();
            }
        }

        if (Peek() == 'e' || Peek() == 'E')
        {
            Advance();
            if (Peek() == '+' || Peek() == '-')
            {
                Advance();
            }
            if (!IsDigit(Peek()))
            {
                AddError(
                    JBeamDiagnosticCode::INVALID_NUMBER,
                    begin,
                    "JBeam number requires exponent digits");
                return;
            }
            while (IsDigit(Peek()))
            {
                Advance();
            }
        }
        EmitText(
            JBeamTokenKind::NUMBER,
            m_source.substr(begin_offset, m_offset - begin_offset),
            begin);
    }

    bool MatchKeyword(const char* keyword)
    {
        std::size_t length = 0;
        while (keyword[length] != '\0')
        {
            if (Peek(length) != keyword[length])
            {
                return false;
            }
            ++length;
        }
        for (std::size_t i = 0; i < length; ++i)
        {
            Advance();
        }
        return true;
    }

    void LexOne()
    {
        const JBeamSourcePosition begin = Position();
        const char value = Peek();
        switch (value)
        {
        case '{':
            Advance();
            EmitSimple(JBeamTokenKind::LEFT_BRACE, begin, Position());
            return;
        case '}':
            Advance();
            EmitSimple(JBeamTokenKind::RIGHT_BRACE, begin, Position());
            return;
        case '[':
            Advance();
            EmitSimple(JBeamTokenKind::LEFT_BRACKET, begin, Position());
            return;
        case ']':
            Advance();
            EmitSimple(JBeamTokenKind::RIGHT_BRACKET, begin, Position());
            return;
        case ':':
            Advance();
            EmitSimple(JBeamTokenKind::COLON, begin, Position());
            return;
        case ',':
            Advance();
            EmitSimple(JBeamTokenKind::COMMA, begin, Position());
            return;
        case '"':
            LexString();
            return;
        case '-':
            LexNumber();
            return;
        default:
            break;
        }

        if (IsDigit(value))
        {
            LexNumber();
            return;
        }
        if (MatchKeyword("true"))
        {
            EmitSimple(JBeamTokenKind::TRUE_VALUE, begin, Position());
            return;
        }
        if (MatchKeyword("false"))
        {
            EmitSimple(JBeamTokenKind::FALSE_VALUE, begin, Position());
            return;
        }
        if (MatchKeyword("null"))
        {
            EmitSimple(JBeamTokenKind::NULL_VALUE, begin, Position());
            return;
        }

        Advance();
        AddError(
            JBeamDiagnosticCode::INVALID_CHARACTER,
            begin,
            "Invalid character in JBeam source");
    }

    const std::string& m_source;
    const std::string& m_source_name;
    const JBeamParseLimits& m_limits;
    std::size_t m_offset;
    std::size_t m_line;
    std::size_t m_column;
    bool m_stopped;
    JBeamLexResult m_result;
};

class Parser
{
public:
    Parser(
        const std::vector<JBeamToken>& tokens,
        const std::vector<JBeamDiagnostic>& lex_diagnostics,
        const JBeamParseLimits& limits)
        : m_tokens(tokens)
        , m_limits(limits)
        , m_index(0)
        , m_node_count(0)
    {
        m_result.diagnostics = lex_diagnostics;
    }

    JBeamParseResult Run()
    {
        if (HasErrors(m_result.diagnostics) || m_tokens.empty())
        {
            return m_result;
        }
        JBeamValue value;
        if (!ParseValue(0, value))
        {
            return m_result;
        }
        m_result.root = value;
        m_result.has_root = true;
        if (Current().kind != JBeamTokenKind::END_OF_INPUT)
        {
            AddError(
                JBeamDiagnosticCode::TRAILING_CONTENT,
                Current().span,
                "Unexpected content after the root JBeam value");
        }
        return m_result;
    }

private:
    const JBeamToken& Current() const
    {
        if (m_index < m_tokens.size())
        {
            return m_tokens[m_index];
        }
        return m_tokens.back();
    }

    const JBeamToken& Previous() const
    {
        return m_tokens[m_index - 1];
    }

    bool Match(JBeamTokenKind kind)
    {
        if (Current().kind != kind)
        {
            return false;
        }
        ++m_index;
        return true;
    }

    void AddError(
        JBeamDiagnosticCode code,
        const JBeamSourceSpan& span,
        const std::string& message)
    {
        if (m_result.diagnostics.size() < m_limits.max_diagnostics)
        {
            m_result.diagnostics.push_back(MakeDiagnostic(
                code, JBeamDiagnosticSeverity::ERROR, span, message));
        }
        else
        {
            m_result.diagnostics.push_back(MakeDiagnostic(
                JBeamDiagnosticCode::DIAGNOSTIC_LIMIT,
                JBeamDiagnosticSeverity::ERROR,
                span,
                "JBeam diagnostic count reached the configured limit"));
        }
    }

    bool AddWarning(
        JBeamDiagnosticCode code,
        const JBeamSourceSpan& span,
        const std::string& message)
    {
        if (m_result.diagnostics.size() >= m_limits.max_diagnostics)
        {
            m_result.diagnostics.push_back(MakeDiagnostic(
                JBeamDiagnosticCode::DIAGNOSTIC_LIMIT,
                JBeamDiagnosticSeverity::ERROR,
                span,
                "JBeam diagnostic count reached the configured limit"));
            return false;
        }
        m_result.diagnostics.push_back(MakeDiagnostic(
            code, JBeamDiagnosticSeverity::WARNING, span, message));
        return true;
    }

    bool BeginNode(std::size_t depth, const JBeamSourceSpan& span)
    {
        if (depth > m_limits.max_depth)
        {
            AddError(
                JBeamDiagnosticCode::DEPTH_LIMIT,
                span,
                "JBeam nesting exceeds the configured depth limit");
            return false;
        }
        if (m_node_count >= m_limits.max_nodes)
        {
            AddError(
                JBeamDiagnosticCode::NODE_LIMIT,
                span,
                "JBeam value count exceeds the configured node limit");
            return false;
        }
        ++m_node_count;
        return true;
    }

    static bool ParseFiniteDouble(const std::string& text, double& value)
    {
        std::istringstream input(text);
        input.imbue(std::locale::classic());
        input >> std::noskipws >> value;
        return input.eof() && !input.fail() && std::isfinite(value);
    }

    bool ParseValue(std::size_t depth, JBeamValue& output)
    {
        const JBeamToken token = Current();
        if (!BeginNode(depth, token.span))
        {
            return false;
        }
        switch (token.kind)
        {
        case JBeamTokenKind::NULL_VALUE:
            ++m_index;
            output.type = JBeamValueType::NULL_VALUE;
            output.span = token.span;
            return true;
        case JBeamTokenKind::TRUE_VALUE:
        case JBeamTokenKind::FALSE_VALUE:
            ++m_index;
            output.type = JBeamValueType::BOOLEAN;
            output.boolean_value =
                token.kind == JBeamTokenKind::TRUE_VALUE;
            output.span = token.span;
            return true;
        case JBeamTokenKind::STRING:
            ++m_index;
            output.type = JBeamValueType::STRING;
            output.scalar_text = token.text;
            output.span = token.span;
            return true;
        case JBeamTokenKind::NUMBER:
            ++m_index;
            output.type = JBeamValueType::NUMBER;
            output.scalar_text = token.text;
            output.span = token.span;
            if (!ParseFiniteDouble(token.text, output.number_value))
            {
                AddError(
                    JBeamDiagnosticCode::NON_FINITE_NUMBER,
                    token.span,
                    "JBeam number does not have a finite double value");
                return false;
            }
            return true;
        case JBeamTokenKind::LEFT_BRACE:
            return ParseObject(depth, output);
        case JBeamTokenKind::LEFT_BRACKET:
            return ParseArray(depth, output);
        default:
            AddError(
                JBeamDiagnosticCode::EXPECTED_VALUE,
                token.span,
                "Expected a JBeam value");
            return false;
        }
    }

    bool ParseObject(std::size_t depth, JBeamValue& output)
    {
        Match(JBeamTokenKind::LEFT_BRACE);
        const JBeamToken opening = Previous();
        output.type = JBeamValueType::OBJECT;
        output.span = opening.span;
        bool after_value = false;
        bool consumed_comma = false;
        std::map<std::string, JBeamSourceSpan> first_keys;

        while (Current().kind != JBeamTokenKind::RIGHT_BRACE)
        {
            if (Current().kind == JBeamTokenKind::END_OF_INPUT)
            {
                AddError(
                    JBeamDiagnosticCode::EXPECTED_OBJECT_END,
                    Current().span,
                    "Expected '}' before end of JBeam source");
                return false;
            }
            if (Match(JBeamTokenKind::COMMA))
            {
                if (!after_value || consumed_comma)
                {
                    AddError(
                        JBeamDiagnosticCode::UNEXPECTED_COMMA,
                        Previous().span,
                        "Unexpected comma in JBeam object");
                    return false;
                }
                consumed_comma = true;
                continue;
            }
            if (Current().kind != JBeamTokenKind::STRING)
            {
                AddError(
                    JBeamDiagnosticCode::EXPECTED_OBJECT_KEY,
                    Current().span,
                    "Expected a quoted JBeam object key");
                return false;
            }

            const JBeamToken key = Current();
            ++m_index;
            if (!Match(JBeamTokenKind::COLON))
            {
                AddError(
                    JBeamDiagnosticCode::EXPECTED_COLON,
                    Current().span,
                    "Expected ':' after JBeam object key");
                return false;
            }
            JBeamValue value;
            if (!ParseValue(depth + 1, value))
            {
                return false;
            }
            JBeamObjectField field;
            field.key = key.text;
            field.key_span = key.span;
            field.value =
                std::shared_ptr<const JBeamValue>(new JBeamValue(value));
            output.object_fields.push_back(field);

            const std::map<std::string, JBeamSourceSpan>::const_iterator found =
                first_keys.find(key.text);
            if (found == first_keys.end())
            {
                first_keys.insert(std::make_pair(key.text, key.span));
            }
            else
            {
                if (!AddWarning(
                    JBeamDiagnosticCode::DUPLICATE_OBJECT_KEY,
                    key.span,
                    "Duplicate case-sensitive JBeam object key; the last "
                    "assignment is effective"))
                {
                    return false;
                }
            }
            after_value = true;
            consumed_comma = false;
        }
        Match(JBeamTokenKind::RIGHT_BRACE);
        output.span.end = Previous().span.end;
        return true;
    }

    bool ParseArray(std::size_t depth, JBeamValue& output)
    {
        Match(JBeamTokenKind::LEFT_BRACKET);
        const JBeamToken opening = Previous();
        output.type = JBeamValueType::ARRAY;
        output.span = opening.span;
        bool after_value = false;
        bool consumed_comma = false;

        while (Current().kind != JBeamTokenKind::RIGHT_BRACKET)
        {
            if (Current().kind == JBeamTokenKind::END_OF_INPUT)
            {
                AddError(
                    JBeamDiagnosticCode::EXPECTED_ARRAY_END,
                    Current().span,
                    "Expected ']' before end of JBeam source");
                return false;
            }
            if (Match(JBeamTokenKind::COMMA))
            {
                if (!after_value || consumed_comma)
                {
                    AddError(
                        JBeamDiagnosticCode::UNEXPECTED_COMMA,
                        Previous().span,
                        "Unexpected comma in JBeam array");
                    return false;
                }
                consumed_comma = true;
                continue;
            }
            JBeamValue value;
            if (!ParseValue(depth + 1, value))
            {
                return false;
            }
            output.array_values.push_back(value);
            after_value = true;
            consumed_comma = false;
        }
        Match(JBeamTokenKind::RIGHT_BRACKET);
        output.span.end = Previous().span.end;
        return true;
    }

    const std::vector<JBeamToken>& m_tokens;
    const JBeamParseLimits& m_limits;
    std::size_t m_index;
    std::size_t m_node_count;
    JBeamParseResult m_result;
};

std::string EscapePathKey(const std::string& key)
{
    std::ostringstream output;
    for (std::size_t i = 0; i < key.size(); ++i)
    {
        const unsigned char value = static_cast<unsigned char>(key[i]);
        if ((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '_' || value == '-' || value == '.')
        {
            output << static_cast<char>(value);
        }
        else
        {
            output << '%' << std::uppercase << std::hex << std::setw(2)
                   << std::setfill('0') << static_cast<unsigned int>(value)
                   << std::nouppercase << std::dec;
        }
    }
    return output.str();
}

bool IsTableHeader(const JBeamValue& value)
{
    if (value.type != JBeamValueType::ARRAY ||
        value.array_values.empty())
    {
        return false;
    }
    for (std::size_t i = 0; i < value.array_values.size(); ++i)
    {
        if (value.array_values[i].type != JBeamValueType::STRING)
        {
            return false;
        }
    }
    return true;
}

std::shared_ptr<const JBeamValue> CopyValue(const JBeamValue& value)
{
    return std::shared_ptr<const JBeamValue>(new JBeamValue(value));
}

void AppendObjectAssignments(
    const JBeamValue& object,
    JBeamFieldOrigin origin,
    std::vector<JBeamFieldAssignment>& output)
{
    for (std::size_t i = 0; i < object.object_fields.size(); ++i)
    {
        const JBeamObjectField& field = object.object_fields[i];
        JBeamFieldAssignment assignment;
        assignment.name = field.key;
        assignment.origin = origin;
        assignment.span = field.value->span;
        assignment.value = field.value;
        output.push_back(assignment);
    }
}

void NormalizeTable(
    const JBeamValue& value,
    const std::string& path,
    JBeamNormalizeResult& result)
{
    JBeamNormalizedTable table;
    table.path = path;
    table.span = value.span;
    const JBeamValue& header = value.array_values[0];
    std::map<std::string, std::size_t> header_names;
    for (std::size_t i = 0; i < header.array_values.size(); ++i)
    {
        JBeamTableColumn column;
        column.name = header.array_values[i].scalar_text;
        column.span = header.array_values[i].span;
        table.columns.push_back(column);
        if (header_names.find(column.name) != header_names.end())
        {
            result.diagnostics.push_back(MakeDiagnostic(
                JBeamDiagnosticCode::DUPLICATE_TABLE_HEADER,
                JBeamDiagnosticSeverity::WARNING,
                column.span,
                "Duplicate case-sensitive JBeam table header is preserved"));
        }
        else
        {
            header_names.insert(std::make_pair(column.name, i));
        }
    }

    std::shared_ptr<std::vector<JBeamFieldAssignment> > active_defaults(
        new std::vector<JBeamFieldAssignment>());
    for (std::size_t entry_index = 1;
         entry_index < value.array_values.size();
         ++entry_index)
    {
        const JBeamValue& entry_value = value.array_values[entry_index];
        JBeamNormalizedTableEntry entry;
        entry.raw_value = entry_value;
        if (entry_value.type == JBeamValueType::OBJECT)
        {
            entry.kind = JBeamNormalizedTableEntryKind::DEFAULT_MODIFIER;
            AppendObjectAssignments(
                entry_value,
                JBeamFieldOrigin::INHERITED_DEFAULT,
                *active_defaults);
        }
        else if (entry_value.type == JBeamValueType::ARRAY)
        {
            entry.kind = JBeamNormalizedTableEntryKind::DATA_ROW;
            entry.data_row.span = entry_value.span;
            entry.data_row.raw_row = entry_value;
            entry.data_row.inherited_assignment_storage = active_defaults;
            entry.data_row.inherited_assignment_count =
                active_defaults->size();

            std::size_t positional_count = entry_value.array_values.size();
            if (positional_count > table.columns.size() &&
                entry_value.array_values[positional_count - 1].type ==
                    JBeamValueType::OBJECT)
            {
                const JBeamValue& overrides =
                    entry_value.array_values[positional_count - 1];
                AppendObjectAssignments(
                    overrides,
                    JBeamFieldOrigin::ROW_LOCAL_OVERRIDE,
                    entry.data_row.row_local_assignments);
                --positional_count;
            }

            const std::size_t mapped_count =
                std::min(positional_count, table.columns.size());
            for (std::size_t column_index = 0;
                 column_index < mapped_count;
                 ++column_index)
            {
                JBeamFieldAssignment assignment;
                assignment.name = table.columns[column_index].name;
                assignment.origin = JBeamFieldOrigin::POSITIONAL_CELL;
                assignment.span =
                    entry_value.array_values[column_index].span;
                assignment.value =
                    CopyValue(entry_value.array_values[column_index]);
                entry.data_row.positional_assignments.push_back(assignment);
            }
            if (positional_count < table.columns.size())
            {
                result.diagnostics.push_back(MakeDiagnostic(
                    JBeamDiagnosticCode::TABLE_ROW_TOO_SHORT,
                    JBeamDiagnosticSeverity::WARNING,
                    entry_value.span,
                    "JBeam table row has fewer positional cells than its "
                    "header; the raw row is preserved"));
            }
            else if (positional_count > table.columns.size())
            {
                result.diagnostics.push_back(MakeDiagnostic(
                    JBeamDiagnosticCode::TABLE_ROW_TOO_LONG,
                    JBeamDiagnosticSeverity::WARNING,
                    entry_value.span,
                    "JBeam table row has more positional cells than its "
                    "header; extra raw cells are preserved"));
            }
        }
        else
        {
            entry.kind = JBeamNormalizedTableEntryKind::INVALID_ENTRY;
            result.diagnostics.push_back(MakeDiagnostic(
                JBeamDiagnosticCode::TABLE_INVALID_ENTRY,
                JBeamDiagnosticSeverity::WARNING,
                entry_value.span,
                "JBeam table entry is neither a default dictionary nor a "
                "data-row array; the raw value is preserved"));
        }
        table.entries.push_back(entry);
    }
    result.tables.push_back(table);
}

void DiscoverTables(
    const JBeamValue& value,
    const std::string& path,
    JBeamNormalizeResult& result)
{
    if (value.type == JBeamValueType::ARRAY)
    {
        if (!value.array_values.empty() &&
            IsTableHeader(value.array_values[0]))
        {
            NormalizeTable(value, path, result);
        }
        for (std::size_t i = 0; i < value.array_values.size(); ++i)
        {
            std::ostringstream child_path;
            child_path << path << "/[" << i << "]";
            DiscoverTables(value.array_values[i], child_path.str(), result);
        }
        return;
    }
    if (value.type != JBeamValueType::OBJECT)
    {
        return;
    }

    std::map<std::string, std::size_t> occurrences;
    for (std::size_t i = 0; i < value.object_fields.size(); ++i)
    {
        const JBeamObjectField& field = value.object_fields[i];
        const std::size_t occurrence = occurrences[field.key]++;
        std::ostringstream child_path;
        child_path << path << "/" << EscapePathKey(field.key)
                   << "#" << occurrence;
        DiscoverTables(*field.value, child_path.str(), result);
    }
}

const JBeamFieldAssignment* FindLastAssignment(
    const std::vector<JBeamFieldAssignment>& assignments,
    const std::string& name)
{
    for (std::size_t i = assignments.size(); i > 0; --i)
    {
        if (assignments[i - 1].name == name)
        {
            return &assignments[i - 1];
        }
    }
    return NULL;
}

const JBeamFieldAssignment* FindLastAssignmentPrefix(
    const std::shared_ptr<const std::vector<JBeamFieldAssignment> >&
        assignments,
    std::size_t count,
    const std::string& name)
{
    if (!assignments)
    {
        return NULL;
    }
    const std::size_t bounded_count = std::min(count, assignments->size());
    for (std::size_t i = bounded_count; i > 0; --i)
    {
        if ((*assignments)[i - 1].name == name)
        {
            return &(*assignments)[i - 1];
        }
    }
    return NULL;
}

} // namespace

JBeamSourcePosition::JBeamSourcePosition()
    : byte_offset(0)
    , line(1)
    , column(1)
{
}

JBeamSourceSpan::JBeamSourceSpan()
{
}

JBeamDiagnostic::JBeamDiagnostic()
    : code(JBeamDiagnosticCode::INVALID_CHARACTER)
    , severity(JBeamDiagnosticSeverity::ERROR)
{
}

JBeamParseLimits::JBeamParseLimits()
    : max_source_bytes(16U * 1024U * 1024U)
    , max_tokens(1000000U)
    , max_nodes(500000U)
    , max_depth(128U)
    , max_string_bytes(4U * 1024U * 1024U)
    , max_diagnostics(4096U)
{
}

JBeamToken::JBeamToken()
    : kind(JBeamTokenKind::INVALID)
{
}

bool JBeamLexResult::IsValid() const
{
    return !HasErrors(diagnostics);
}

JBeamObjectField::JBeamObjectField()
{
}

JBeamValue::JBeamValue()
    : type(JBeamValueType::NULL_VALUE)
    , boolean_value(false)
    , number_value(0.0)
{
}

JBeamParseResult::JBeamParseResult()
    : has_root(false)
{
}

bool JBeamParseResult::IsValid() const
{
    return has_root && !HasErrors(diagnostics);
}

JBeamLexResult LexJBeam(
    const std::string& source,
    const std::string& source_name,
    const JBeamParseLimits& limits)
{
    return Lexer(source, source_name, limits).Run();
}

JBeamParseResult ParseJBeam(
    const std::string& source,
    const std::string& source_name,
    const JBeamParseLimits& limits)
{
    const JBeamLexResult lexed = LexJBeam(source, source_name, limits);
    JBeamParseResult result =
        Parser(lexed.tokens, lexed.diagnostics, limits).Run();
    SortDiagnostics(result.diagnostics);
    return result;
}

const JBeamObjectField* FindLastJBeamObjectField(
    const JBeamValue& object,
    const std::string& key)
{
    if (object.type != JBeamValueType::OBJECT)
    {
        return NULL;
    }
    for (std::size_t i = object.object_fields.size(); i > 0; --i)
    {
        if (object.object_fields[i - 1].key == key)
        {
            return &object.object_fields[i - 1];
        }
    }
    return NULL;
}

bool JBeamNormalizeResult::IsValid() const
{
    return !HasErrors(diagnostics);
}

JBeamNormalizeResult NormalizeJBeamTables(const JBeamValue& root)
{
    JBeamNormalizeResult result;
    DiscoverTables(root, "$", result);
    SortDiagnostics(result.diagnostics);
    return result;
}

const JBeamFieldAssignment* FindEffectiveJBeamField(
    const JBeamNormalizedDataRow& row,
    const std::string& name)
{
    const JBeamFieldAssignment* assignment =
        FindLastAssignment(row.row_local_assignments, name);
    if (assignment != NULL)
    {
        return assignment;
    }
    assignment = FindLastAssignment(row.positional_assignments, name);
    if (assignment != NULL)
    {
        return assignment;
    }
    return FindLastAssignmentPrefix(
        row.inherited_assignment_storage,
        row.inherited_assignment_count,
        name);
}

const char* JBeamDiagnosticCodeToString(JBeamDiagnosticCode code)
{
    switch (code)
    {
    case JBeamDiagnosticCode::SOURCE_SIZE_LIMIT:
        return "source-size-limit";
    case JBeamDiagnosticCode::TOKEN_LIMIT:
        return "token-limit";
    case JBeamDiagnosticCode::NODE_LIMIT:
        return "node-limit";
    case JBeamDiagnosticCode::DEPTH_LIMIT:
        return "depth-limit";
    case JBeamDiagnosticCode::STRING_SIZE_LIMIT:
        return "string-size-limit";
    case JBeamDiagnosticCode::DIAGNOSTIC_LIMIT:
        return "diagnostic-limit";
    case JBeamDiagnosticCode::INVALID_CHARACTER:
        return "invalid-character";
    case JBeamDiagnosticCode::UNTERMINATED_BLOCK_COMMENT:
        return "unterminated-block-comment";
    case JBeamDiagnosticCode::UNTERMINATED_STRING:
        return "unterminated-string";
    case JBeamDiagnosticCode::INVALID_STRING_CHARACTER:
        return "invalid-string-character";
    case JBeamDiagnosticCode::INVALID_UTF8:
        return "invalid-utf8";
    case JBeamDiagnosticCode::INVALID_ESCAPE:
        return "invalid-escape";
    case JBeamDiagnosticCode::INVALID_UNICODE_ESCAPE:
        return "invalid-unicode-escape";
    case JBeamDiagnosticCode::INVALID_UNICODE_SURROGATE:
        return "invalid-unicode-surrogate";
    case JBeamDiagnosticCode::INVALID_NUMBER:
        return "invalid-number";
    case JBeamDiagnosticCode::NON_FINITE_NUMBER:
        return "non-finite-number";
    case JBeamDiagnosticCode::EXPECTED_VALUE:
        return "expected-value";
    case JBeamDiagnosticCode::EXPECTED_OBJECT_KEY:
        return "expected-object-key";
    case JBeamDiagnosticCode::EXPECTED_COLON:
        return "expected-colon";
    case JBeamDiagnosticCode::EXPECTED_OBJECT_END:
        return "expected-object-end";
    case JBeamDiagnosticCode::EXPECTED_ARRAY_END:
        return "expected-array-end";
    case JBeamDiagnosticCode::UNEXPECTED_COMMA:
        return "unexpected-comma";
    case JBeamDiagnosticCode::TRAILING_CONTENT:
        return "trailing-content";
    case JBeamDiagnosticCode::DUPLICATE_OBJECT_KEY:
        return "duplicate-object-key";
    case JBeamDiagnosticCode::DUPLICATE_TABLE_HEADER:
        return "duplicate-table-header";
    case JBeamDiagnosticCode::TABLE_ROW_TOO_SHORT:
        return "table-row-too-short";
    case JBeamDiagnosticCode::TABLE_ROW_TOO_LONG:
        return "table-row-too-long";
    case JBeamDiagnosticCode::TABLE_INVALID_ENTRY:
        return "table-invalid-entry";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
