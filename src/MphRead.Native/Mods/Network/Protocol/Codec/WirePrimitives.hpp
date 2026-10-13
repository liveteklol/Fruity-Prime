#pragma once

#include "NativeRuntime/System/Exceptions.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MphRead::Mods::Network::Protocol::Wire
{
    [[noreturn]] inline void R() { throw std::out_of_range("Index was outside the bounds of the array."); }
    template <typename T>
    T &At(std::span<T> span, std::size_t index)
    {
        if (index >= span.size())
        {
            R();
        }
        return span[index];
    }
    template <typename T>
    const T &At(std::span<const T> span, std::size_t index)
    {
        if (index >= span.size())
        {
            R();
        }
        return span[index];
    }
    template <typename T>
    std::span<T> Slice(std::span<T> span, std::size_t offset)
    {
        if (offset > span.size())
        {
            R();
        }
        return span.subspan(offset);
    }
    template <typename T>
    std::span<const T> Slice(std::span<const T> span, std::size_t offset)
    {
        if (offset > span.size())
        {
            R();
        }
        return span.subspan(offset);
    }
    template <typename T>
    std::span<T> Slice(std::span<T> span, std::size_t offset, std::size_t count)
    {
        if (offset > span.size() || count > span.size() - offset)
        {
            R();
        }
        return span.subspan(offset, count);
    }
    template <typename T>
    std::span<const T> Slice(std::span<const T> span, std::size_t offset, std::size_t count)
    {
        if (offset > span.size() || count > span.size() - offset)
        {
            R();
        }
        return span.subspan(offset, count);
    }
    template <typename T>
    inline void L(std::span<T> span, std::size_t count)
    {
        if (span.size() < count)
        {
            R();
        }
    }
    template <typename T>
    inline void L(std::span<const T> span, std::size_t count)
    {
        if (span.size() < count)
        {
            R();
        }
    }
    inline void Clear(std::span<std::uint8_t> span) { std::fill(span.begin(), span.end(), static_cast<std::uint8_t>(0)); }
    inline void CP(std::span<std::uint8_t> span, std::size_t count) { Clear(Slice(span, 0, count)); }
    inline void W16(std::span<std::uint8_t> destination, std::uint16_t value)
    {
        L(destination, 2);
        destination[0] = static_cast<std::uint8_t>(value);
        destination[1] = static_cast<std::uint8_t>(value >> 8);
    }
    inline std::uint16_t R16(std::span<const std::uint8_t> source)
    {
        L(source, 2);
        const std::uint16_t b0 = source[0];
        const std::uint16_t b1 = source[1];
        return static_cast<std::uint16_t>(b0 | static_cast<std::uint16_t>(b1 << 8));
    }
    inline void WI16(std::span<std::uint8_t> destination, std::int16_t value) { W16(destination, static_cast<std::uint16_t>(value)); }
    inline std::int16_t RI16(std::span<const std::uint8_t> source) { return static_cast<std::int16_t>(R16(source)); }
    inline void W32(std::span<std::uint8_t> destination, std::uint32_t value)
    {
        L(destination, 4);
        destination[0] = static_cast<std::uint8_t>(value);
        destination[1] = static_cast<std::uint8_t>(value >> 8);
        destination[2] = static_cast<std::uint8_t>(value >> 16);
        destination[3] = static_cast<std::uint8_t>(value >> 24);
    }
    inline std::uint32_t R32(std::span<const std::uint8_t> source)
    {
        L(source, 4);
        return static_cast<std::uint32_t>(source[0]) | (static_cast<std::uint32_t>(source[1]) << 8) | (static_cast<std::uint32_t>(source[2]) << 16) | (static_cast<std::uint32_t>(source[3]) << 24);
    }
    inline void WB32(std::span<std::uint8_t> destination, std::uint32_t value)
    {
        L(destination, 4);
        destination[0] = static_cast<std::uint8_t>(value >> 24);
        destination[1] = static_cast<std::uint8_t>(value >> 16);
        destination[2] = static_cast<std::uint8_t>(value >> 8);
        destination[3] = static_cast<std::uint8_t>(value);
    }
    inline std::uint32_t RB32(std::span<const std::uint8_t> source)
    {
        L(source, 4);
        return (static_cast<std::uint32_t>(source[0]) << 24) | (static_cast<std::uint32_t>(source[1]) << 16) | (static_cast<std::uint32_t>(source[2]) << 8) | static_cast<std::uint32_t>(source[3]);
    }
    inline void W64(std::span<std::uint8_t> destination, std::uint64_t value)
    {
        L(destination, 8);
        for (std::size_t i = 0; i < 8; ++i)
        {
            destination[i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    }
    inline std::uint64_t R64(std::span<const std::uint8_t> source)
    {
        L(source, 8);
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i)
        {
            value |= static_cast<std::uint64_t>(source[i]) << (8 * i);
        }
        return value;
    }
    inline void WF(std::span<std::uint8_t> destination, float value) { W32(destination, std::bit_cast<std::uint32_t>(value)); }
    inline float RF(std::span<const std::uint8_t> source) { return std::bit_cast<float>(R32(source)); }
    inline std::vector<std::uint16_t> U16(std::string_view utf8)
    {
        std::vector<std::uint16_t> result;
        result.reserve(utf8.size());
        std::size_t index = 0;
        while (index < utf8.size())
        {
            const auto first = static_cast<unsigned char>(utf8[index]);
            std::uint32_t codePoint = 0xFFFDU;
            std::size_t consumed = 1;
            if (first < 0x80U)
            {
                codePoint = first;
            }
            else if (first >= 0xC2U && first <= 0xDFU && index + 1 < utf8.size())
            {
                const auto b1 = static_cast<unsigned char>(utf8[index + 1]);
                if ((b1 & 0xC0U) == 0x80U)
                {
                    codePoint = (static_cast<std::uint32_t>(first & 0x1FU) << 6) | static_cast<std::uint32_t>(b1 & 0x3FU);
                    consumed = 2;
                }
            }
            else if (first >= 0xE0U && first <= 0xEFU && index + 2 < utf8.size())
            {
                const auto b1 = static_cast<unsigned char>(utf8[index + 1]);
                const auto b2 = static_cast<unsigned char>(utf8[index + 2]);
                const bool continuation = (b1 & 0xC0U) == 0x80U && (b2 & 0xC0U) == 0x80U;
                const bool notOverlong = first != 0xE0U || b1 >= 0xA0U;
                const bool notSurrogate = first != 0xEDU || b1 <= 0x9FU;
                if (continuation && notOverlong && notSurrogate)
                {
                    codePoint = (static_cast<std::uint32_t>(first & 0x0FU) << 12) | (static_cast<std::uint32_t>(b1 & 0x3FU) << 6) | static_cast<std::uint32_t>(b2 & 0x3FU);
                    consumed = 3;
                }
            }
            else if (first >= 0xF0U && first <= 0xF4U && index + 3 < utf8.size())
            {
                const auto b1 = static_cast<unsigned char>(utf8[index + 1]);
                const auto b2 = static_cast<unsigned char>(utf8[index + 2]);
                const auto b3 = static_cast<unsigned char>(utf8[index + 3]);
                const bool continuation = (b1 & 0xC0U) == 0x80U && (b2 & 0xC0U) == 0x80U && (b3 & 0xC0U) == 0x80U;
                const bool notOverlong = first != 0xF0U || b1 >= 0x90U;
                const bool inRange = first != 0xF4U || b1 <= 0x8FU;
                if (continuation && notOverlong && inRange)
                {
                    codePoint = (static_cast<std::uint32_t>(first & 0x07U) << 18) | (static_cast<std::uint32_t>(b1 & 0x3FU) << 12) | (static_cast<std::uint32_t>(b2 & 0x3FU) << 6) | static_cast<std::uint32_t>(b3 & 0x3FU);
                    consumed = 4;
                }
            }
            if (codePoint <= 0xFFFFU)
            {
                result.push_back(static_cast<std::uint16_t>(codePoint));
            }
            else
            {
                const std::uint32_t scalar = codePoint - 0x10000U;
                result.push_back(static_cast<std::uint16_t>(0xD800U + (scalar >> 10)));
                result.push_back(static_cast<std::uint16_t>(0xDC00U + (scalar & 0x3FFU)));
            }
            index += consumed;
        }
        return result;
    }
    inline std::string DA(std::span<const std::uint8_t> source)
    {
        std::string result;
        result.reserve(source.size());
        for (const std::uint8_t value : source)
        {
            result.push_back(value <= 0x7FU ? static_cast<char>(value) : '?');
        }
        return result;
    }
    inline bool ReadyAt(const std::shared_ptr<std::vector<bool>> &array, std::size_t index)
    {
        if (array == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (index >= array->size())
        {
            R();
        }
        return (*array)[index];
    }
    template <typename T>
    const T &AA(const std::shared_ptr<std::vector<T>> &array, std::size_t index)
    {
        if (array == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (index >= array->size())
        {
            R();
        }
        return (*array)[index];
    }
    template <typename T>
    T &AAM(const std::shared_ptr<std::vector<T>> &array, std::size_t index)
    {
        if (array == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (index >= array->size())
        {
            R();
        }
        return (*array)[index];
    }
}
