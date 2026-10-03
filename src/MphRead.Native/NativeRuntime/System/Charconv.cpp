#include "Charconv.hpp"

#if !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <system_error>
#include <type_traits>

#if defined(__APPLE__)
#include <xlocale.h>
#elif !defined(__ANDROID__)
#include <locale.h>
#endif

namespace MphRead::NativeRuntime
{
    namespace
    {
        [[nodiscard]] bool IsDigit(char value) noexcept
        {
            return value >= '0' && value <= '9';
        }

        [[nodiscard]] char Lower(char value) noexcept
        {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
        }

        [[nodiscard]] bool StartsWithNoCase(const char* first, const char* last, const char* word) noexcept
        {
            for (; *word != '\0'; ++word, ++first)
            {
                if (first == last || Lower(*first) != *word)
                {
                    return false;
                }
            }
            return true;
        }

        // The end of the longest prefix from_chars would accept, or first when
        // there is none.
        [[nodiscard]] const char* Match(const char* first, const char* last, std::chars_format format) noexcept
        {
            const char* p = first;
            if (p != last && *p == '-')
            {
                ++p;
            }
            if (StartsWithNoCase(p, last, "infinity"))
            {
                return p + 8;
            }
            if (StartsWithNoCase(p, last, "inf"))
            {
                return p + 3;
            }
            if (StartsWithNoCase(p, last, "nan"))
            {
                const char* end = p + 3;
                if (end != last && *end == '(')
                {
                    const char* q = end + 1;
                    while (q != last && (IsDigit(*q) || (Lower(*q) >= 'a' && Lower(*q) <= 'z') || *q == '_'))
                    {
                        ++q;
                    }
                    if (q != last && *q == ')')
                    {
                        return q + 1;
                    }
                }
                return end;
            }
            const char* digitsStart = p;
            bool anyDigit = false;
            while (p != last && IsDigit(*p))
            {
                ++p;
                anyDigit = true;
            }
            if (p != last && *p == '.')
            {
                const char* afterPoint = p + 1;
                const char* q = afterPoint;
                bool fraction = false;
                while (q != last && IsDigit(*q))
                {
                    ++q;
                    fraction = true;
                }
                if (anyDigit || fraction)
                {
                    p = q;
                    anyDigit = true;
                }
            }
            if (!anyDigit || p == digitsStart)
            {
                return first;
            }
            const bool scientific = (format & std::chars_format::scientific) == std::chars_format::scientific;
            const bool fixed = (format & std::chars_format::fixed) == std::chars_format::fixed;
            if (scientific && p != last && (*p == 'e' || *p == 'E'))
            {
                const char* q = p + 1;
                if (q != last && (*q == '+' || *q == '-'))
                {
                    ++q;
                }
                const char* exponentDigits = q;
                while (q != last && IsDigit(*q))
                {
                    ++q;
                }
                if (q != exponentDigits)
                {
                    p = q;
                }
                else if (!fixed)
                {
                    // scientific alone requires the exponent.
                    return first;
                }
            }
            else if (scientific && !fixed)
            {
                return first;
            }
            return p;
        }

#if defined(__APPLE__) || (!defined(__ANDROID__) && !defined(_WIN32))
        [[nodiscard]] locale_t CLocale()
        {
            static const locale_t locale = ::newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(0));
            return locale;
        }
#elif defined(_WIN32)
        // MinGW: libc++ without floating from_chars; the CRT's _l variants.
        [[nodiscard]] _locale_t CLocale()
        {
            static const _locale_t locale = ::_create_locale(LC_ALL, "C");
            return locale;
        }
#endif

        template <typename T>
        [[nodiscard]] T Convert(const char* text, char** end)
        {
#if defined(__ANDROID__)
            // Bionic's conversions ignore the locale: the point is always '.'.
            if constexpr (std::is_same_v<T, float>)
            {
                return std::strtof(text, end);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return std::strtod(text, end);
            }
            else
            {
                return std::strtold(text, end);
            }
#elif defined(_WIN32)
            if constexpr (std::is_same_v<T, float>)
            {
                return ::_strtof_l(text, end, CLocale());
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return ::_strtod_l(text, end, CLocale());
            }
            else
            {
                // MinGW has no _strtold_l; the CRT starts in the "C" locale.
                return std::strtold(text, end);
            }
#else
            if constexpr (std::is_same_v<T, float>)
            {
                return ::strtof_l(text, end, CLocale());
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return ::strtod_l(text, end, CLocale());
            }
            else
            {
                return ::strtold_l(text, end, CLocale());
            }
#endif
        }

        template <typename T>
        [[nodiscard]] std::from_chars_result Parse(const char* first, const char* last, T& value,
            std::chars_format format)
        {
            const char* end = Match(first, last, format);
            if (end == first)
            {
                return {first, std::errc::invalid_argument};
            }
            const std::string text(first, end);
            char* parsedEnd = nullptr;
            errno = 0;
            const T parsed = Convert<T>(text.c_str(), &parsedEnd);
            const int error = errno;
            if (parsedEnd == text.c_str())
            {
                return {first, std::errc::invalid_argument};
            }
            const char* ptr = first + (parsedEnd - text.c_str());
            if (error == ERANGE && (std::isinf(parsed) || parsed == T{0}))
            {
                // Too large or too small to represent: from_chars leaves the
                // value alone and says so.
                return {ptr, std::errc::result_out_of_range};
            }
            value = parsed;
            return {ptr, std::errc{}};
        }
    }

    std::from_chars_result FromChars(const char* first, const char* last, float& value,
        std::chars_format format)
    {
        return Parse(first, last, value, format);
    }

    std::from_chars_result FromChars(const char* first, const char* last, double& value,
        std::chars_format format)
    {
        return Parse(first, last, value, format);
    }

    std::from_chars_result FromChars(const char* first, const char* last, long double& value,
        std::chars_format format)
    {
        return Parse(first, last, value, format);
    }
}

#endif
