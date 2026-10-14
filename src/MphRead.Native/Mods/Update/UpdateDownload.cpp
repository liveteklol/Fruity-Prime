#include "UpdateDownload.hpp"
#include "../../NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/AtomicSharedPtr.hpp"

#include "../Branding.hpp"
#include "../MapGen/CurlTrust.hpp"
#include "BuildVersion.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/IO.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using ::MphRead::NativeRuntime::FileDelete;
using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;

namespace MphRead::Mods::Update
{
    namespace
    {
        ::MphRead::NativeRuntime::AtomicSharedPtr<const std::string> LastErrorValue{nullptr};

        using OperationCanceledException = ::System::OperationCanceledException;

        using IOException = ::System::IO::IOException;

        using UnauthorizedAccessException = ::System::UnauthorizedAccessException;

        [[nodiscard]] bool EndsWithOrdinalIgnoreCaseAscii(
            std::string_view value, std::string_view suffix) noexcept
        {
            return value.size() >= suffix.size()
                && StringEqualsOrdinalIgnoreCase(
                    value.substr(value.size() - suffix.size()), suffix);
        }

        struct CurlStringDeleter
        {
            void operator()(char* value) const noexcept
            {
                curl_free(value);
            }
        };

        struct CurlUrlDeleter
        {
            void operator()(CURLU* value) const noexcept
            {
                if (value != nullptr)
                {
                    curl_url_cleanup(value);
                }
            }
        };

        [[nodiscard]] bool TryGetUriSchemeAndHost(const std::string& url,
            std::string& scheme, std::string& host)
        {
            std::unique_ptr<CURLU, CurlUrlDeleter> parsed(curl_url());
            if (!parsed)
            {
                throw std::bad_alloc();
            }
            if (curl_url_set(parsed.get(), CURLUPART_URL, url.c_str(), 0) != CURLUE_OK)
            {
                return false;
            }

            char* rawScheme = nullptr;
            char* rawHost = nullptr;
            const CURLUcode schemeCode = curl_url_get(
                parsed.get(), CURLUPART_SCHEME, &rawScheme, 0);
            const CURLUcode hostCode = curl_url_get(
                parsed.get(), CURLUPART_HOST, &rawHost, 0);
            std::unique_ptr<char, CurlStringDeleter> schemeOwner(rawScheme);
            std::unique_ptr<char, CurlStringDeleter> hostOwner(rawHost);
            if (schemeCode != CURLUE_OK || hostCode != CURLUE_OK
                || rawScheme == nullptr || rawHost == nullptr)
            {
                return false;
            }
            scheme.assign(rawScheme);
            host.assign(rawHost);
            return true;
        }

        [[nodiscard]] bool IsSuccessStatus(long status) noexcept
        {
            return status >= 200 && status <= 299;
        }

        [[nodiscard]] bool IsAutomaticRedirectStatus(long status) noexcept
        {
            return status == 300 || status == 301 || status == 302
                || status == 303 || status == 307 || status == 308;
        }

        [[nodiscard]] std::string_view TrimHttpValue(std::string_view value) noexcept
        {
            while (!value.empty()
                && (value.front() == ' ' || value.front() == '\t'))
            {
                value.remove_prefix(1);
            }
            while (!value.empty()
                && (value.back() == ' ' || value.back() == '\t'
                    || value.back() == '\r' || value.back() == '\n'))
            {
                value.remove_suffix(1);
            }
            return value;
        }

        [[nodiscard]] bool HeaderNameEquals(
            std::string_view value, std::string_view expected) noexcept
        {
            return StringEqualsOrdinalIgnoreCase(value, expected);
        }

        [[nodiscard]] std::optional<std::int64_t> ParseContentLength(
            std::string_view value) noexcept
        {
            value = TrimHttpValue(value);
            if (value.empty())
            {
                return std::nullopt;
            }
            std::int64_t result = 0;
            const char* first = value.data();
            const char* last = first + value.size();
            const auto parsed = std::from_chars(first, last, result, 10);
            if (parsed.ec != std::errc{} || parsed.ptr != last || result < 0)
            {
                return std::nullopt;
            }
            return result;
        }

        [[noreturn]] void ThrowCurl(CURLcode code)
        {
            if (code == CURLE_OPERATION_TIMEDOUT
                || code == CURLE_ABORTED_BY_CALLBACK)
            {
                throw OperationCanceledException(curl_easy_strerror(code));
            }
            throw std::runtime_error(curl_easy_strerror(code));
        }

        [[noreturn]] void ThrowCurlMulti(CURLMcode code)
        {
            throw std::runtime_error(curl_multi_strerror(code));
        }

        class CurlTransfer final
        {
        public:
            CurlTransfer(std::string url, std::string userAgent,
                std::chrono::milliseconds timeout, CancellationToken cancel)
                : _url(std::move(url)), _userAgent(std::move(userAgent)), _cancel(cancel)
            {
                ThrowIfCancellationRequested();
                static std::once_flag curlOnce;
                static CURLcode curlInit = CURLE_OK;
                std::call_once(curlOnce, []
                {
                    curlInit = curl_global_init(CURL_GLOBAL_DEFAULT);
                });
                if (curlInit != CURLE_OK)
                {
                    ThrowCurl(curlInit);
                }

                _easy = curl_easy_init();
                if (_easy == nullptr)
                {
                    throw std::runtime_error("curl_easy_init failed");
                }
                // The phone's certificate authorities, as for the check.
                MapGen::CurlTrust::Apply(_easy);
                _multi = curl_multi_init();
                if (_multi == nullptr)
                {
                    curl_easy_cleanup(_easy);
                    _easy = nullptr;
                    throw std::runtime_error("curl_multi_init failed");
                }

                curl_slist* headers = nullptr;
                headers = curl_slist_append(headers, "Accept:");
                if (headers == nullptr)
                {
                    Cleanup();
                    throw std::bad_alloc();
                }
                _headers = headers;

                SetEasy(CURLOPT_URL, _url.c_str());
                SetEasy(CURLOPT_HTTPGET, 1L);
                SetEasy(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
                SetEasy(CURLOPT_HTTPHEADER, _headers);
                SetEasy(CURLOPT_USERAGENT, _userAgent.c_str());
                // HttpClientHandler.UseCookies is true by default. Enable
                // libcurl's in-memory cookie engine so redirect responses have
                // the same opportunity to set cookies for the next request.
                SetEasy(CURLOPT_COOKIEFILE, "");
                SetEasy(CURLOPT_FOLLOWLOCATION, 1L);
                SetEasy(CURLOPT_MAXREDIRS, 50L);
                SetEasy(CURLOPT_PROTOCOLS_STR, "https");
                SetEasy(CURLOPT_REDIR_PROTOCOLS_STR, "https");
                SetEasy(CURLOPT_NOSIGNAL, 1L);
                SetEasy(CURLOPT_TIMEOUT_MS,
                    static_cast<long>(timeout.count()));
                SetEasy(CURLOPT_HEADERFUNCTION, &CurlTransfer::HeaderThunk);
                SetEasy(CURLOPT_HEADERDATA, this);
                SetEasy(CURLOPT_WRITEFUNCTION, &CurlTransfer::WriteThunk);
                SetEasy(CURLOPT_WRITEDATA, this);
                SetEasy(CURLOPT_NOPROGRESS, 0L);
                SetEasy(CURLOPT_XFERINFOFUNCTION, &CurlTransfer::ProgressThunk);
                SetEasy(CURLOPT_XFERINFODATA, this);

                const CURLMcode add = curl_multi_add_handle(_multi, _easy);
                if (add != CURLM_OK)
                {
                    Cleanup();
                    ThrowCurlMulti(add);
                }
                _added = true;
            }

            CurlTransfer(const CurlTransfer&) = delete;
            CurlTransfer& operator=(const CurlTransfer&) = delete;

            ~CurlTransfer()
            {
                Cleanup();
            }

            void ReadHeaders()
            {
                for (;;)
                {
                    ThrowIfCancellationRequested();
                    Perform();
                    if (_headersComplete && !_redirecting)
                    {
                        ThrowIfCancellationRequested();
                        _responseExposed = true;
                        // With ResponseHeadersRead, HttpClient.Timeout covers the
                        // SendAsync operation only. Content reads are outside it.
                        const CURLcode clearTimeout = curl_easy_setopt(
                            _easy, CURLOPT_TIMEOUT_MS, 0L);
                        if (clearTimeout != CURLE_OK)
                        {
                            ThrowCurl(clearTimeout);
                        }
                        return;
                    }
                    if (_done)
                    {
                        ThrowIfCancellationRequested();
                        if (_result != CURLE_OK)
                        {
                            ThrowCurl(_result);
                        }
                        if (!_headersComplete)
                        {
                            throw std::runtime_error(
                                "An error occurred while sending the request.");
                        }
                        _responseExposed = true;
                        return;
                    }
                    Poll();
                }
            }

            [[nodiscard]] long StatusCode() const noexcept
            {
                return _statusCode;
            }

            [[nodiscard]] std::optional<std::int64_t> ContentLength() const noexcept
            {
                return _contentLength;
            }

            [[nodiscard]] std::size_t Read(char* destination, std::size_t length)
            {
                if (length == 0)
                {
                    return 0;
                }
                if (!_pending.empty())
                {
                    return ConsumePending(destination, length);
                }
                if (_done)
                {
                    // HttpContent streams can report EOF and let Fetch compare
                    // the received count. Do the same for libcurl's explicit
                    // short-Content-Length result so the C# error text wins.
                    if (_result != CURLE_OK && _result != CURLE_PARTIAL_FILE)
                    {
                        ThrowCurl(_result);
                    }
                    return 0;
                }

                _acceptBodyChunk = true;
                if (_paused)
                {
                    _paused = false;
                    const CURLcode resumed = curl_easy_pause(_easy, CURLPAUSE_CONT);
                    if (resumed != CURLE_OK)
                    {
                        ThrowCurl(resumed);
                    }
                    if (!_pending.empty())
                    {
                        return ConsumePending(destination, length);
                    }
                }

                for (;;)
                {
                    Perform();
                    if (!_pending.empty())
                    {
                        return ConsumePending(destination, length);
                    }
                    if (_done)
                    {
                        if (_result != CURLE_OK && _result != CURLE_PARTIAL_FILE)
                        {
                            ThrowCurl(_result);
                        }
                        return 0;
                    }
                    Poll();
                }
            }

        private:
            void ThrowIfCancellationRequested() const
            {
                if (_cancel != nullptr && _cancel->stop_requested())
                {
                    throw OperationCanceledException();
                }
            }

            template <typename T>
            void SetEasy(CURLoption option, T value)
            {
                const CURLcode code = curl_easy_setopt(_easy, option, value);
                if (code != CURLE_OK)
                {
                    Cleanup();
                    ThrowCurl(code);
                }
            }

            void Cleanup() noexcept
            {
                if (_multi != nullptr && _easy != nullptr && _added)
                {
                    curl_multi_remove_handle(_multi, _easy);
                    _added = false;
                }
                if (_headers != nullptr)
                {
                    curl_slist_free_all(_headers);
                    _headers = nullptr;
                }
                if (_easy != nullptr)
                {
                    curl_easy_cleanup(_easy);
                    _easy = nullptr;
                }
                if (_multi != nullptr)
                {
                    curl_multi_cleanup(_multi);
                    _multi = nullptr;
                }
            }

            void Perform()
            {
                int running = 0;
                const CURLMcode code = curl_multi_perform(_multi, &running);
                if (code != CURLM_OK)
                {
                    ThrowCurlMulti(code);
                }
                DrainMessages();
                if (_callbackFailure)
                {
                    std::rethrow_exception(_callbackFailure);
                }
            }

            void Poll()
            {
                int count = 0;
                const CURLMcode code = curl_multi_poll(
                    _multi, nullptr, 0, 1000, &count);
                if (code != CURLM_OK)
                {
                    ThrowCurlMulti(code);
                }
                (void)count;
            }

            void DrainMessages() noexcept
            {
                int remaining = 0;
                while (CURLMsg* message = curl_multi_info_read(_multi, &remaining))
                {
                    if (message->msg == CURLMSG_DONE
                        && message->easy_handle == _easy)
                    {
                        _done = true;
                        _result = message->data.result;
                    }
                }
            }

            [[nodiscard]] std::size_t ConsumePending(
                char* destination, std::size_t length)
            {
                const std::size_t count = std::min(length, _pending.size());
                std::copy_n(_pending.data(), count, destination);
                if (count == _pending.size())
                {
                    _pending.clear();
                }
                else
                {
                    _pending.erase(_pending.begin(),
                        _pending.begin() + static_cast<std::ptrdiff_t>(count));
                }
                return count;
            }

            static std::size_t HeaderThunk(char* data, std::size_t size,
                std::size_t count, void* opaque) noexcept
            {
                const std::size_t bytes = size * count;
                auto* self = static_cast<CurlTransfer*>(opaque);
                try
                {
                    self->OnHeader(std::string_view(data, bytes));
                    return bytes;
                }
                catch (...)
                {
                    self->_callbackFailure = std::current_exception();
                    return 0;
                }
            }

            static std::size_t WriteThunk(char* data, std::size_t size,
                std::size_t count, void* opaque) noexcept
            {
                const std::size_t bytes = size * count;
                auto* self = static_cast<CurlTransfer*>(opaque);
                try
                {
                    if (!self->_responseExposed)
                    {
                        if (self->_redirecting)
                        {
                            return bytes;
                        }
                        self->_paused = true;
                        return CURL_WRITEFUNC_PAUSE;
                    }
                    if (!self->_acceptBodyChunk)
                    {
                        self->_paused = true;
                        return CURL_WRITEFUNC_PAUSE;
                    }
                    self->_pending.insert(self->_pending.end(), data, data + bytes);
                    self->_acceptBodyChunk = false;
                    return bytes;
                }
                catch (...)
                {
                    self->_callbackFailure = std::current_exception();
                    return 0;
                }
            }

            static int ProgressThunk(void* opaque, curl_off_t, curl_off_t,
                curl_off_t, curl_off_t) noexcept
            {
                const auto* self = static_cast<const CurlTransfer*>(opaque);
                // ResponseHeadersRead ends HttpClient.Send's cancellation
                // window when headers are exposed; the body loop checks the
                // token after each synchronous read, as the C# source does.
                return !self->_responseExposed
                    && self->_cancel != nullptr
                    && self->_cancel->stop_requested()
                        ? 1 : 0;
            }

            void OnHeader(std::string_view line)
            {
                if (_responseExposed)
                {
                    return;
                }
                if (line.size() >= 5 && line.substr(0, 5) == "HTTP/")
                {
                    _headersComplete = false;
                    _redirecting = false;
                    _hasLocation = false;
                    _contentLength.reset();
                    _statusCode = 0;

                    const std::size_t space = line.find(' ');
                    if (space != std::string_view::npos)
                    {
                        std::string_view rest = line.substr(space + 1);
                        while (!rest.empty() && rest.front() == ' ')
                        {
                            rest.remove_prefix(1);
                        }
                        const std::size_t end = rest.find(' ');
                        const std::string_view codeText = rest.substr(0, end);
                        long parsed = 0;
                        const char* first = codeText.data();
                        const char* last = first + codeText.size();
                        const auto result = std::from_chars(first, last, parsed, 10);
                        if (result.ec == std::errc{} && result.ptr == last)
                        {
                            _statusCode = parsed;
                        }
                    }
                    return;
                }

                if (line == "\r\n" || line == "\n")
                {
                    if (_statusCode >= 100 && _statusCode <= 199)
                    {
                        _headersComplete = false;
                        return;
                    }
                    _headersComplete = true;
                    _redirecting = IsAutomaticRedirectStatus(_statusCode)
                        && _hasLocation;
                    return;
                }

                const std::size_t colon = line.find(':');
                if (colon == std::string_view::npos)
                {
                    return;
                }
                const std::string_view name = line.substr(0, colon);
                const std::string_view value = line.substr(colon + 1);
                if (HeaderNameEquals(name, "Content-Length"))
                {
                    _contentLength = ParseContentLength(value);
                }
                else if (HeaderNameEquals(name, "Location"))
                {
                    _hasLocation = true;
                }
            }

            std::string _url;
            std::string _userAgent;
            CancellationToken _cancel = nullptr;
            CURL* _easy = nullptr;
            CURLM* _multi = nullptr;
            curl_slist* _headers = nullptr;
            bool _added = false;
            bool _headersComplete = false;
            bool _redirecting = false;
            bool _hasLocation = false;
            bool _responseExposed = false;
            bool _paused = false;
            bool _acceptBodyChunk = false;
            bool _done = false;
            CURLcode _result = CURLE_OK;
            long _statusCode = 0;
            std::optional<std::int64_t> _contentLength;
            std::vector<char> _pending;
            std::exception_ptr _callbackFailure;
        };

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
            explicit CurlResponseMessage(std::unique_ptr<CurlTransfer> transfer)
                : Transfer(std::move(transfer))
            {
            }

            [[nodiscard]] long StatusCode() const noexcept
            {
                return Transfer->StatusCode();
            }

            [[nodiscard]] std::optional<std::int64_t> ContentLength() const noexcept
            {
                return Transfer->ContentLength();
            }

            [[nodiscard]] std::size_t Read(char* destination, std::size_t length)
            {
                return Transfer->Read(destination, length);
            }

            void DisposeContent() noexcept
            {
                Transfer.reset();
            }

            std::unique_ptr<CurlTransfer> Transfer;
        };

        class CurlContentStream final
        {
        public:
            explicit CurlContentStream(CurlResponseMessage& response) noexcept
                : _response(response)
            {
            }

            [[nodiscard]] std::size_t Read(char* destination, std::size_t length)
            {
                return _response.Read(destination, length);
            }

            void Dispose() noexcept
            {
                if (!_disposed)
                {
                    _response.DisposeContent();
                    _disposed = true;
                }
            }

            ~CurlContentStream()
            {
                Dispose();
            }

        private:
            CurlResponseMessage& _response;
            bool _disposed = false;
        };

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
            explicit CurlClient(std::chrono::milliseconds timeout)
                : _timeout(timeout)
            {
            }

            void AddUserAgent(std::string value)
            {
                _userAgent = std::move(value);
            }

            [[nodiscard]] std::unique_ptr<HttpResponseTask> SendAsync(
                HttpRequestMessage* request,
                HttpCompletionOption completion,
                CancellationToken cancel) override
            {
                try
                {
                    if (completion != HttpCompletionOption::ResponseHeadersRead)
                    {
                        throw std::out_of_range("completionOption");
                    }
                    auto* concrete = dynamic_cast<CurlRequestMessage*>(request);
                    if (concrete == nullptr)
                    {
                        throw NullReferenceException();
                    }
                    auto transfer = std::make_unique<CurlTransfer>(
                        concrete->Url, _userAgent, _timeout, cancel);
                    transfer->ReadHeaders();
                    return std::make_unique<CurlTask>(
                        std::make_unique<CurlResponseMessage>(std::move(transfer)));
                }
                catch (...)
                {
                    return std::make_unique<CurlTask>(std::current_exception());
                }
            }

        private:
            std::chrono::milliseconds _timeout;
            std::string _userAgent;
        };

        [[noreturn]] void ThrowFileError(
            const std::string& path, const std::error_code& error)
        {
            if (error == std::errc::permission_denied)
            {
                throw UnauthorizedAccessException(
                    "Access to the path '" + path + "' is denied.");
            }
            throw IOException(error.message());
        }

        void CreateParentDirectory(const std::string& path)
        {
            const std::filesystem::path native = PathFromUtf8(path);
            const std::filesystem::path directory = native.parent_path();
            if (directory.empty())
            {
                return;
            }
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error)
            {
                ThrowFileError(path, error);
            }
        }

        void MoveFile(const std::string& source, const std::string& destination)
        {
            std::error_code error;
            std::filesystem::rename(
                PathFromUtf8(source), PathFromUtf8(destination), error);
            if (error)
            {
                ThrowFileError(destination, error);
            }
        }

        class OutputFile final
        {
        public:
            explicit OutputFile(const std::string& path)
            {
#if defined(_WIN32)
                const std::filesystem::path native = PathFromUtf8(path);
                _handle = CreateFileW(
                    native.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
                if (_handle == INVALID_HANDLE_VALUE)
                {
                    ThrowFileError(path, std::error_code(
                        static_cast<int>(GetLastError()), std::system_category()));
                }
#else
                int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_CLOEXEC
                flags |= O_CLOEXEC;
#endif
                _fd = ::open(PathFromUtf8(path).c_str(), flags, 0666);
                if (_fd < 0)
                {
                    ThrowFileError(path,
                        std::error_code(errno, std::generic_category()));
                }
                if (::flock(_fd, LOCK_EX | LOCK_NB) != 0)
                {
                    const std::error_code error(errno, std::generic_category());
                    const int descriptor = _fd;
                    _fd = -1;
                    (void)::close(descriptor);
                    ThrowFileError(path, error);
                }
#endif
            }

            void Write(const char* data, std::size_t size)
            {
#if defined(_WIN32)
                std::size_t offset = 0;
                while (offset < size)
                {
                    const std::size_t remaining = size - offset;
                    const DWORD count = static_cast<DWORD>(std::min<std::size_t>(
                        remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
                    DWORD written = 0;
                    if (!WriteFile(_handle, data + offset, count, &written, nullptr))
                    {
                        ThrowFileError("", std::error_code(
                            static_cast<int>(GetLastError()), std::system_category()));
                    }
                    if (written == 0)
                    {
                        throw IOException("The disk is full.");
                    }
                    offset += static_cast<std::size_t>(written);
                }
#else
                std::size_t offset = 0;
                while (offset < size)
                {
                    const ssize_t written = ::write(
                        _fd, data + offset, size - offset);
                    if (written < 0)
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        throw IOException(
                            std::error_code(errno, std::generic_category()).message());
                    }
                    if (written == 0)
                    {
                        throw IOException("No space left on device");
                    }
                    offset += static_cast<std::size_t>(written);
                }
#endif
            }

            void Close()
            {
                if (_closed)
                {
                    return;
                }
#if defined(_WIN32)
                HANDLE handle = _handle;
                _handle = INVALID_HANDLE_VALUE;
                if (!CloseHandle(handle))
                {
                    throw IOException(std::error_code(
                        static_cast<int>(GetLastError()), std::system_category()).message());
                }
#else
                const int descriptor = _fd;
                _fd = -1;
                if (::close(descriptor) != 0)
                {
                    throw IOException(
                        std::error_code(errno, std::generic_category()).message());
                }
#endif
                _closed = true;
            }

            ~OutputFile()
            {
                if (_closed)
                {
                    return;
                }
#if defined(_WIN32)
                if (_handle != INVALID_HANDLE_VALUE)
                {
                    (void)CloseHandle(_handle);
                }
#else
                if (_fd >= 0)
                {
                    (void)::close(_fd);
                }
#endif
            }

        private:
#if defined(_WIN32)
            HANDLE _handle = INVALID_HANDLE_VALUE;
#else
            int _fd = -1;
#endif
            bool _closed = false;
        };

        [[nodiscard]] std::int64_t AddUnchecked(
            std::int64_t value, std::size_t amount) noexcept
        {
            const std::uint64_t left = static_cast<std::uint64_t>(value);
            const std::uint64_t right = static_cast<std::uint64_t>(amount);
            return static_cast<std::int64_t>(left + right);
        }

        [[nodiscard]] float ProgressValue(
            std::int64_t done, std::int64_t total) noexcept
        {
            if (total <= 0)
            {
                return -1.0F;
            }
            const double ratio = static_cast<double>(done)
                / static_cast<double>(total);
            const float converted = static_cast<float>(ratio);
            return std::min(1.0F, converted);
        }

        void AssignLastError(std::optional<std::string> value)
        {
            LastErrorValue.store(value.has_value()
                ? std::make_shared<const std::string>(std::move(*value))
                : std::shared_ptr<const std::string>(),
                std::memory_order_relaxed);
        }

        [[nodiscard]] bool FetchCore(
            const std::string& url,
            const std::string* path,
            const std::string& partial,
            std::int64_t expectedBytes,
            const std::function<void(float)>& progress,
            CancellationToken cancel,
            std::chrono::milliseconds timeout)
        {
            if (path != nullptr)
            {
                CreateParentDirectory(*path);
            }

            CurlClient client(timeout);
            client.AddUserAgent(std::string(Mods::Branding::FileName) + "/"
                + BuildVersion::Display());
            CurlRequestMessage request(url);
            std::unique_ptr<HttpResponseMessage> response = SyncHttp::Send(
                &client, &request, HttpCompletionOption::ResponseHeadersRead, cancel);
            auto* concrete = dynamic_cast<CurlResponseMessage*>(response.get());
            if (concrete == nullptr)
            {
                throw NullReferenceException();
            }
            if (!IsSuccessStatus(concrete->StatusCode()))
            {
                AssignLastError(
                    "GitHub answered " + std::to_string(concrete->StatusCode()));
                return false;
            }

            const std::int64_t total
                = concrete->ContentLength().value_or(expectedBytes);
            {
                // ReadAsStreamAsync(...) is the outer using and FileStream is
                // the inner using in C#: target therefore closes first, then
                // the response content stream, before destination replacement.
                if (cancel != nullptr && cancel->stop_requested())
                {
                    throw OperationCanceledException();
                }
                CurlContentStream source(*concrete);
                OutputFile target(partial);
                std::vector<char> buffer;
                std::int64_t done = 0;
                bool endedEarly = false;
                std::exception_ptr failure;
                try
                {
                    buffer.resize(64U * 1024U);
                    for (;;)
                    {
                        const std::size_t read = source.Read(
                            buffer.data(), buffer.size());
                        if (read == 0)
                        {
                            break;
                        }
                        // C# checks cancellation after each successful Read
                        // and before writing the returned chunk.
                        if (cancel != nullptr && cancel->stop_requested())
                        {
                            throw OperationCanceledException();
                        }
                        target.Write(buffer.data(), read);
                        done = AddUnchecked(done, read);
                        if (progress)
                        {
                            progress(ProgressValue(done, total));
                        }
                    }
                    if (total > 0 && done != total)
                    {
                        // In C#, this assignment occurs before leaving the using
                        // scope. A FileStream.Dispose failure then replaces this
                        // message in the outer catch.
                        AssignLastError("the download ended early");
                        endedEarly = true;
                    }
                }
                catch (...)
                {
                    failure = std::current_exception();
                }

                // Nested C# using statements dispose target first and source
                // second, including while another exception is already active.
                // A target-dispose failure replaces the body failure.
                try
                {
                    target.Close();
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
                source.Dispose();

                if (failure)
                {
                    std::rethrow_exception(failure);
                }
                if (endedEarly)
                {
                    return false;
                }
            }

            if (path != nullptr && FileExists(*path))
            {
                FileDelete(*path);
            }
            if (path == nullptr)
            {
                throw std::invalid_argument(
                    "Value cannot be null. (Parameter 'destFileName')");
            }
            if (path->empty())
            {
                throw std::invalid_argument(
                    "The value cannot be an empty string. (Parameter 'destFileName')");
            }
            MoveFile(partial, *path);
            if (progress)
            {
                progress(1.0F);
            }
            return true;
        }
    }

    std::optional<std::string> UpdateDownload::LastError()
    {
        const std::shared_ptr<const std::string> value
            = LastErrorValue.load(std::memory_order_relaxed);
        return value == nullptr
            ? std::nullopt
            : std::optional<std::string>(*value);
    }

    void UpdateDownload::SetLastError(std::optional<std::string> value)
    {
        AssignLastError(std::move(value));
    }

    bool UpdateDownload::Fetch(
        const std::string& url,
        const std::string& path,
        std::int64_t expectedBytes,
        const std::function<void(float)>& progress,
        CancellationToken cancel)
    {
        return FetchPath(url, &path, expectedBytes, progress, cancel);
    }

    bool UpdateDownload::Fetch(
        std::nullptr_t,
        const std::string&,
        std::int64_t,
        const std::function<void(float)>&,
        CancellationToken)
    {
        SetLastError(std::nullopt);
        SetLastError("that download address is not GitHub's");
        return false;
    }

    bool UpdateDownload::Fetch(
        const std::string& url,
        std::nullptr_t,
        std::int64_t expectedBytes,
        const std::function<void(float)>& progress,
        CancellationToken cancel)
    {
        return FetchPath(url, nullptr, expectedBytes, progress, cancel);
    }

    bool UpdateDownload::Fetch(
        std::nullptr_t,
        std::nullptr_t,
        std::int64_t,
        const std::function<void(float)>&,
        CancellationToken)
    {
        SetLastError(std::nullopt);
        SetLastError("that download address is not GitHub's");
        return false;
    }

    bool UpdateDownload::FetchPath(
        const std::string& url,
        const std::string* path,
        std::int64_t expectedBytes,
        const std::function<void(float)>& progress,
        CancellationToken cancel)
    {
        SetLastError(std::nullopt);
        if (!IsAllowed(url))
        {
            SetLastError("that download address is not GitHub's");
            return false;
        }

        const std::string partial = path == nullptr
            ? std::string(".part")
            : *path + ".part";
        bool result = false;
        try
        {
            result = FetchCore(url, path, partial, expectedBytes, progress, cancel,
                std::chrono::duration_cast<std::chrono::milliseconds>(_timeout));
        }
        catch (const OperationCanceledException&)
        {
            SetLastError("cancelled");
            result = false;
        }
        catch (const std::exception& ex)
        {
            SetLastError(ex.what());
            result = false;
        }
        catch (...)
        {
            SetLastError("Exception");
            result = false;
        }

        try
        {
            if (FileExists(partial))
            {
                FileDelete(partial);
            }
        }
        catch (const IOException&)
        {
            // A leftover .part is litter, not a failure.
        }
        return result;
    }

    bool UpdateDownload::IsAllowed(const std::string& url)
    {
        std::string scheme;
        std::string host;
        if (!TryGetUriSchemeAndHost(url, scheme, host)
            || !StringEqualsOrdinalIgnoreCase(scheme, "https"))
        {
            return false;
        }
        return StringEqualsOrdinalIgnoreCase(host, _releaseHost)
            || StringEqualsOrdinalIgnoreCase(host, _assetHost)
            || EndsWithOrdinalIgnoreCaseAscii(host,
                std::string(".") + std::string(_releaseHost))
            || EndsWithOrdinalIgnoreCaseAscii(host, ".githubusercontent.com");
    }
}
