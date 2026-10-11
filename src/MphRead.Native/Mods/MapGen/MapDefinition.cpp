#include "MapDefinition.hpp"
#include "../../NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/Charconv.hpp"

#include "CustomRooms.hpp"
#include "../../NativeRuntime/System/Json.hpp"
#include "MapBundle.hpp"
#include "MapTexturePack.hpp"
#include "../../Formats/Types.hpp"
#include "../Launcher/Portable/GameFiles.hpp"
#include "../../Program.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::AppendUtf8;
using ::MphRead::NativeRuntime::DecodeUtf8Scalar;
using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::FileReadAllText;
using ::MphRead::NativeRuntime::FileWriteAllText;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathGetDirectoryName;
using ::MphRead::NativeRuntime::PathGetFileName;
using ::MphRead::NativeRuntime::PathGetFullPath;
using ::MphRead::NativeRuntime::PathIsPathRooted;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::MphRead::NativeRuntime::Utf8Scalar;

namespace System::Text::Json
{
    using JsonException = ::System::Text::Json::JsonException;
}

namespace
{
    using namespace MphRead::Mods::MapGen;

    [[nodiscard]] std::uint16_t Read16(const std::uint8_t* bytes, bool little) noexcept
    {
        if (little)
        {
            return static_cast<std::uint16_t>(bytes[0]
                | (static_cast<std::uint16_t>(bytes[1]) << 8));
        }
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8)
            | bytes[1]);
    }

    [[nodiscard]] std::uint32_t Read32(const std::uint8_t* bytes, bool little) noexcept
    {
        if (little)
        {
            return static_cast<std::uint32_t>(bytes[0])
                | (static_cast<std::uint32_t>(bytes[1]) << 8)
                | (static_cast<std::uint32_t>(bytes[2]) << 16)
                | (static_cast<std::uint32_t>(bytes[3]) << 24);
        }
        return (static_cast<std::uint32_t>(bytes[0]) << 24)
            | (static_cast<std::uint32_t>(bytes[1]) << 16)
            | (static_cast<std::uint32_t>(bytes[2]) << 8)
            | static_cast<std::uint32_t>(bytes[3]);
    }

    enum class JsonKind
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    struct JsonObjectMember;

    struct JsonValue final
    {
        JsonKind Kind = JsonKind::Null;
        bool Boolean = false;
        std::string Text{};
        std::vector<JsonValue> Array{};
        std::vector<JsonObjectMember> Object;

        JsonValue();
        explicit JsonValue(JsonKind kind);
        ~JsonValue();
        JsonValue(const JsonValue&);
        JsonValue& operator=(const JsonValue&);
        JsonValue(JsonValue&&) noexcept;
        JsonValue& operator=(JsonValue&&) noexcept;
    };

    struct JsonObjectMember final
    {
        std::string Name;
        JsonValue Value;

        JsonObjectMember(std::string name, JsonValue value)
            : Name(std::move(name)), Value(std::move(value))
        {
        }
    };

    JsonValue::JsonValue() = default;
    JsonValue::JsonValue(JsonKind kind) : Kind(kind)
    {
    }
    JsonValue::~JsonValue() = default;
    JsonValue::JsonValue(const JsonValue&) = default;
    JsonValue& JsonValue::operator=(const JsonValue&) = default;
    JsonValue::JsonValue(JsonValue&&) noexcept = default;
    [[maybe_unused]] JsonValue& JsonValue::operator=(JsonValue&&) noexcept = default;

    class JsonParser final
    {
    public:
        explicit JsonParser(std::string_view text) noexcept : _text(text)
        {
        }

        [[nodiscard]] JsonValue Parse()
        {
            SkipTrivia();
            JsonValue result = ParseValue(0);
            SkipTrivia();
            if (_position != _text.size())
            {
                Fail("Additional text encountered after JSON value.");
            }
            return result;
        }

    private:
        [[noreturn]] void Fail(const char* message) const
        {
            throw System::Text::Json::JsonException(message);
        }

        void SkipTrivia()
        {
            for (;;)
            {
                while (_position < _text.size())
                {
                    const char ch = _text[_position];
                    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
                    {
                        break;
                    }
                    ++_position;
                }
                if (_position + 1 >= _text.size() || _text[_position] != '/')
                {
                    return;
                }
                if (_text[_position + 1] == '/')
                {
                    _position += 2;
                    while (_position < _text.size()
                        && _text[_position] != '\r' && _text[_position] != '\n')
                    {
                        ++_position;
                    }
                    continue;
                }
                if (_text[_position + 1] == '*')
                {
                    _position += 2;
                    bool closed = false;
                    while (_position + 1 < _text.size())
                    {
                        if (_text[_position] == '*' && _text[_position + 1] == '/')
                        {
                            _position += 2;
                            closed = true;
                            break;
                        }
                        ++_position;
                    }
                    if (!closed)
                    {
                        Fail("Unterminated JSON comment.");
                    }
                    continue;
                }
                return;
            }
        }

        [[nodiscard]] JsonValue ParseValue(std::size_t depth)
        {
            if (_position >= _text.size())
            {
                Fail("Expected a JSON value.");
            }
            switch (_text[_position])
            {
            case 'n':
                ExpectLiteral("null");
                return JsonValue{JsonKind::Null};
            case 't':
            {
                ExpectLiteral("true");
                JsonValue value;
                value.Kind = JsonKind::Boolean;
                value.Boolean = true;
                return value;
            }
            case 'f':
            {
                ExpectLiteral("false");
                JsonValue value;
                value.Kind = JsonKind::Boolean;
                value.Boolean = false;
                return value;
            }
            case '"':
            {
                JsonValue value;
                value.Kind = JsonKind::String;
                value.Text = ParseString();
                return value;
            }
            case '[':
                return ParseArray(depth);
            case '{':
                return ParseObject(depth);
            default:
                if (_text[_position] == '-' || (_text[_position] >= '0' && _text[_position] <= '9'))
                {
                    JsonValue value;
                    value.Kind = JsonKind::Number;
                    value.Text = ParseNumber();
                    return value;
                }
                Fail("Invalid JSON value.");
            }
        }

        void ExpectLiteral(std::string_view literal)
        {
            if (_text.substr(_position, literal.size()) != literal)
            {
                Fail("Invalid JSON literal.");
            }
            _position += literal.size();
        }

        [[nodiscard]] static std::uint32_t Hex(char ch)
        {
            if (ch >= '0' && ch <= '9')
            {
                return static_cast<std::uint32_t>(ch - '0');
            }
            if (ch >= 'a' && ch <= 'f')
            {
                return static_cast<std::uint32_t>(ch - 'a' + 10);
            }
            if (ch >= 'A' && ch <= 'F')
            {
                return static_cast<std::uint32_t>(ch - 'A' + 10);
            }
            throw System::Text::Json::JsonException("Invalid Unicode escape in JSON string.");
        }

        [[nodiscard]] std::uint16_t ParseHex4()
        {
            if (_position + 4 > _text.size())
            {
                Fail("Incomplete Unicode escape in JSON string.");
            }
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value = (value << 4) | Hex(_text[_position++]);
            }
            return static_cast<std::uint16_t>(value);
        }

        [[nodiscard]] std::string ParseString()
        {
            if (_text[_position] != '"')
            {
                Fail("Expected JSON string.");
            }
            ++_position;
            std::string result;
            while (_position < _text.size())
            {
                const unsigned char ch = static_cast<unsigned char>(_text[_position++]);
                if (ch == '"')
                {
                    return result;
                }
                if (ch < 0x20U)
                {
                    Fail("Unescaped control character in JSON string.");
                }
                if (ch != '\\')
                {
                    result.push_back(static_cast<char>(ch));
                    continue;
                }
                if (_position >= _text.size())
                {
                    Fail("Incomplete escape in JSON string.");
                }
                const char escaped = _text[_position++];
                switch (escaped)
                {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u':
                {
                    const std::uint16_t first = ParseHex4();
                    if (first >= 0xD800U && first <= 0xDBFFU)
                    {
                        if (_position + 2 > _text.size()
                            || _text[_position] != '\\' || _text[_position + 1] != 'u')
                        {
                            Fail("Incomplete surrogate pair in JSON string.");
                        }
                        _position += 2;
                        const std::uint16_t second = ParseHex4();
                        if (second < 0xDC00U || second > 0xDFFFU)
                        {
                            Fail("Invalid surrogate pair in JSON string.");
                        }
                        const std::uint32_t scalar = 0x10000U
                            + ((static_cast<std::uint32_t>(first) - 0xD800U) << 10)
                            + (static_cast<std::uint32_t>(second) - 0xDC00U);
                        AppendUtf8(result, scalar);
                    }
                    else if (first >= 0xDC00U && first <= 0xDFFFU)
                    {
                        Fail("Unexpected low surrogate in JSON string.");
                    }
                    else
                    {
                        AppendUtf8(result, first);
                    }
                    break;
                }
                default:
                    Fail("Invalid escape in JSON string.");
                }
            }
            Fail("Unterminated JSON string.");
        }

        [[nodiscard]] std::string ParseNumber()
        {
            const std::size_t start = _position;
            if (_text[_position] == '-')
            {
                ++_position;
                if (_position >= _text.size())
                {
                    Fail("Invalid JSON number.");
                }
            }
            if (_text[_position] == '0')
            {
                ++_position;
                if (_position < _text.size() && _text[_position] >= '0' && _text[_position] <= '9')
                {
                    Fail("Leading zero in JSON number.");
                }
            }
            else if (_text[_position] >= '1' && _text[_position] <= '9')
            {
                while (_position < _text.size()
                    && _text[_position] >= '0' && _text[_position] <= '9')
                {
                    ++_position;
                }
            }
            else
            {
                Fail("Invalid JSON number.");
            }
            if (_position < _text.size() && _text[_position] == '.')
            {
                ++_position;
                if (_position >= _text.size() || _text[_position] < '0' || _text[_position] > '9')
                {
                    Fail("Invalid JSON number fraction.");
                }
                while (_position < _text.size()
                    && _text[_position] >= '0' && _text[_position] <= '9')
                {
                    ++_position;
                }
            }
            if (_position < _text.size() && (_text[_position] == 'e' || _text[_position] == 'E'))
            {
                ++_position;
                if (_position < _text.size() && (_text[_position] == '+' || _text[_position] == '-'))
                {
                    ++_position;
                }
                if (_position >= _text.size() || _text[_position] < '0' || _text[_position] > '9')
                {
                    Fail("Invalid JSON number exponent.");
                }
                while (_position < _text.size()
                    && _text[_position] >= '0' && _text[_position] <= '9')
                {
                    ++_position;
                }
            }
            return std::string(_text.substr(start, _position - start));
        }

        [[nodiscard]] JsonValue ParseArray(std::size_t depth)
        {
            if (depth >= 64)
            {
                Fail("The maximum configured JSON depth of 64 has been exceeded.");
            }
            JsonValue value;
            value.Kind = JsonKind::Array;
            ++_position;
            SkipTrivia();
            if (_position < _text.size() && _text[_position] == ']')
            {
                ++_position;
                return value;
            }
            for (;;)
            {
                SkipTrivia();
                value.Array.push_back(ParseValue(depth + 1));
                SkipTrivia();
                if (_position >= _text.size())
                {
                    Fail("Unterminated JSON array.");
                }
                if (_text[_position] == ']')
                {
                    ++_position;
                    return value;
                }
                if (_text[_position] != ',')
                {
                    Fail("Expected comma in JSON array.");
                }
                ++_position;
                SkipTrivia();
                if (_position < _text.size() && _text[_position] == ']')
                {
                    ++_position;
                    return value;
                }
            }
        }

        [[nodiscard]] JsonValue ParseObject(std::size_t depth)
        {
            if (depth >= 64)
            {
                Fail("The maximum configured JSON depth of 64 has been exceeded.");
            }
            JsonValue value;
            value.Kind = JsonKind::Object;
            ++_position;
            SkipTrivia();
            if (_position < _text.size() && _text[_position] == '}')
            {
                ++_position;
                return value;
            }
            for (;;)
            {
                SkipTrivia();
                if (_position >= _text.size() || _text[_position] != '"')
                {
                    Fail("Expected property name in JSON object.");
                }
                std::string key = ParseString();
                SkipTrivia();
                if (_position >= _text.size() || _text[_position] != ':')
                {
                    Fail("Expected colon after JSON property name.");
                }
                ++_position;
                SkipTrivia();
                value.Object.emplace_back(std::move(key), ParseValue(depth + 1));
                SkipTrivia();
                if (_position >= _text.size())
                {
                    Fail("Unterminated JSON object.");
                }
                if (_text[_position] == '}')
                {
                    ++_position;
                    return value;
                }
                if (_text[_position] != ',')
                {
                    Fail("Expected comma in JSON object.");
                }
                ++_position;
                SkipTrivia();
                if (_position < _text.size() && _text[_position] == '}')
                {
                    ++_position;
                    return value;
                }
            }
        }

        std::string_view _text;
        std::size_t _position = 0;
    };

    [[noreturn]] void ConversionError()
    {
        throw System::Text::Json::JsonException(
            "The JSON value could not be converted to the target type.");
    }

    [[nodiscard]] const std::string& JsonString(const JsonValue& value)
    {
        if (value.Kind != JsonKind::String)
        {
            ConversionError();
        }
        return value.Text;
    }

    [[nodiscard]] bool JsonBool(const JsonValue& value)
    {
        if (value.Kind != JsonKind::Boolean)
        {
            ConversionError();
        }
        return value.Boolean;
    }

    [[nodiscard]] bool IntegerToken(const std::string& text) noexcept
    {
        return text.find_first_of(".eE") == std::string::npos;
    }

    template <typename T>
    [[nodiscard]] T SignedInteger(const JsonValue& value)
    {
        if (value.Kind != JsonKind::Number || !IntegerToken(value.Text))
        {
            ConversionError();
        }
        std::int64_t parsed = 0;
        const auto result = std::from_chars(value.Text.data(), value.Text.data() + value.Text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != value.Text.data() + value.Text.size()
            || parsed < static_cast<std::int64_t>(std::numeric_limits<T>::min())
            || parsed > static_cast<std::int64_t>(std::numeric_limits<T>::max()))
        {
            ConversionError();
        }
        return static_cast<T>(parsed);
    }

    template <typename T>
    [[nodiscard]] T UnsignedInteger(const JsonValue& value)
    {
        if (value.Kind != JsonKind::Number || !IntegerToken(value.Text)
            || (!value.Text.empty() && value.Text.front() == '-'))
        {
            ConversionError();
        }
        std::uint64_t parsed = 0;
        const auto result = std::from_chars(value.Text.data(), value.Text.data() + value.Text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != value.Text.data() + value.Text.size()
            || parsed > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
        {
            ConversionError();
        }
        return static_cast<T>(parsed);
    }

    [[nodiscard]] float JsonFloat(const JsonValue& value)
    {
        // Utf8JsonReader.TryGetSingle: the number as float.Parse reads it, and
        // a value that comes out infinite -- too large for a float -- is not
        // one.
        float parsed = 0.0F;
        if (value.Kind != JsonKind::Number
            || !::MphRead::NativeRuntime::SingleTryParseInvariant(value.Text, parsed)
            || !std::isfinite(parsed))
        {
            ConversionError();
        }
        return parsed;
    }
    template <typename T, typename Converter>
    [[nodiscard]] std::shared_ptr<std::vector<T>> JsonArray(
        const JsonValue& value, Converter&& converter)
    {
        if (value.Kind == JsonKind::Null)
        {
            return {};
        }
        if (value.Kind != JsonKind::Array)
        {
            ConversionError();
        }
        auto result = std::make_shared<std::vector<T>>();
        result->reserve(value.Array.size());
        for (const JsonValue& item : value.Array)
        {
            result->push_back(converter(item));
        }
        return result;
    }

    void AppendHex4(std::string& output, std::uint16_t value)
    {
        static constexpr char HexDigits[] = "0123456789ABCDEF";
        output += "\\u";
        output.push_back(HexDigits[(value >> 12) & 0xFU]);
        output.push_back(HexDigits[(value >> 8) & 0xFU]);
        output.push_back(HexDigits[(value >> 4) & 0xFU]);
        output.push_back(HexDigits[value & 0xFU]);
    }

    void WriteJsonString(std::string& output, std::string_view value)
    {
        output.push_back('"');
        for (std::size_t position = 0; position < value.size();)
        {
            const Utf8Scalar decoded = DecodeUtf8Scalar(value, position);
            const std::uint32_t scalar = decoded.Value;
            position += decoded.Length;
            switch (scalar)
            {
            case '\\': output += "\\\\"; continue;
            case '\b': output += "\\b"; continue;
            case '\f': output += "\\f"; continue;
            case '\n': output += "\\n"; continue;
            case '\r': output += "\\r"; continue;
            case '\t': output += "\\t"; continue;
            default: break;
            }
            if (scalar < 0x20U || scalar == '"' || scalar == '\'' || scalar == '&'
                || scalar == '+' || scalar == '<' || scalar == '>' || scalar == '`')
            {
                AppendHex4(output, static_cast<std::uint16_t>(scalar));
            }
            else if (scalar <= 0x7EU)
            {
                output.push_back(static_cast<char>(scalar));
            }
            else if (scalar <= 0xFFFFU)
            {
                AppendHex4(output, static_cast<std::uint16_t>(scalar));
            }
            else
            {
                const std::uint32_t value32 = scalar - 0x10000U;
                AppendHex4(output, static_cast<std::uint16_t>(0xD800U + (value32 >> 10)));
                AppendHex4(output, static_cast<std::uint16_t>(0xDC00U + (value32 & 0x3FFU)));
            }
        }
        output.push_back('"');
    }

    void AppendNewLine(std::string& output)
    {
#ifdef _WIN32
        output += "\r\n";
#else
        output.push_back('\n');
#endif
    }

    void Indent(std::string& output, int depth)
    {
        output.append(static_cast<std::size_t>(depth) * 2U, ' ');
    }

    // A dictionary's key, written as it is: DictionaryKeyPolicy is not set.
    template <typename Writer>
    void DictionaryProperty(std::string& output, int depth, bool& first, std::string_view name, Writer&& writer)
    {
        if (!first)
        {
            output.push_back(',');
        }
        AppendNewLine(output);
        Indent(output, depth + 1);
        WriteJsonString(output, name);
        output += ": ";
        writer();
        first = false;
    }

    // A property, named by PropertyNamingPolicy = JsonNamingPolicy.CamelCase:
    // every recipe in the repository is camelCase.
    template <typename Writer>
    void Property(std::string& output, int depth, bool& first, std::string_view name, Writer&& writer)
    {
        DictionaryProperty(output, depth, first, ::MphRead::NativeRuntime::JsonNamingPolicyCamelCase(name),
            std::forward<Writer>(writer));
    }

    template <typename T, typename Writer>
    void WriteArray(std::string& output, int depth, const std::vector<T>& values, Writer&& writer)
    {
        output.push_back('[');
        if (!values.empty())
        {
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i != 0)
                {
                    output.push_back(',');
                }
                AppendNewLine(output);
                Indent(output, depth + 1);
                writer(values[i], depth + 1);
            }
            AppendNewLine(output);
            Indent(output, depth);
        }
        output.push_back(']');
    }

    void WriteInteger(std::string& output, std::int64_t value)
    {
        output += ::MphRead::NativeRuntime::ToStringInvariant(value);
    }
    void WriteUnsigned(std::string& output, std::uint64_t value)
    {
        output += ::MphRead::NativeRuntime::ToStringInvariant(value);
    }
    void WriteFloat(std::string& output, float value)
    {
        // Utf8JsonWriter: the shortest invariant text, and no NaN or infinity.
        if (!std::isfinite(value))
        {
            throw System::ArgumentException();
        }
        output += ::MphRead::NativeRuntime::ToStringInvariant(value);
    }
}

namespace MphRead::Mods::MapGen
{
    class MapDefinitionJson final
    {
    public:
        [[nodiscard]] static std::shared_ptr<MapDefinition> Deserialize(const std::string& text)
        {
            const JsonValue root = JsonParser(text).Parse();
            if (root.Kind == JsonKind::Null)
            {
                return {};
            }
            if (root.Kind != JsonKind::Object)
            {
                ConversionError();
            }
            auto result = std::make_shared<MapDefinition>();
            ReadMapDefinition(root, *result);
            return result;
        }

        [[nodiscard]] static std::string Serialize(const MapDefinition& value)
        {
            std::string output;
            WriteMapDefinition(output, 0, value);
            return output;
        }

    private:
        template <typename T, typename Reader>
        [[nodiscard]] static std::shared_ptr<T> ReadObject(const JsonValue& value, Reader&& reader)
        {
            if (value.Kind == JsonKind::Null)
            {
                return {};
            }
            if (value.Kind != JsonKind::Object)
            {
                ConversionError();
            }
            auto result = std::make_shared<T>();
            reader(value, *result);
            return result;
        }

        template <typename T, typename Reader>
        [[nodiscard]] static std::shared_ptr<std::vector<std::shared_ptr<T>>> ReadObjectList(
            const JsonValue& value, Reader&& reader)
        {
            if (value.Kind == JsonKind::Null)
            {
                return {};
            }
            if (value.Kind != JsonKind::Array)
            {
                ConversionError();
            }
            auto result = std::make_shared<std::vector<std::shared_ptr<T>>>();
            result->reserve(value.Array.size());
            for (const JsonValue& item : value.Array)
            {
                result->push_back(ReadObject<T>(item, reader));
            }
            return result;
        }

        static void ReadMapDefinition(const JsonValue& value, MapDefinition& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Name"))
                {
                    result._name = item.Kind == JsonKind::Null
                        ? std::shared_ptr<std::string>{}
                        : std::make_shared<std::string>(JsonString(item));
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "InGameName"))
                {
                    result._inGameName = item.Kind == JsonKind::Null
                        ? std::optional<std::string>{}
                        : std::optional<std::string>{JsonString(item)};
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "TextureSource"))
                {
                    result._textureSource = item.Kind == JsonKind::Null
                        ? std::shared_ptr<std::string>{}
                        : std::make_shared<std::string>(JsonString(item));
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "ScaleFactor"))
                {
                    result._scaleFactor = SignedInteger<std::int32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "KillHeight"))
                {
                    result._killHeight = JsonFloat(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "FarClip"))
                {
                    result._farClip = JsonFloat(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "FogEnabled"))
                {
                    result._fogEnabled = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "FogColor"))
                {
                    result._fogColor = JsonArray<std::int32_t>(item,
                        [](const JsonValue& element) { return SignedInteger<std::int32_t>(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "FogSlope"))
                {
                    result._fogSlope = SignedInteger<std::int32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "FogOffset"))
                {
                    result._fogOffset = SignedInteger<std::int32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Light1Color"))
                {
                    result._light1Color = JsonArray<std::int32_t>(item,
                        [](const JsonValue& element) { return SignedInteger<std::int32_t>(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Light1Vector"))
                {
                    result._light1Vector = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Light2Color"))
                {
                    result._light2Color = JsonArray<std::int32_t>(item,
                        [](const JsonValue& element) { return SignedInteger<std::int32_t>(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Light2Vector"))
                {
                    result._light2Vector = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "BattleTimeLimit"))
                {
                    result._battleTimeLimit = UnsignedInteger<std::uint32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "PointLimit"))
                {
                    result._pointLimit = SignedInteger<std::int16_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Import"))
                {
                    result._import = ReadObject<MapImport>(item, ReadMapImport);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Collision"))
                {
                    result._collision = ReadObject<MapCollision>(item, ReadMapCollision);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Preview"))
                {
                    result._preview = ReadObject<MapPreview>(item, ReadMapPreview);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Materials"))
                {
                    result._materials = ReadObjectList<MapMaterial>(item, ReadMapMaterial);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Brushes"))
                {
                    result._brushes = ReadObjectList<MapBrush>(item, ReadMapBrush);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Spawns"))
                {
                    result._spawns = ReadObjectList<MapSpawn>(item, ReadMapSpawn);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "JumpPads"))
                {
                    result._jumpPads = ReadObjectList<MapJumpPad>(item, ReadMapJumpPad);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Items"))
                {
                    result._items = ReadObjectList<MapItem>(item, ReadMapItem);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Teleporters"))
                {
                    result._teleporters = ReadObjectList<MapTeleporter>(item, ReadMapTeleporter);
                }
                // JsonIgnore properties and unknown properties are ignored.
            }
        }

        static void ReadMapCollision(const JsonValue& value, MapCollision& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Source"))
                {
                    result.Source = item.Kind == JsonKind::Null
                        ? std::optional<std::string>{}
                        : std::optional<std::string>{JsonString(item)};
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "ZUp"))
                {
                    result.ZUp = JsonBool(item);
                }
                // BaseDirectory and BundlePath are JsonIgnore.
            }
        }

        static void ReadMapPreview(const JsonValue& value, MapPreview& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Position"))
                {
                    result._position = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Target"))
                {
                    result._target = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
            }
        }

        static void ReadMapImport(const JsonValue& value, MapImport& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Source"))
                {
                    result._source = item.Kind == JsonKind::Null
                        ? std::shared_ptr<std::string>{}
                        : std::make_shared<std::string>(JsonString(item));
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "MapName"))
                {
                    result._mapName = item.Kind == JsonKind::Null
                        ? std::optional<std::string>{}
                        : std::optional<std::string>{JsonString(item)};
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "UnitsPerUnit"))
                {
                    result._unitsPerUnit = JsonFloat(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Textures"))
                {
                    result._textures = item.Kind == JsonKind::Null
                        ? std::optional<std::string>{}
                        : std::optional<std::string>{JsonString(item)};
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "ShaderMaterials"))
                {
                    if (item.Kind == JsonKind::Null)
                    {
                        result._shaderMaterials.reset();
                    }
                    else
                    {
                        if (item.Kind != JsonKind::Object)
                        {
                            ConversionError();
                        }
                        auto dictionary = std::make_shared<MapImport::ShaderMaterialDictionary>();
                        for (const auto& [key, dictionaryValue] : item.Object)
                        {
                            (*dictionary)[key] = SignedInteger<std::int32_t>(dictionaryValue);
                        }
                        result._shaderMaterials = std::move(dictionary);
                    }
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "DefaultMaterial"))
                {
                    result._defaultMaterial = SignedInteger<std::int32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "TexScale"))
                {
                    result._texScale = JsonFloat(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "KeepSky"))
                {
                    result._keepSky = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "KeepClip"))
                {
                    result._keepClip = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "PatchLevel"))
                {
                    result._patchLevel = SignedInteger<std::int32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "KeepSpawns"))
                {
                    result._keepSpawns = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "KeepItems"))
                {
                    result._keepItems = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "TexturePaks"))
                {
                    const auto paks = JsonArray<std::string>(item,
                        [](const JsonValue& element) { return JsonString(element); });
                    result._texturePaks = paks ? *paks : std::vector<std::string>{};
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "TextureAliases"))
                {
                    result._textureAliases.clear();
                    if (item.Kind != JsonKind::Null)
                    {
                        if (item.Kind != JsonKind::Object)
                        {
                            ConversionError();
                        }
                        for (const auto& [key, aliasValue] : item.Object)
                        {
                            result._textureAliases.emplace_back(key, JsonString(aliasValue));
                        }
                    }
                }
                // BaseDirectory and BundlePath are JsonIgnore.
            }
        }

        static void ReadMapMaterial(const JsonValue& value, MapMaterial& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Name"))
                {
                    result._name = item.Kind == JsonKind::Null
                        ? std::shared_ptr<std::string>{}
                        : std::make_shared<std::string>(JsonString(item));
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "SourceMaterial"))
                {
                    result._sourceMaterial = SignedInteger<std::int32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "TexScale"))
                {
                    result._texScale = JsonFloat(item);
                }
            }
        }

        static void ReadMapBrush(const JsonValue& value, MapBrush& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Min"))
                {
                    result._min = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Max"))
                {
                    result._max = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Material"))
                {
                    result._material = SignedInteger<std::int32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Shade"))
                {
                    result._shade = JsonFloat(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Solid"))
                {
                    result._solid = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Damaging"))
                {
                    result._damaging = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "NoBeams"))
                {
                    result._noBeams = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Visible"))
                {
                    result._visible = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Terrain"))
                {
                    result._terrain = item.Kind == JsonKind::Null
                        ? std::optional<std::string>{}
                        : std::optional<std::string>{JsonString(item)};
                }
            }
        }

        static void ReadMapSpawn(const JsonValue& value, MapSpawn& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Position"))
                {
                    result._position = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Yaw"))
                {
                    result._yaw = JsonFloat(item);
                }
            }
        }

        static void ReadMapJumpPad(const JsonValue& value, MapJumpPad& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Position"))
                {
                    result._position = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Target"))
                {
                    result._target = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Vector"))
                {
                    result._vector = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Speed"))
                {
                    result._speed = JsonFloat(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Size"))
                {
                    result._size = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "ModelId"))
                {
                    result._modelId = UnsignedInteger<std::uint32_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "CooldownTime"))
                {
                    result._cooldownTime = UnsignedInteger<std::uint16_t>(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "ControlLockTime"))
                {
                    result._controlLockTime = UnsignedInteger<std::uint16_t>(item);
                }
            }
        }

        static void ReadMapTeleporter(const JsonValue& value, MapTeleporter& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Position"))
                {
                    result._position = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Target"))
                {
                    result._target = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Yaw"))
                {
                    result._yaw = JsonFloat(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Visible"))
                {
                    result._visible = JsonBool(item);
                }
            }
        }

        static void ReadMapItem(const JsonValue& value, MapItem& result)
        {
            for (const auto& [name, item] : value.Object)
            {
                if (StringEqualsOrdinalIgnoreCase(name, "Position"))
                {
                    result._position = JsonArray<float>(item,
                        [](const JsonValue& element) { return JsonFloat(element); });
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "Type"))
                {
                    result._type = item.Kind == JsonKind::Null
                        ? std::shared_ptr<std::string>{}
                        : std::make_shared<std::string>(JsonString(item));
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "HasBase"))
                {
                    result._hasBase = JsonBool(item);
                }
                else if (StringEqualsOrdinalIgnoreCase(name, "SpawnInterval"))
                {
                    result._spawnInterval = UnsignedInteger<std::uint16_t>(item);
                }
            }
        }

        static void WriteMapDefinition(std::string& output, int depth, const MapDefinition& value)
        {
            output.push_back('{');
            bool first = true;
            if (value._name)
            {
                Property(output, depth, first, "Name", [&] { WriteJsonString(output, *value._name); });
            }
            if (value._inGameName)
            {
                Property(output, depth, first, "InGameName", [&] { WriteJsonString(output, *value._inGameName); });
            }
            if (value._textureSource)
            {
                Property(output, depth, first, "TextureSource", [&] { WriteJsonString(output, *value._textureSource); });
            }
            Property(output, depth, first, "ScaleFactor", [&] { WriteInteger(output, value._scaleFactor); });
            Property(output, depth, first, "KillHeight", [&] { WriteFloat(output, value._killHeight); });
            Property(output, depth, first, "FarClip", [&] { WriteFloat(output, value._farClip); });
            Property(output, depth, first, "FogEnabled", [&] { output += value._fogEnabled ? "true" : "false"; });
            if (value._fogColor)
            {
                Property(output, depth, first, "FogColor", [&]
                {
                    WriteArray(output, depth + 1, *value._fogColor,
                        [&](std::int32_t item, int) { WriteInteger(output, item); });
                });
            }
            Property(output, depth, first, "FogSlope", [&] { WriteInteger(output, value._fogSlope); });
            Property(output, depth, first, "FogOffset", [&] { WriteInteger(output, value._fogOffset); });
            if (value._light1Color)
            {
                Property(output, depth, first, "Light1Color", [&]
                {
                    WriteArray(output, depth + 1, *value._light1Color,
                        [&](std::int32_t item, int) { WriteInteger(output, item); });
                });
            }
            if (value._light1Vector)
            {
                Property(output, depth, first, "Light1Vector", [&]
                {
                    WriteArray(output, depth + 1, *value._light1Vector,
                        [&](float item, int) { WriteFloat(output, item); });
                });
            }
            if (value._light2Color)
            {
                Property(output, depth, first, "Light2Color", [&]
                {
                    WriteArray(output, depth + 1, *value._light2Color,
                        [&](std::int32_t item, int) { WriteInteger(output, item); });
                });
            }
            if (value._light2Vector)
            {
                Property(output, depth, first, "Light2Vector", [&]
                {
                    WriteArray(output, depth + 1, *value._light2Vector,
                        [&](float item, int) { WriteFloat(output, item); });
                });
            }
            Property(output, depth, first, "BattleTimeLimit", [&] { WriteUnsigned(output, value._battleTimeLimit); });
            Property(output, depth, first, "PointLimit", [&] { WriteInteger(output, value._pointLimit); });
            if (value._import)
            {
                Property(output, depth, first, "Import", [&] { WriteMapImport(output, depth + 1, *value._import); });
            }
            if (value._collision)
            {
                Property(output, depth, first, "Collision", [&]
                {
                    output.push_back('{');
                    bool collisionFirst = true;
                    if (value._collision->Source)
                    {
                        Property(output, depth + 1, collisionFirst, "Source", [&]
                        {
                            WriteJsonString(output, *value._collision->Source);
                        });
                    }
                    Property(output, depth + 1, collisionFirst, "ZUp", [&] { output += value._collision->ZUp ? "true" : "false"; });
                    FinishObject(output, depth + 1, collisionFirst);
                });
            }
            if (value._preview)
            {
                Property(output, depth, first, "Preview", [&] { WriteMapPreview(output, depth + 1, *value._preview); });
            }
            WriteObjectListProperty(output, depth, first, "Materials", value._materials, WriteMapMaterial);
            WriteObjectListProperty(output, depth, first, "Brushes", value._brushes, WriteMapBrush);
            WriteObjectListProperty(output, depth, first, "Spawns", value._spawns, WriteMapSpawn);
            WriteObjectListProperty(output, depth, first, "JumpPads", value._jumpPads, WriteMapJumpPad);
            WriteObjectListProperty(output, depth, first, "Items", value._items, WriteMapItem);
            if (value._teleporters && !value._teleporters->empty())
            {
                WriteObjectListProperty(output, depth, first, "Teleporters", value._teleporters, WriteMapTeleporter);
            }
            if (!first)
            {
                AppendNewLine(output);
                Indent(output, depth);
            }
            output.push_back('}');
        }

        template <typename T, typename Writer>
        static void WriteObjectListProperty(
            std::string& output, int depth, bool& first, std::string_view name,
            const std::shared_ptr<std::vector<std::shared_ptr<T>>>& list, Writer writer)
        {
            if (!list)
            {
                return;
            }
            Property(output, depth, first, name, [&]
            {
                WriteArray(output, depth + 1, *list,
                    [&](const std::shared_ptr<T>& item, int itemDepth)
                    {
                        if (!item)
                        {
                            output += "null";
                        }
                        else
                        {
                            writer(output, itemDepth, *item);
                        }
                    });
            });
        }

        static void WriteMapPreview(std::string& output, int depth, const MapPreview& value)
        {
            output.push_back('{');
            bool first = true;
            WriteFloatArrayProperty(output, depth, first, "Position", value._position);
            WriteFloatArrayProperty(output, depth, first, "Target", value._target);
            FinishObject(output, depth, first);
        }

        static void WriteMapImport(std::string& output, int depth, const MapImport& value)
        {
            output.push_back('{');
            bool first = true;
            if (value._source)
            {
                Property(output, depth, first, "Source", [&] { WriteJsonString(output, *value._source); });
            }
            if (value._mapName)
            {
                Property(output, depth, first, "MapName", [&] { WriteJsonString(output, *value._mapName); });
            }
            Property(output, depth, first, "UnitsPerUnit", [&] { WriteFloat(output, value._unitsPerUnit); });
            if (value._textures)
            {
                Property(output, depth, first, "Textures", [&] { WriteJsonString(output, *value._textures); });
            }
            if (value._shaderMaterials)
            {
                Property(output, depth, first, "ShaderMaterials", [&]
                {
                    output.push_back('{');
                    bool dictionaryFirst = true;
                    for (const auto& [key, dictionaryValue] : *value._shaderMaterials)
                    {
                        DictionaryProperty(output, depth + 1, dictionaryFirst, key,
                            [&] { WriteInteger(output, dictionaryValue); });
                    }
                    FinishObject(output, depth + 1, dictionaryFirst);
                });
            }
            Property(output, depth, first, "DefaultMaterial", [&] { WriteInteger(output, value._defaultMaterial); });
            Property(output, depth, first, "TexScale", [&] { WriteFloat(output, value._texScale); });
            Property(output, depth, first, "KeepSky", [&] { output += value._keepSky ? "true" : "false"; });
            Property(output, depth, first, "KeepClip", [&] { output += value._keepClip ? "true" : "false"; });
            Property(output, depth, first, "PatchLevel", [&] { WriteInteger(output, value._patchLevel); });
            Property(output, depth, first, "KeepSpawns", [&] { output += value._keepSpawns ? "true" : "false"; });
            Property(output, depth, first, "KeepItems", [&] { output += value._keepItems ? "true" : "false"; });
            if (!value._texturePaks.empty())
            {
                Property(output, depth, first, "TexturePaks", [&]
                {
                    WriteArray(output, depth + 1, value._texturePaks,
                        [&](const std::string& pak, int) { WriteJsonString(output, pak); });
                });
            }
            if (!value._textureAliases.empty())
            {
                Property(output, depth, first, "TextureAliases", [&]
                {
                    output.push_back('{');
                    bool dictionaryFirst = true;
                    for (const auto& [key, alias] : value._textureAliases)
                    {
                        DictionaryProperty(output, depth + 1, dictionaryFirst, key,
                            [&] { WriteJsonString(output, alias); });
                    }
                    FinishObject(output, depth + 1, dictionaryFirst);
                });
            }
            FinishObject(output, depth, first);
        }

        static void WriteMapMaterial(std::string& output, int depth, const MapMaterial& value)
        {
            output.push_back('{');
            bool first = true;
            if (value._name)
            {
                Property(output, depth, first, "Name", [&] { WriteJsonString(output, *value._name); });
            }
            Property(output, depth, first, "SourceMaterial", [&] { WriteInteger(output, value._sourceMaterial); });
            Property(output, depth, first, "TexScale", [&] { WriteFloat(output, value._texScale); });
            FinishObject(output, depth, first);
        }

        static void WriteMapBrush(std::string& output, int depth, const MapBrush& value)
        {
            output.push_back('{');
            bool first = true;
            WriteFloatArrayProperty(output, depth, first, "Min", value._min);
            WriteFloatArrayProperty(output, depth, first, "Max", value._max);
            Property(output, depth, first, "Material", [&] { WriteInteger(output, value._material); });
            Property(output, depth, first, "Shade", [&] { WriteFloat(output, value._shade); });
            Property(output, depth, first, "Solid", [&] { output += value._solid ? "true" : "false"; });
            Property(output, depth, first, "Damaging", [&] { output += value._damaging ? "true" : "false"; });
            if (value._noBeams)
            {
                Property(output, depth, first, "NoBeams", [&] { output += "true"; });
            }
            if (!value._visible)
            {
                Property(output, depth, first, "Visible", [&] { output += "false"; });
            }
            if (value._terrain)
            {
                Property(output, depth, first, "Terrain", [&] { WriteJsonString(output, *value._terrain); });
            }
            FinishObject(output, depth, first);
        }

        static void WriteMapSpawn(std::string& output, int depth, const MapSpawn& value)
        {
            output.push_back('{');
            bool first = true;
            WriteFloatArrayProperty(output, depth, first, "Position", value._position);
            Property(output, depth, first, "Yaw", [&] { WriteFloat(output, value._yaw); });
            FinishObject(output, depth, first);
        }

        static void WriteMapJumpPad(std::string& output, int depth, const MapJumpPad& value)
        {
            output.push_back('{');
            bool first = true;
            WriteFloatArrayProperty(output, depth, first, "Position", value._position);
            WriteFloatArrayProperty(output, depth, first, "Target", value._target);
            WriteFloatArrayProperty(output, depth, first, "Vector", value._vector);
            Property(output, depth, first, "Speed", [&] { WriteFloat(output, value._speed); });
            WriteFloatArrayProperty(output, depth, first, "Size", value._size);
            Property(output, depth, first, "ModelId", [&] { WriteUnsigned(output, value._modelId); });
            Property(output, depth, first, "CooldownTime", [&] { WriteUnsigned(output, value._cooldownTime); });
            Property(output, depth, first, "ControlLockTime", [&] { WriteUnsigned(output, value._controlLockTime); });
            FinishObject(output, depth, first);
        }

        static void WriteMapTeleporter(std::string& output, int depth, const MapTeleporter& value)
        {
            output.push_back('{');
            bool first = true;
            WriteFloatArrayProperty(output, depth, first, "Position", value._position);
            WriteFloatArrayProperty(output, depth, first, "Target", value._target);
            Property(output, depth, first, "Yaw", [&] { WriteFloat(output, value._yaw); });
            Property(output, depth, first, "Visible", [&] { output += value._visible ? "true" : "false"; });
            FinishObject(output, depth, first);
        }

        static void WriteMapItem(std::string& output, int depth, const MapItem& value)
        {
            output.push_back('{');
            bool first = true;
            WriteFloatArrayProperty(output, depth, first, "Position", value._position);
            if (value._type)
            {
                Property(output, depth, first, "Type", [&] { WriteJsonString(output, *value._type); });
            }
            Property(output, depth, first, "HasBase", [&] { output += value._hasBase ? "true" : "false"; });
            Property(output, depth, first, "SpawnInterval", [&] { WriteUnsigned(output, value._spawnInterval); });
            FinishObject(output, depth, first);
        }

        static void WriteFloatArrayProperty(
            std::string& output, int depth, bool& first, std::string_view name,
            const std::shared_ptr<std::vector<float>>& values)
        {
            if (!values)
            {
                return;
            }
            Property(output, depth, first, name, [&]
            {
                WriteArray(output, depth + 1, *values,
                    [&](float item, int) { WriteFloat(output, item); });
            });
        }

        static void FinishObject(std::string& output, int depth, bool first)
        {
            if (!first)
            {
                AppendNewLine(output);
                Indent(output, depth);
            }
            output.push_back('}');
        }
    };

    MapDefinition::MapDefinition()
        : _name(std::make_shared<std::string>("CUSTOM")),
          _textureSource(std::make_shared<std::string>("MP3 PROVING GROUND")),
          _fogColor(std::make_shared<std::vector<std::int32_t>>(
              std::initializer_list<std::int32_t>{8, 10, 16})),
          _light1Color(std::make_shared<std::vector<std::int32_t>>(
              std::initializer_list<std::int32_t>{31, 28, 24})),
          _light1Vector(std::make_shared<std::vector<float>>(
              std::initializer_list<float>{0.3F, -1.0F, 0.2F})),
          _light2Color(std::make_shared<std::vector<std::int32_t>>(
              std::initializer_list<std::int32_t>{10, 11, 16})),
          _light2Vector(std::make_shared<std::vector<float>>(
              std::initializer_list<float>{-0.3F, 1.0F, -0.2F})),
          _materials(std::make_shared<MaterialList>()),
          _brushes(std::make_shared<BrushList>()),
          _spawns(std::make_shared<SpawnList>()),
          _jumpPads(std::make_shared<JumpPadList>()),
          _items(std::make_shared<ItemList>()),
          _teleporters(std::make_shared<TeleporterList>())
    {
    }

    const std::string& MapDefinition::Name() const { return RequireReference(_name); }
    void MapDefinition::Name(std::string value) { _name = std::make_shared<std::string>(std::move(value)); }
    void MapDefinition::Name(std::nullptr_t) noexcept { _name.reset(); }
    const std::optional<std::string>& MapDefinition::InGameName() const noexcept { return _inGameName; }
    void MapDefinition::InGameName(std::optional<std::string> value) noexcept { _inGameName = std::move(value); }
    const std::string& MapDefinition::TextureSource() const { return RequireReference(_textureSource); }
    void MapDefinition::TextureSource(std::string value) { _textureSource = std::make_shared<std::string>(std::move(value)); }
    void MapDefinition::TextureSource(std::nullptr_t) noexcept { _textureSource.reset(); }
    std::int32_t MapDefinition::ScaleFactor() const noexcept { return _scaleFactor; }
    void MapDefinition::ScaleFactor(std::int32_t value) noexcept { _scaleFactor = value; }
    float MapDefinition::KillHeight() const noexcept { return _killHeight; }
    void MapDefinition::KillHeight(float value) noexcept { _killHeight = value; }
    float MapDefinition::FarClip() const noexcept { return _farClip; }
    void MapDefinition::FarClip(float value) noexcept { _farClip = value; }
    bool MapDefinition::FogEnabled() const noexcept { return _fogEnabled; }
    void MapDefinition::FogEnabled(bool value) noexcept { _fogEnabled = value; }
    std::vector<std::int32_t>* MapDefinition::FogColor() noexcept { return _fogColor.get(); }
    const std::vector<std::int32_t>* MapDefinition::FogColor() const noexcept { return _fogColor.get(); }
    void MapDefinition::FogColor(std::shared_ptr<std::vector<std::int32_t>> value) noexcept { _fogColor = std::move(value); }
    std::int32_t MapDefinition::FogSlope() const noexcept { return _fogSlope; }
    void MapDefinition::FogSlope(std::int32_t value) noexcept { _fogSlope = value; }
    std::int32_t MapDefinition::FogOffset() const noexcept { return _fogOffset; }
    void MapDefinition::FogOffset(std::int32_t value) noexcept { _fogOffset = value; }
    std::vector<std::int32_t>* MapDefinition::Light1Color() noexcept { return _light1Color.get(); }
    const std::vector<std::int32_t>* MapDefinition::Light1Color() const noexcept { return _light1Color.get(); }
    void MapDefinition::Light1Color(std::shared_ptr<std::vector<std::int32_t>> value) noexcept { _light1Color = std::move(value); }
    std::vector<float>* MapDefinition::Light1Vector() noexcept { return _light1Vector.get(); }
    const std::vector<float>* MapDefinition::Light1Vector() const noexcept { return _light1Vector.get(); }
    void MapDefinition::Light1Vector(std::shared_ptr<std::vector<float>> value) noexcept { _light1Vector = std::move(value); }
    std::vector<std::int32_t>* MapDefinition::Light2Color() noexcept { return _light2Color.get(); }
    const std::vector<std::int32_t>* MapDefinition::Light2Color() const noexcept { return _light2Color.get(); }
    void MapDefinition::Light2Color(std::shared_ptr<std::vector<std::int32_t>> value) noexcept { _light2Color = std::move(value); }
    std::vector<float>* MapDefinition::Light2Vector() noexcept { return _light2Vector.get(); }
    const std::vector<float>* MapDefinition::Light2Vector() const noexcept { return _light2Vector.get(); }
    void MapDefinition::Light2Vector(std::shared_ptr<std::vector<float>> value) noexcept { _light2Vector = std::move(value); }
    std::uint32_t MapDefinition::BattleTimeLimit() const noexcept { return _battleTimeLimit; }
    void MapDefinition::BattleTimeLimit(std::uint32_t value) noexcept { _battleTimeLimit = value; }
    std::int16_t MapDefinition::PointLimit() const noexcept { return _pointLimit; }
    void MapDefinition::PointLimit(std::int16_t value) noexcept { _pointLimit = value; }
    MapImport* MapDefinition::Import() noexcept { return _import.get(); }
    const MapImport* MapDefinition::Import() const noexcept { return _import.get(); }
    void MapDefinition::Import(std::shared_ptr<MapImport> value) noexcept { _import = std::move(value); }
    MapPreview* MapDefinition::Preview() noexcept { return _preview.get(); }
    const MapPreview* MapDefinition::Preview() const noexcept { return _preview.get(); }
    void MapDefinition::Preview(std::shared_ptr<MapPreview> value) noexcept { _preview = std::move(value); }
    MapDefinition::MaterialList* MapDefinition::Materials() noexcept { return _materials.get(); }
    const MapDefinition::MaterialList* MapDefinition::Materials() const noexcept { return _materials.get(); }
    void MapDefinition::Materials(std::shared_ptr<MaterialList> value) noexcept { _materials = std::move(value); }
    MapDefinition::BrushList* MapDefinition::Brushes() noexcept { return _brushes.get(); }
    const MapDefinition::BrushList* MapDefinition::Brushes() const noexcept { return _brushes.get(); }
    void MapDefinition::Brushes(std::shared_ptr<BrushList> value) noexcept { _brushes = std::move(value); }
    MapDefinition::SpawnList* MapDefinition::Spawns() noexcept { return _spawns.get(); }
    const MapDefinition::SpawnList* MapDefinition::Spawns() const noexcept { return _spawns.get(); }
    void MapDefinition::Spawns(std::shared_ptr<SpawnList> value) noexcept { _spawns = std::move(value); }
    MapDefinition::JumpPadList* MapDefinition::JumpPads() noexcept { return _jumpPads.get(); }
    const MapDefinition::JumpPadList* MapDefinition::JumpPads() const noexcept { return _jumpPads.get(); }
    void MapDefinition::JumpPads(std::shared_ptr<JumpPadList> value) noexcept { _jumpPads = std::move(value); }
    MapDefinition::ItemList* MapDefinition::Items() noexcept { return _items.get(); }
    const MapDefinition::ItemList* MapDefinition::Items() const noexcept { return _items.get(); }
    void MapDefinition::Items(std::shared_ptr<ItemList> value) noexcept { _items = std::move(value); }
    MapDefinition::TeleporterList* MapDefinition::Teleporters() noexcept { return _teleporters.get(); }
    const MapDefinition::TeleporterList* MapDefinition::Teleporters() const noexcept { return _teleporters.get(); }
    void MapDefinition::Teleporters(std::shared_ptr<TeleporterList> value) noexcept { _teleporters = std::move(value); }
    const std::optional<std::string>& MapDefinition::BaseDirectory() const noexcept { return _baseDirectory; }
    void MapDefinition::BaseDirectory(std::optional<std::string> value) noexcept { _baseDirectory = std::move(value); }
    const std::optional<std::string>& MapDefinition::BundlePath() const noexcept { return _bundlePath; }
    void MapDefinition::BundlePath(std::optional<std::string> value) noexcept { _bundlePath = std::move(value); }
    const std::optional<std::string>& MapDefinition::SourcePath() const noexcept { return _sourcePath; }
    void MapDefinition::SourcePath(std::optional<std::string> value) noexcept { _sourcePath = std::move(value); }
    bool MapDefinition::Translated() const noexcept { return _translated; }
    const std::vector<MapDefinition::Portal>& MapDefinition::Portals() const noexcept { return _portals; }
    void MapDefinition::Portals(std::vector<Portal> value) noexcept { _portals = std::move(value); }
    void MapDefinition::Translated(bool value) noexcept { _translated = value; }

    std::shared_ptr<MapDefinition> MapDefinition::Load(const std::string& path)
    {
        std::string text;
        if (MapBundle::Is(path))
        {
            std::optional<std::string> recipe = MapBundle::ReadRecipe(path);
            if (!recipe)
            {
                throw ProgramException(PathGetFileName(path) + " has no map in it.");
            }
            text = std::move(*recipe);
        }
        else
        {
            text = FileReadAllText(path);
        }

        std::shared_ptr<MapDefinition> result = MapDefinitionJson::Deserialize(text);
        if (!result)
        {
            throw ProgramException("Could not read map definition " + path + ".");
        }
        result->_baseDirectory = PathGetDirectoryName(PathGetFullPath(path));
        result->_sourcePath = PathGetFullPath(path);
        result->_bundlePath = MapBundle::Is(path) ? result->_sourcePath : std::optional<std::string>{};
        if (result->_import)
        {
            result->_import->BaseDirectory(result->_baseDirectory);
            result->_import->BundlePath(result->_bundlePath);
        }
        if (result->_collision)
        {
            result->_collision->BaseDirectory = result->_baseDirectory;
            result->_collision->BundlePath = result->_bundlePath;
        }
        return result;
    }

    void MapDefinition::Save(const std::string& path) const
    {
        FileWriteAllText(path, Serialize());
    }

    std::string MapDefinition::Serialize() const
    {
        return MapDefinitionJson::Serialize(*this);
    }

    MapPreview::MapPreview()
        : _position(std::make_shared<std::vector<float>>(3)),
          _target(std::make_shared<std::vector<float>>(3))
    {
    }
    std::vector<float>* MapPreview::Position() noexcept { return _position.get(); }
    const std::vector<float>* MapPreview::Position() const noexcept { return _position.get(); }
    void MapPreview::Position(std::shared_ptr<std::vector<float>> value) noexcept { _position = std::move(value); }
    std::vector<float>* MapPreview::Target() noexcept { return _target.get(); }
    const std::vector<float>* MapPreview::Target() const noexcept { return _target.get(); }
    void MapPreview::Target(std::shared_ptr<std::vector<float>> value) noexcept { _target = std::move(value); }

    std::int32_t MapImport::ShaderMaterialDictionary::Count() const noexcept
    {
        return static_cast<std::int32_t>(_values.size());
    }
    bool MapImport::ShaderMaterialDictionary::TryGetValue(
        const std::string& key, std::int32_t& value) const noexcept
    {
        const auto found = std::find_if(_values.begin(), _values.end(),
            [&](const Value& item) { return item.first == key; });
        if (found == _values.end())
        {
            value = 0;
            return false;
        }
        value = found->second;
        return true;
    }
    std::vector<std::string> MapImport::ShaderMaterialDictionary::Keys() const
    {
        std::vector<std::string> result;
        result.reserve(_values.size());
        for (const Value& item : _values)
        {
            result.push_back(item.first);
        }
        return result;
    }
    std::int32_t& MapImport::ShaderMaterialDictionary::operator[](const std::string& key)
    {
        auto found = std::find_if(_values.begin(), _values.end(),
            [&](const Value& item) { return item.first == key; });
        if (found == _values.end())
        {
            _values.emplace_back(key, 0);
            return _values.back().second;
        }
        return found->second;
    }
    const std::int32_t& MapImport::ShaderMaterialDictionary::operator[](const std::string& key) const
    {
        const auto found = std::find_if(_values.begin(), _values.end(),
            [&](const Value& item) { return item.first == key; });
        if (found == _values.end())
        {
            throw std::out_of_range("The given key was not present in the dictionary.");
        }
        return found->second;
    }
    MapImport::ShaderMaterialDictionary::const_iterator
    MapImport::ShaderMaterialDictionary::begin() const noexcept { return _values.begin(); }
    MapImport::ShaderMaterialDictionary::const_iterator
    MapImport::ShaderMaterialDictionary::end() const noexcept { return _values.end(); }

    MapImport::MapImport()
        : _source(std::make_shared<std::string>()),
          _shaderMaterials(std::make_shared<ShaderMaterialDictionary>())
    {
    }
    const std::string& MapImport::Source() const { return RequireReference(_source); }
    void MapImport::Source(std::string value) { _source = std::make_shared<std::string>(std::move(value)); }
    void MapImport::Source(std::nullptr_t) noexcept { _source.reset(); }
    const std::optional<std::string>& MapImport::BundlePath() const noexcept { return _bundlePath; }
    void MapImport::BundlePath(std::optional<std::string> value) noexcept { _bundlePath = std::move(value); }
    const std::optional<std::string>& MapImport::BaseDirectory() const noexcept { return _baseDirectory; }
    void MapImport::BaseDirectory(std::optional<std::string> value) noexcept { _baseDirectory = std::move(value); }
    const std::optional<std::string>& MapImport::MapName() const noexcept { return _mapName; }
    void MapImport::MapName(std::optional<std::string> value) noexcept { _mapName = std::move(value); }
    float MapImport::UnitsPerUnit() const noexcept { return _unitsPerUnit; }
    void MapImport::UnitsPerUnit(float value) noexcept { _unitsPerUnit = value; }
    const std::optional<std::string>& MapImport::Textures() const noexcept { return _textures; }
    void MapImport::Textures(std::optional<std::string> value) noexcept { _textures = std::move(value); }

    std::optional<std::string> MapImport::ResolveCandidate(const std::string& name) const
    {
        if (FileExists(name))
        {
            return name;
        }
        if (PathIsPathRooted(name))
        {
            return std::nullopt;
        }
        if (_baseDirectory)
        {
            std::string candidate = PathCombine(*_baseDirectory, name);
            if (FileExists(candidate))
            {
                return candidate;
            }
        }
        {
            std::string candidate = PathCombine(CustomRooms::MapDirectory(), name);
            if (FileExists(candidate))
            {
                return candidate;
            }
        }
        {
            std::string candidate = PathCombine(Launcher::GameFiles::Root(), name);
            if (FileExists(candidate))
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> MapImport::Resolve() const
    {
        const std::string& source = RequireReference(_source);
        if (source.empty())
        {
            return std::nullopt;
        }
        if (_bundlePath)
        {
            return _bundlePath;
        }
        return ResolveCandidate(source);
    }

    std::optional<std::vector<std::uint8_t>> MapImport::ReadBundledTextures() const
    {
        if (!_bundlePath || !_textures || _textures->empty())
        {
            return std::nullopt;
        }
        return MapBundle::ReadEntry(*_bundlePath, *_textures);
    }

    std::shared_ptr<MapTexturePack> MapImport::LoadTexturePack() const
    {
        std::optional<std::vector<std::uint8_t>> bundled = ReadBundledTextures();
        if (bundled)
        {
            return std::shared_ptr<MapTexturePack>(
                new MapTexturePack(MapTexturePack::Load(*bundled, _textures.value_or(std::string{}))));
        }
        std::optional<std::string> path = ResolveTextures();
        if (!path)
        {
            return {};
        }
        return std::shared_ptr<MapTexturePack>(new MapTexturePack(MapTexturePack::Load(*path)));
    }

    std::optional<std::string> MapImport::ResolveTextures() const
    {
        if (!_textures || _textures->empty())
        {
            return std::nullopt;
        }
        return ResolveCandidate(*_textures);
    }

    MapImport::ShaderMaterialDictionary* MapImport::ShaderMaterials() noexcept { return _shaderMaterials.get(); }
    const MapImport::ShaderMaterialDictionary* MapImport::ShaderMaterials() const noexcept { return _shaderMaterials.get(); }
    void MapImport::ShaderMaterials(std::shared_ptr<ShaderMaterialDictionary> value) noexcept { _shaderMaterials = std::move(value); }
    std::int32_t MapImport::DefaultMaterial() const noexcept { return _defaultMaterial; }
    void MapImport::DefaultMaterial(std::int32_t value) noexcept { _defaultMaterial = value; }
    float MapImport::TexScale() const noexcept { return _texScale; }
    void MapImport::TexScale(float value) noexcept { _texScale = value; }
    bool MapImport::KeepSky() const noexcept { return _keepSky; }
    void MapImport::KeepSky(bool value) noexcept { _keepSky = value; }
    bool MapImport::KeepClip() const noexcept { return _keepClip; }
    void MapImport::KeepClip(bool value) noexcept { _keepClip = value; }
    std::int32_t MapImport::PatchLevel() const noexcept { return _patchLevel; }
    void MapImport::PatchLevel(std::int32_t value) noexcept { _patchLevel = value; }
    MapCollision* MapDefinition::Collision() noexcept { return _collision.get(); }
    const MapCollision* MapDefinition::Collision() const noexcept { return _collision.get(); }
    void MapDefinition::Collision(std::shared_ptr<MapCollision> value) noexcept { _collision = std::move(value); }

    std::optional<std::vector<std::uint8_t>> MapCollision::ReadBytes() const
    {
        const std::string& source = RequireReference(Source);
        if (source.empty())
        {
            return std::nullopt;
        }
        if (BundlePath.has_value())
        {
            std::optional<std::vector<std::uint8_t>> bundled = MapBundle::ReadEntry(*BundlePath, source);
            if (bundled.has_value())
            {
                return bundled;
            }
        }
        const std::optional<std::string> path = Resolve();
        return path.has_value() ? std::optional<std::vector<std::uint8_t>>(::MphRead::NativeRuntime::FileReadAllBytes(*path)) : std::nullopt;
    }

    std::optional<std::string> MapCollision::Resolve() const
    {
        const std::string& source = RequireReference(Source);
        if (source.empty())
        {
            return std::nullopt;
        }
        for (const std::string& candidate : Candidates())
        {
            if (FileExists(candidate))
            {
                return candidate;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> MapCollision::Candidates() const
    {
        const std::string& source = RequireReference(Source);
        std::vector<std::string> candidates{source};
        if (::MphRead::NativeRuntime::PathIsPathRooted(source))
        {
            return candidates;
        }
        if (BaseDirectory.has_value())
        {
            candidates.push_back(PathCombine(*BaseDirectory, source));
        }
        candidates.push_back(PathCombine(CustomRooms::MapDirectory(), source));
        candidates.push_back(PathCombine(Launcher::GameFiles::Root(), source));
        return candidates;
    }

    bool MapImport::KeepItems() const noexcept { return _keepItems; }
    void MapImport::KeepItems(bool value) noexcept { _keepItems = value; }

    const std::vector<std::string>& MapImport::TexturePaks() const noexcept { return _texturePaks; }
    void MapImport::TexturePaks(std::vector<std::string> value) noexcept { _texturePaks = std::move(value); }
    std::vector<std::string> MapImport::ResolveTexturePaks() const
    {
        std::vector<std::string> result;
        for (const std::string& pak : _texturePaks)
        {
            if (std::optional<std::string> path = ResolveCandidate(pak))
            {
                result.push_back(std::move(*path));
            }
        }
        return result;
    }
    const std::vector<std::pair<std::string, std::string>>& MapImport::TextureAliases() const noexcept { return _textureAliases; }
    void MapImport::TextureAliases(std::vector<std::pair<std::string, std::string>> value) noexcept { _textureAliases = std::move(value); }

    std::vector<float>* MapTeleporter::Position() noexcept { return _position.get(); }
    const std::vector<float>* MapTeleporter::Position() const noexcept { return _position.get(); }
    void MapTeleporter::Position(std::shared_ptr<std::vector<float>> value) noexcept { _position = std::move(value); }
    std::vector<float>* MapTeleporter::Target() noexcept { return _target.get(); }
    const std::vector<float>* MapTeleporter::Target() const noexcept { return _target.get(); }
    void MapTeleporter::Target(std::shared_ptr<std::vector<float>> value) noexcept { _target = std::move(value); }
    float MapTeleporter::Yaw() const noexcept { return _yaw; }
    void MapTeleporter::Yaw(float value) noexcept { _yaw = value; }
    bool MapTeleporter::Visible() const noexcept { return _visible; }
    void MapTeleporter::Visible(bool value) noexcept { _visible = value; }

    bool MapImport::KeepSpawns() const noexcept { return _keepSpawns; }
    void MapImport::KeepSpawns(bool value) noexcept { _keepSpawns = value; }

    MapMaterial::MapMaterial() : _name(std::make_shared<std::string>("mat")) {}
    const std::string& MapMaterial::Name() const { return RequireReference(_name); }
    void MapMaterial::Name(std::string value) { _name = std::make_shared<std::string>(std::move(value)); }
    void MapMaterial::Name(std::nullptr_t) noexcept { _name.reset(); }
    std::int32_t MapMaterial::SourceMaterial() const noexcept { return _sourceMaterial; }
    void MapMaterial::SourceMaterial(std::int32_t value) noexcept { _sourceMaterial = value; }
    float MapMaterial::TexScale() const noexcept { return _texScale; }
    void MapMaterial::TexScale(float value) noexcept { _texScale = value; }

    MapBrush::MapBrush()
        : _min(std::make_shared<std::vector<float>>(3)),
          _max(std::make_shared<std::vector<float>>(3))
    {
    }
    std::vector<float>* MapBrush::Min() noexcept { return _min.get(); }
    const std::vector<float>* MapBrush::Min() const noexcept { return _min.get(); }
    void MapBrush::Min(std::shared_ptr<std::vector<float>> value) noexcept { _min = std::move(value); }
    std::vector<float>* MapBrush::Max() noexcept { return _max.get(); }
    const std::vector<float>* MapBrush::Max() const noexcept { return _max.get(); }
    void MapBrush::Max(std::shared_ptr<std::vector<float>> value) noexcept { _max = std::move(value); }
    std::int32_t MapBrush::Material() const noexcept { return _material; }
    void MapBrush::Material(std::int32_t value) noexcept { _material = value; }
    float MapBrush::Shade() const noexcept { return _shade; }
    void MapBrush::Shade(float value) noexcept { _shade = value; }
    bool MapBrush::Solid() const noexcept { return _solid; }
    void MapBrush::Solid(bool value) noexcept { _solid = value; }
    bool MapBrush::Damaging() const noexcept { return _damaging; }
    void MapBrush::Damaging(bool value) noexcept { _damaging = value; }
    bool MapBrush::NoBeams() const noexcept { return _noBeams; }
    void MapBrush::NoBeams(bool value) noexcept { _noBeams = value; }
    bool MapBrush::Visible() const noexcept { return _visible; }
    void MapBrush::Visible(bool value) noexcept { _visible = value; }
    const std::optional<std::string>& MapBrush::Terrain() const noexcept { return _terrain; }
    void MapBrush::Terrain(std::optional<std::string> value) noexcept { _terrain = std::move(value); }

    MapSpawn::MapSpawn() : _position(std::make_shared<std::vector<float>>(3)) {}
    std::vector<float>* MapSpawn::Position() noexcept { return _position.get(); }
    const std::vector<float>* MapSpawn::Position() const noexcept { return _position.get(); }
    void MapSpawn::Position(std::shared_ptr<std::vector<float>> value) noexcept { _position = std::move(value); }
    float MapSpawn::Yaw() const noexcept { return _yaw; }
    void MapSpawn::Yaw(float value) noexcept { _yaw = value; }

    MapJumpPad::MapJumpPad()
        : _position(std::make_shared<std::vector<float>>(3)),
          _size(std::make_shared<std::vector<float>>(
              std::initializer_list<float>{1.6F, 1.8F, 1.6F}))
    {
    }
    std::vector<float>* MapJumpPad::Position() noexcept { return _position.get(); }
    const std::vector<float>* MapJumpPad::Position() const noexcept { return _position.get(); }
    void MapJumpPad::Position(std::shared_ptr<std::vector<float>> value) noexcept { _position = std::move(value); }
    std::vector<float>* MapJumpPad::Target() noexcept { return _target.get(); }
    const std::vector<float>* MapJumpPad::Target() const noexcept { return _target.get(); }
    void MapJumpPad::Target(std::shared_ptr<std::vector<float>> value) noexcept { _target = std::move(value); }
    std::vector<float>* MapJumpPad::Vector() noexcept { return _vector.get(); }
    const std::vector<float>* MapJumpPad::Vector() const noexcept { return _vector.get(); }
    void MapJumpPad::Vector(std::shared_ptr<std::vector<float>> value) noexcept { _vector = std::move(value); }
    float MapJumpPad::Speed() const noexcept { return _speed; }
    void MapJumpPad::Speed(float value) noexcept { _speed = value; }
    std::vector<float>* MapJumpPad::Size() noexcept { return _size.get(); }
    const std::vector<float>* MapJumpPad::Size() const noexcept { return _size.get(); }
    void MapJumpPad::Size(std::shared_ptr<std::vector<float>> value) noexcept { _size = std::move(value); }
    std::uint32_t MapJumpPad::ModelId() const noexcept { return _modelId; }
    void MapJumpPad::ModelId(std::uint32_t value) noexcept { _modelId = value; }
    std::uint16_t MapJumpPad::CooldownTime() const noexcept { return _cooldownTime; }
    void MapJumpPad::CooldownTime(std::uint16_t value) noexcept { _cooldownTime = value; }
    std::uint16_t MapJumpPad::ControlLockTime() const noexcept { return _controlLockTime; }
    void MapJumpPad::ControlLockTime(std::uint16_t value) noexcept { _controlLockTime = value; }
    const std::vector<float>* MapJumpPad::Box() const noexcept { return _box.get(); }
    void MapJumpPad::Box(std::shared_ptr<std::vector<float>> value) noexcept { _box = std::move(value); }
    std::uint8_t MapJumpPad::Q3Kind() const noexcept { return _q3Kind; }
    void MapJumpPad::Q3Kind(std::uint8_t value) noexcept { _q3Kind = value; }
    float MapJumpPad::Q3Value() const noexcept { return _q3Value; }
    void MapJumpPad::Q3Value(float value) noexcept { _q3Value = value; }
    std::uint16_t MapJumpPad::Q3Unit() const noexcept { return _q3Unit; }
    void MapJumpPad::Q3Unit(std::uint16_t value) noexcept { _q3Unit = value; }

    MapItem::MapItem()
        : _position(std::make_shared<std::vector<float>>(3)),
          _type(std::make_shared<std::string>("MissileExpansion"))
    {
    }
    std::vector<float>* MapItem::Position() noexcept { return _position.get(); }
    const std::vector<float>* MapItem::Position() const noexcept { return _position.get(); }
    void MapItem::Position(std::shared_ptr<std::vector<float>> value) noexcept { _position = std::move(value); }
    const std::string& MapItem::Type() const { return RequireReference(_type); }
    void MapItem::Type(std::string value) { _type = std::make_shared<std::string>(std::move(value)); }
    void MapItem::Type(std::nullptr_t) noexcept { _type.reset(); }
    bool MapItem::HasBase() const noexcept { return _hasBase; }
    void MapItem::HasBase(bool value) noexcept { _hasBase = value; }
    std::uint16_t MapItem::SpawnInterval() const noexcept { return _spawnInterval; }
    void MapItem::SpawnInterval(std::uint16_t value) noexcept { _spawnInterval = value; }
}
