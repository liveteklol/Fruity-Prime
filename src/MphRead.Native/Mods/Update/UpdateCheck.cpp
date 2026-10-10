#include "UpdateCheck.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/AtomicSharedPtr.hpp"

#include "../Branding.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::AppendUtf8;
using ::MphRead::NativeRuntime::RuneDecodeFromUtf8;
using ::MphRead::NativeRuntime::Utf8Scalar;

namespace MphRead::Mods::Update
{
    namespace
    {
        constexpr std::chrono::seconds Timeout(20);

        constexpr std::string_view Api =
            "https://api.github.com/repos/liveteklol/Fruity-Prime/releases/latest";

        ::MphRead::NativeRuntime::AtomicSharedPtr<const std::string> LastReasonValue{nullptr};

        class NamedException : public std::runtime_error
        {
        public:
            NamedException(std::string name, std::string message)
                : std::runtime_error(std::move(message)), _name(std::move(name))
            {
            }

            [[nodiscard]] const std::string& Name() const noexcept
            {
                return _name;
            }

        private:
            std::string _name;
        };

        [[noreturn]] void ThrowJsonException(std::string_view message)
        {
            throw NamedException("JsonException", std::string(message));
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

        struct JsonString
        {
            std::string Text;
            bool Escaped = false;
        };

        struct JsonValue
        {
            JsonKind Kind = JsonKind::Null;
            bool Boolean = false;
            std::string Text;
            bool Escaped = false;
            std::vector<JsonValue> Array;
            std::vector<std::pair<JsonString, JsonValue>> Object;
        };

        [[nodiscard]] bool IsJsonWhitespace(char value) noexcept
        {
            return value == ' ' || value == '\t' || value == '\r' || value == '\n';
        }

        // Utf8JsonReader refuses ill-formed UTF-8 in a string outright.
        [[nodiscard]] std::size_t Utf8SequenceLength(std::string_view input,
            std::size_t offset)
        {
            const Utf8Scalar scalar = RuneDecodeFromUtf8(input.substr(offset));
            if (!scalar.Valid())
            {
                ThrowJsonException("Invalid UTF-8 in JSON string.");
            }
            return scalar.Length;
        }

        [[nodiscard]] int HexDigit(char value) noexcept
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return value - 'a' + 10;
            }
            if (value >= 'A' && value <= 'F')
            {
                return value - 'A' + 10;
            }
            return -1;
        }

        [[nodiscard]] char32_t ReadHex4(std::string_view text,
            std::size_t offset) noexcept
        {
            char32_t value = 0;
            for (std::size_t index = 0; index < 4; ++index)
            {
                value = (value << 4)
                    | static_cast<char32_t>(HexDigit(text[offset + index]));
            }
            return value;
        }

        [[noreturn]] void ThrowInvalidUtf16(char32_t value)
        {
            throw InvalidOperationException(
                "Cannot read invalid UTF-16 JSON text as string. Invalid surrogate value: '0x"
                + ::MphRead::NativeRuntime::ToStringInvariant(static_cast<std::uint32_t>(value), "X2") + "'.");
        }

        [[noreturn]] void ThrowIncompleteUtf16()
        {
            throw InvalidOperationException(
                "Cannot read incomplete UTF-16 JSON text as string with missing low surrogate.");
        }

        [[nodiscard]] std::string UnescapeJsonString(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            std::size_t offset = 0;
            while (offset < text.size())
            {
                if (text[offset] != '\\')
                {
                    const std::size_t length = Utf8SequenceLength(text, offset);
                    result.append(text.substr(offset, length));
                    offset += length;
                    continue;
                }

                const char escape = text[offset + 1];
                offset += 2;
                switch (escape)
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
                    char32_t scalar = ReadHex4(text, offset);
                    offset += 4;
                    if (scalar >= 0xD800U && scalar <= 0xDBFFU)
                    {
                        if (offset + 6 > text.size()
                            || text[offset] != '\\'
                            || text[offset + 1] != 'u')
                        {
                            ThrowIncompleteUtf16();
                        }
                        const char32_t low = ReadHex4(text, offset + 2);
                        offset += 6;
                        if (low < 0xDC00U || low > 0xDFFFU)
                        {
                            ThrowInvalidUtf16(low);
                        }
                        scalar = 0x10000U
                            + ((scalar - 0xD800U) << 10)
                            + (low - 0xDC00U);
                    }
                    else if (scalar >= 0xDC00U && scalar <= 0xDFFFU)
                    {
                        ThrowInvalidUtf16(scalar);
                    }
                    AppendUtf8(result, scalar);
                    break;
                }
                default:
                    throw std::logic_error("unvalidated JSON escape");
                }
            }
            return result;
        }

        [[nodiscard]] std::string JsonKindName(const JsonValue& value)
        {
            switch (value.Kind)
            {
            case JsonKind::Null: return "Null";
            case JsonKind::Boolean: return value.Boolean ? "True" : "False";
            case JsonKind::Number: return "Number";
            case JsonKind::String: return "String";
            case JsonKind::Array: return "Array";
            case JsonKind::Object: return "Object";
            }
            return "Undefined";
        }

        [[noreturn]] void ThrowWrongType(std::string_view expected,
            const JsonValue& actual)
        {
            throw InvalidOperationException(
                "The requested operation requires an element of type '"
                + std::string(expected)
                + "', but the target element has type '"
                + JsonKindName(actual) + "'.");
        }

        class JsonParser final
        {
        public:
            explicit JsonParser(std::string_view input) : _input(input)
            {
            }

            [[nodiscard]] JsonValue ParseDocument()
            {
                SkipWhitespace();
                JsonValue value = ParseValue(0);
                SkipWhitespace();
                if (_offset != _input.size())
                {
                    ThrowJsonException(
                        "Additional text encountered after finished reading JSON content.");
                }
                return value;
            }

        private:
            static constexpr std::size_t DefaultMaxDepth = 64;

            [[nodiscard]] JsonValue ParseValue(std::size_t depth)
            {
                if (_offset >= _input.size())
                {
                    ThrowJsonException("Expected a JSON value.");
                }
                switch (_input[_offset])
                {
                case 'n':
                {
                    ReadLiteral("null");
                    JsonValue value;
                    value.Kind = JsonKind::Null;
                    return value;
                }
                case 't':
                {
                    ReadLiteral("true");
                    JsonValue value;
                    value.Kind = JsonKind::Boolean;
                    value.Boolean = true;
                    return value;
                }
                case 'f':
                {
                    ReadLiteral("false");
                    JsonValue value;
                    value.Kind = JsonKind::Boolean;
                    value.Boolean = false;
                    return value;
                }
                case '"':
                {
                    JsonString parsed = ParseString();
                    JsonValue value;
                    value.Kind = JsonKind::String;
                    value.Text = std::move(parsed.Text);
                    value.Escaped = parsed.Escaped;
                    return value;
                }
                case '[':
                    if (depth >= DefaultMaxDepth)
                    {
                        ThrowJsonException("The maximum configured depth has been exceeded.");
                    }
                    return ParseArray(depth + 1);
                case '{':
                    if (depth >= DefaultMaxDepth)
                    {
                        ThrowJsonException("The maximum configured depth has been exceeded.");
                    }
                    return ParseObject(depth + 1);
                default:
                    if (_input[_offset] == '-'
                        || (_input[_offset] >= '0' && _input[_offset] <= '9'))
                    {
                        JsonValue value;
                        value.Kind = JsonKind::Number;
                        value.Text = ParseNumber();
                        return value;
                    }
                    ThrowJsonException("Expected a JSON value.");
                }
            }

            [[nodiscard]] JsonValue ParseArray(std::size_t depth)
            {
                JsonValue value;
                value.Kind = JsonKind::Array;
                ++_offset;
                SkipWhitespace();
                if (Consume(']'))
                {
                    return value;
                }
                for (;;)
                {
                    value.Array.push_back(ParseValue(depth));
                    SkipWhitespace();
                    if (Consume(']'))
                    {
                        return value;
                    }
                    if (!Consume(','))
                    {
                        ThrowJsonException("Expected ',' or ']' in JSON array.");
                    }
                    SkipWhitespace();
                    if (_offset < _input.size() && _input[_offset] == ']')
                    {
                        ThrowJsonException("Trailing commas are not permitted.");
                    }
                }
            }

            [[nodiscard]] JsonValue ParseObject(std::size_t depth)
            {
                JsonValue value;
                value.Kind = JsonKind::Object;
                ++_offset;
                SkipWhitespace();
                if (Consume('}'))
                {
                    return value;
                }
                for (;;)
                {
                    if (_offset >= _input.size() || _input[_offset] != '"')
                    {
                        ThrowJsonException("Expected a JSON property name.");
                    }
                    JsonString name = ParseString();
                    SkipWhitespace();
                    if (!Consume(':'))
                    {
                        ThrowJsonException("Expected ':' after JSON property name.");
                    }
                    SkipWhitespace();
                    value.Object.emplace_back(std::move(name), ParseValue(depth));
                    SkipWhitespace();
                    if (Consume('}'))
                    {
                        return value;
                    }
                    if (!Consume(','))
                    {
                        ThrowJsonException("Expected ',' or '}' in JSON object.");
                    }
                    SkipWhitespace();
                    if (_offset < _input.size() && _input[_offset] == '}')
                    {
                        ThrowJsonException("Trailing commas are not permitted.");
                    }
                }
            }

            [[nodiscard]] JsonString ParseString()
            {
                ++_offset;
                const std::size_t start = _offset;
                bool escaped = false;
                while (_offset < _input.size())
                {
                    const auto byte = static_cast<unsigned char>(_input[_offset]);
                    if (byte == '"')
                    {
                        JsonString value{
                            std::string(_input.substr(start, _offset - start)),
                            escaped
                        };
                        ++_offset;
                        return value;
                    }
                    if (byte == '\\')
                    {
                        escaped = true;
                        ++_offset;
                        if (_offset >= _input.size())
                        {
                            ThrowJsonException("Incomplete JSON escape sequence.");
                        }
                        const char escape = _input[_offset++];
                        switch (escape)
                        {
                        case '"':
                        case '\\':
                        case '/':
                        case 'b':
                        case 'f':
                        case 'n':
                        case 'r':
                        case 't':
                            break;
                        case 'u':
                            ValidateHex4();
                            break;
                        default:
                            ThrowJsonException("Invalid JSON escape sequence.");
                        }
                        continue;
                    }
                    if (byte < 0x20U)
                    {
                        ThrowJsonException("Unescaped control character in JSON string.");
                    }
                    _offset += Utf8SequenceLength(_input, _offset);
                }
                ThrowJsonException("Unterminated JSON string.");
            }

            void ValidateHex4()
            {
                if (_offset + 4 > _input.size())
                {
                    ThrowJsonException("Incomplete Unicode escape in JSON string.");
                }
                for (std::size_t index = 0; index < 4; ++index)
                {
                    if (HexDigit(_input[_offset + index]) < 0)
                    {
                        ThrowJsonException("Invalid Unicode escape in JSON string.");
                    }
                }
                _offset += 4;
            }

            [[nodiscard]] std::string ParseNumber()
            {
                const std::size_t start = _offset;
                if (Consume('-') && _offset >= _input.size())
                {
                    ThrowJsonException("Invalid JSON number.");
                }
                if (_input[_offset] == '0')
                {
                    ++_offset;
                    if (_offset < _input.size()
                        && _input[_offset] >= '0' && _input[_offset] <= '9')
                    {
                        ThrowJsonException(
                            "Leading zeroes are not permitted in JSON numbers.");
                    }
                }
                else
                {
                    if (_input[_offset] < '1' || _input[_offset] > '9')
                    {
                        ThrowJsonException("Invalid JSON number.");
                    }
                    while (_offset < _input.size()
                        && _input[_offset] >= '0' && _input[_offset] <= '9')
                    {
                        ++_offset;
                    }
                }
                if (_offset < _input.size() && _input[_offset] == '.')
                {
                    ++_offset;
                    const std::size_t fraction = _offset;
                    while (_offset < _input.size()
                        && _input[_offset] >= '0' && _input[_offset] <= '9')
                    {
                        ++_offset;
                    }
                    if (_offset == fraction)
                    {
                        ThrowJsonException("Invalid JSON number.");
                    }
                }
                if (_offset < _input.size()
                    && (_input[_offset] == 'e' || _input[_offset] == 'E'))
                {
                    ++_offset;
                    if (_offset < _input.size()
                        && (_input[_offset] == '+' || _input[_offset] == '-'))
                    {
                        ++_offset;
                    }
                    const std::size_t exponent = _offset;
                    while (_offset < _input.size()
                        && _input[_offset] >= '0' && _input[_offset] <= '9')
                    {
                        ++_offset;
                    }
                    if (_offset == exponent)
                    {
                        ThrowJsonException("Invalid JSON number.");
                    }
                }
                return std::string(_input.substr(start, _offset - start));
            }

            void ReadLiteral(std::string_view literal)
            {
                if (_input.substr(_offset, literal.size()) != literal)
                {
                    ThrowJsonException("Invalid JSON literal.");
                }
                _offset += literal.size();
            }

            void SkipWhitespace() noexcept
            {
                while (_offset < _input.size() && IsJsonWhitespace(_input[_offset]))
                {
                    ++_offset;
                }
            }

            [[nodiscard]] bool Consume(char value) noexcept
            {
                if (_offset < _input.size() && _input[_offset] == value)
                {
                    ++_offset;
                    return true;
                }
                return false;
            }

            std::string_view _input;
            std::size_t _offset = 0;
        };

        [[nodiscard]] const JsonValue* TryGetProperty(const JsonValue& value,
            std::string_view name)
        {
            if (value.Kind != JsonKind::Object)
            {
                ThrowWrongType("Object", value);
            }

            for (auto iterator = value.Object.rbegin();
                iterator != value.Object.rend(); ++iterator)
            {
                const JsonString& current = iterator->first;
                if (!current.Escaped)
                {
                    if (current.Text == name)
                    {
                        return &iterator->second;
                    }
                    continue;
                }

                if (current.Text.size() <= name.size())
                {
                    continue;
                }
                const std::size_t slash = current.Text.find('\\');
                if (slash == std::string::npos
                    || name.size() <= slash
                    || current.Text.substr(0, slash) != name.substr(0, slash))
                {
                    continue;
                }

                const std::string remainder
                    = UnescapeJsonString(std::string_view(current.Text).substr(slash));
                if (remainder == name.substr(slash))
                {
                    return &iterator->second;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<std::string> GetString(const JsonValue& value)
        {
            if (value.Kind == JsonKind::Null)
            {
                return std::nullopt;
            }
            if (value.Kind != JsonKind::String)
            {
                ThrowWrongType("String", value);
            }
            return value.Escaped
                ? UnescapeJsonString(value.Text)
                : value.Text;
        }

        [[nodiscard]] bool TryGetInt64(const JsonValue& value, std::int64_t& result)
        {
            if (value.Kind != JsonKind::Number)
            {
                ThrowWrongType("Number", value);
            }
            const char* first = value.Text.data();
            const char* last = first + value.Text.size();
            const auto parsed = std::from_chars(first, last, result, 10);
            return parsed.ec == std::errc{} && parsed.ptr == last;
        }

        struct Asset
        {
            std::string Name;
            std::string Url;
            std::int64_t Size = 0;
        };

        [[nodiscard]] std::optional<Asset> PickAsset(const std::vector<Asset>& assets);
        [[nodiscard]] std::optional<Asset> PickAsset(const std::vector<Asset>& assets,
            const std::string& rid, bool server);

        class CurlRequestMessage final : public HttpRequestMessage
        {
        public:
            explicit CurlRequestMessage(std::string url) : Url(std::move(url))
            {
            }

            std::string Url;
        };

        class CurlResponseMessage final : public HttpResponseMessage
        {
        public:
            long StatusCode = 0;
            std::string Body;
        };

        struct CurlWriteContext
        {
            std::string* Body = nullptr;
            std::exception_ptr Failure;
            CancellationToken Cancel = nullptr;
        };

        std::size_t CurlWrite(char* data, std::size_t size, std::size_t count,
            void* opaque) noexcept
        {
            const std::size_t bytes = size * count;
            auto* context = static_cast<CurlWriteContext*>(opaque);
            try
            {
                context->Body->append(data, bytes);
                return bytes;
            }
            catch (...)
            {
                context->Failure = std::current_exception();
                return 0;
            }
        }

        int CurlProgress(void* opaque, curl_off_t, curl_off_t,
            curl_off_t, curl_off_t) noexcept
        {
            const auto* context = static_cast<const CurlWriteContext*>(opaque);
            return context->Cancel != nullptr && context->Cancel->stop_requested()
                ? 1 : 0;
        }

        [[nodiscard]] std::unique_ptr<HttpResponseMessage> PerformRequest(
            const CurlRequestMessage& request,
            std::chrono::milliseconds timeout,
            std::string_view userAgent,
            std::string_view accept,
            CancellationToken cancel)
        {
            if (cancel != nullptr && cancel->stop_requested())
            {
                throw NamedException(
                    "TaskCanceledException", "The operation was canceled.");
            }

            static std::once_flag curlOnce;
            static CURLcode curlInit = CURLE_OK;
            std::call_once(curlOnce, []
            {
                curlInit = curl_global_init(CURL_GLOBAL_DEFAULT);
            });
            if (curlInit != CURLE_OK)
            {
                throw NamedException("HttpRequestException", curl_easy_strerror(curlInit));
            }

            using EasyPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
            EasyPtr easy(curl_easy_init(), &curl_easy_cleanup);
            if (!easy)
            {
                throw NamedException("HttpRequestException", "curl_easy_init failed");
            }

            struct SlistDeleter
            {
                void operator()(curl_slist* list) const noexcept
                {
                    if (list != nullptr)
                    {
                        curl_slist_free_all(list);
                    }
                }
            };
            std::unique_ptr<curl_slist, SlistDeleter> headers;
            curl_slist* rawHeaders = nullptr;
            const std::string acceptHeader = "Accept: " + std::string(accept);
            rawHeaders = curl_slist_append(rawHeaders, acceptHeader.c_str());
            if (rawHeaders == nullptr)
            {
                throw std::bad_alloc();
            }
            headers.reset(rawHeaders);

            auto response = std::make_unique<CurlResponseMessage>();
            CurlWriteContext context{&response->Body, nullptr, cancel};
            const std::string url = request.Url;
            const std::string agent(userAgent);

            curl_easy_setopt(easy.get(), CURLOPT_URL, url.c_str());
            curl_easy_setopt(easy.get(), CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(easy.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            curl_easy_setopt(easy.get(), CURLOPT_HTTPHEADER, headers.get());
            curl_easy_setopt(easy.get(), CURLOPT_USERAGENT, agent.c_str());
            curl_easy_setopt(easy.get(), CURLOPT_TIMEOUT_MS,
                static_cast<long>(timeout.count()));
            curl_easy_setopt(easy.get(), CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(easy.get(), CURLOPT_MAXREDIRS, 50L);
            curl_easy_setopt(easy.get(), CURLOPT_PROTOCOLS_STR, "https");
            curl_easy_setopt(easy.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
            curl_easy_setopt(easy.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(easy.get(), CURLOPT_WRITEFUNCTION, &CurlWrite);
            curl_easy_setopt(easy.get(), CURLOPT_WRITEDATA, &context);
            curl_easy_setopt(easy.get(), CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(easy.get(), CURLOPT_XFERINFOFUNCTION, &CurlProgress);
            curl_easy_setopt(easy.get(), CURLOPT_XFERINFODATA, &context);

            const CURLcode code = curl_easy_perform(easy.get());
            if (context.Failure)
            {
                std::rethrow_exception(context.Failure);
            }
            if (code != CURLE_OK)
            {
                if (code == CURLE_OPERATION_TIMEDOUT
                    || (code == CURLE_ABORTED_BY_CALLBACK
                        && cancel != nullptr && cancel->stop_requested()))
                {
                    throw NamedException(
                        "TaskCanceledException", curl_easy_strerror(code));
                }
                throw NamedException("HttpRequestException", curl_easy_strerror(code));
            }
            const CURLcode infoCode = curl_easy_getinfo(
                easy.get(), CURLINFO_RESPONSE_CODE, &response->StatusCode);
            if (infoCode != CURLE_OK)
            {
                throw NamedException("HttpRequestException", curl_easy_strerror(infoCode));
            }
            return response;
        }

        class CurlAwaiter final : public HttpResponseAwaiter
        {
        public:
            explicit CurlAwaiter(std::unique_ptr<HttpResponseMessage> response)
                : _response(std::move(response))
            {
            }

            explicit CurlAwaiter(std::exception_ptr failure)
                : _failure(std::move(failure))
            {
            }

            [[nodiscard]] std::unique_ptr<HttpResponseMessage> GetResult() override
            {
                if (_failure)
                {
                    std::rethrow_exception(_failure);
                }
                return std::move(_response);
            }

        private:
            std::unique_ptr<HttpResponseMessage> _response;
            std::exception_ptr _failure;
        };

        class CurlTask final : public HttpResponseTask
        {
        public:
            explicit CurlTask(std::unique_ptr<HttpResponseMessage> response)
                : _awaiter(std::move(response))
            {
            }

            explicit CurlTask(std::exception_ptr failure)
                : _awaiter(std::move(failure))
            {
            }

            [[nodiscard]] HttpResponseAwaiter& GetAwaiter() override
            {
                return _awaiter;
            }

        private:
            CurlAwaiter _awaiter;
        };

        class CurlClient final : public HttpClient
        {
        public:
            explicit CurlClient(std::chrono::milliseconds timeout) : _timeout(timeout)
            {
            }

            void AddUserAgent(std::string value)
            {
                _userAgent = std::move(value);
            }

            void AddAccept(std::string value)
            {
                _accept = std::move(value);
            }

            [[nodiscard]] std::unique_ptr<HttpResponseTask> SendAsync(
                HttpRequestMessage* request,
                HttpCompletionOption completion,
                CancellationToken cancel) override
            {
                try
                {
                    if (completion != HttpCompletionOption::ResponseContentRead)
                    {
                        throw NamedException("ArgumentOutOfRangeException", "completionOption");
                    }
                    auto* concrete = dynamic_cast<CurlRequestMessage*>(request);
                    if (concrete == nullptr)
                    {
                        throw NullReferenceException();
                    }
                    return std::make_unique<CurlTask>(PerformRequest(*concrete,
                        _timeout, _userAgent, _accept, cancel));
                }
                catch (...)
                {
                    return std::make_unique<CurlTask>(std::current_exception());
                }
            }

        private:
            std::chrono::milliseconds _timeout;
            std::string _userAgent;
            std::string _accept;
        };

        [[nodiscard]] std::string ExceptionTypeName(const std::exception& ex)
        {
            if (const auto* named = dynamic_cast<const NamedException*>(&ex))
            {
                return named->Name();
            }
            return ::MphRead::NativeRuntime::ExceptionTypeName(ex);
        }
        [[nodiscard]] std::optional<Asset> PickAsset(const std::vector<Asset>& assets)
        {
            return PickAsset(assets, UpdateCheck::Rid(), UpdateCheck::IsServerBuild());
        }

        [[nodiscard]] std::optional<Asset> PickAsset(const std::vector<Asset>& assets,
            const std::string& rid, bool server)
        {
            if (rid.empty())
            {
                return std::nullopt;
            }
            for (const Asset& asset : assets)
            {
                const std::string name = ::MphRead::NativeRuntime::ToLowerInvariant(asset.Name);
                if (name.find(rid) == std::string::npos)
                {
                    continue;
                }
                const bool containsServer = name.find("-server-") != std::string::npos;
                if (containsServer != server)
                {
                    continue;
                }
                return asset;
            }
            return std::nullopt;
        }
    }

    std::optional<std::string> UpdateCheck::LastReason()
    {
        const std::shared_ptr<const std::string> value
            = LastReasonValue.load(std::memory_order_relaxed);
        return value == nullptr
            ? std::nullopt
            : std::optional<std::string>(*value);
    }

    void UpdateCheck::SetLastReason(std::optional<std::string> reason)
    {
        LastReasonValue.store(reason.has_value()
            ? std::make_shared<const std::string>(std::move(*reason))
            : std::shared_ptr<const std::string>(),
            std::memory_order_relaxed);
    }

    std::optional<UpdateInfo> UpdateCheck::Latest(CancellationToken cancel)
    {
        SetLastReason(std::nullopt);
        if (!BuildVersion::IsRelease())
        {
            SetLastReason("this is a local build, so it is left alone");
            return std::nullopt;
        }
        const std::optional<std::string> json = FetchLatest(cancel);
        return !json.has_value() ? std::nullopt : Parse(*json);
    }

    std::optional<UpdateInfo> UpdateCheck::LatestServer(CancellationToken cancel)
    {
        SetLastReason(std::nullopt);
        if (!BuildVersion::IsRelease())
        {
            SetLastReason("this is a local build, so it is left alone");
            return std::nullopt;
        }
        const std::optional<std::string> json = FetchLatest(cancel);
        if (!json.has_value())
        {
            return std::nullopt;
        }
        // Parse is what decides whether the release is newer than this build,
        // but the package it picks is the player's: a server is the same
        // binary as the game, so Rid() cannot tell them apart. A server swaps
        // in the server package, which carries its own loader and libraries.
        const std::optional<UpdateInfo> newer = Parse(std::string_view(*json));
        if (!newer.has_value())
        {
            return std::nullopt;
        }
        const std::optional<UpdateInfo> server = ServerAsset(std::string_view(*json));
        if (!server.has_value())
        {
            return std::nullopt;
        }
        return UpdateInfo{
            .Tag = newer->Tag.Get(),
            .Version = newer->Version.Get(),
            .AssetName = server->AssetName.Get(),
            .AssetUrl = server->AssetUrl.Get(),
            .AssetSize = server->AssetSize.Get(),
            .PageUrl = newer->PageUrl.Get(),
            .Notes = newer->Notes.Get()
        };
    }

    std::optional<std::string> UpdateCheck::FetchLatest(CancellationToken cancel)
    {
        std::string json;
        try
        {
            CurlClient client(std::chrono::duration_cast<std::chrono::milliseconds>(Timeout));
            client.AddUserAgent(std::string(Mods::Branding::FileName) + "/"
                + BuildVersion::Display());
            client.AddAccept("application/vnd.github+json");
            CurlRequestMessage request{std::string(Api)};
            std::unique_ptr<HttpResponseMessage> response = SyncHttp::Send(&client,
                &request, HttpCompletionOption::ResponseContentRead, cancel);
            auto* concrete = dynamic_cast<CurlResponseMessage*>(response.get());
            if (concrete == nullptr)
            {
                throw NullReferenceException();
            }
            if (concrete->StatusCode == 404)
            {
                SetLastReason("no releases have been published yet");
                return std::nullopt;
            }
            if (concrete->StatusCode == 403 || concrete->StatusCode == 429)
            {
                SetLastReason("GitHub is rate-limiting this address; try later");
                return std::nullopt;
            }
            if (concrete->StatusCode < 200 || concrete->StatusCode > 299)
            {
                SetLastReason("GitHub answered " + std::to_string(concrete->StatusCode));
                return std::nullopt;
            }
            json = concrete->Body;
        }
        catch (const std::exception& ex)
        {
            SetLastReason("could not reach GitHub (" + ExceptionTypeName(ex) + ")");
            return std::nullopt;
        }
        catch (...)
        {
            SetLastReason("could not reach GitHub (Exception)");
            return std::nullopt;
        }
        return json;
    }

    std::optional<UpdateInfo> UpdateCheck::ServerAsset(CancellationToken cancel)
    {
        SetLastReason(std::nullopt);
        const std::optional<std::string> json = FetchLatest(cancel);
        if (!json.has_value())
        {
            return std::nullopt;
        }
        return ServerAsset(std::string_view(*json));
    }

    std::optional<UpdateInfo> UpdateCheck::ServerAsset(std::string_view json)
    {
        std::string tag;
        std::string page;
        std::vector<Asset> assets;
        try
        {
            const JsonValue root = JsonParser(json).ParseDocument();
            if (const JsonValue* value = TryGetProperty(root, "tag_name"))
            {
                tag = GetString(*value).value_or("");
            }
            if (const JsonValue* value = TryGetProperty(root, "html_url"))
            {
                page = GetString(*value).value_or("");
            }
            if (const JsonValue* list = TryGetProperty(root, "assets"))
            {
                if (list->Kind != JsonKind::Array)
                {
                    ThrowWrongType("Array", *list);
                }
                for (const JsonValue& item : list->Array)
                {
                    std::string name;
                    std::string url;
                    std::int64_t size = 0;
                    if (const JsonValue* value = TryGetProperty(item, "name"))
                    {
                        name = GetString(*value).value_or("");
                    }
                    if (const JsonValue* value = TryGetProperty(item, "browser_download_url"))
                    {
                        url = GetString(*value).value_or("");
                    }
                    if (const JsonValue* value = TryGetProperty(item, "size"))
                    {
                        std::int64_t parsedSize = 0;
                        if (TryGetInt64(*value, parsedSize))
                        {
                            size = parsedSize;
                        }
                    }
                    if (!name.empty())
                    {
                        assets.push_back(Asset{std::move(name), std::move(url), size});
                    }
                }
            }
        }
        catch (const NamedException& ex)
        {
            if (ex.Name() == "JsonException")
            {
                SetLastReason("GitHub's answer could not be read");
                return std::nullopt;
            }
            throw;
        }
        const std::optional<Asset> package = PickAsset(assets, ServerRid(), true);
        if (!package.has_value())
        {
            SetLastReason(ServerRid().empty()
                ? std::string("there is no dedicated-server package for macOS")
                : "the latest release (" + tag + ") has no server package for " + ServerRid());
            return std::nullopt;
        }
        const std::optional<Version> published = BuildVersion::Parse(tag);
        return UpdateInfo{
            .Tag = std::move(tag),
            .Version = published.has_value() ? *published : Version(0, 0, 0, 0),
            .AssetName = package->Name,
            .AssetUrl = package->Url,
            .AssetSize = package->Size,
            .PageUrl = !page.empty() ? std::move(page) : std::string(ReleasesPage),
            .Notes = std::string()
        };
    }

    std::string UpdateCheck::ServerRid()
    {
#if defined(__ANDROID__) || defined(__APPLE__)
        return "";
#else
    #if defined(__aarch64__) || defined(_M_ARM64)
        constexpr bool arm = true;
    #else
        constexpr bool arm = false;
    #endif
    #if defined(_WIN32)
        return arm ? "" : "win-x64";
    #else
        return arm ? "linux-arm64" : "linux-x64";
    #endif
#endif
    }

    std::string UpdateCheck::ServerBinaryName()
    {
#if defined(_WIN32)
        return std::string(Mods::Branding::FileName) + "Server.exe";
#else
        return std::string(Mods::Branding::FileName);
#endif
    }

    std::optional<UpdateInfo> UpdateCheck::Parse(std::string_view json,
        std::optional<Version> installed)
    {
        SetLastReason(std::nullopt);
        if (!installed.has_value())
        {
            installed = BuildVersion::Current();
        }
        if (!installed.has_value())
        {
            SetLastReason("this is a local build, so it is left alone");
            return std::nullopt;
        }

        std::string tag;
        std::string notes;
        std::string page;
        std::vector<Asset> assets;
        try
        {
            const JsonValue root = JsonParser(json).ParseDocument();
            if (const JsonValue* value = TryGetProperty(root, "tag_name"))
            {
                tag = GetString(*value).value_or("");
            }
            if (const JsonValue* value = TryGetProperty(root, "body"))
            {
                notes = GetString(*value).value_or("");
            }
            if (const JsonValue* value = TryGetProperty(root, "html_url"))
            {
                page = GetString(*value).value_or("");
            }
            if (const JsonValue* list = TryGetProperty(root, "assets"))
            {
                if (list->Kind != JsonKind::Array)
                {
                    ThrowWrongType("Array", *list);
                }
                for (const JsonValue& item : list->Array)
                {
                    std::string name;
                    std::string url;
                    std::int64_t size = 0;
                    if (const JsonValue* value = TryGetProperty(item, "name"))
                    {
                        name = GetString(*value).value_or("");
                    }
                    if (const JsonValue* value = TryGetProperty(item, "browser_download_url"))
                    {
                        url = GetString(*value).value_or("");
                    }
                    if (const JsonValue* value = TryGetProperty(item, "size"))
                    {
                        std::int64_t parsedSize = 0;
                        if (TryGetInt64(*value, parsedSize))
                        {
                            size = parsedSize;
                        }
                    }
                    if (!name.empty())
                    {
                        assets.push_back(Asset{std::move(name), std::move(url), size});
                    }
                }
            }
        }
        catch (const NamedException& ex)
        {
            if (ex.Name() == "JsonException")
            {
                SetLastReason("GitHub's answer could not be read");
                return std::nullopt;
            }
            throw;
        }

        const std::optional<Version> published = BuildVersion::Parse(tag);
        if (!published.has_value())
        {
            SetLastReason("the latest release (" + tag + ") is not a plain version tag");
            return std::nullopt;
        }
        const Version current = BuildVersion::Normalise(*installed);
        if (*published <= current)
        {
            SetLastReason("v" + current.ToString(3) + " is already the latest");
            return std::nullopt;
        }

        const std::optional<Asset> package = PickAsset(assets);
        return UpdateInfo{
            .Tag = std::move(tag),
            .Version = *published,
            .AssetName = package.has_value() ? package->Name : std::string(),
            .AssetUrl = package.has_value() ? package->Url : std::string(),
            .AssetSize = package.has_value() ? package->Size : 0,
            .PageUrl = !page.empty() ? std::move(page) : std::string(ReleasesPage),
            .Notes = std::move(notes)
        };
    }

    std::optional<UpdateInfo> UpdateCheck::Parse(std::nullptr_t,
        std::optional<Version> installed)
    {
        SetLastReason(std::nullopt);
        if (!installed.has_value())
        {
            installed = BuildVersion::Current();
        }
        if (!installed.has_value())
        {
            SetLastReason("this is a local build, so it is left alone");
            return std::nullopt;
        }
        throw ArgumentNullException("json");
    }

    bool UpdateCheck::IsServerBuild() noexcept
    {
#if defined(MPHREAD_SERVER)
        return true;
#else
        return false;
#endif
    }

    std::string UpdateCheck::Rid()
    {
#if defined(__ANDROID__)
        return "android";
#else
    #if defined(_WIN32)
        constexpr std::string_view os = "win";
    #elif defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
        constexpr std::string_view os = "osx";
    #else
        constexpr std::string_view os = "linux";
    #endif

    #if defined(__x86_64__) || defined(_M_X64)
        constexpr std::string_view arch = "x64";
    #elif defined(__aarch64__) || defined(_M_ARM64)
        constexpr std::string_view arch = "arm64";
    #elif defined(__i386__) || defined(_M_IX86)
        constexpr std::string_view arch = "x86";
    #elif defined(__arm__) || defined(_M_ARM)
        #if defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) \
            || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) \
            || defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__)
        constexpr std::string_view arch = "armv6";
        #else
        constexpr std::string_view arch = "arm";
        #endif
    #elif defined(__wasm__)
        constexpr std::string_view arch = "wasm";
    #elif defined(__s390x__)
        constexpr std::string_view arch = "s390x";
    #elif defined(__loongarch64)
        constexpr std::string_view arch = "loongarch64";
    #elif defined(__powerpc64__) && (defined(__LITTLE_ENDIAN__) \
        || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) \
            && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
        constexpr std::string_view arch = "ppc64le";
    #elif defined(__riscv) && __riscv_xlen == 64
        constexpr std::string_view arch = "riscv64";
    #else
        constexpr std::string_view arch = "unknown";
    #endif
        return std::string(os) + "-" + std::string(arch);
#endif
    }

    std::string UpdateCheck::PackageSuffix()
    {
        return IsServerBuild() ? "server-" + Rid() : Rid();
    }

    std::string UpdateCheck::BinaryName()
    {
#if !defined(_WIN32)
        return std::string(Mods::Branding::FileName);
#else
        std::string name(Mods::Branding::FileName);
        if (IsServerBuild())
        {
            name += "Server";
        }
        return name + ".exe";
#endif
    }
}
