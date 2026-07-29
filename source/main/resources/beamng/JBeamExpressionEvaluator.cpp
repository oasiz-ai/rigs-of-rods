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

#include "JBeamExpressionEvaluator.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>

namespace RoR {
namespace BeamNG {
namespace {

static_assert(
    sizeof(double) == sizeof(std::uint64_t),
    "The JBeam evaluator requires 64-bit doubles");
static_assert(
    std::numeric_limits<double>::is_iec559,
    "The JBeam evaluator requires IEEE-754 doubles");

bool IsAsciiAlpha(unsigned char value)
{
    return (value >= static_cast<unsigned char>('a') &&
            value <= static_cast<unsigned char>('z')) ||
        (value >= static_cast<unsigned char>('A') &&
         value <= static_cast<unsigned char>('Z'));
}

bool IsAsciiDigit(unsigned char value)
{
    return value >= static_cast<unsigned char>('0') &&
        value <= static_cast<unsigned char>('9');
}

bool IsVariableStart(unsigned char value)
{
    return IsAsciiAlpha(value) || value == static_cast<unsigned char>('_');
}

bool IsVariableContinue(unsigned char value)
{
    return IsVariableStart(value) || IsAsciiDigit(value);
}

bool IsWhitespace(unsigned char value)
{
    return value == static_cast<unsigned char>(' ') ||
        value == static_cast<unsigned char>('\t') ||
        value == static_cast<unsigned char>('\r') ||
        value == static_cast<unsigned char>('\n') ||
        value == static_cast<unsigned char>('\f') ||
        value == static_cast<unsigned char>('\v');
}

std::uint64_t DoubleBits(double value)
{
    // A volatile round trip prevents -ffinite-math-only from replacing the
    // subsequent raw-bit classification with an assumption that all inputs
    // and arithmetic results are finite.
    volatile double observed = value;
    const double snapshot = observed;
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &snapshot, sizeof(bits));
    return bits;
}

bool IsFiniteNumber(double value)
{
    return (DoubleBits(value) & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

double NormalizeNumber(double value)
{
    // Positive and negative zero are indistinguishable in JBeam expressions.
    // Canonicalizing here makes equivalent source spellings serialize alike.
    return (DoubleBits(value) & UINT64_C(0x7fffffffffffffff)) == 0U
        ? 0.0
        : value;
}

bool IsValidUtf8(const std::string& value)
{
    std::size_t index = 0U;
    while (index < value.size())
    {
        const unsigned char first =
            static_cast<unsigned char>(value[index]);
        if (first <= 0x7fU)
        {
            ++index;
            continue;
        }

        std::size_t count = 0U;
        std::uint32_t codepoint = 0U;
        std::uint32_t minimum = 0U;
        if (first >= 0xc2U && first <= 0xdfU)
        {
            count = 2U;
            codepoint = first & 0x1fU;
            minimum = 0x80U;
        }
        else if (first >= 0xe0U && first <= 0xefU)
        {
            count = 3U;
            codepoint = first & 0x0fU;
            minimum = 0x800U;
        }
        else if (first >= 0xf0U && first <= 0xf4U)
        {
            count = 4U;
            codepoint = first & 0x07U;
            minimum = 0x10000U;
        }
        else
        {
            return false;
        }

        if (count > value.size() - index)
        {
            return false;
        }
        for (std::size_t offset = 1U; offset < count; ++offset)
        {
            const unsigned char continuation =
                static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xc0U) != 0x80U)
            {
                return false;
            }
            codepoint =
                (codepoint << 6U) |
                static_cast<std::uint32_t>(continuation & 0x3fU);
        }
        if (codepoint < minimum ||
            codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU))
        {
            return false;
        }
        index += count;
    }
    return true;
}

bool IsValidVariableName(const std::string& name)
{
    if (name.size() < 2U ||
        name[0] != '$' ||
        !IsVariableStart(static_cast<unsigned char>(name[1])))
    {
        return false;
    }
    bool has_component_path = false;
    for (std::size_t index = 2U; index < name.size(); ++index)
    {
        if (name[index] == '.')
        {
            has_component_path = true;
            if (index + 1U >= name.size() ||
                !IsVariableStart(
                    static_cast<unsigned char>(name[index + 1U])))
            {
                return false;
            }
            continue;
        }
        if (!IsVariableContinue(static_cast<unsigned char>(name[index])))
        {
            return false;
        }
    }
    return !has_component_path ||
        (name.size() > 12U &&
         name.compare(0U, 12U, "$components.") == 0);
}

JBeamExpressionDiagnostic MakeDiagnostic(
    JBeamExpressionDiagnosticCode code,
    std::size_t byte_offset,
    const char* message)
{
    JBeamExpressionDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.byte_offset = byte_offset;
    diagnostic.message = message;
    return diagnostic;
}

class Runtime
{
public:
    Runtime(
        JBeamExpressionResult& result,
        const JBeamExpressionLimits& limits)
        : m_result(result)
        , m_limits(limits)
        , m_failed(false)
    {
    }

    bool Charge(std::size_t units, std::size_t byte_offset)
    {
        if (m_failed)
        {
            return false;
        }
        if (units > m_limits.max_work_units - m_result.work_units)
        {
            Fail(
                JBeamExpressionDiagnosticCode::WORK_LIMIT,
                byte_offset,
                "JBeam expression work exceeds the configured limit");
            return false;
        }
        m_result.work_units += units;
        return true;
    }

    bool Fail(
        JBeamExpressionDiagnosticCode code,
        std::size_t byte_offset,
        const char* message)
    {
        if (!m_failed)
        {
            m_result.diagnostics.push_back(
                MakeDiagnostic(code, byte_offset, message));
            m_failed = true;
        }
        return false;
    }

    bool Failed() const
    {
        return m_failed;
    }

private:
    JBeamExpressionResult& m_result;
    const JBeamExpressionLimits& m_limits;
    bool m_failed;
};

enum class TokenKind
{
    NUMBER,
    STRING,
    VARIABLE,
    NIL_VALUE,
    TRUE_VALUE,
    FALSE_VALUE,
    AND,
    OR,
    NOT,
    CASE_VALUE,
    IDENTIFIER,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    CARET,
    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    CONCATENATE,
    LENGTH,
    LEFT_PAREN,
    RIGHT_PAREN,
    COMMA,
    END_OF_INPUT
};

struct Token
{
    TokenKind kind;
    std::size_t begin;
    std::size_t end;
    JBeamExpressionValue value;
    std::string text;
};

bool ParseFiniteDouble(const std::string& text, double& value)
{
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::noskipws >> value;
    return input.eof() && !input.fail() && IsFiniteNumber(value);
}

class Lexer
{
public:
    Lexer(
        const std::string& source,
        const JBeamExpressionLimits& limits,
        Runtime& runtime)
        : m_source(source)
        , m_limits(limits)
        , m_runtime(runtime)
        , m_index(2U)
    {
    }

    bool Run(std::vector<Token>& output)
    {
        while (!m_runtime.Failed())
        {
            while (m_index < m_source.size() &&
                   IsWhitespace(Peek()))
            {
                if (!Advance())
                {
                    return false;
                }
            }
            if (m_index == m_source.size())
            {
                Token token;
                token.kind = TokenKind::END_OF_INPUT;
                token.begin = m_index;
                token.end = m_index;
                return Emit(token, output);
            }

            const unsigned char current = Peek();
            if (IsAsciiDigit(current) ||
                (current == static_cast<unsigned char>('.') &&
                 IsAsciiDigit(Peek(1U))))
            {
                if (!ReadNumber(output))
                {
                    return false;
                }
                continue;
            }
            if (current == static_cast<unsigned char>('\''))
            {
                if (!ReadString(output))
                {
                    return false;
                }
                continue;
            }
            if (current == static_cast<unsigned char>('$'))
            {
                if (!ReadVariable(output))
                {
                    return false;
                }
                continue;
            }
            if (IsVariableStart(current))
            {
                if (!ReadIdentifier(output))
                {
                    return false;
                }
                continue;
            }
            if (!ReadOperator(output))
            {
                return false;
            }
        }
        return false;
    }

private:
    unsigned char Peek(std::size_t lookahead = 0U) const
    {
        if (lookahead >= m_source.size() - m_index)
        {
            return 0U;
        }
        return static_cast<unsigned char>(m_source[m_index + lookahead]);
    }

    bool Advance()
    {
        if (!m_runtime.Charge(1U, m_index))
        {
            return false;
        }
        ++m_index;
        return true;
    }

    bool Emit(const Token& token, std::vector<Token>& output)
    {
        if (output.size() >= m_limits.max_tokens)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TOKEN_LIMIT,
                token.begin,
                "JBeam expression token count exceeds the configured limit");
        }
        if (!m_runtime.Charge(1U, token.begin))
        {
            return false;
        }
        output.push_back(token);
        return true;
    }

    bool ReadNumber(std::vector<Token>& output)
    {
        const std::size_t begin = m_index;
        if (Peek() == static_cast<unsigned char>('.'))
        {
            if (!Advance())
            {
                return false;
            }
            while (IsAsciiDigit(Peek()))
            {
                if (!Advance())
                {
                    return false;
                }
            }
        }
        else
        {
            while (IsAsciiDigit(Peek()))
            {
                if (!Advance())
                {
                    return false;
                }
            }
            if (Peek() == static_cast<unsigned char>('.'))
            {
                if (!Advance())
                {
                    return false;
                }
                while (IsAsciiDigit(Peek()))
                {
                    if (!Advance())
                    {
                        return false;
                    }
                }
            }
        }

        if (Peek() == static_cast<unsigned char>('e') ||
            Peek() == static_cast<unsigned char>('E'))
        {
            if (!Advance())
            {
                return false;
            }
            if (Peek() == static_cast<unsigned char>('+') ||
                Peek() == static_cast<unsigned char>('-'))
            {
                if (!Advance())
                {
                    return false;
                }
            }
            if (!IsAsciiDigit(Peek()))
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::INVALID_NUMBER,
                    begin,
                    "JBeam expression exponent requires a decimal digit");
            }
            while (IsAsciiDigit(Peek()))
            {
                if (!Advance())
                {
                    return false;
                }
            }
        }

        const std::string spelling =
            m_source.substr(begin, m_index - begin);
        double number = 0.0;
        if (!ParseFiniteDouble(spelling, number))
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::NON_FINITE_NUMBER,
                begin,
                "JBeam expression number must be a finite decimal double");
        }

        Token token;
        token.kind = TokenKind::NUMBER;
        token.begin = begin;
        token.end = m_index;
        token.value = JBeamExpressionValue::Number(
            NormalizeNumber(number));
        return Emit(token, output);
    }

    bool AppendStringByte(
        char byte,
        std::size_t byte_offset,
        std::string& output)
    {
        if (output.size() >= m_limits.max_string_bytes)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::STRING_SIZE_LIMIT,
                byte_offset,
                "JBeam expression string exceeds the configured limit");
        }
        output.push_back(byte);
        return true;
    }

    bool ReadString(std::vector<Token>& output)
    {
        const std::size_t begin = m_index;
        if (!Advance())
        {
            return false;
        }
        std::string decoded;
        while (m_index < m_source.size())
        {
            const std::size_t byte_offset = m_index;
            const unsigned char current = Peek();
            if (current == static_cast<unsigned char>('\''))
            {
                if (!Advance())
                {
                    return false;
                }
                if (!m_runtime.Charge(decoded.size(), begin))
                {
                    return false;
                }
                if (!IsValidUtf8(decoded))
                {
                    return m_runtime.Fail(
                        JBeamExpressionDiagnosticCode::INVALID_UTF8,
                        begin,
                        "JBeam expression string is not valid UTF-8");
                }
                Token token;
                token.kind = TokenKind::STRING;
                token.begin = begin;
                token.end = m_index;
                token.value = JBeamExpressionValue::String(decoded);
                return Emit(token, output);
            }
            if (current < 0x20U || current == 0x7fU)
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::INVALID_CHARACTER,
                    byte_offset,
                    "JBeam expression string contains a raw control byte");
            }
            if (current == static_cast<unsigned char>('\\'))
            {
                if (!Advance())
                {
                    return false;
                }
                if (m_index == m_source.size())
                {
                    return m_runtime.Fail(
                        JBeamExpressionDiagnosticCode::UNTERMINATED_STRING,
                        begin,
                        "JBeam expression string is unterminated");
                }
                const unsigned char escaped = Peek();
                char decoded_byte = 0;
                switch (escaped)
                {
                case static_cast<unsigned char>('\''):
                    decoded_byte = '\'';
                    break;
                case static_cast<unsigned char>('\\'):
                    decoded_byte = '\\';
                    break;
                case static_cast<unsigned char>('n'):
                    decoded_byte = '\n';
                    break;
                case static_cast<unsigned char>('r'):
                    decoded_byte = '\r';
                    break;
                case static_cast<unsigned char>('t'):
                    decoded_byte = '\t';
                    break;
                default:
                    return m_runtime.Fail(
                        JBeamExpressionDiagnosticCode::INVALID_ESCAPE,
                        m_index,
                        "JBeam expression string escape is not allowlisted");
                }
                if (!Advance() ||
                    !AppendStringByte(
                        decoded_byte, byte_offset, decoded))
                {
                    return false;
                }
                continue;
            }
            if (!Advance() ||
                !AppendStringByte(
                    static_cast<char>(current), byte_offset, decoded))
            {
                return false;
            }
        }
        return m_runtime.Fail(
            JBeamExpressionDiagnosticCode::UNTERMINATED_STRING,
            begin,
            "JBeam expression string is unterminated");
    }

    bool ReadVariable(std::vector<Token>& output)
    {
        const std::size_t begin = m_index;
        if (!Advance())
        {
            return false;
        }
        if (!IsVariableStart(Peek()))
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::INVALID_CHARACTER,
                begin,
                "JBeam expression variable name is invalid");
        }
        while (IsVariableContinue(Peek()))
        {
            if (!Advance())
            {
                return false;
            }
        }
        while (Peek() == static_cast<unsigned char>('.') &&
               Peek(1U) != static_cast<unsigned char>('.'))
        {
            if (!IsVariableStart(Peek(1U)) ||
                !Advance())
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::INVALID_CHARACTER,
                    m_index,
                    "JBeam component path segment is invalid");
            }
            while (IsVariableContinue(Peek()))
            {
                if (!Advance())
                {
                    return false;
                }
            }
        }
        if (m_index - begin > m_limits.max_variable_name_bytes)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::STRING_SIZE_LIMIT,
                begin,
                "JBeam expression variable name exceeds the configured limit");
        }
        Token token;
        token.kind = TokenKind::VARIABLE;
        token.begin = begin;
        token.end = m_index;
        token.text = m_source.substr(begin, m_index - begin);
        if (!IsValidVariableName(token.text))
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::INVALID_CHARACTER,
                begin,
                "Only $components may use dotted JBeam paths");
        }
        return Emit(token, output);
    }

    bool ReadIdentifier(std::vector<Token>& output)
    {
        const std::size_t begin = m_index;
        while (IsVariableContinue(Peek()))
        {
            if (!Advance())
            {
                return false;
            }
        }
        const std::string text =
            m_source.substr(begin, m_index - begin);
        Token token;
        token.begin = begin;
        token.end = m_index;
        token.text = text;
        if (text == "nil")
        {
            token.kind = TokenKind::NIL_VALUE;
        }
        else if (text == "true")
        {
            token.kind = TokenKind::TRUE_VALUE;
        }
        else if (text == "false")
        {
            token.kind = TokenKind::FALSE_VALUE;
        }
        else if (text == "and")
        {
            token.kind = TokenKind::AND;
        }
        else if (text == "or")
        {
            token.kind = TokenKind::OR;
        }
        else if (text == "not")
        {
            token.kind = TokenKind::NOT;
        }
        else if (text == "case")
        {
            token.kind = TokenKind::CASE_VALUE;
        }
        else
        {
            token.kind = TokenKind::IDENTIFIER;
        }
        return Emit(token, output);
    }

    bool EmitOperator(
        TokenKind kind,
        std::size_t begin,
        std::size_t byte_count,
        std::vector<Token>& output)
    {
        for (std::size_t index = 0U; index < byte_count; ++index)
        {
            if (!Advance())
            {
                return false;
            }
        }
        Token token;
        token.kind = kind;
        token.begin = begin;
        token.end = m_index;
        return Emit(token, output);
    }

    bool ReadOperator(std::vector<Token>& output)
    {
        const std::size_t begin = m_index;
        switch (Peek())
        {
        case static_cast<unsigned char>('+'):
            return EmitOperator(TokenKind::PLUS, begin, 1U, output);
        case static_cast<unsigned char>('-'):
            return EmitOperator(TokenKind::MINUS, begin, 1U, output);
        case static_cast<unsigned char>('*'):
            return EmitOperator(TokenKind::STAR, begin, 1U, output);
        case static_cast<unsigned char>('/'):
            return EmitOperator(TokenKind::SLASH, begin, 1U, output);
        case static_cast<unsigned char>('%'):
            return EmitOperator(TokenKind::PERCENT, begin, 1U, output);
        case static_cast<unsigned char>('^'):
            return EmitOperator(TokenKind::CARET, begin, 1U, output);
        case static_cast<unsigned char>('#'):
            return EmitOperator(TokenKind::LENGTH, begin, 1U, output);
        case static_cast<unsigned char>('('):
            return EmitOperator(TokenKind::LEFT_PAREN, begin, 1U, output);
        case static_cast<unsigned char>(')'):
            return EmitOperator(TokenKind::RIGHT_PAREN, begin, 1U, output);
        case static_cast<unsigned char>(','):
            return EmitOperator(TokenKind::COMMA, begin, 1U, output);
        case static_cast<unsigned char>('.'):
            if (Peek(1U) == static_cast<unsigned char>('.'))
            {
                return EmitOperator(
                    TokenKind::CONCATENATE, begin, 2U, output);
            }
            break;
        case static_cast<unsigned char>('='):
            if (Peek(1U) == static_cast<unsigned char>('='))
            {
                return EmitOperator(
                    TokenKind::EQUAL, begin, 2U, output);
            }
            break;
        case static_cast<unsigned char>('~'):
            if (Peek(1U) == static_cast<unsigned char>('='))
            {
                return EmitOperator(
                    TokenKind::NOT_EQUAL, begin, 2U, output);
            }
            break;
        case static_cast<unsigned char>('<'):
            if (Peek(1U) == static_cast<unsigned char>('='))
            {
                return EmitOperator(
                    TokenKind::LESS_EQUAL, begin, 2U, output);
            }
            return EmitOperator(TokenKind::LESS, begin, 1U, output);
        case static_cast<unsigned char>('>'):
            if (Peek(1U) == static_cast<unsigned char>('='))
            {
                return EmitOperator(
                    TokenKind::GREATER_EQUAL, begin, 2U, output);
            }
            return EmitOperator(TokenKind::GREATER, begin, 1U, output);
        default:
            break;
        }
        return m_runtime.Fail(
            JBeamExpressionDiagnosticCode::INVALID_CHARACTER,
            begin,
            "JBeam expression contains a character or operator outside "
            "the allowlist");
    }

    const std::string& m_source;
    const JBeamExpressionLimits& m_limits;
    Runtime& m_runtime;
    std::size_t m_index;
};

bool IsTruthy(const JBeamExpressionValue& value)
{
    return value.type != JBeamExpressionValueType::NIL_VALUE &&
        (value.type != JBeamExpressionValueType::BOOLEAN ||
         value.boolean_value);
}

int CompareStrings(const std::string& left, const std::string& right)
{
    const std::size_t common =
        left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0U; index < common; ++index)
    {
        const unsigned char left_byte =
            static_cast<unsigned char>(left[index]);
        const unsigned char right_byte =
            static_cast<unsigned char>(right[index]);
        if (left_byte < right_byte)
        {
            return -1;
        }
        if (left_byte > right_byte)
        {
            return 1;
        }
    }
    if (left.size() < right.size())
    {
        return -1;
    }
    if (left.size() > right.size())
    {
        return 1;
    }
    return 0;
}

bool ValuesEqual(
    const JBeamExpressionValue& left,
    const JBeamExpressionValue& right)
{
    if (left.type != right.type)
    {
        return false;
    }
    switch (left.type)
    {
    case JBeamExpressionValueType::NIL_VALUE:
        return true;
    case JBeamExpressionValueType::BOOLEAN:
        return left.boolean_value == right.boolean_value;
    case JBeamExpressionValueType::NUMBER:
        return left.number_value == right.number_value;
    case JBeamExpressionValueType::STRING:
        return left.string_value == right.string_value;
    }
    return false;
}

double AddNumbers(double left, double right)
{
    volatile double observed_left = left;
    volatile double observed_right = right;
    volatile double result = observed_left + observed_right;
    return result;
}

double SubtractNumbers(double left, double right)
{
    volatile double observed_left = left;
    volatile double observed_right = right;
    volatile double result = observed_left - observed_right;
    return result;
}

double MultiplyNumbers(double left, double right)
{
    volatile double observed_left = left;
    volatile double observed_right = right;
    volatile double result = observed_left * observed_right;
    return result;
}

double DivideNumbers(double left, double right)
{
    volatile double observed_left = left;
    volatile double observed_right = right;
    volatile double result = observed_left / observed_right;
    return result;
}

bool IsExactIntegerInRange(
    double value,
    double minimum,
    double maximum,
    std::int64_t& integer)
{
    if (value < minimum || value > maximum)
    {
        return false;
    }
    const std::int64_t candidate = static_cast<std::int64_t>(value);
    if (static_cast<double>(candidate) != value)
    {
        return false;
    }
    integer = candidate;
    return true;
}

bool PowerNumbers(double base, int exponent, double& output)
{
    unsigned int remaining = static_cast<unsigned int>(
        exponent < 0 ? -exponent : exponent);
    double factor = base;
    if (exponent < 0)
    {
        factor = DivideNumbers(1.0, factor);
        if (!IsFiniteNumber(factor))
        {
            return false;
        }
    }

    double result = 1.0;
    while (remaining != 0U)
    {
        if ((remaining & 1U) != 0U)
        {
            result = MultiplyNumbers(result, factor);
            if (!IsFiniteNumber(result))
            {
                return false;
            }
        }
        remaining >>= 1U;
        if (remaining != 0U)
        {
            factor = MultiplyNumbers(factor, factor);
            if (!IsFiniteNumber(factor))
            {
                return false;
            }
        }
    }
    output = result;
    return true;
}

double ModuloIntegers(std::int64_t left, std::int64_t right)
{
    std::int64_t remainder = left % right;
    if (remainder != 0 &&
        ((remainder < 0) != (right < 0)))
    {
        remainder += right;
    }
    return static_cast<double>(remainder);
}

class Parser
{
public:
    Parser(
        const std::vector<Token>& tokens,
        const JBeamExpressionEnvironment& environment,
        const JBeamExpressionLimits& limits,
        Runtime& runtime)
        : m_tokens(tokens)
        , m_environment(environment)
        , m_limits(limits)
        , m_runtime(runtime)
        , m_index(0U)
        , m_depth(0U)
    {
    }

    bool Run(JBeamExpressionValue& output)
    {
        if (!ParseOr(true, output))
        {
            return false;
        }
        if (Current().kind != TokenKind::END_OF_INPUT)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TRAILING_CONTENT,
                Current().begin,
                "JBeam expression has trailing content");
        }
        if (!Consume())
        {
            return false;
        }
        return !m_runtime.Failed();
    }

private:
    static const std::size_t MAX_ALLOWLISTED_FUNCTION_ARGUMENTS = 64U;

    const Token& Current() const
    {
        return m_tokens[m_index];
    }

    const Token& Next() const
    {
        return m_tokens[
            m_index + 1U < m_tokens.size() ? m_index + 1U : m_index];
    }

    bool Consume()
    {
        if (!m_runtime.Charge(1U, Current().begin))
        {
            return false;
        }
        if (m_index + 1U < m_tokens.size())
        {
            ++m_index;
        }
        return true;
    }

    bool Match(TokenKind kind)
    {
        if (Current().kind != kind)
        {
            return false;
        }
        return Consume();
    }

    bool Expect(TokenKind kind, const char* message)
    {
        if (Current().kind != kind)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::EXPECTED_TOKEN,
                Current().begin,
                message);
        }
        return Consume();
    }

    bool BeginDepth(std::size_t byte_offset)
    {
        if (m_depth >= m_limits.max_depth)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::DEPTH_LIMIT,
                byte_offset,
                "JBeam expression nesting exceeds the configured limit");
        }
        ++m_depth;
        return true;
    }

    void EndDepth()
    {
        if (m_depth > 0U)
        {
            --m_depth;
        }
    }

    bool ParseOr(bool evaluate, JBeamExpressionValue& output)
    {
        if (!ParseAnd(evaluate, output))
        {
            return false;
        }
        while (Current().kind == TokenKind::OR)
        {
            const std::size_t offset = Current().begin;
            if (!Consume())
            {
                return false;
            }
            const bool use_left = evaluate && IsTruthy(output);
            JBeamExpressionValue right;
            if (!ParseAnd(evaluate && !use_left, right))
            {
                return false;
            }
            if (evaluate)
            {
                if (!m_runtime.Charge(1U, offset))
                {
                    return false;
                }
                if (!use_left)
                {
                    if (!ChargeValueCopy(right, offset))
                    {
                        return false;
                    }
                    output = right;
                }
            }
        }
        return !m_runtime.Failed();
    }

    bool ParseAnd(bool evaluate, JBeamExpressionValue& output)
    {
        if (!ParseComparison(evaluate, output))
        {
            return false;
        }
        while (Current().kind == TokenKind::AND)
        {
            const std::size_t offset = Current().begin;
            if (!Consume())
            {
                return false;
            }
            const bool use_right = evaluate && IsTruthy(output);
            JBeamExpressionValue right;
            if (!ParseComparison(use_right, right))
            {
                return false;
            }
            if (evaluate)
            {
                if (!m_runtime.Charge(1U, offset))
                {
                    return false;
                }
                if (use_right)
                {
                    if (!ChargeValueCopy(right, offset))
                    {
                        return false;
                    }
                    output = right;
                }
            }
        }
        return !m_runtime.Failed();
    }

    bool ParseComparison(bool evaluate, JBeamExpressionValue& output)
    {
        if (!ParseConcatenation(evaluate, output))
        {
            return false;
        }
        while (Current().kind == TokenKind::EQUAL ||
               Current().kind == TokenKind::NOT_EQUAL ||
               Current().kind == TokenKind::LESS ||
               Current().kind == TokenKind::LESS_EQUAL ||
               Current().kind == TokenKind::GREATER ||
               Current().kind == TokenKind::GREATER_EQUAL)
        {
            const Token operation = Current();
            if (!Consume())
            {
                return false;
            }
            JBeamExpressionValue right;
            if (!ParseConcatenation(evaluate, right))
            {
                return false;
            }
            if (evaluate &&
                !ApplyComparison(operation, output, right, output))
            {
                return false;
            }
        }
        return !m_runtime.Failed();
    }

    bool ParseConcatenation(
        bool evaluate,
        JBeamExpressionValue& output)
    {
        if (!ParseAdditive(evaluate, output))
        {
            return false;
        }
        if (Current().kind == TokenKind::CONCATENATE)
        {
            const Token operation = Current();
            if (!Consume() || !BeginDepth(operation.begin))
            {
                return false;
            }
            JBeamExpressionValue right;
            const bool parsed = ParseConcatenation(evaluate, right);
            EndDepth();
            if (!parsed)
            {
                return false;
            }
            if (evaluate &&
                !ApplyConcatenation(operation, output, right, output))
            {
                return false;
            }
        }
        return !m_runtime.Failed();
    }

    bool ParseAdditive(bool evaluate, JBeamExpressionValue& output)
    {
        if (!ParseMultiplicative(evaluate, output))
        {
            return false;
        }
        while (Current().kind == TokenKind::PLUS ||
               Current().kind == TokenKind::MINUS)
        {
            const Token operation = Current();
            if (!Consume())
            {
                return false;
            }
            JBeamExpressionValue right;
            if (!ParseMultiplicative(evaluate, right))
            {
                return false;
            }
            if (evaluate &&
                !ApplyArithmetic(operation, output, right, output))
            {
                return false;
            }
        }
        return !m_runtime.Failed();
    }

    bool ParseMultiplicative(
        bool evaluate,
        JBeamExpressionValue& output)
    {
        if (!ParseUnary(evaluate, output))
        {
            return false;
        }
        while (Current().kind == TokenKind::STAR ||
               Current().kind == TokenKind::SLASH ||
               Current().kind == TokenKind::PERCENT)
        {
            const Token operation = Current();
            if (!Consume())
            {
                return false;
            }
            JBeamExpressionValue right;
            if (!ParseUnary(evaluate, right))
            {
                return false;
            }
            if (evaluate &&
                !ApplyArithmetic(operation, output, right, output))
            {
                return false;
            }
        }
        return !m_runtime.Failed();
    }

    bool ParseUnary(bool evaluate, JBeamExpressionValue& output)
    {
        if (Current().kind == TokenKind::MINUS ||
            Current().kind == TokenKind::NOT ||
            Current().kind == TokenKind::LENGTH)
        {
            const Token operation = Current();
            if (!Consume() || !BeginDepth(operation.begin))
            {
                return false;
            }
            JBeamExpressionValue operand;
            const bool parsed = ParseUnary(evaluate, operand);
            EndDepth();
            if (!parsed)
            {
                return false;
            }
            if (evaluate &&
                !ApplyUnary(operation, operand, output))
            {
                return false;
            }
            return true;
        }
        return ParsePower(evaluate, output);
    }

    bool ParsePower(bool evaluate, JBeamExpressionValue& output)
    {
        if (!ParsePrimary(evaluate, output))
        {
            return false;
        }
        if (Current().kind == TokenKind::CARET)
        {
            const Token operation = Current();
            if (!Consume() || !BeginDepth(operation.begin))
            {
                return false;
            }
            JBeamExpressionValue right;
            const bool parsed = ParseUnary(evaluate, right);
            EndDepth();
            if (!parsed)
            {
                return false;
            }
            if (evaluate &&
                !ApplyArithmetic(operation, output, right, output))
            {
                return false;
            }
        }
        return !m_runtime.Failed();
    }

    bool ParsePrimary(bool evaluate, JBeamExpressionValue& output)
    {
        const Token token = Current();
        switch (token.kind)
        {
        case TokenKind::NUMBER:
        case TokenKind::STRING:
            if (!Consume())
            {
                return false;
            }
            if (evaluate)
            {
                if (!ChargeValueCopy(token.value, token.begin))
                {
                    return false;
                }
                output = token.value;
            }
            else
            {
                output = JBeamExpressionValue::Nil();
            }
            return true;
        case TokenKind::NIL_VALUE:
            if (!Consume())
            {
                return false;
            }
            output = JBeamExpressionValue::Nil();
            return true;
        case TokenKind::TRUE_VALUE:
        case TokenKind::FALSE_VALUE:
            if (!Consume())
            {
                return false;
            }
            output = evaluate
                ? JBeamExpressionValue::Boolean(
                    token.kind == TokenKind::TRUE_VALUE)
                : JBeamExpressionValue::Nil();
            return true;
        case TokenKind::VARIABLE:
            if (!Consume())
            {
                return false;
            }
            if (!evaluate)
            {
                output = JBeamExpressionValue::Nil();
                return true;
            }
            return LookupVariable(token, output);
        case TokenKind::LEFT_PAREN:
            if (!Consume() || !BeginDepth(token.begin))
            {
                return false;
            }
            {
                const bool parsed = ParseOr(evaluate, output);
                EndDepth();
                if (!parsed)
                {
                    return false;
                }
            }
            return Expect(
                TokenKind::RIGHT_PAREN,
                "Expected ')' after JBeam expression group");
        case TokenKind::CASE_VALUE:
            return ParseCase(evaluate, output);
        case TokenKind::IDENTIFIER:
            if (Next().kind == TokenKind::LEFT_PAREN &&
                IsAllowlistedScalarFunction(token.text))
            {
                return ParseScalarFunction(evaluate, output);
            }
            return m_runtime.Fail(
                Next().kind == TokenKind::LEFT_PAREN
                    ? JBeamExpressionDiagnosticCode::UNSUPPORTED_FUNCTION
                    : JBeamExpressionDiagnosticCode::EXPECTED_VALUE,
                token.begin,
                Next().kind == TokenKind::LEFT_PAREN
                    ? "JBeam expression function is outside the allowlist"
                    : "Expected an allowlisted JBeam expression value");
        default:
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::EXPECTED_VALUE,
                token.begin,
                "Expected an allowlisted JBeam expression value");
        }
    }

    bool IsAllowlistedScalarFunction(const std::string& name) const
    {
        return name == "abs" ||
            name == "square" ||
            name == "clamp" ||
            name == "min" ||
            name == "max";
    }

    bool ParseScalarFunction(
        bool evaluate,
        JBeamExpressionValue& output)
    {
        const Token function = Current();
        if (!Consume() ||
            !Expect(
                TokenKind::LEFT_PAREN,
                "Expected '(' after JBeam scalar function") ||
            !BeginDepth(function.begin))
        {
            return false;
        }

        const std::size_t argument_limit =
            m_limits.max_function_arguments <
                    MAX_ALLOWLISTED_FUNCTION_ARGUMENTS
                ? m_limits.max_function_arguments
                : MAX_ALLOWLISTED_FUNCTION_ARGUMENTS;
        const bool unary =
            function.text == "abs" || function.text == "square";
        const bool variadic =
            function.text == "min" || function.text == "max";
        std::size_t argument_count = 0U;
        bool all_numeric = true;
        JBeamExpressionValue fixed_arguments[3];
        double extremum = 0.0;
        bool have_extremum = false;
        bool parsed = true;
        if (Current().kind != TokenKind::RIGHT_PAREN)
        {
            while (parsed)
            {
                if (argument_count >= argument_limit)
                {
                    parsed = m_runtime.Fail(
                        JBeamExpressionDiagnosticCode::
                            FUNCTION_ARGUMENT_LIMIT,
                        Current().begin,
                        "JBeam function argument count exceeds the "
                        "configured or allowlisted limit");
                    break;
                }

                JBeamExpressionValue argument;
                parsed = ParseOr(evaluate, argument);
                if (!parsed)
                {
                    break;
                }
                if (evaluate)
                {
                    if (argument.type !=
                        JBeamExpressionValueType::NUMBER)
                    {
                        all_numeric = false;
                    }
                    else if (variadic)
                    {
                        if (!have_extremum)
                        {
                            extremum = argument.number_value;
                            have_extremum = true;
                        }
                        else if (
                            (function.text == "min" &&
                             argument.number_value < extremum) ||
                            (function.text == "max" &&
                             argument.number_value > extremum))
                        {
                            extremum = argument.number_value;
                        }
                    }
                    else if (argument_count < 3U)
                    {
                        fixed_arguments[argument_count] = argument;
                    }
                }
                ++argument_count;
                if (Current().kind != TokenKind::COMMA)
                {
                    break;
                }
                parsed = Consume();
            }
        }
        if (parsed)
        {
            parsed = Expect(
                TokenKind::RIGHT_PAREN,
                "Expected ')' after JBeam scalar function arguments");
        }
        EndDepth();
        if (!parsed)
        {
            return false;
        }

        const bool valid_arity =
            (unary && argument_count == 1U) ||
            (function.text == "clamp" && argument_count == 3U) ||
            (variadic &&
             argument_count >= 1U &&
             argument_count <=
                 MAX_ALLOWLISTED_FUNCTION_ARGUMENTS);
        if (!valid_arity)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::FUNCTION_ARITY,
                function.begin,
                "JBeam scalar function has an invalid argument count");
        }
        if (!evaluate)
        {
            output = JBeamExpressionValue::Nil();
            return true;
        }
        if (!m_runtime.Charge(
                1U + argument_count, function.begin))
        {
            return false;
        }
        if (!all_numeric)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TYPE_MISMATCH,
                function.begin,
                "JBeam scalar function arguments must all be numbers");
        }

        double result = 0.0;
        if (function.text == "abs")
        {
            volatile double observed =
                fixed_arguments[0].number_value;
            result = observed < 0.0 ? -observed : observed;
        }
        else if (function.text == "square")
        {
            result = MultiplyNumbers(
                fixed_arguments[0].number_value,
                fixed_arguments[0].number_value);
            if (!IsFiniteNumber(result))
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::NON_FINITE_RESULT,
                    function.begin,
                    "JBeam square function produced a non-finite value");
            }
        }
        else if (function.text == "clamp")
        {
            const double value =
                fixed_arguments[0].number_value;
            const double lower =
                fixed_arguments[1].number_value;
            const double upper =
                fixed_arguments[2].number_value;
            if (lower > upper)
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::
                        INVALID_FUNCTION_ARGUMENT,
                    function.begin,
                    "JBeam clamp lower bound must not exceed its "
                    "upper bound");
            }
            result = value < lower
                ? lower
                : (value > upper ? upper : value);
        }
        else
        {
            result = extremum;
        }
        if (!IsFiniteNumber(result))
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::NON_FINITE_RESULT,
                function.begin,
                "JBeam scalar function produced a non-finite value");
        }
        output = JBeamExpressionValue::Number(
            NormalizeNumber(result));
        return true;
    }

    bool ParseCase(bool evaluate, JBeamExpressionValue& output)
    {
        const Token function = Current();
        if (!Consume() ||
            !Expect(
                TokenKind::LEFT_PAREN,
                "Expected '(' after case") ||
            !BeginDepth(function.begin))
        {
            return false;
        }
        if (m_limits.max_function_arguments < 3U)
        {
            EndDepth();
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::FUNCTION_ARGUMENT_LIMIT,
                function.begin,
                "JBeam case argument count exceeds the configured limit");
        }
        JBeamExpressionValue selector;
        JBeamExpressionValue when_true;
        JBeamExpressionValue when_false;
        bool parsed = ParseOr(evaluate, selector);
        if (parsed)
        {
            parsed = Expect(
                TokenKind::COMMA,
                "Expected ',' after case selector");
        }
        if (parsed)
        {
            parsed = ParseOr(evaluate, when_true);
        }
        if (parsed)
        {
            parsed = Expect(
                TokenKind::COMMA,
                "Expected ',' after case true value");
        }
        if (parsed)
        {
            parsed = ParseOr(evaluate, when_false);
        }
        if (parsed)
        {
            parsed = Expect(
                TokenKind::RIGHT_PAREN,
                "Expected ')' after three case arguments");
        }
        EndDepth();
        if (!parsed)
        {
            return false;
        }
        if (!evaluate)
        {
            output = JBeamExpressionValue::Nil();
            return true;
        }
        if (!m_runtime.Charge(1U, function.begin))
        {
            return false;
        }
        if (selector.type != JBeamExpressionValueType::BOOLEAN)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::UNSUPPORTED_CASE_SIGNATURE,
                function.begin,
                "Only Boolean three-argument case is allowlisted");
        }
        const JBeamExpressionValue& selected =
            selector.boolean_value ? when_true : when_false;
        if (!ChargeValueCopy(selected, function.begin))
        {
            return false;
        }
        output = selected;
        return true;
    }

    bool LookupVariable(
        const Token& token,
        JBeamExpressionValue& output)
    {
        for (std::size_t index = m_environment.variables.size();
             index > 0U;
             --index)
        {
            const JBeamExpressionVariable& variable =
                m_environment.variables[index - 1U];
            if (!m_runtime.Charge(1U, token.begin) ||
                !m_runtime.Charge(variable.name.size(), token.begin) ||
                !m_runtime.Charge(token.text.size(), token.begin))
            {
                return false;
            }
            if (variable.name == token.text)
            {
                if (!ChargeValueCopy(variable.value, token.begin))
                {
                    return false;
                }
                output = variable.value;
                if (output.type == JBeamExpressionValueType::NUMBER)
                {
                    output.number_value =
                        NormalizeNumber(output.number_value);
                }
                return true;
            }
        }
        output = JBeamExpressionValue::Nil();
        return true;
    }

    bool ChargeValueCopy(
        const JBeamExpressionValue& value,
        std::size_t byte_offset)
    {
        return value.type != JBeamExpressionValueType::STRING ||
            m_runtime.Charge(value.string_value.size(), byte_offset);
    }

    bool RequireNumbers(
        const Token& operation,
        const JBeamExpressionValue& left,
        const JBeamExpressionValue& right)
    {
        if (left.type != JBeamExpressionValueType::NUMBER ||
            right.type != JBeamExpressionValueType::NUMBER)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TYPE_MISMATCH,
                operation.begin,
                "JBeam arithmetic operands must both be numbers");
        }
        return true;
    }

    bool ApplyArithmetic(
        const Token& operation,
        const JBeamExpressionValue& left,
        const JBeamExpressionValue& right,
        JBeamExpressionValue& output)
    {
        if (!m_runtime.Charge(1U, operation.begin) ||
            !RequireNumbers(operation, left, right))
        {
            return false;
        }
        if ((operation.kind == TokenKind::SLASH ||
             operation.kind == TokenKind::PERCENT) &&
            right.number_value == 0.0)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::DIVISION_BY_ZERO,
                operation.begin,
                "JBeam expression division by zero is rejected");
        }

        double result = 0.0;
        switch (operation.kind)
        {
        case TokenKind::PLUS:
            result = AddNumbers(left.number_value, right.number_value);
            break;
        case TokenKind::MINUS:
            result =
                SubtractNumbers(left.number_value, right.number_value);
            break;
        case TokenKind::STAR:
            result =
                MultiplyNumbers(left.number_value, right.number_value);
            break;
        case TokenKind::SLASH:
            result =
                DivideNumbers(left.number_value, right.number_value);
            break;
        case TokenKind::PERCENT:
        {
            // Decimal floating-point remainder is delegated to libm by many
            // standard libraries and its last bit is not a cross-platform
            // identity contract. Integer modulo is exact and implements Lua's
            // divisor-signed remainder without any floating-point library.
            static const double MAX_EXACT_INTEGER =
                9007199254740992.0; // 2^53
            std::int64_t left_integer = 0;
            std::int64_t right_integer = 0;
            if (!IsExactIntegerInRange(
                    left.number_value,
                    -MAX_EXACT_INTEGER,
                    MAX_EXACT_INTEGER,
                    left_integer) ||
                !IsExactIntegerInRange(
                    right.number_value,
                    -MAX_EXACT_INTEGER,
                    MAX_EXACT_INTEGER,
                    right_integer))
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::
                        NON_DETERMINISTIC_OPERAND,
                    operation.begin,
                    "JBeam '%' operands must be exact integers in "
                    "[-2^53, 2^53]");
            }
            result = ModuloIntegers(left_integer, right_integer);
            break;
        }
        case TokenKind::CARET:
        {
            // std::pow is intentionally avoided: its last-bit result is not
            // specified across C libraries. Integer exponents use this pinned
            // binary exponentiation sequence and the same explicit binary64
            // round trips as the evaluator's other arithmetic.
            static const std::int64_t MAX_POWER_EXPONENT = 1024;
            std::int64_t exponent = 0;
            if (!IsExactIntegerInRange(
                    right.number_value,
                    -static_cast<double>(MAX_POWER_EXPONENT),
                    static_cast<double>(MAX_POWER_EXPONENT),
                    exponent))
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::
                        NON_DETERMINISTIC_OPERAND,
                    operation.begin,
                    "JBeam '^' exponent must be an exact integer in "
                    "[-1024, 1024]");
            }
            if (left.number_value == 0.0 && exponent < 0)
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::DIVISION_BY_ZERO,
                    operation.begin,
                    "JBeam zero cannot be raised to a negative power");
            }
            if (!PowerNumbers(
                    left.number_value,
                    static_cast<int>(exponent),
                    result))
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::NON_FINITE_RESULT,
                    operation.begin,
                    "JBeam expression exponentiation produced a "
                    "non-finite value");
            }
            break;
        }
        default:
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TYPE_MISMATCH,
                operation.begin,
                "Internal JBeam arithmetic operator mismatch");
        }
        if (!IsFiniteNumber(result))
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::NON_FINITE_RESULT,
                operation.begin,
                "JBeam expression arithmetic produced a non-finite value");
        }
        output = JBeamExpressionValue::Number(
            NormalizeNumber(result));
        return true;
    }

    bool ApplyUnary(
        const Token& operation,
        const JBeamExpressionValue& operand,
        JBeamExpressionValue& output)
    {
        if (!m_runtime.Charge(1U, operation.begin))
        {
            return false;
        }
        if (operation.kind == TokenKind::NOT)
        {
            output = JBeamExpressionValue::Boolean(!IsTruthy(operand));
            return true;
        }
        if (operation.kind == TokenKind::LENGTH)
        {
            if (operand.type != JBeamExpressionValueType::STRING)
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::TYPE_MISMATCH,
                    operation.begin,
                    "JBeam length operand must be a string");
            }
            const double length =
                static_cast<double>(operand.string_value.size());
            if (static_cast<std::size_t>(length) !=
                operand.string_value.size())
            {
                return m_runtime.Fail(
                    JBeamExpressionDiagnosticCode::NON_FINITE_RESULT,
                    operation.begin,
                    "JBeam string length is not exactly representable");
            }
            output = JBeamExpressionValue::Number(length);
            return true;
        }
        if (operand.type != JBeamExpressionValueType::NUMBER)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TYPE_MISMATCH,
                operation.begin,
                "JBeam unary minus operand must be a number");
        }
        volatile double observed = operand.number_value;
        volatile double result = -observed;
        if (!IsFiniteNumber(result))
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::NON_FINITE_RESULT,
                operation.begin,
                "JBeam unary arithmetic produced a non-finite value");
        }
        output = JBeamExpressionValue::Number(
            NormalizeNumber(result));
        return true;
    }

    bool ApplyComparison(
        const Token& operation,
        const JBeamExpressionValue& left,
        const JBeamExpressionValue& right,
        JBeamExpressionValue& output)
    {
        if (!m_runtime.Charge(1U, operation.begin))
        {
            return false;
        }
        if (operation.kind == TokenKind::EQUAL ||
            operation.kind == TokenKind::NOT_EQUAL)
        {
            if (left.type == JBeamExpressionValueType::STRING &&
                right.type == JBeamExpressionValueType::STRING)
            {
                const std::size_t compared_bytes =
                    left.string_value.size() < right.string_value.size()
                        ? left.string_value.size()
                        : right.string_value.size();
                if (!m_runtime.Charge(compared_bytes, operation.begin))
                {
                    return false;
                }
            }
            const bool equal = ValuesEqual(left, right);
            output = JBeamExpressionValue::Boolean(
                operation.kind == TokenKind::EQUAL ? equal : !equal);
            return true;
        }

        int comparison = 0;
        if (left.type == JBeamExpressionValueType::NUMBER &&
            right.type == JBeamExpressionValueType::NUMBER)
        {
            comparison = left.number_value < right.number_value
                ? -1
                : (left.number_value > right.number_value ? 1 : 0);
        }
        else if (left.type == JBeamExpressionValueType::STRING &&
                 right.type == JBeamExpressionValueType::STRING)
        {
            const std::size_t compared_bytes =
                left.string_value.size() < right.string_value.size()
                    ? left.string_value.size()
                    : right.string_value.size();
            if (!m_runtime.Charge(compared_bytes, operation.begin))
            {
                return false;
            }
            comparison =
                CompareStrings(left.string_value, right.string_value);
        }
        else
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TYPE_MISMATCH,
                operation.begin,
                "JBeam relational operands must be matching numbers "
                "or strings");
        }

        bool result = false;
        switch (operation.kind)
        {
        case TokenKind::LESS:
            result = comparison < 0;
            break;
        case TokenKind::LESS_EQUAL:
            result = comparison <= 0;
            break;
        case TokenKind::GREATER:
            result = comparison > 0;
            break;
        case TokenKind::GREATER_EQUAL:
            result = comparison >= 0;
            break;
        default:
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TYPE_MISMATCH,
                operation.begin,
                "Internal JBeam relational operator mismatch");
        }
        output = JBeamExpressionValue::Boolean(result);
        return true;
    }

    bool ApplyConcatenation(
        const Token& operation,
        const JBeamExpressionValue& left,
        const JBeamExpressionValue& right,
        JBeamExpressionValue& output)
    {
        if (!m_runtime.Charge(1U, operation.begin))
        {
            return false;
        }
        if (left.type != JBeamExpressionValueType::STRING ||
            right.type != JBeamExpressionValueType::STRING)
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::TYPE_MISMATCH,
                operation.begin,
                "The safe JBeam concatenation subset requires two strings");
        }
        if (left.string_value.size() >
                m_limits.max_output_string_bytes ||
            right.string_value.size() >
                m_limits.max_output_string_bytes -
                    left.string_value.size())
        {
            return m_runtime.Fail(
                JBeamExpressionDiagnosticCode::OUTPUT_STRING_SIZE_LIMIT,
                operation.begin,
                "JBeam expression output string exceeds the configured limit");
        }
        const std::size_t output_size =
            left.string_value.size() + right.string_value.size();
        if (!m_runtime.Charge(output_size, operation.begin))
        {
            return false;
        }
        std::string joined;
        joined.reserve(output_size);
        joined.append(left.string_value);
        joined.append(right.string_value);
        output = JBeamExpressionValue::String(joined);
        return true;
    }

    const std::vector<Token>& m_tokens;
    const JBeamExpressionEnvironment& m_environment;
    const JBeamExpressionLimits& m_limits;
    Runtime& m_runtime;
    std::size_t m_index;
    std::size_t m_depth;
};

bool ValidateEnvironment(
    const JBeamExpressionEnvironment& environment,
    const JBeamExpressionLimits& limits,
    Runtime& runtime)
{
    if (environment.variables.size() > limits.max_variables)
    {
        return runtime.Fail(
            JBeamExpressionDiagnosticCode::ENVIRONMENT_LIMIT,
            0U,
            "JBeam expression environment exceeds the variable limit");
    }
    std::size_t retained_string_bytes = 0U;
    for (std::size_t index = 0U;
         index < environment.variables.size();
         ++index)
    {
        const JBeamExpressionVariable& variable =
            environment.variables[index];
        if (!runtime.Charge(1U, 0U) ||
            !runtime.Charge(variable.name.size(), 0U))
        {
            return false;
        }
        if (variable.name.size() >
                limits.max_variable_name_bytes ||
            !IsValidVariableName(variable.name))
        {
            return runtime.Fail(
                JBeamExpressionDiagnosticCode::
                    INVALID_ENVIRONMENT_VARIABLE,
                0U,
                "JBeam expression environment has an invalid variable name");
        }
        switch (variable.value.type)
        {
        case JBeamExpressionValueType::NIL_VALUE:
        case JBeamExpressionValueType::BOOLEAN:
            break;
        case JBeamExpressionValueType::NUMBER:
            if (!IsFiniteNumber(variable.value.number_value))
            {
                return runtime.Fail(
                    JBeamExpressionDiagnosticCode::
                        INVALID_ENVIRONMENT_VALUE,
                    0U,
                    "JBeam expression environment number is not finite");
            }
            break;
        case JBeamExpressionValueType::STRING:
            if (variable.value.string_value.size() >
                    limits.max_string_bytes ||
                variable.value.string_value.size() >
                    limits.max_environment_string_bytes -
                        retained_string_bytes)
            {
                return runtime.Fail(
                    JBeamExpressionDiagnosticCode::ENVIRONMENT_LIMIT,
                    0U,
                    "JBeam expression environment strings exceed "
                    "the configured limit");
            }
            if (!runtime.Charge(
                    variable.value.string_value.size(), 0U))
            {
                return false;
            }
            if (!IsValidUtf8(variable.value.string_value))
            {
                return runtime.Fail(
                    JBeamExpressionDiagnosticCode::
                        INVALID_ENVIRONMENT_VALUE,
                    0U,
                    "JBeam expression environment string is not valid UTF-8");
            }
            retained_string_bytes +=
                variable.value.string_value.size();
            break;
        default:
            return runtime.Fail(
                JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VALUE,
                0U,
                "JBeam expression environment value type is invalid");
        }
    }
    return true;
}

void AppendSize(std::size_t value, std::string& output)
{
    char digits[
        std::numeric_limits<std::size_t>::digits10 + 2U];
    std::size_t count = 0U;
    do
    {
        digits[count++] = static_cast<char>(
            '0' + static_cast<char>(value % 10U));
        value /= 10U;
    } while (value > 0U);
    while (count > 0U)
    {
        output.push_back(digits[--count]);
    }
}

} // namespace

JBeamExpressionValue::JBeamExpressionValue()
    : type(JBeamExpressionValueType::NIL_VALUE)
    , boolean_value(false)
    , number_value(0.0)
{
}

JBeamExpressionValue JBeamExpressionValue::Nil()
{
    return JBeamExpressionValue();
}

JBeamExpressionValue JBeamExpressionValue::Boolean(bool value)
{
    JBeamExpressionValue result;
    result.type = JBeamExpressionValueType::BOOLEAN;
    result.boolean_value = value;
    return result;
}

JBeamExpressionValue JBeamExpressionValue::Number(double value)
{
    JBeamExpressionValue result;
    result.type = JBeamExpressionValueType::NUMBER;
    result.number_value = value;
    return result;
}

JBeamExpressionValue JBeamExpressionValue::String(
    const std::string& value)
{
    JBeamExpressionValue result;
    result.type = JBeamExpressionValueType::STRING;
    result.string_value = value;
    return result;
}

JBeamExpressionLimits::JBeamExpressionLimits()
    : max_expression_bytes(65536U)
    , max_tokens(4096U)
    , max_depth(128U)
    , max_function_arguments(64U)
    , max_work_units(4194304U)
    , max_string_bytes(65536U)
    , max_output_string_bytes(65536U)
    , max_variables(4096U)
    , max_variable_name_bytes(256U)
    , max_environment_string_bytes(1048576U)
{
}

JBeamExpressionDiagnostic::JBeamExpressionDiagnostic()
    : code(JBeamExpressionDiagnosticCode::EXPECTED_VALUE)
    , byte_offset(0U)
{
}

JBeamExpressionResult::JBeamExpressionResult()
    : token_count(0U)
    , work_units(0U)
    , has_value(false)
{
}

bool JBeamExpressionResult::IsValid() const
{
    return has_value && diagnostics.empty();
}

JBeamExpressionResult EvaluateJBeamExpression(
    const std::string& expression,
    const JBeamExpressionEnvironment& environment,
    const JBeamExpressionLimits& limits)
{
    JBeamExpressionResult result;
    Runtime runtime(result, limits);
    if (expression.size() > limits.max_expression_bytes)
    {
        runtime.Fail(
            JBeamExpressionDiagnosticCode::EXPRESSION_SIZE_LIMIT,
            0U,
            "JBeam expression exceeds the configured byte limit");
        return result;
    }
    if (expression.size() < 2U ||
        expression[0] != '$' ||
        expression[1] != '=')
    {
        runtime.Fail(
            JBeamExpressionDiagnosticCode::MISSING_EXPRESSION_PREFIX,
            0U,
            "JBeam expression must begin with '$='");
        return result;
    }
    if (!ValidateEnvironment(environment, limits, runtime))
    {
        return result;
    }

    std::vector<Token> tokens;
    const std::size_t reserve_count =
        expression.size() < limits.max_tokens
            ? expression.size()
            : limits.max_tokens;
    tokens.reserve(reserve_count);
    Lexer lexer(expression, limits, runtime);
    if (!lexer.Run(tokens))
    {
        result.token_count = tokens.size();
        return result;
    }
    result.token_count = tokens.size();
    Parser parser(tokens, environment, limits, runtime);
    JBeamExpressionValue value;
    if (!parser.Run(value))
    {
        return result;
    }
    if (value.type == JBeamExpressionValueType::NUMBER)
    {
        if (!IsFiniteNumber(value.number_value))
        {
            runtime.Fail(
                JBeamExpressionDiagnosticCode::NON_FINITE_RESULT,
                0U,
                "JBeam expression result is not finite");
            return result;
        }
        value.number_value = NormalizeNumber(value.number_value);
    }
    if (value.type == JBeamExpressionValueType::STRING &&
        value.string_value.size() > limits.max_output_string_bytes)
    {
        runtime.Fail(
            JBeamExpressionDiagnosticCode::OUTPUT_STRING_SIZE_LIMIT,
            0U,
            "JBeam expression output string exceeds the configured limit");
        return result;
    }
    if (value.type == JBeamExpressionValueType::STRING &&
        !runtime.Charge(value.string_value.size(), 0U))
    {
        return result;
    }
    result.value = value;
    result.has_value = true;
    return result;
}

std::string SerializeCanonicalJBeamExpressionValue(
    const JBeamExpressionValue& value)
{
    switch (value.type)
    {
    case JBeamExpressionValueType::NIL_VALUE:
        return "jbeam-expression-value-v1:nil";
    case JBeamExpressionValueType::BOOLEAN:
        return value.boolean_value
            ? "jbeam-expression-value-v1:boolean:1"
            : "jbeam-expression-value-v1:boolean:0";
    case JBeamExpressionValueType::NUMBER:
    {
        if (!IsFiniteNumber(value.number_value))
        {
            return "jbeam-expression-value-v1:invalid";
        }
        std::uint64_t bits = DoubleBits(value.number_value);
        if ((bits & UINT64_C(0x7fffffffffffffff)) == 0U)
        {
            // Mask the sign in integer space. Under -fno-signed-zeros the
            // optimizer may legally preserve a floating-point -0 even when a
            // helper returns literal +0.
            bits = 0U;
        }
        static const char hex[] = "0123456789abcdef";
        std::string output = "jbeam-expression-value-v1:number:";
        for (unsigned int shift = 60U;; shift -= 4U)
        {
            output.push_back(
                hex[static_cast<std::size_t>((bits >> shift) & 0xfU)]);
            if (shift == 0U)
            {
                break;
            }
        }
        return output;
    }
    case JBeamExpressionValueType::STRING:
    {
        if (!IsValidUtf8(value.string_value))
        {
            return "jbeam-expression-value-v1:invalid";
        }
        std::string output = "jbeam-expression-value-v1:string:";
        AppendSize(value.string_value.size(), output);
        output.push_back(':');
        output.append(value.string_value);
        return output;
    }
    }
    return "jbeam-expression-value-v1:invalid";
}

const char* JBeamExpressionDiagnosticCodeToString(
    JBeamExpressionDiagnosticCode code)
{
    switch (code)
    {
    case JBeamExpressionDiagnosticCode::EXPRESSION_SIZE_LIMIT:
        return "expression-size-limit";
    case JBeamExpressionDiagnosticCode::MISSING_EXPRESSION_PREFIX:
        return "missing-expression-prefix";
    case JBeamExpressionDiagnosticCode::TOKEN_LIMIT:
        return "token-limit";
    case JBeamExpressionDiagnosticCode::DEPTH_LIMIT:
        return "depth-limit";
    case JBeamExpressionDiagnosticCode::WORK_LIMIT:
        return "work-limit";
    case JBeamExpressionDiagnosticCode::STRING_SIZE_LIMIT:
        return "string-size-limit";
    case JBeamExpressionDiagnosticCode::OUTPUT_STRING_SIZE_LIMIT:
        return "output-string-size-limit";
    case JBeamExpressionDiagnosticCode::ENVIRONMENT_LIMIT:
        return "environment-limit";
    case JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VARIABLE:
        return "invalid-environment-variable";
    case JBeamExpressionDiagnosticCode::INVALID_ENVIRONMENT_VALUE:
        return "invalid-environment-value";
    case JBeamExpressionDiagnosticCode::INVALID_CHARACTER:
        return "invalid-character";
    case JBeamExpressionDiagnosticCode::INVALID_UTF8:
        return "invalid-utf8";
    case JBeamExpressionDiagnosticCode::INVALID_NUMBER:
        return "invalid-number";
    case JBeamExpressionDiagnosticCode::NON_FINITE_NUMBER:
        return "non-finite-number";
    case JBeamExpressionDiagnosticCode::UNTERMINATED_STRING:
        return "unterminated-string";
    case JBeamExpressionDiagnosticCode::INVALID_ESCAPE:
        return "invalid-escape";
    case JBeamExpressionDiagnosticCode::EXPECTED_VALUE:
        return "expected-value";
    case JBeamExpressionDiagnosticCode::EXPECTED_TOKEN:
        return "expected-token";
    case JBeamExpressionDiagnosticCode::TRAILING_CONTENT:
        return "trailing-content";
    case JBeamExpressionDiagnosticCode::UNSUPPORTED_FUNCTION:
        return "unsupported-function";
    case JBeamExpressionDiagnosticCode::FUNCTION_ARITY:
        return "function-arity";
    case JBeamExpressionDiagnosticCode::FUNCTION_ARGUMENT_LIMIT:
        return "function-argument-limit";
    case JBeamExpressionDiagnosticCode::INVALID_FUNCTION_ARGUMENT:
        return "invalid-function-argument";
    case JBeamExpressionDiagnosticCode::UNSUPPORTED_CASE_SIGNATURE:
        return "unsupported-case-signature";
    case JBeamExpressionDiagnosticCode::TYPE_MISMATCH:
        return "type-mismatch";
    case JBeamExpressionDiagnosticCode::DIVISION_BY_ZERO:
        return "division-by-zero";
    case JBeamExpressionDiagnosticCode::NON_DETERMINISTIC_OPERAND:
        return "non-deterministic-operand";
    case JBeamExpressionDiagnosticCode::NON_FINITE_RESULT:
        return "non-finite-result";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
