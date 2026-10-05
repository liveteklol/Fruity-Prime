#include "TestMisc.hpp"

#include "../Entities/CamSeq/CamSeqEntity.hpp"
#include "../Entities/CamSeq/CameraSequence.hpp"
#include "../Formats/Collision.hpp"
#include "../Formats/Sound.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Metadata/Rooms.hpp"
#include "../Read.hpp"
#include "../Utility/Archive.hpp"
#include "../Utility/Compress.hpp"
#include "../Utility/Repack.hpp"
#include "../Utility/RepackCollision.hpp"
#include "../Utility/Rng.hpp"
#include "../NativeRuntime/System/Globalization.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/Stb/Image.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::DirectoryCreateDirectory;
using ::MphRead::NativeRuntime::FileDelete;
using ::MphRead::NativeRuntime::FileReadAllBytes;
using ::MphRead::NativeRuntime::FileWriteAllBytes;
using ::MphRead::NativeRuntime::Int32ToUInt32;
using ::MphRead::NativeRuntime::PathGetDirectoryName;
using ::MphRead::NativeRuntime::PathGetFileName;
using ::MphRead::NativeRuntime::PathGetFileNameWithoutExtension;
using ::MphRead::NativeRuntime::StringReplace;
using ::MphRead::NativeRuntime::UInt32ToInt32;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::MphRead::NativeRuntime::UncheckedSubtract;

namespace MphRead::Export::ImagesInterop
{
    void WritePngRgb(
        std::span<const std::uint8_t> buffer,
        std::int32_t width,
        std::int32_t height,
        std::ostream& stream);
}

namespace MphRead::Testing::TestMiscInterop
{
    // StbImage.Load(stream, StbiImageFormat.Rgb).Data: the decoded pixels,
    // three bytes each. Declared and never defined before, which GCC and Clang
    // forgave and MSVC refused at the final link.
    [[nodiscard]] std::vector<std::uint8_t> LoadPngRgb(std::istream& stream)
    {
        const std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        return ::MphRead::NativeRuntime::LoadPng(bytes, 3).Pixels;
    }
}

namespace
{
    using byte = std::uint8_t;
    using ushort = std::uint16_t;
    using uint = std::uint32_t;
    using ulong = std::uint64_t;

    [[nodiscard]] constexpr int ArithmeticShiftRight(int value, unsigned count) noexcept
    {
        const uint bits = Int32ToUInt32(value);
        if (value >= 0)
        {
            return UInt32ToInt32(bits >> count);
        }
        return UInt32ToInt32((bits >> count) | (~uint{0} << (32U - count)));
    }

    void ReadExactly(std::istream& stream, std::span<byte> destination)
    {
        if (destination.empty())
        {
            return;
        }
        stream.read(reinterpret_cast<char*>(destination.data()),
            static_cast<std::streamsize>(destination.size()));
        if (stream.gcount() != static_cast<std::streamsize>(destination.size()))
        {
            throw System::IO::EndOfStreamException();
        }
    }

    [[nodiscard]] std::int32_t ReadInt32LittleEndian(std::istream& stream)
    {
        std::array<byte, 4> bytes{};
        ReadExactly(stream, bytes);
        const uint value = static_cast<uint>(bytes[0])
            | (static_cast<uint>(bytes[1]) << 8)
            | (static_cast<uint>(bytes[2]) << 16)
            | (static_cast<uint>(bytes[3]) << 24);
        return std::bit_cast<std::int32_t>(value);
    }

    [[nodiscard]] std::int32_t ReadInt32LittleEndian(std::span<const byte> bytes)
    {
        if (bytes.size() < 4)
        {
            throw System::ArgumentOutOfRangeException("start");
        }
        const uint value = static_cast<uint>(bytes[0])
            | (static_cast<uint>(bytes[1]) << 8)
            | (static_cast<uint>(bytes[2]) << 16)
            | (static_cast<uint>(bytes[3]) << 24);
        return std::bit_cast<std::int32_t>(value);
    }

    [[nodiscard]] std::vector<std::string> EnumerateFiles(const std::string& path)
    {
        std::vector<std::string> result;
        std::error_code error;
        std::filesystem::directory_iterator iterator(path, error);
        if (error)
        {
            throw System::IO::DirectoryNotFoundException(path);
        }
        const std::filesystem::directory_iterator end;
        for (; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                throw System::IO::IOException(error.message());
            }
            if (iterator->is_regular_file())
            {
                result.push_back(::MphRead::NativeRuntime::PathToUtf8(iterator->path()));
            }
        }
        return result;
    }

    void CopyFile(const std::string& source, const std::string& destination)
    {
        std::error_code error;
        const bool copied = std::filesystem::copy_file(
            source, destination, std::filesystem::copy_options::none, error);
        if (!copied || error)
        {
            throw System::IO::IOException(error ? error.message() : "The file already exists.");
        }
    }

    [[nodiscard]] double Average(const std::vector<double>& values)
    {
        if (values.empty())
        {
            throw std::runtime_error("Sequence contains no elements");
        }
        return std::accumulate(values.begin(), values.end(), 0.0)
            / static_cast<double>(values.size());
    }

    [[nodiscard]] double Round3(double value)
    {
        return std::nearbyint(value * 1000.0) / 1000.0;
    }

    [[nodiscard]] std::string FormatDouble(double value)
    {
        std::ostringstream stream;
        stream << std::defaultfloat << value;
        return stream.str();
    }

    void DebugWriteLine(const std::string& value)
    {
#ifndef NDEBUG
        std::clog << value << '\n';
#else
        (void)value;
#endif
    }

    void DebugBreak()
    {
#ifndef NDEBUG
        std::raise(SIGTRAP);
#endif
    }

    template <typename Map, typename Key>
    [[nodiscard]] decltype(auto) DictionaryAt(const Map& map, const Key& key)
    {
        const auto found = map.find(key);
        if (found == map.end())
        {
            throw std::out_of_range("The given key was not present in the dictionary.");
        }
        return (found->second);
    }

    [[nodiscard]] const std::string& RequireString(
        const std::optional<std::string>& value, const char* parameter)
    {
        if (!value)
        {
            throw System::ArgumentNullException(parameter);
        }
        return *value;
    }

    [[nodiscard]] std::string FrameFileName(int frameIndex)
    {
        std::ostringstream stream;
        stream << std::setfill('0') << std::setw(3) << frameIndex << ".png";
        return stream.str();
    }

    void WriteDebugAverages(const std::vector<double>& averages)
    {
        std::vector<double> sorted = averages;
        std::sort(sorted.begin(), sorted.end());
        std::ostringstream stream;
        stream << "Averages   = ";
        for (std::size_t i = 0; i < sorted.size(); ++i)
        {
            if (i != 0)
            {
                stream << ", ";
            }
            stream << FormatDouble(Round3(sorted[i]));
        }
        DebugWriteLine(stream.str());
    }
}

namespace MphRead::Testing
{
    const std::vector<int> TestMisc::_dword206B720 =
    {
-0x7F99, -0x7ECC, -0x7E00, -0x7D33, -0x7C66, -0x7B99, -0x7ACC, -0x7A00, -0x77FF, -0x74CD,
        -0x7199, -0x6E65, -0x6B33, -0x67FF, -0x64CD, -0x6199, -0x5E65, -0x5B33, -0x57FF, -0x5334,
        -0x4CCC, -0x4668, -0x4000, -0x3998, -0x3334, -0x2CCC, -0x2668, -0x2000, -0x1998, -0x1334,
        -0xCCC, -0x668, 0, 0x668, 0xCCC, 0x1334, 0x1998, 0x2000, 0x2668, 0x2CCC, 0x3334, 0x3998,
        0x4000, 0x4668, 0x4CCC, 0x5334, 0x57FF, 0x5B33, 0x5E67, 0x6199, 0x64CD, 0x67FF, 0x6B33,
        0x6E65, 0x7199, 0x74CD, 0x77FF, 0x7A00, 0x7ACC, 0x7B99, 0x7C66, 0x7D33, 0x7E00, 0x7ECC
    };
    const std::vector<int> TestMisc::_dword206B820 =
    {
-0x6B33, -0x67FF, -0x64CD, -0x6199, -0x5E65, -0x5B33, -0x57FF, -0x5334, -0x4CCC, -0x4668,
        -0x4000, -0x3998, -0x3334, -0x2CCC, -0x2668, -0x2000, -0x1998, -0x1334, -0xCCC, -0x668,
        0, 0x668, 0xCCC, 0x1334, 0x1998, 0x2000, 0x2668, 0x2CCC, 0x3334, 0x3998, 0x4000, 0x4668
    };
    const std::vector<int> TestMisc::_dword206B8A0 =
    {
-0x4668, -0x4000, -0x3998, -0x3334, -0x2CCC, -0x2668, -0x2000, -0x1998, -0x1334, -0xCCC,
        -0x668, 0, 0x668, 0xCCC, 0x1334, 0x1998, 0x2000, 0x2668, 0x2CCC, 0x3334, 0x3998, 0x4000,
        0x4668, 0x4CCC, 0x5334, 0x57FF, 0x5B33, 0x5E67, 0x6199, 0x64CD, 0x67FF, 0x6B33
    };
    const std::vector<int> TestMisc::_dword206B920 =
    {
-0x4CD0, -0x436C, -0x3A0C, -0x30A8, -0x2744, -0x1DE0, -0x1480, -0xB1C, -0x1B8, 0x7A8,
        0x110C, 0x1A70, 0x23D4, 0x2D34, 0x3698, 0x3FFC
    };
    const std::vector<int> TestMisc::_dword206B960 =
    {
-0x3334, -0x23D8, -0x147C, -0x520, 0xA3C, 0x1998, 0x28F4, 0x384C
    };
    const std::vector<int> TestMisc::_dword206B980 =
    {
-0x199C, -0xB1C, 0x368, 0x11E8, 0x2068, 0x2EEC, 0x3D6C, 0x4BEC
    };
    const std::vector<int> TestMisc::_dword206BEBC =
    {
-0x2668, -0x1DDC, -0x1554, -0xCCC, -0x444, 0x444, 0xCCC, 0x1554, 0x1DDC, 0x2668, 0x2EF0,
        0x3778, 0x4000, 0x4888, 0x5110, 0x57FF
    };
    const std::vector<short> TestMisc::_dword206C268 =
    {
-0x1C, -0x14, -0x0C, -0x04, 0x04, 0x0C, 0x14, 0x1C, -0x38, -0x28, -0x18,
        -0x08, 0x08, 0x18, 0x28, 0x38, -0x54, -0x3C, -0x24, -0x0C, 0x0C, 0x24, 0x3C, 0x54, -0x70,
        -0x50, -0x30, -0x10, 0x10, 0x30, 0x50, 0x70, -0x8C, -0x64, -0x3C, -0x14,
        0x14, 0x3C, 0x64, 0x8C, -0xA8, -0x78, -0x48, -0x18, 0x18, 0x48, 0x78, 0xA8, -0xC4, -0x8C,
        -0x54, -0x1C, 0x1C, 0x54, 0x8C, 0xC4, -0xE0, -0xA0, -0x60, -0x20, 0x20, 0x60, 0xA0,
        0xE0, -0xFC, -0xB4, -0x6C, -0x24, 0x24, 0x6C, 0xB4, 0xFC, -0x118, -0xC8, -0x78,
        -0x28, 0x28, 0x78, 0xC8, 0x118, -0x134, -0xDC, -0x84, -0x2C, 0x2C, 0x84, 0xDC, 0x134, -0x150,
        -0xF0, -0x90, -0x30, 0x30, 0x90, 0xF0, 0x150, -0x16C, -0x104, -0x9C, -0x34,
        0x34, 0x9C, 0x104, 0x16C, -0x188, -0x118, -0xA8, -0x38, 0x38, 0xA8, 0x118, 0x188, -0x1A4, -0x12C,
        -0xB4, -0x3C, 0x3C, 0xB4, 0x12C, 0x1A4, -0x1C0, -0x140, -0xC0, -0x40, 0x40, 0xC0, 0x140,
        0x1C0, -0x1F8, -0x168, -0xD8, -0x48, 0x48, 0xD8, 0x168, 0x1F8, -0x230, -0x190, -0xF0,
        -0x50, 0x50, 0xF0, 0x190, 0x230, -0x268, -0x1B8, -0x108, -0x58, 0x58, 0x108, 0x1B8, 0x268,
        -0x2A0, -0x1E0, -0x120, -0x60, 0x60, 0x120, 0x1E0, 0x2A0, -0x2D8, -0x208, -0x138,
        -0x68, 0x68, 0x138, 0x208, 0x2D8, -0x310, -0x230, -0x150, -0x70, 0x70, 0x150, 0x230,
        0x310, -0x348, -0x258, -0x168, -0x78, 0x78, 0x168, 0x258, 0x348, -0x380, -0x280,
        -0x180, -0x80, 0x80, 0x180, 0x280, 0x380, -0x3F0, -0x2D0, -0x1B0, -0x90, 0x90,
        0x1B0, 0x2D0, 0x3F0, -0x460, -0x320, -0x1E0, -0xA0, 0xA0, 0x1E0, 0x320, 0x460, -0x4D0,
        -0x370, -0x210, -0xB0, 0xB0, 0x210, 0x370, 0x4D0, -0x540, -0x3C0, -0x240, -0xC0,
        0xC0, 0x240, 0x3C0, 0x540, -0x5B0, -0x410, -0x270, -0xD0, 0xD0, 0x270, 0x410, 0x5B0,
        -0x620, -0x460, -0x2A0, -0xE0, 0xE0, 0x2A0, 0x460, 0x620, -0x690, -0x4B0, -0x2D0,
        -0xF0, 0xF0, 0x2D0, 0x4B0, 0x690, -0x700, -0x500, -0x300, -0x100, 0x100, 0x300, 0x500,
        0x700, -0x7E0, -0x5A0, -0x360, -0x120, 0x120, 0x360, 0x5A0, 0x7E0, -0x8C0, -0x640,
        -0x3C0, -0x140, 0x140, 0x3C0, 0x640, 0x8C0, -0x9A0, -0x6E0, -0x420, -0x160, 0x160,
        0x420, 0x6E0, 0x9A0, -0xA80, -0x780, -0x480, -0x180, 0x180, 0x480, 0x780, 0xA80, -0xB60,
        -0x820, -0x4E0, -0x1A0, 0x1A0, 0x4E0, 0x820, 0xB60, -0xC40, -0x8C0, -0x540, -0x1C0,
        0x1C0, 0x540, 0x8C0, 0xC40, -0xD20, -0x960, -0x5A0, -0x1E0, 0x1E0, 0x5A0, 0x960, 0xD20,
        -0xE00, -0xA00, -0x600, -0x200, 0x200, 0x600, 0xA00, 0xE00, -0xFC0, -0xB40, -0x6C0,
        -0x240, 0x240, 0x6C0, 0xB40, 0xFC0, -0x1180, -0xC80, -0x780, -0x280, 0x280, 0x780, 0xC80,
        0x1180, -0x1340, -0xDC0, -0x840, -0x2C0, 0x2C0, 0x840, 0xDC0, 0x1340, -0x1500, -0xF00,
        -0x900, -0x300, 0x300, 0x900, 0xF00, 0x1500, -0x16C0, -0x1040, -0x9C0, -0x340, 0x340,
        0x9C0, 0x1040, 0x16C0, -0x1880, -0x1180, -0xA80, -0x380, 0x380, 0xA80, 0x1180, 0x1880, -0x1A40,
        -0x12C0, -0xB40, -0x3C0, 0x3C0, 0xB40, 0x12C0, 0x1A40, -0x1C00, -0x1400, -0xC00, -0x400,
        0x400, 0xC00, 0x1400, 0x1C00, -0x1F7F, -0x167F, -0xD80, -0x480, 0x480, 0xD80, 0x1680, 0x1F80,
        -0x22FF, -0x18FF, -0xF00, -0x500, 0x500, 0xF00, 0x1900, 0x2300, -0x267F, -0x1B7F, -0x1080,
        -0x580, 0x580, 0x1080, 0x1B80, 0x2680, -0x29FF, -0x1DFF, -0x1200, -0x600, 0x600, 0x1200,
        0x1E00, 0x2A00, -0x2D7F, -0x207F, -0x1380, -0x680, 0x680, 0x1380, 0x2080, 0x2D80, -0x30FF,
        -0x22FF, -0x1500, -0x700, 0x700, 0x1500, 0x2300, 0x3100, -0x347F, -0x257F, -0x1680,
        -0x780, 0x780, 0x1680, 0x2580, 0x3480, -0x37FF, -0x27FF, -0x1800, -0x800, 0x800, 0x1800,
        0x2800, 0x3800, -0x3EFF, -0x2CFF, -0x1B00, -0x900, 0x900, 0x1B00, 0x2CFF, 0x3EFF, -0x45FF,
        -0x31FF, -0x1E00, -0xA00, 0xA00, 0x1E00, 0x31FF, 0x45FF, -0x4CFF, -0x36FF, -0x2100,
        -0xB00, 0xB00, 0x2100, 0x36FF, 0x4CFF, -0x53FF, -0x3BFF, -0x2400, -0xC00, 0xC00, 0x2400,
        0x3BFF, 0x53FF, -0x5AFF, -0x40FF, -0x2700, -0xD00, 0xD00, 0x2700, 0x40FF, 0x5AFF, -0x61FF,
        -0x45FF, -0x2A00, -0xE00, 0xE00, 0x2A00,0x45FF, 0x61FF, -0x68FF, -0x4AFF, -0x2D00,
        -0xF00, 0xF00, 0x2D00, 0x4AFF, 0x68FF, -0x6FFF, -0x4FFF, -0x3000, -0x1000, 0x1000, 0x3000, 0x4FFF, 0x6FFF
    };
    std::vector<double> TestMisc::_allDecodeTimes{};
    std::vector<double> TestMisc::_individualDecodeAvgs{};
    std::vector<double> TestMisc::_currentDecodeTimes{};
    double TestMisc::_maxDecodeTime = 0;
    double TestMisc::_totalDecodeTime = 0;
    int TestMisc::_readBit = 0;
    alignas(4) std::array<byte, 256 * 192 * 2> TestMisc::_outputBuf1{};
    alignas(4) std::array<byte, 256 * 192 * 2> TestMisc::_outputBuf2{};
    bool TestMisc::_outputBufferSwap = true;

    const std::vector<int> TestMisc::_dword206B2A0 =
    {
-0x1010, -0x100E, -0x100C, -0x100A, -0x1008, -0x1006, -0x1004, -0x1002, -0x1000,
        -0xFFE, -0xFFC, -0xFFA, -0xFF8, -0xFF6, -0xFF4, -0xFF2, -0xE10, -0xE0E, -0xE0C,
        -0xE0A, -0xE08, -0xE06, -0xE04, -0xE02, -0xE00, -0xDFE, -0xDFC, -0xDFA, -0xDF8,
        -0xDF6, -0xDF4, -0xDF2, -0xC10, -0xC0E, -0xC0C, -0xC0A, -0xC08, -0xC06, -0xC04,
        -0xC02, -0xC00, -0xBFE, -0xBFC, -0xBFA, -0xBF8, -0xBF6, -0xBF4, -0xBF2, -0xA10,
        -0xA0E, -0xA0C, -0xA0A, -0xA08, -0xA06, -0xA04, -0xA02, -0xA00, -0x9FE, -0x9FC,
        -0x9FA, -0x9F8, -0x9F6, -0x9F4, -0x9F2, -0x810, -0x80E, -0x80C, -0x80A, -0x808,
        -0x806, -0x804, -0x802, -0x800, -0x7FE, -0x7FC, -0x7FA, -0x7F8, -0x7F6, -0x7F4,
        -0x7F2, -0x610, -0x60E, -0x60C, -0x60A, -0x608, -0x606, -0x604, -0x602, -0x600,
        -0x5FE, -0x5FC, -0x5FA, -0x5F8, -0x5F6, -0x5F4, -0x5F2, -0x410, -0x40E, -0x40C,
        -0x40A, -0x408, -0x406, -0x404, -0x402, -0x400, -0x3FE, -0x3FC, -0x3FA, -0x3F8,
        -0x3F6, -0x3F4, -0x3F2, -0x210, -0x20E, -0x20C, -0x20A, -0x208, -0x206, -0x204,
        -0x202, -0x200, -0x1FE, -0x1FC, -0x1FA, -0x1F8, -0x1F6, -0x1F4, -0x1F2, -0x10,
        -0x0E, -0x0C, -0x0A, -0x08, -0x06, -0x04, -0x02, 0, 2, 4, 6, 8, 0xA, 0xC, 0xE,
        0x1F0, 0x1F2, 0x1F4, 0x1F6, 0x1F8, 0x1FA, 0x1FC, 0x1FE, 0x200, 0x202, 0x204,
        0x206, 0x208, 0x20A, 0x20C, 0x20E, 0x3F0, 0x3F2, 0x3F4, 0x3F6, 0x3F8, 0x3FA,
        0x3FC, 0x3FE, 0x400, 0x402, 0x404, 0x406, 0x408, 0x40A, 0x40C, 0x40E, 0x5F0,
        0x5F2, 0x5F4, 0x5F6, 0x5F8, 0x5FA, 0x5FC, 0x5FE, 0x600, 0x602, 0x604, 0x606,
        0x608, 0x60A, 0x60C, 0x60E, 0x7F0, 0x7F2, 0x7F4, 0x7F6, 0x7F8, 0x7FA, 0x7FC,
        0x7FE, 0x800, 0x802, 0x804, 0x806, 0x808, 0x80A, 0x80C, 0x80E, 0x9F0, 0x9F2,
        0x9F4, 0x9F6, 0x9F8, 0x9FA, 0x9FC, 0x9FE, 0xA00, 0xA02, 0xA04, 0xA06, 0xA08,
        0xA0A, 0xA0C, 0xA0E, 0xBF0, 0xBF2, 0xBF4, 0xBF6, 0xBF8, 0xBFA, 0xBFC, 0xBFE,
        0xC00, 0xC02, 0xC04, 0xC06, 0xC08, 0xC0A, 0xC0C, 0xC0E, 0xDF0, 0xDF2, 0xDF4,
        0xDF6, 0xDF8, 0xDFA, 0xDFC, 0xDFE, 0xE00, 0xE02, 0xE04, 0xE06, 0xE08, 0xE0A,
        0xE0C, 0xE0E
    };
    const std::vector<byte> TestMisc::_byte2067320 =
    {
0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9,
        10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 19,
        20, 20, 20, 21, 21, 21, 22, 22, 22, 23, 23, 23, 24, 24, 24, 25, 25, 25, 26, 26, 26, 27, 27, 27, 28, 28, 28, 29, 29, 29,
        30, 30, 30, 31, 31, 0, 0
    };

    void TestMisc::TestAllFV()
    {
        for (const std::string& path
            : EnumerateFiles(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies)"))
        {
            TestFV(path);
        }
        Nop();
    }

    void TestMisc::VerifyFV()
    {
        _allDecodeTimes.clear();
        _currentDecodeTimes.clear();
        _individualDecodeAvgs.clear();
        _maxDecodeTime = 0;
        _totalDecodeTime = 0;
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\opening-15fps-up-left.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\opening-15fps-down-right.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_yellow-15fps-up-left.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_yellow-15fps-down-right.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_green-15fps-up-left.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_green-15fps-down-right.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_blue-15fps-up-left.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_blue-15fps-down-right.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_white-15fps-up-left.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_white-15fps-down-right.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\death-15fps-up-left.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\death-15fps-down-right.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\teaser-15fps-up-left.avi.fv)", true);
        TestFV(R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\teaser-15fps-down-right.avi.fv)", true);
        WriteDebugAverages(_individualDecodeAvgs);
        DebugWriteLine("All avg    = " + FormatDouble(Round3(Average(_allDecodeTimes))));
        DebugWriteLine("Max time   = " + FormatDouble(Round3(_maxDecodeTime)));
        DebugWriteLine("Total time = " + FormatDouble(Round3(_totalDecodeTime)));
    }

    void TestMisc::TestFV(std::optional<std::string> path, bool verify)
    {
        if (!path)
        {
            path = R"(C:\Users\auser\Home\MPH\_FS\amfe\data\movies\spawn_yellow-15fps-down-right.avi.fv)";
        }
        const std::string movieName = PathGetFileNameWithoutExtension(*path);
        if (verify)
        {
            DebugWriteLine("Verifying " + *path + ".fv...");
            _currentDecodeTimes.clear();
        }

        const std::vector<byte> fileBytes = FileReadAllBytes(*path);
        std::ifstream fileStream(::MphRead::NativeRuntime::PathFromUtf8(*path), std::ios::binary);
        if (!fileStream)
        {
            throw System::IO::FileNotFoundException(*path);
        }

        std::array<char, 4> magic{};
        for (char& value : magic)
        {
            char byteValue = 0;
            fileStream.get(byteValue);
            if (!fileStream)
            {
                throw System::IO::EndOfStreamException();
            }
            value = byteValue;
        }
        const int frameCount = ReadInt32LittleEndian(fileStream);
        const int frameWidth = ReadInt32LittleEndian(fileStream);
        const int frameHeight = ReadInt32LittleEndian(fileStream);
        const int frameRateRaw = ReadInt32LittleEndian(fileStream);
        const long double frameRate = static_cast<long double>(frameRateRaw) / 65536.0L;
        const int audioSampleRate = ReadInt32LittleEndian(fileStream);
        const int totalDataSize = ReadInt32LittleEndian(fileStream);
        const int maxDataSize = ReadInt32LittleEndian(fileStream);
        const int field24 = ReadInt32LittleEndian(fileStream);

        assert(magic[0] == 'F' && magic[1] == 'V' && magic[2] == 'D' && magic[3] == 'S');
        assert(frameCount > 0);
        assert(frameWidth == 256);
        assert(frameHeight == 192);
        assert(frameRate == 14.9999237060546875L);
        assert(audioSampleRate == 32768);
        assert(totalDataSize == static_cast<int>(fileBytes.size()) - 36 - 16);
        assert(field24 == 4608);

        std::vector<std::int64_t> framePositions;
        std::vector<std::int64_t> skipAheadPositions;
        if (maxDataSize < 8)
        {
            throw std::overflow_error("Arithmetic operation resulted in an overflow.");
        }
        std::vector<byte> dataBuffer(static_cast<std::size_t>(maxDataSize - 8));
        _outputBuf1.fill(0);
        _outputBuf2.fill(0);
        _outputBufferSwap = false;
        bool foundMaxSize = false;

        std::array<int, 110> decodeBuf{};
        int audioFrameTotal = 0;
        std::vector<short> samples;
        std::array<short, 256> sampleSpan{};

        std::array<byte, 256 * 192 * 3> imageOutput{};

        for (int i = 0; i < frameCount; ++i)
        {
            std::fill(dataBuffer.begin(), dataBuffer.end(), byte{0});
            const std::streampos position = fileStream.tellg();
            if (position < 0)
            {
                throw System::IO::IOException("I/O error occurred.");
            }
            const std::int64_t currentPosition = static_cast<std::int64_t>(position);
            framePositions.push_back(currentPosition);
            skipAheadPositions.erase(
                std::remove(skipAheadPositions.begin(), skipAheadPositions.end(), currentPosition),
                skipAheadPositions.end());

            const int dataSize = ReadInt32LittleEndian(fileStream);
            const int seekBackOffset = ReadInt32LittleEndian(fileStream);
            const int seekAheadOffset = ReadInt32LittleEndian(fileStream);
            const int frameIndex = ReadInt32LittleEndian(fileStream);
            const int videoSize = ReadInt32LittleEndian(fileStream);
            if (videoSize < 0 || static_cast<std::size_t>(videoSize) > dataBuffer.size())
            {
                throw System::ArgumentOutOfRangeException("length");
            }
            ReadExactly(fileStream,
                std::span<byte>(dataBuffer).subspan(0, static_cast<std::size_t>(videoSize)));
            const int audioSize = ReadInt32LittleEndian(fileStream);
            if (audioSize < 0
                || static_cast<std::size_t>(videoSize) + static_cast<std::size_t>(audioSize)
                    > dataBuffer.size())
            {
                throw System::ArgumentOutOfRangeException("length");
            }
            ReadExactly(fileStream,
                std::span<byte>(dataBuffer).subspan(
                    static_cast<std::size_t>(videoSize), static_cast<std::size_t>(audioSize)));

            assert(frameIndex == i);
            if (i == 0)
            {
                assert(seekBackOffset == 0);
            }
            if (i == frameCount - 1)
            {
                assert(seekAheadOffset <= 0);
            }
            if (seekBackOffset < 0)
            {
                const std::int64_t expected = framePositions.back() + seekBackOffset;
                assert(std::find(framePositions.begin(), framePositions.end(), expected)
                    != framePositions.end());
            }
            if (seekAheadOffset > 0)
            {
                skipAheadPositions.push_back(framePositions.back() + seekAheadOffset);
            }
            assert(seekBackOffset <= 0);
            assert(dataSize == videoSize + audioSize + 8);
            assert(audioSize == 320 || audioSize == 360);
            if (dataSize == maxDataSize)
            {
                foundMaxSize = true;
            }

            const auto start = std::chrono::steady_clock::now();
            TestVideo(frameWidth, frameHeight,
                Span<byte>(dataBuffer).Slice(0, videoSize), frameIndex);
            const auto stop = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double, std::milli>(stop - start).count();
            _currentDecodeTimes.push_back(elapsed);
            _allDecodeTimes.push_back(elapsed);

            const auto& outputBuf = _outputBufferSwap ? _outputBuf1 : _outputBuf2;
            int p = 0;
            for (int j = 0; j < static_cast<int>(outputBuf.size()); j += 2)
            {
                const int color = static_cast<int>(outputBuf[static_cast<std::size_t>(j)])
                    + (static_cast<int>(outputBuf[static_cast<std::size_t>(j + 1)]) << 8);
                const int r = color & 0x1F;
                const int g = (color >> 5) & 0x1F;
                const int b = (color >> 10) & 0x1F;
                imageOutput[static_cast<std::size_t>(p)] = static_cast<byte>(r * 8);
                imageOutput[static_cast<std::size_t>(p + 1)] = static_cast<byte>(g * 8);
                imageOutput[static_cast<std::size_t>(p + 2)] = static_cast<byte>(b * 8);
                p += 3;
            }

            const std::string imageFilename = FrameFileName(frameIndex);
            if (verify)
            {
                const std::string imagePath =
                    R"(C:\Users\auser\Home\MPH\Data\_Export\_FV\_golden\)"
                    + movieName + "\\" + imageFilename;
                std::ifstream imageStream(::MphRead::NativeRuntime::PathFromUtf8(imagePath), std::ios::binary);
                if (!imageStream)
                {
                    throw System::IO::FileNotFoundException(imagePath);
                }
                const std::vector<byte> image = TestMiscInterop::LoadPngRgb(imageStream);
                if (!std::equal(imageOutput.begin(), imageOutput.end(),
                        image.begin(), image.end()))
                {
                    DebugBreak();
                }
            }
            else
            {
                const std::string outputPath =
                    R"(C:\Users\auser\Home\MPH\Data\_Export\_FV\)"
                    + movieName + "\\" + imageFilename;
                DirectoryCreateDirectory(PathGetDirectoryName(outputPath).value());
                std::ofstream imageStream(::MphRead::NativeRuntime::PathFromUtf8(outputPath), std::ios::binary | std::ios::trunc);
                if (!imageStream)
                {
                    throw System::IO::IOException("I/O error occurred.");
                }
                Export::ImagesInterop::WritePngRgb(
                    imageOutput, 256, 192, imageStream);
            }

            const int audioFrameCount = audioSize / 40;
            audioFrameTotal = UncheckedAdd(audioFrameTotal, audioFrameCount);
            Span<uint> audioData = Span<byte>(dataBuffer)
                .Slice(videoSize, audioSize).Cast<uint>();

            auto Sub206B9A0 = [&](Span<uint> audioFrameData, Span<int> decode)
            {
                int destIndex = 0;
                uint value = audioFrameData[0];
                decode[destIndex++] = _dword206B720[static_cast<std::size_t>(value >> 26)];
                decode[destIndex++] = _dword206B720[static_cast<std::size_t>((value >> 20) & 0x3F)];
                decode[destIndex++] = _dword206B820[static_cast<std::size_t>((value >> 15) & 0x1F)];
                decode[destIndex++] = _dword206B8A0[static_cast<std::size_t>((value >> 10) & 0x1F)];
                decode[destIndex++] = _dword206B920[static_cast<std::size_t>((value >> 6) & 0xF)];
                ++destIndex;
                decode[destIndex++] = _dword206B960[static_cast<std::size_t>((value >> 3) & 7)];
                decode[destIndex++] = _dword206B980[static_cast<std::size_t>(value & 7)];

                value = audioFrameData[1];
                decode[destIndex++] = static_cast<int>(value & 3);
                decode[destIndex++] = static_cast<int>((value >> 2) & 3);
                decode[destIndex++] = static_cast<int>((value >> 4) & 3);
                decode[destIndex++] = static_cast<byte>(value) >> 6;
                decode[destIndex++] = static_cast<int>((value >> 8) & 0x3F);
                decode[destIndex++] = static_cast<int>((value >> 14) & 0x3F);
                decode[destIndex++] = static_cast<int>((value >> 20) & 0x3F);
                decode[destIndex++] = static_cast<int>(value >> 26);

                int index5 = 0;
                uint prevValue = 0;
                for (int index = 0; index < 8; ++index)
                {
                    prevValue = value;
                    value = audioFrameData[index + 2];
                    decode[destIndex++] = static_cast<int>(value >> 29);
                    decode[destIndex++] = static_cast<int>((value >> 26) & 7);
                    decode[destIndex++] = static_cast<int>((value >> 23) & 7);
                    decode[destIndex++] = static_cast<int>((value >> 20) & 7);
                    decode[destIndex++] = static_cast<int>((value >> 17) & 7);
                    decode[destIndex++] = static_cast<int>((value >> 14) & 7);
                    decode[destIndex++] = static_cast<int>((value >> 11) & 7);
                    decode[destIndex++] = static_cast<int>((value >> 8) & 7);
                    decode[destIndex++] = static_cast<byte>(value) >> 5;
                    decode[destIndex++] = static_cast<int>((value >> 2) & 7);
                    if (index % 2 == 1)
                    {
                        decode[destIndex++] = (static_cast<int>(value & 2) >> 1)
                            | (2 * static_cast<int>(prevValue & 3));
                        index5 |= static_cast<int>(value & 1) << ((8 - index) / 2);
                    }
                }
                decode[5] = _dword206BEBC[static_cast<std::size_t>(index5)];
            };

            auto Sub206BEFC = [&](Span<short> sampleSlice, Span<int> decodeSlice,
                                   std::size_t tableStart, int skipCount)
            {
                sampleSlice.Clear();
                for (int index = 0; index < 21; ++index)
                {
                    const int tableIndex = decodeSlice[index];
                    if (tableIndex < 0
                        || tableStart + static_cast<std::size_t>(tableIndex)
                            >= _dword206C268.size())
                    {
                        throw System::IndexOutOfRangeException();
                    }
                    sampleSlice[index * 3 + skipCount]
                        = _dword206C268[tableStart + static_cast<std::size_t>(tableIndex)];
                }
            };

            for (int j = 0; j < audioFrameCount; ++j)
            {
                Span<uint> audioFrameData = audioData.Slice(j * 10, 10);
                Sub206B9A0(audioFrameData, Span<int>(decodeBuf));
                for (int k = 0; k < 4; ++k)
                {
                    Span<short> sampleSlice = Span<short>(sampleSpan).Slice(64 * k, 64);
                    Span<int> decodeSlice = Span<int>(decodeBuf).Slice(16 + 21 * k, 21);
                    const int tableOffset = decodeBuf[static_cast<std::size_t>(k + 12)] * 8;
                    if (tableOffset < 0)
                    {
                        throw System::ArgumentOutOfRangeException("start");
                    }
                    Sub206BEFC(sampleSlice, decodeSlice,
                        static_cast<std::size_t>(tableOffset),
                        decodeBuf[static_cast<std::size_t>(k + 8)]);
                }

                int halfSample = decodeBuf[109];
                for (int k = 0; k < 256; ++k)
                {
                    int runningValue = sampleSpan[static_cast<std::size_t>(k)];
                    for (int l = 107; l >= 100; --l)
                    {
                        const int currentValue = decodeBuf[static_cast<std::size_t>(l)];
                        const int oppositeValue = decodeBuf[static_cast<std::size_t>(l - 100)];
                        const int product = UncheckedMultiply(oppositeValue, currentValue);
                        runningValue = UncheckedSubtract(runningValue,
                            ArithmeticShiftRight(UncheckedAdd(product, 0x4000), 15));
                        const int product2 = UncheckedMultiply(oppositeValue, runningValue);
                        decodeBuf[static_cast<std::size_t>(l + 1)] = UncheckedAdd(
                            currentValue,
                            ArithmeticShiftRight(UncheckedAdd(product2, 0x4000), 15));
                    }
                    decodeBuf[100] = runningValue;
                    const int filtered = ArithmeticShiftRight(
                        UncheckedAdd(UncheckedMultiply(0x6E14, halfSample), 0x4000), 15);
                    halfSample = UncheckedAdd(runningValue, filtered);
                    const int doubled = UncheckedMultiply(halfSample, 2);
                    const int clamped = std::clamp(
                        doubled,
                        static_cast<int>(std::numeric_limits<short>::min()),
                        static_cast<int>(std::numeric_limits<short>::max()));
                    const short sample = static_cast<short>(clamped);
                    sampleSpan[static_cast<std::size_t>(k)] = sample;
                    samples.push_back(sample);
                }
                decodeBuf[109] = halfSample;
            }
            Nop();
            Nop();
        }

        assert(foundMaxSize);
        assert(skipAheadPositions.empty());
        assert(ReadInt32LittleEndian(fileStream) == 0);
        assert(ReadInt32LittleEndian(fileStream) == 0);
        assert(ReadInt32LittleEndian(fileStream) == 0);
        assert(ReadInt32LittleEndian(fileStream) == 0);
        const std::streampos endPosition = fileStream.tellg();
        assert(endPosition >= 0
            && static_cast<std::size_t>(endPosition) == fileBytes.size());

        if (verify)
        {
            _individualDecodeAvgs.push_back(Average(_currentDecodeTimes));
            _maxDecodeTime = std::max(
                _maxDecodeTime,
                *std::max_element(_currentDecodeTimes.begin(), _currentDecodeTimes.end()));
            _totalDecodeTime += std::accumulate(
                _currentDecodeTimes.begin(), _currentDecodeTimes.end(), 0.0);
        }
        else
        {
            const std::string audioOutput =
                R"(C:\Users\auser\Home\MPH\Data\_Export\_FV\)"
                + movieName + R"(\audio.wav)";
            DirectoryCreateDirectory(PathGetDirectoryName(audioOutput).value());
            std::ofstream output(::MphRead::NativeRuntime::PathFromUtf8(audioOutput), std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw System::IO::IOException("I/O error occurred.");
            }
            Formats::Sound::BinaryWriter writer(output);
            Formats::Sound::SoundRead::WriteWavHeader(
                writer,
                static_cast<uint>(audioFrameTotal) * 256U,
                static_cast<ushort>(audioSampleRate),
                WaveFormat::PCM16);
            for (short sample : samples)
            {
                const ushort data = static_cast<ushort>(sample);
                writer.Write(static_cast<byte>(data & 0xFF));
                writer.Write(static_cast<byte>((data >> 8) & 0xFF));
            }
        }
        Nop();
        Nop();
    }

    void TestMisc::TestVideo(int width, int height, Span<byte> videoData, int frameIndex)
    {
        _outputBufferSwap = !_outputBufferSwap;
        Span<byte> outputSpan1;
        Span<byte> outputSpan2;
        if (_outputBufferSwap)
        {
            outputSpan1 = Span<byte>(_outputBuf2);
            outputSpan2 = Span<byte>(_outputBuf1);
        }
        else
        {
            outputSpan1 = Span<byte>(_outputBuf1);
            outputSpan2 = Span<byte>(_outputBuf2);
        }

        auto ReadVideoInt32 = [](Span<byte> source) -> int
        {
            if (source.Length() < 4)
            {
                throw System::ArgumentOutOfRangeException("source");
            }
            return ReadInt32LittleEndian(std::span<const byte>(&source[0], 4));
        };

        const int offset1 = UncheckedAdd(ReadVideoInt32(videoData), 8);
        const int offset2 = ReadVideoInt32(videoData.Slice(4));
        Span<uint> videoDataUint =
            videoData.Slice(UncheckedAdd(offset1, offset2)).Cast<uint>();
        Span<ushort> videoDataUshort = videoData.Slice(offset1).Cast<ushort>();
        Span<byte> videoDataByte = videoData.Slice(8);
        _readBit = 1;
        uint value = NextValueCarry(videoDataUint);
        uint outputPos = 0;
        assert(width % 8 == 0);
        assert(height % 8 == 0);
        for (int y = 0; y < height / 8; ++y)
        {
            for (int x = 0; x < width / 8; ++x)
            {
                const int block = y * 32 + x;
                const int breakFrame = 6;
                const int breakBlock = 187;
                const bool breakBefore = false;
                const bool breakAfter = false;
                if (breakBefore && frameIndex == breakFrame && block == breakBlock)
                {
                    DebugBreak();
                }
                Sub20679B0(outputPos, value, outputSpan1, outputSpan2,
                    videoDataUint, videoDataUshort, videoDataByte);
                if (breakAfter && frameIndex == breakFrame && block == breakBlock)
                {
                    DebugBreak();
                }
                outputPos += 16;
            }
            outputPos += 4096 - 16 * (256 / 8);
        }
    }

    void TestMisc::Sub20679B0(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        byte v130 = byteBuf.Consume();
                        uint v131 = (uint)_dword206B2A0[v130];
                        ushort v132 = wordBuf.Consume();
                        int v133 = UInt32ToInt32(static_cast<uint>(v132 | ((static_cast<uint>(v132) << 16))));
                        uint prevOffset = v131 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        uint v136 = outputSpan1Int[1] + (uint)v133;
                        uint v137 = outputSpan1Int[2] + (uint)v133;
                        uint v138 = outputSpan1Int[3] + (uint)v133;
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v133;
                        outputSpan2Int[1] = v136;
                        outputSpan2Int[2] = v137;
                        outputSpan2Int[3] = v138;
                        uint v141 = outputSpan1Int[256 / 2 + 1] + (uint)v133;
                        uint v142 = outputSpan1Int[256 / 2 + 2] + (uint)v133;
                        uint v143 = outputSpan1Int[256 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2] + (uint)v133;
                        outputSpan2Int[256 / 2 + 1] = v141;
                        outputSpan2Int[256 / 2 + 2] = v142;
                        outputSpan2Int[256 / 2 + 3] = v143;
                        uint v144 = outputSpan1Int[256 * 2 / 2 + 1] + (uint)v133;
                        uint v145 = outputSpan1Int[256 * 2 / 2 + 2] + (uint)v133;
                        uint v146 = outputSpan1Int[256 * 2 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2] + (uint)v133;
                        outputSpan2Int[256 * 2 / 2 + 1] = v144;
                        outputSpan2Int[256 * 2 / 2 + 2] = v145;
                        outputSpan2Int[256 * 2 / 2 + 3] = v146;
                        uint v147 = outputSpan1Int[256 * 3 / 2 + 1] + (uint)v133;
                        uint v148 = outputSpan1Int[256 * 3 / 2 + 2] + (uint)v133;
                        uint v149 = outputSpan1Int[256 * 3 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2] + (uint)v133;
                        outputSpan2Int[256 * 3 / 2 + 1] = v147;
                        outputSpan2Int[256 * 3 / 2 + 2] = v148;
                        outputSpan2Int[256 * 3 / 2 + 3] = v149;
                        uint v150 = outputSpan1Int[256 * 4 / 2 + 1] + (uint)v133;
                        uint v151 = outputSpan1Int[256 * 4 / 2 + 2] + (uint)v133;
                        uint v152 = outputSpan1Int[256 * 4 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 * 4 / 2] = outputSpan1Int[256 * 4 / 2] + (uint)v133;
                        outputSpan2Int[256 * 4 / 2 + 1] = v150;
                        outputSpan2Int[256 * 4 / 2 + 2] = v151;
                        outputSpan2Int[256 * 4 / 2 + 3] = v152;
                        uint v153 = outputSpan1Int[256 * 5 / 2 + 1] + (uint)v133;
                        uint v154 = outputSpan1Int[256 * 5 / 2 + 2] + (uint)v133;
                        uint v155 = outputSpan1Int[256 * 5 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 * 5 / 2] = outputSpan1Int[256 * 5 / 2] + (uint)v133;
                        outputSpan2Int[256 * 5 / 2 + 1] = v153;
                        outputSpan2Int[256 * 5 / 2 + 2] = v154;
                        outputSpan2Int[256 * 5 / 2 + 3] = v155;
                        uint v156 = outputSpan1Int[256 * 6 / 2 + 1] + (uint)v133;
                        uint v157 = outputSpan1Int[256 * 6 / 2 + 2] + (uint)v133;
                        uint v158 = outputSpan1Int[256 * 6 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 * 6 / 2] = outputSpan1Int[256 * 6 / 2] + (uint)v133;
                        outputSpan2Int[256 * 6 / 2 + 1] = v156;
                        outputSpan2Int[256 * 6 / 2 + 2] = v157;
                        outputSpan2Int[256 * 6 / 2 + 3] = v158;
                        uint v160 = outputSpan1Int[256 * 7 / 2 + 1] + (uint)v133;
                        uint v161 = outputSpan1Int[256 * 7 / 2 + 2] + (uint)v133;
                        uint v162 = outputSpan1Int[256 * 7 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 * 7 / 2] = outputSpan1Int[256 * 7 / 2] + (uint)v133;
                        outputSpan2Int[256 * 7 / 2 + 1] = v160;
                        outputSpan2Int[256 * 7 / 2 + 2] = v161;
                        outputSpan2Int[256 * 7 / 2 + 3] = v162;
                    }
                    else
                    {
                        // 1 1 0
                        ReadNextBit(value, intBuf);
                        if (_readBit != 0)
                        {
                            // 1 1 0 1
                            ReadNextBit(value, intBuf);
                            if (_readBit != 0)
                            {
                                // 1 1 0 1 1
                                Sub20674E4(wordBuf, outputSpan2, outputPos);
                            }
                            else
                            {
                                // 1 1 0 1 0
                                Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                                Sub2067388(wordBuf, outputSpan2Slice);
                                // advanced by 1536 bytes, subtract 1528 bytes, now positioned at 8 bytes
                                Sub2067388(wordBuf, outputSpan2Slice.Slice(4));
                                // advanced by 1536 + 8 initial bytes = 1544 bytes, add 504 bytes, now positioned at 2048 bytes
                                Sub2067388(wordBuf, outputSpan2Slice.Slice(1024));
                                // advanced by 1536 + 2048 initial bytes = 3584 bytes, subtract 1528 bytes, now positioned at 2056 bytes
                                Sub2067388(wordBuf, outputSpan2Slice.Slice(1028));
                            }
                        }
                        else
                        {
                            // 1 1 0 0
                            assert(outputPos >= 512);
                            Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                            Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                            // todo: similar to the inlines in Sub Sub206A0B8 and Sub2068488 and Sub206935C and Sub2068B24
                            ushort v73 = wordBuf[0];
                            outputSpan2Slice[256 * 2 + 2] = v73;
                            outputSpan2Slice[256 * 2 + 3] = v73;
                            outputSpan2Slice[256 * 2 + 4] = v73;
                            outputSpan2Slice[256 * 2 + 5] = v73;
                            outputSpan2Slice[256 * 2 + 6] = v73;
                            outputSpan2Slice[256 * 2 + 7] = v73;
                            outputSpan2Slice[256 * 3 + 2] = v73;
                            outputSpan2Slice[256 * 3 + 3] = v73;
                            outputSpan2Slice[256 * 3 + 4] = v73;
                            outputSpan2Slice[256 * 3 + 5] = v73;
                            outputSpan2Slice[256 * 3 + 6] = v73;
                            outputSpan2Slice[256 * 3 + 7] = v73;
                            outputSpan2Slice[256 * 4 + 2] = v73;
                            outputSpan2Slice[256 * 4 + 3] = v73;
                            outputSpan2Slice[256 * 4 + 4] = v73;
                            outputSpan2Slice[256 * 4 + 5] = v73;
                            outputSpan2Slice[256 * 4 + 6] = v73;
                            outputSpan2Slice[256 * 4 + 7] = v73;
                            outputSpan2Slice[256 * 5 + 2] = v73;
                            outputSpan2Slice[256 * 5 + 3] = v73;
                            outputSpan2Slice[256 * 5 + 4] = v73;
                            outputSpan2Slice[256 * 5 + 5] = v73;
                            outputSpan2Slice[256 * 5 + 6] = v73;
                            outputSpan2Slice[256 * 5 + 7] = v73;
                            outputSpan2Slice[256 * 6 + 2] = v73;
                            outputSpan2Slice[256 * 6 + 3] = v73;
                            outputSpan2Slice[256 * 6 + 4] = v73;
                            outputSpan2Slice[256 * 6 + 5] = v73;
                            outputSpan2Slice[256 * 6 + 6] = v73;
                            outputSpan2Slice[256 * 6 + 7] = v73;
                            outputSpan2Slice[256 * 7 + 2] = v73;
                            outputSpan2Slice[256 * 7 + 3] = v73;
                            outputSpan2Slice[256 * 7 + 4] = v73;
                            outputSpan2Slice[256 * 7 + 5] = v73;
                            outputSpan2Slice[256 * 7 + 6] = v73;
                            outputSpan2Slice[256 * 7 + 7] = v73;
                            uint v74 = v73 | ((uint)(static_cast<uint>(v73) << 16));
                            uint v82 = v74 & 0x7BDF7BDF;
                            uint v83 = v74 & 0x4200420;
                            uint v86 = outputSpan2Prev[0];
                            uint v87 = v86 & 0x4200420;
                            v86 &= 0x7BDF7BDF;
                            uint v88 = v87 | ((uint)(v82 + v86) >> 1);
                            outputSpan2Slice[0] = (ushort)v88;
                            v88 &= 0x7BDF7BDF;
                            outputSpan2Prev[0] = (ushort)(v83 | ((v88 + v86) >> 1));
                            outputSpan2Slice[256] = (ushort)(v83 | ((v88 + v82) >> 1));
                            uint v89 = outputSpan2Prev[1];
                            v87 = (v87 & 0xFFFF0000) | (v89 & 0x420);
                            v89 &= 0x7BDF7BDF;
                            uint v90 = v83 | ((uint)(v82 + v89) >> 1);
                            outputSpan2Slice[1] = (ushort)v90;
                            v90 &= 0x7BDF7BDF;
                            outputSpan2Prev[1] = (ushort)(v87 | ((v90 + v89) >> 1));
                            outputSpan2Slice[256 + 1] = (ushort)(v87 | ((v90 + v82) >> 1));
                            uint v91 = outputSpan2Prev[2];
                            uint v92 = v91 & 0x4200420;
                            v91 &= 0x7BDF7BDF;
                            uint v93 = v92 | ((uint)(v82 + v91) >> 1);
                            outputSpan2Slice[2] = (ushort)v93;
                            v93 &= 0x7BDF7BDF;
                            outputSpan2Prev[2] = (ushort)(v83 | ((v93 + v91) >> 1));
                            outputSpan2Slice[256 + 2] = (ushort)(v83 | ((v93 + v82) >> 1));
                            uint v94 = outputSpan2Prev[3];
                            v92 = (v92 & 0xFFFF0000) | (v94 & 0x420);
                            v94 &= 0x7BDF7BDF;
                            uint v95 = v83 | ((uint)(v82 + v94) >> 1);
                            outputSpan2Slice[3] = (ushort)v95;
                            v95 &= 0x7BDF7BDF;
                            outputSpan2Prev[3] = (ushort)(v92 | ((v95 + v94) >> 1));
                            outputSpan2Slice[256 + 3] = (ushort)(v92 | ((v95 + v82) >> 1));
                            uint v96 = outputSpan2Prev[4];
                            uint v97 = v96 & 0x4200420;
                            v96 &= 0x7BDF7BDF;
                            uint v98 = v97 | ((uint)(v82 + v96) >> 1);
                            outputSpan2Slice[4] = (ushort)v98;
                            v98 &= 0x7BDF7BDF;
                            outputSpan2Prev[4] = (ushort)(v83 | ((v98 + v96) >> 1));
                            outputSpan2Slice[256 + 4] = (ushort)(v83 | ((v98 + v82) >> 1));
                            uint v99 = outputSpan2Prev[5];
                            v97 = (v97 & 0xFFFF0000) | (v99 & 0x420);
                            v99 &= 0x7BDF7BDF;
                            uint v100 = v83 | ((uint)(v82 + v99) >> 1);
                            outputSpan2Slice[5] = (ushort)v100;
                            v100 &= 0x7BDF7BDF;
                            outputSpan2Prev[5] = (ushort)(v97 | ((v100 + v99) >> 1));
                            outputSpan2Slice[256 + 5] = (ushort)(v97 | ((v100 + v82) >> 1));
                            uint v101 = outputSpan2Prev[6];
                            uint v102 = v101 & 0x4200420;
                            v101 &= 0x7BDF7BDF;
                            uint v103 = v102 | ((uint)(v82 + v101) >> 1);
                            outputSpan2Slice[6] = (ushort)v103;
                            v103 &= 0x7BDF7BDF;
                            outputSpan2Prev[6] = (ushort)(v83 | ((v103 + v101) >> 1));
                            outputSpan2Slice[256 + 6] = (ushort)(v83 | ((v103 + v82) >> 1));
                            uint v104 = outputSpan2Prev[7];
                            v102 = (v102 & 0xFFFF0000) | (v104 & 0x420);
                            v104 &= 0x7BDF7BDF;
                            uint v105 = v83 | ((uint)(v82 + v104) >> 1);
                            outputSpan2Slice[7] = (ushort)v105;
                            v105 &= 0x7BDF7BDF;
                            outputSpan2Prev[7] = (ushort)(v102 | ((v105 + v104) >> 1));
                            outputSpan2Slice[256 + 7] = (ushort)(v102 | ((v105 + v82) >> 1));
                            uint v106 = outputSpan2Prev[256 - 1];
                            uint v107 = v106 & 0x4200420;
                            v106 &= 0x7BDF7BDF;
                            uint v108 = v107 | ((uint)(v82 + v106) >> 1);
                            outputSpan2Slice[0] = (ushort)v108;
                            v108 &= 0x7BDF7BDF;
                            outputSpan2Prev[256 - 1] = (ushort)(v83 | ((v108 + v106) >> 1));
                            outputSpan2Slice[1] = (ushort)(v83 | ((v108 + v82) >> 1));
                            uint v109 = outputSpan2Slice[256 - 1];
                            v107 = (v107 & 0xFFFF0000) | (v109 & 0x420);
                            v109 &= 0x7BDF7BDF;
                            uint v110 = v83 | ((uint)(v82 + v109) >> 1);
                            outputSpan2Slice[256] = (ushort)v110;
                            v110 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 - 1] = (ushort)(v107 | ((v110 + v109) >> 1));
                            outputSpan2Slice[256 + 1] = (ushort)(v107 | ((v110 + v82) >> 1));
                            uint v111 = outputSpan2Slice[256 * 2 - 1];
                            uint v112 = v111 & 0x4200420;
                            v111 &= 0x7BDF7BDF;
                            uint v113 = v112 | ((uint)(v82 + v111) >> 1);
                            outputSpan2Slice[256 * 2] = (ushort)v113;
                            v113 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 2 - 1] = (ushort)(v83 | ((v113 + v111) >> 1));
                            outputSpan2Slice[256 * 2 + 1] = (ushort)(v83 | ((v113 + v82) >> 1));
                            uint v114 = outputSpan2Slice[256 * 3 - 1];
                            v112 = (v112 & 0xFFFF0000) | (v114 & 0x420);
                            v114 &= 0x7BDF7BDF;
                            uint v115 = v83 | ((uint)(v82 + v114) >> 1);
                            outputSpan2Slice[256 * 3] = (ushort)v115;
                            v115 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 3 - 1] = (ushort)(v112 | ((v115 + v114) >> 1));
                            outputSpan2Slice[256 * 3 + 1] = (ushort)(v112 | ((v115 + v82) >> 1));
                            uint v116 = outputSpan2Slice[256 * 4 - 1];
                            uint v117 = v116 & 0x4200420;
                            v116 &= 0x7BDF7BDF;
                            uint v118 = v117 | ((uint)(v82 + v116) >> 1);
                            outputSpan2Slice[256 * 4] = (ushort)v118;
                            v118 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 4 - 1] = (ushort)(v83 | ((v118 + v116) >> 1));
                            outputSpan2Slice[256 * 4 + 1] = (ushort)(v83 | ((v118 + v82) >> 1));
                            uint v119 = outputSpan2Slice[256 * 5 - 1];
                            v117 = (v117 & 0xFFFF0000) | (v119 & 0x420);
                            v119 &= 0x7BDF7BDF;
                            uint v120 = v83 | ((uint)(v82 + v119) >> 1);
                            outputSpan2Slice[256 * 5] = (ushort)v120;
                            v120 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 5 - 1] = (ushort)(v117 | ((v120 + v119) >> 1));
                            outputSpan2Slice[256 * 5 + 1] = (ushort)(v117 | ((v120 + v82) >> 1));
                            uint v121 = outputSpan2Slice[256 * 6 - 1];
                            uint v122 = v121 & 0x4200420;
                            v121 &= 0x7BDF7BDF;
                            uint v123 = v122 | ((uint)(v82 + v121) >> 1);
                            outputSpan2Slice[256 * 6] = (ushort)v123;
                            v123 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 6 - 1] = (ushort)(v83 | ((v123 + v121) >> 1));
                            outputSpan2Slice[256 * 6 + 1] = (ushort)(v83 | ((v123 + v82) >> 1));
                            uint v124 = outputSpan2Slice[256 * 7 - 1];
                            v122 = (v122 & 0xFFFF0000) | (v124 & 0x420);
                            v124 &= 0x7BDF7BDF;
                            uint v125 = v83 | ((uint)(v82 + v124) >> 1);
                            outputSpan2Slice[256 * 7] = (ushort)v125;
                            v125 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 7 - 1] = (ushort)(v122 | ((v125 + v124) >> 1));
                            outputSpan2Slice[256 * 7 + 1] = (ushort)(v122 | ((v125 + v82) >> 1));
                            wordBuf = wordBuf.Slice(1);
                        }
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        Sub2068B24(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 8;
                        Sub2068B24(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 8;
                    }
                    else
                    {
                        // 1 0 0
                        // decode pixels 0-7 of lines 0-3 of this 8x8 block
                        Sub2068488(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        // advance by 2048 bytes = 1024 words/pixels = 4 lines
                        outputPos += 2048;
                        // decode pixels 0-7 of lines 4-7 of this 8x8 block
                        Sub2068488(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        // reset output position, to be advanced by the caller to the next 8x8 block
                        outputPos -= 2048;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets more values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                uint v15 = outputSpan1Int[1];           // for 0 0, or also 0 1 if it has dword alignment
                uint v16 = outputSpan1Int[2];
                uint v17 = outputSpan1Int[3];
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[1] = v15;
                outputSpan2Int[2] = v16;
                outputSpan2Int[3] = v17;
                uint v20 = outputSpan1Int[256 / 2 + 1];
                uint v21 = outputSpan1Int[256 / 2 + 2];
                uint v22 = outputSpan1Int[256 / 2 + 3];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
                outputSpan2Int[256 / 2 + 1] = v20;
                outputSpan2Int[256 / 2 + 2] = v21;
                outputSpan2Int[256 / 2 + 3] = v22;
                uint v23 = outputSpan1Int[256 * 2 / 2 + 1];
                uint v24 = outputSpan1Int[256 * 2 / 2 + 2];
                uint v25 = outputSpan1Int[256 * 2 / 2 + 3];
                outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2];
                outputSpan2Int[256 * 2 / 2 + 1] = v23;
                outputSpan2Int[256 * 2 / 2 + 2] = v24;
                outputSpan2Int[256 * 2 / 2 + 3] = v25;
                uint v26 = outputSpan1Int[256 * 3 / 2 + 1];
                uint v27 = outputSpan1Int[256 * 3 / 2 + 2];
                uint v28 = outputSpan1Int[256 * 3 / 2 + 3];
                outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2];
                outputSpan2Int[256 * 3 / 2 + 1] = v26;
                outputSpan2Int[256 * 3 / 2 + 2] = v27;
                outputSpan2Int[256 * 3 / 2 + 3] = v28;
                uint v29 = outputSpan1Int[256 * 4 / 2 + 1];
                uint v30 = outputSpan1Int[256 * 4 / 2 + 2];
                uint v31 = outputSpan1Int[256 * 4 / 2 + 3];
                outputSpan2Int[256 * 4 / 2] = outputSpan1Int[256 * 4 / 2];
                outputSpan2Int[256 * 4 / 2 + 1] = v29;
                outputSpan2Int[256 * 4 / 2 + 2] = v30;
                outputSpan2Int[256 * 4 / 2 + 3] = v31;
                uint v32 = outputSpan1Int[256 * 5 / 2 + 1];
                uint v33 = outputSpan1Int[256 * 5 / 2 + 2];
                uint v34 = outputSpan1Int[256 * 5 / 2 + 3];
                outputSpan2Int[256 * 5 / 2] = outputSpan1Int[256 * 5 / 2];
                outputSpan2Int[256 * 5 / 2 + 1] = v32;
                outputSpan2Int[256 * 5 / 2 + 2] = v33;
                outputSpan2Int[256 * 5 / 2 + 3] = v34;
                uint v35 = outputSpan1Int[256 * 6 / 2 + 1];
                uint v36 = outputSpan1Int[256 * 6 / 2 + 2];
                uint v37 = outputSpan1Int[256 * 6 / 2 + 3];
                outputSpan2Int[256 * 6 / 2] = outputSpan1Int[256 * 6 / 2];
                outputSpan2Int[256 * 6 / 2 + 1] = v35;
                outputSpan2Int[256 * 6 / 2 + 2] = v36;
                outputSpan2Int[256 * 6 / 2 + 3] = v37;
                uint v38 = outputSpan1Int[256 * 7 / 2 + 1];
                uint v39 = outputSpan1Int[256 * 7 / 2 + 2];
                uint v40 = outputSpan1Int[256 * 7 / 2 + 3];
                outputSpan2Int[256 * 7 / 2] = outputSpan1Int[256 * 7 / 2];
                outputSpan2Int[256 * 7 / 2 + 1] = v38;
                outputSpan2Int[256 * 7 / 2 + 2] = v39;
                outputSpan2Int[256 * 7 / 2 + 3] = v40;
            }
        }

        // todo: similar structure to Sub20679B0, except 1101 doesn't branch into 11011/11010
    void TestMisc::Sub2068488(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        byte v130 = byteBuf.Consume();
                        uint v131 = (uint)_dword206B2A0[v130];
                        ushort v132 = wordBuf.Consume();
                        int v133 = UInt32ToInt32(static_cast<uint>(v132 | ((static_cast<uint>(v132) << 16))));
                        uint prevOffset = v131 + outputPos;
                        // todo: same as the 111 block in Sub206CA4, but stopping after x3 instead of going through x7
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        uint v136 = outputSpan1Int[1] + (uint)v133;
                        uint v137 = outputSpan1Int[2] + (uint)v133;
                        uint v138 = outputSpan1Int[3] + (uint)v133;
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v133;
                        outputSpan2Int[1] = v136;
                        outputSpan2Int[2] = v137;
                        outputSpan2Int[3] = v138;
                        uint v141 = outputSpan1Int[256 / 2 + 1] + (uint)v133;
                        uint v142 = outputSpan1Int[256 / 2 + 2] + (uint)v133;
                        uint v143 = outputSpan1Int[256 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2] + (uint)v133;
                        outputSpan2Int[256 / 2 + 1] = v141;
                        outputSpan2Int[256 / 2 + 2] = v142;
                        outputSpan2Int[256 / 2 + 3] = v143;
                        uint v144 = outputSpan1Int[256 * 2 / 2 + 1] + (uint)v133;
                        uint v145 = outputSpan1Int[256 * 2 / 2 + 2] + (uint)v133;
                        uint v146 = outputSpan1Int[256 * 2 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2] + (uint)v133;
                        outputSpan2Int[256 * 2 / 2 + 1] = v144;
                        outputSpan2Int[256 * 2 / 2 + 2] = v145;
                        outputSpan2Int[256 * 2 / 2 + 3] = v146;
                        uint v147 = outputSpan1Int[256 * 3 / 2 + 1] + (uint)v133;
                        uint v148 = outputSpan1Int[256 * 3 / 2 + 2] + (uint)v133;
                        uint v149 = outputSpan1Int[256 * 3 / 2 + 3] + (uint)v133;
                        outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2] + (uint)v133;
                        outputSpan2Int[256 * 3 / 2 + 1] = v147;
                        outputSpan2Int[256 * 3 / 2 + 2] = v148;
                        outputSpan2Int[256 * 3 / 2 + 3] = v149;
                    }
                    else
                    {
                        // 1 1 0
                        ReadNextBit(value, intBuf);
                        if (_readBit != 0)
                        {
                            // 1 1 0 1
                            Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                            // decode pixels 0-3 of lines 0-3 of this 8x8 block
                            Sub2067388(wordBuf, outputSpan2Slice);
                            // decode pixels 4-7 of lines 0-3 of this 8x8 block
                            Sub2067388(wordBuf, outputSpan2Slice.Slice(4));
                        }
                        else
                        {
                            // 1 1 0 0
                            assert(outputPos >= 512);
                            Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                            Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                            ushort v29 = wordBuf[0];
                            // lines 6-7 pixels 2-7
                            outputSpan2Slice[256 * 2 + 2] = v29;
                            outputSpan2Slice[256 * 2 + 3] = v29;
                            outputSpan2Slice[256 * 2 + 4] = v29;
                            outputSpan2Slice[256 * 2 + 5] = v29;
                            outputSpan2Slice[256 * 2 + 6] = v29;
                            outputSpan2Slice[256 * 2 + 7] = v29;
                            outputSpan2Slice[256 * 3 + 2] = v29;
                            outputSpan2Slice[256 * 3 + 3] = v29;
                            outputSpan2Slice[256 * 3 + 4] = v29;
                            outputSpan2Slice[256 * 3 + 5] = v29;
                            outputSpan2Slice[256 * 3 + 6] = v29;
                            outputSpan2Slice[256 * 3 + 7] = v29;
                            uint v30 = v29 | ((uint)(static_cast<uint>(v29) << 16));
                            uint v33 = v30 & 0x7BDF7BDF;
                            uint v34 = v30 & 0x4200420;
                            uint v37 = outputSpan2Prev[0]; // line 3 pixel 0
                            uint v38 = v37 & 0x4200420;
                            v37 &= 0x7BDF7BDF;
                            uint v39 = v38 | ((uint)(v33 + v37) >> 1);
                            // line 4 pixel 0
                            outputSpan2Slice[0] = (ushort)v39; // overwritten below
                            v39 &= 0x7BDF7BDF;
                            // line 3 pixel 0
                            outputSpan2Prev[0] = (ushort)(v34 | ((v39 + v37) >> 1));
                            // line 5 pixel 0
                            outputSpan2Slice[256] = (ushort)(v34 | ((v39 + v33) >> 1)); // overwritten below
                            uint v40 = outputSpan2Prev[1]; // line 3 pixel 1
                            v38 = (v38 & 0xFFFF0000) | (v40 & 0x420);
                            v40 &= 0x7BDF7BDF;
                            uint v41 = v34 | ((uint)(v33 + v40) >> 1);
                            // line 4 pixel 1
                            outputSpan2Slice[1] = (ushort)v41; // overwritten below
                            v41 &= 0x7BDF7BDF;
                            // line 3 pixel 1
                            outputSpan2Prev[1] = (ushort)(v38 | ((v41 + v40) >> 1));
                            // line 5 pixel 1
                            outputSpan2Slice[256 + 1] = (ushort)(v38 | ((v41 + v33) >> 1)); // overwritten below
                            uint v42 = outputSpan2Prev[2]; // line 3 pixel 2
                            uint v43 = v42 & 0x4200420;
                            v42 &= 0x7BDF7BDF;
                            uint v44 = v43 | ((uint)(v33 + v42) >> 1);
                            // line 4 pixel 2
                            outputSpan2Slice[2] = (ushort)v44;
                            v44 &= 0x7BDF7BDF;
                            // line 3 pixel 2
                            outputSpan2Prev[2] = (ushort)(v34 | ((v44 + v42) >> 1));
                            // line 5 pixel 2
                            outputSpan2Slice[256 + 2] = (ushort)(v34 | ((v44 + v33) >> 1));
                            uint v45 = outputSpan2Prev[3]; // line 3 pixel 3
                            v43 = (v43 & 0xFFFF0000) | (v45 & 0x420);
                            v45 &= 0x7BDF7BDF;
                            uint v46 = v34 | ((uint)(v33 + v45) >> 1);
                            // line 4 pixel 3
                            outputSpan2Slice[3] = (ushort)v46;
                            v46 &= 0x7BDF7BDF;
                            // line 3 pixel 3
                            outputSpan2Prev[3] = (ushort)(v43 | ((v46 + v45) >> 1));
                            // line 5 pixel 3
                            outputSpan2Slice[256 + 3] = (ushort)(v43 | ((v46 + v33) >> 1));
                            // todo: same as the 1100 inline in Sub206A0B8 except for having this section and setting more with v29 above
                            uint v47 = outputSpan2Prev[4]; // line 3 pixel 4
                            uint v48 = v47 & 0x4200420;
                            v47 &= 0x7BDF7BDF;
                            uint v49 = v48 | ((uint)(v33 + v47) >> 1);
                            // line 4 pixel 4
                            outputSpan2Slice[4] = (ushort)v49;
                            v49 &= 0x7BDF7BDF;
                            outputSpan2Prev[4] = (ushort)(v34 | ((v49 + v47) >> 1));
                            // line 5 pixel 4
                            outputSpan2Slice[256 + 4] = (ushort)(v34 | ((v49 + v33) >> 1));
                            uint v16 = outputSpan2Prev[5]; // line 3 pixel 5
                            v48 = (v48 & 0xFFFF0000) | (v16 & 0x420);
                            v16 &= 0x7BDF7BDF;
                            uint v17 = v34 | ((uint)(v33 + v16) >> 1);
                            // line 4 pixel 5
                            outputSpan2Slice[5] = (ushort)v17;
                            v17 &= 0x7BDF7BDF;
                            outputSpan2Prev[5] = (ushort)(v48 | ((v17 + v16) >> 1));
                            // line 5 pixel 5
                            outputSpan2Slice[256 + 5] = (ushort)(v48 | ((v17 + v33) >> 1));
                            uint v18 = outputSpan2Prev[6]; // line 3 pixel 6
                            uint v19 = v18 & 0x4200420;
                            v18 &= 0x7BDF7BDF;
                            uint v20 = v19 | ((uint)(v33 + v18) >> 1);
                            // line 4 pixel 6
                            outputSpan2Slice[6] = (ushort)v20;
                            v20 &= 0x7BDF7BDF;
                            outputSpan2Prev[6] = (ushort)(v34 | ((v20 + v18) >> 1));
                            // line 5 pixel 6
                            outputSpan2Slice[256 + 6] = (ushort)(v34 | ((v20 + v33) >> 1));
                            uint v21 = outputSpan2Prev[7]; // line 3 pixel 7
                            v19 = (v19 & 0xFFFF0000) | (v21 & 0x420);
                            v21 &= 0x7BDF7BDF;
                            uint v22 = v34 | ((uint)(v33 + v21) >> 1);
                            // line 4 pixel 7
                            outputSpan2Slice[7] = (ushort)v22;
                            v22 &= 0x7BDF7BDF;
                            outputSpan2Prev[7] = (ushort)(v19 | ((v22 + v21) >> 1));
                            // line 5 pixel 7
                            outputSpan2Slice[256 + 7] = (ushort)(v19 | ((v22 + v33) >> 1));
                            uint v23 = outputSpan2Prev[256 - 1]; // line 4 pixel -1
                            uint v24 = v23 & 0x4200420;
                            v23 &= 0x7BDF7BDF;
                            v47 = v23;
                            v49 = v24 | ((uint)(v33 + v23) >> 1);
                            // todo: ^ (last two lines move values into earlier variables to continue being used below)
                            // line 4 pixel 0
                            outputSpan2Slice[0] = (ushort)v49; // overwriting value set above
                            v49 &= 0x7BDF7BDF;
                            // line 4 pixel -1
                            outputSpan2Prev[256 - 1] = (ushort)(v34 | ((v49 + v47) >> 1));
                            // line 4 pixel 1
                            outputSpan2Slice[1] = (ushort)(v34 | ((v49 + v33) >> 1)); // overwriting value set above
                            uint v50 = outputSpan2Slice[256 - 1]; // line 5 pixel -1
                            v48 = (v48 & 0xFFFF0000) | (v50 & 0x420);
                            v50 &= 0x7BDF7BDF;
                            uint v51 = v34 | ((uint)(v33 + v50) >> 1);
                            // line 5 pixel 0
                            outputSpan2Slice[256] = (ushort)v51; // overwriting value set above
                            v51 &= 0x7BDF7BDF;
                            // line 5 pixel -1
                            outputSpan2Slice[256 - 1] = (ushort)(v48 | ((v51 + v50) >> 1));
                            // line 5 pixel 1
                            outputSpan2Slice[256 + 1] = (ushort)(v48 | ((v51 + v33) >> 1)); // overwriting value set above
                            uint v52 = outputSpan2Slice[256 * 2 - 1]; // line 6 pixel -1
                            uint v53 = v52 & 0x4200420;
                            v52 &= 0x7BDF7BDF;
                            uint v54 = v53 | ((uint)(v33 + v52) >> 1);
                            // line 6 pixel 0
                            outputSpan2Slice[256 * 2] = (ushort)v54;
                            v54 &= 0x7BDF7BDF;
                            // line 6 pixel -1
                            outputSpan2Slice[256 * 2 - 1] = (ushort)(v34 | ((v54 + v52) >> 1));
                            // line 6 pixel 1
                            outputSpan2Slice[256 * 2 + 1] = (ushort)(v34 | ((v54 + v33) >> 1));
                            uint v55 = outputSpan2Slice[256 * 3 - 1]; // line 7 pixel -1
                            v53 = (v53 & 0xFFFF0000) | (v55 & 0x420);
                            v55 &= 0x7BDF7BDF;
                            uint v56 = v34 | ((uint)(v33 + v55) >> 1);
                            // line 7 pixel 0
                            outputSpan2Slice[256 * 3] = (ushort)v56;
                            v56 &= 0x7BDF7BDF;
                            // line 7 pixel -1
                            outputSpan2Slice[256 * 3 - 1] = (ushort)(v53 | ((v56 + v55) >> 1));
                            // line 7 pixel 1
                            outputSpan2Slice[256 * 3 + 1] = (ushort)(v53 | ((v56 + v33) >> 1));
                            wordBuf = wordBuf.Slice(1);
                        }
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        // decode pixels 0-3 of lines 4-7 of this 8x8 block
                        Sub206A0B8(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        // advance by 8 bytes = 4 words/pixels
                        outputPos += 8;
                        // decode pixels 4-7 of lines 4-7 of this 8x8 block
                        Sub206A0B8(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        // reset output position, to be advanced by the caller to the next 8x8 block
                        outputPos -= 8;
                    }
                    else
                    {
                        // 1 0 0
                        Sub206935C(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 1024;
                        Sub206935C(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 1024;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets more values (but fewer than in Sub20679B0)
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                uint v15 = outputSpan1Int[1];           // for 0 0, or also 0 1 if it has dword alignment
                uint v16 = outputSpan1Int[2];
                uint v17 = outputSpan1Int[3];
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[1] = v15;
                outputSpan2Int[2] = v16;
                outputSpan2Int[3] = v17;
                uint v20 = outputSpan1Int[256 / 2 + 1];
                uint v21 = outputSpan1Int[256 / 2 + 2];
                uint v22 = outputSpan1Int[256 / 2 + 3];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
                outputSpan2Int[256 / 2 + 1] = v20;
                outputSpan2Int[256 / 2 + 2] = v21;
                outputSpan2Int[256 / 2 + 3] = v22;
                uint v23 = outputSpan1Int[256 * 2 / 2 + 1];
                uint v24 = outputSpan1Int[256 * 2 / 2 + 2];
                uint v25 = outputSpan1Int[256 * 2 / 2 + 3];
                outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2];
                outputSpan2Int[256 * 2 / 2 + 1] = v23;
                outputSpan2Int[256 * 2 / 2 + 2] = v24;
                outputSpan2Int[256 * 2 / 2 + 3] = v25;
                uint v26 = outputSpan1Int[256 * 3 / 2 + 1];
                uint v27 = outputSpan1Int[256 * 3 / 2 + 2];
                uint v28 = outputSpan1Int[256 * 3 / 2 + 3];
                outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2];
                outputSpan2Int[256 * 3 / 2 + 1] = v26;
                outputSpan2Int[256 * 3 / 2 + 2] = v27;
                outputSpan2Int[256 * 3 / 2 + 3] = v28;
            }
        }

        // todo: same structure as Sub2068488
    void TestMisc::Sub206A0B8(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        // todo: like the section in Sub20679B0, but sets fewer values
                        byte v57 = byteBuf.Consume();
                        uint v58 = (uint)_dword206B2A0[v57];
                        ushort v59 = wordBuf.Consume();
                        int v60 = UInt32ToInt32(static_cast<uint>(v59 | ((static_cast<uint>(v59) << 16))));
                        uint prevOffset = v58 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v60;
                        outputSpan2Int[1] = outputSpan1Int[1] + (uint)v60;
                        outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2] + (uint)v60;
                        outputSpan2Int[256 / 2 + 1] = outputSpan1Int[256 / 2 + 1] + (uint)v60;
                        outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2] + (uint)v60;
                        outputSpan2Int[256 * 2 / 2 + 1] = outputSpan1Int[256 * 2 / 2 + 1] + (uint)v60;
                        outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2] + (uint)v60;
                        outputSpan2Int[256 * 3 / 2 + 1] = outputSpan1Int[256 * 3 / 2 + 1] + (uint)v60;
                    }
                    else
                    {
                        // 1 1 0
                        ReadNextBit(value, intBuf);
                        if (_readBit != 0)
                        {
                            // 1 1 0 1
                            Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                            // decode pixels 0-3 of lines 4-7 of this 8x8 block
                            Sub2067388(wordBuf, outputSpan2Slice);
                        }
                        else
                        {
                            // 1 1 0 0
                            // decode pixels 4-7 of lines 4-7 of this 8x8 block (and others?)
                            assert(outputPos >= 512);
                            Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                            Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                            ushort v29 = wordBuf[0];
                            outputSpan2Slice[256 * 2 + 2] = v29;
                            outputSpan2Slice[256 * 2 + 3] = v29;
                            outputSpan2Slice[256 * 3 + 2] = v29;
                            outputSpan2Slice[256 * 3 + 3] = v29;
                            uint v30 = v29 | ((uint)(static_cast<uint>(v29) << 16));
                            uint v33 = v30 & 0x7BDF7BDF;
                            uint v34 = v30 & 0x4200420;
                            uint v37 = outputSpan2Prev[0];
                            uint v38 = v37 & 0x4200420;
                            v37 &= 0x7BDF7BDF;
                            uint v39 = v38 | ((uint)(v33 + v37) >> 1);
                            outputSpan2Slice[0] = (ushort)v39; // overwritten below
                            v39 &= 0x7BDF7BDF;
                            outputSpan2Prev[0] = (ushort)(v34 | ((v39 + v37) >> 1));
                            outputSpan2Slice[256] = (ushort)(v34 | ((v39 + v33) >> 1));
                            uint v40 = outputSpan2Prev[1];
                            v38 = (v38 & 0xFFFF0000) | (v40 & 0x420);
                            v40 &= 0x7BDF7BDF;
                            uint v41 = v34 | ((uint)(v33 + v40) >> 1);
                            outputSpan2Slice[1] = (ushort)v41; // overwritten below
                            v41 &= 0x7BDF7BDF;
                            outputSpan2Prev[1] = (ushort)(v38 | ((v41 + v40) >> 1));
                            outputSpan2Slice[256 + 1] = (ushort)(v38 | ((v41 + v33) >> 1));
                            uint v42 = outputSpan2Prev[2];
                            uint v43 = v42 & 0x4200420;
                            v42 &= 0x7BDF7BDF;
                            uint v44 = v43 | ((uint)(v33 + v42) >> 1);
                            outputSpan2Slice[2] = (ushort)v44;
                            v44 &= 0x7BDF7BDF;
                            outputSpan2Prev[2] = (ushort)(v34 | ((v44 + v42) >> 1));
                            outputSpan2Slice[256 + 2] = (ushort)(v34 | ((v44 + v33) >> 1));
                            uint v45 = outputSpan2Prev[3];
                            v43 = (v43 & 0xFFFF0000) | (v45 & 0x420);
                            v45 &= 0x7BDF7BDF;
                            uint v46 = v34 | ((uint)(v33 + v45) >> 1);
                            outputSpan2Slice[3] = (ushort)v46;
                            v46 &= 0x7BDF7BDF;
                            outputSpan2Prev[3] = (ushort)(v43 | ((v46 + v45) >> 1));
                            outputSpan2Slice[256 + 3] = (ushort)(v43 | ((v46 + v33) >> 1));
                            uint v47 = outputSpan2Prev[256 - 1];
                            uint v48 = v47 & 0x4200420;
                            v47 &= 0x7BDF7BDF;
                            uint v49 = v48 | ((uint)(v33 + v47) >> 1);
                            outputSpan2Slice[0] = (ushort)v49; // overwriting value set above
                            v49 &= 0x7BDF7BDF;
                            outputSpan2Prev[256 - 1] = (ushort)(v34 | ((v49 + v47) >> 1));
                            outputSpan2Slice[1] = (ushort)(v34 | ((v49 + v33) >> 1)); // overwriting value set above
                            uint v50 = outputSpan2Slice[256 - 1];
                            v48 = (v48 & 0xFFFF0000) | (v50 & 0x420);
                            v50 &= 0x7BDF7BDF;
                            uint v51 = v34 | ((uint)(v33 + v50) >> 1);
                            outputSpan2Slice[256] = (ushort)v51;
                            v51 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 - 1] = (ushort)(v48 | ((v51 + v50) >> 1));
                            outputSpan2Slice[256 + 1] = (ushort)(v48 | ((v51 + v33) >> 1));
                            uint v52 = outputSpan2Slice[256 * 2 - 1];
                            uint v53 = v52 & 0x4200420;
                            v52 &= 0x7BDF7BDF;
                            uint v54 = v53 | ((uint)(v33 + v52) >> 1);
                            outputSpan2Slice[256 * 2] = (ushort)v54;
                            v54 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 2 - 1] = (ushort)(v34 | ((v54 + v52) >> 1));
                            outputSpan2Slice[256 * 2 + 1] = (ushort)(v34 | ((v54 + v33) >> 1));
                            uint v55 = outputSpan2Slice[256 * 3 - 1];
                            v53 = (v53 & 0xFFFF0000) | (v55 & 0x420);
                            v55 &= 0x7BDF7BDF;
                            uint v56 = v34 | ((uint)(v33 + v55) >> 1);
                            outputSpan2Slice[256 * 3] = (ushort)v56;
                            v56 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 3 - 1] = (ushort)(v53 | ((v56 + v55) >> 1));
                            outputSpan2Slice[256 * 3 + 1] = (ushort)(v53 | ((v56 + v33) >> 1));
                            wordBuf = wordBuf.Slice(1);
                        }
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        Sub206A8C0(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 4;
                        Sub206A8C0(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 4;
                    }
                    else
                    {
                        // 1 0 0
                        Sub206A5A4(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 1024;
                        Sub206A5A4(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 1024;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets even fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[1] = outputSpan1Int[1];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
                outputSpan2Int[256 / 2 + 1] = outputSpan1Int[256 / 2 + 1];
                outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2];
                outputSpan2Int[256 * 2 / 2 + 1] = outputSpan1Int[256 * 2 / 2 + 1];
                outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2];
                outputSpan2Int[256 * 3 / 2 + 1] = outputSpan1Int[256 * 3 / 2 + 1];
            }
        }

        // todo: same structure as Sub2068488 and Sub206A0B8, except 110 doesn't branch into 1101/1100
    void TestMisc::Sub206935C(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        assert(outputPos >= 512);
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                        // todo: similar to the inlines in Sub Sub206A0B8 and Sub2068488
                        uint v58 = wordBuf[0];
                        v58 = v58 | ((static_cast<uint>(v58) << 16));
                        uint v61 = outputSpan2Prev[0];
                        uint v62 = v61 & 0x4200420;
                        v61 &= 0x7BDF7BDF;
                        uint v63 = v62 | (((v58 & 0x7BDF7BDF) + v61) >> 1);
                        outputSpan2Slice[0] = (ushort)v63;
                        v63 &= 0x7BDF7BDF;
                        outputSpan2Prev[0] = (ushort)(v58 & 0x420 | ((v63 + v61) >> 1));
                        outputSpan2Slice[256] = (ushort)(v58 & 0x420 | ((v63 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v64 = outputSpan2Prev[1];
                        v62 = (v62 & 0xFFFF0000) | (v64 & 0x420);
                        v64 &= 0x7BDF7BDF;
                        outputSpan2Slice[1] = (ushort)(v58 & 0x420 | (((v58 & 0x7BDF7BDF) + v64) >> 1));
                        uint v65 = (((v58 & 0x7BDF7BDF) + v64) >> 1) & 0x7BDF7BDF;
                        outputSpan2Prev[1] = (ushort)(v62 | ((v65 + v64) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v62 | ((v65 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v66 = outputSpan2Prev[2];
                        uint v67 = v66 & 0x4200420;
                        v66 &= 0x7BDF7BDF;
                        uint v68 = v67 | (((v58 & 0x7BDF7BDF) + v66) >> 1);
                        outputSpan2Slice[2] = (ushort)v68;
                        v68 &= 0x7BDF7BDF;
                        outputSpan2Prev[2] = (ushort)(v58 & 0x420 | ((v68 + v66) >> 1));
                        outputSpan2Slice[256 + 2] = (ushort)(v58 & 0x420 | ((v68 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v69 = outputSpan2Prev[3];
                        v67 = (v67 & 0xFFFF0000) | (v69 & 0x420);
                        v69 &= 0x7BDF7BDF;
                        outputSpan2Slice[3] = (ushort)(v58 & 0x420 | (((v58 & 0x7BDF7BDF) + v69) >> 1));
                        uint v70 = (((v58 & 0x7BDF7BDF) + v69) >> 1) & 0x7BDF7BDF;
                        outputSpan2Prev[3] = (ushort)(v67 | ((v70 + v69) >> 1));
                        outputSpan2Slice[256 + 3] = (ushort)(v67 | ((v70 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v71 = outputSpan2Prev[4];
                        uint v72 = v71 & 0x4200420;
                        v71 &= 0x7BDF7BDF;
                        uint v73 = v72 | (((v58 & 0x7BDF7BDF) + v71) >> 1);
                        outputSpan2Slice[4] = (ushort)v73;
                        v73 &= 0x7BDF7BDF;
                        outputSpan2Prev[4] = (ushort)(v58 & 0x420 | ((v73 + v71) >> 1));
                        outputSpan2Slice[256 + 4] = (ushort)(v58 & 0x420 | ((v73 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v74 = outputSpan2Prev[5];
                        v72 = (v74 & 0xFFFF0000) | (v74 & 0x420);
                        v74 &= 0x7BDF7BDF;
                        outputSpan2Slice[5] = (ushort)(v58 & 0x420 | (((v58 & 0x7BDF7BDF) + v74) >> 1));
                        uint v75 = (((v58 & 0x7BDF7BDF) + v74) >> 1) & 0x7BDF7BDF;
                        outputSpan2Prev[5] = (ushort)(v72 | ((v75 + v74) >> 1));
                        outputSpan2Slice[256 + 5] = (ushort)(v72 | ((v75 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v76 = outputSpan2Prev[6];
                        uint v77 = v76 & 0x4200420;
                        v76 &= 0x7BDF7BDF;
                        uint v78 = v77 | (((v58 & 0x7BDF7BDF) + v76) >> 1);
                        outputSpan2Slice[6] = (ushort)v78;
                        v78 &= 0x7BDF7BDF;
                        outputSpan2Prev[6] = (ushort)(v58 & 0x420 | ((v78 + v76) >> 1));
                        outputSpan2Slice[256 + 6] = (ushort)(v58 & 0x420 | ((v78 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v79 = outputSpan2Prev[7];
                        v77 = (v77 & 0xFFFF0000) | (v79 & 0x420);
                        v79 &= 0x7BDF7BDF;
                        outputSpan2Slice[7] = (ushort)(v58 & 0x420 | (((v58 & 0x7BDF7BDF) + v79) >> 1));
                        uint v80 = (((v58 & 0x7BDF7BDF) + v79) >> 1) & 0x7BDF7BDF;
                        outputSpan2Prev[7] = (ushort)(v77 | ((v80 + v79) >> 1));
                        outputSpan2Slice[256 + 7] = (ushort)(v77 | ((v80 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v81 = outputSpan2Prev[256 - 1];
                        uint v82 = v81 & 0x4200420;
                        v81 &= 0x7BDF7BDF;
                        uint v83 = v82 | (((v58 & 0x7BDF7BDF) + v81) >> 1);
                        outputSpan2Slice[0] = (ushort)v83;
                        v83 &= 0x7BDF7BDF;
                        outputSpan2Prev[256 - 1] = (ushort)(v58 & 0x420 | ((v83 + v81) >> 1));
                        outputSpan2Slice[1] = (ushort)(v58 & 0x420 | ((v83 + (v58 & 0x7BDF7BDF)) >> 1));
                        uint v84 = outputSpan2Slice[256 - 1];
                        v82 = (v82 & 0xFFFF0000) | (v84 & 0x420);
                        v84 &= 0x7BDF7BDF;
                        outputSpan2Slice[256] = (ushort)(v58 & 0x420 | (((v58 & 0x7BDF7BDF) + v84) >> 1));
                        uint v85 = (((v58 & 0x7BDF7BDF) + v84) >> 1) & 0x7BDF7BDF;
                        outputSpan2Slice[256 - 1] = (ushort)(v82 | ((v85 + v84) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v82 | ((v85 + (v58 & 0x7BDF7BDF)) >> 1));
                        wordBuf = wordBuf.Slice(1);
                    }
                    else
                    {
                        // 1 1 0
                        byte v37 = byteBuf.Consume();
                        uint v38 = (uint)_dword206B2A0[v37];
                        ushort v39 = wordBuf.Consume();
                        int v40 = UInt32ToInt32(static_cast<uint>(v39 | ((static_cast<uint>(v39) << 16))));
                        uint prevOffset = v38 + outputPos;
                        // todo: similar to other blocks with the dword alignment thing
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        uint v43 = outputSpan1Int[0] + (uint)v40;
                        uint v44 = outputSpan1Int[1] + (uint)v40;
                        uint v45 = outputSpan1Int[2] + (uint)v40;
                        uint v46 = outputSpan1Int[3] + (uint)v40;
                        outputSpan2Slice[0] = (ushort)(v43 & 0xFFFF);
                        outputSpan2Slice[1] = (ushort)(v43 >> 16);
                        outputSpan2Slice[2] = (ushort)(v44 & 0xFFFF);
                        outputSpan2Slice[3] = (ushort)(v44 >> 16);
                        outputSpan2Slice[4] = (ushort)(v45 & 0xFFFF);
                        outputSpan2Slice[5] = (ushort)(v45 >> 16);
                        outputSpan2Slice[6] = (ushort)(v46 & 0xFFFF);
                        outputSpan2Slice[7] = (ushort)(v46 >> 16);
                        uint v49 = outputSpan1Int[256 / 2] + (uint)v40;
                        uint v50 = outputSpan1Int[256 / 2 + 1] + (uint)v40;
                        uint v51 = outputSpan1Int[256 / 2 + 2] + (uint)v40;
                        uint v52 = outputSpan1Int[256 / 2 + 3] + (uint)v40;
                        outputSpan2Slice[256] = (ushort)(v49 & 0xFFFF);
                        outputSpan2Slice[256 + 1] = (ushort)(v49 >> 16);
                        outputSpan2Slice[256 + 2] = (ushort)(v50 & 0xFFFF);
                        outputSpan2Slice[256 + 3] = (ushort)(v50 >> 16);
                        outputSpan2Slice[256 + 4] = (ushort)(v51 & 0xFFFF);
                        outputSpan2Slice[256 + 5] = (ushort)(v51 >> 16);
                        outputSpan2Slice[256 + 6] = (ushort)(v52 & 0xFFFF);
                        outputSpan2Slice[256 + 7] = (ushort)(v52 >> 16);
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        Sub206A5A4(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 8;
                        Sub206A5A4(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 8;
                    }
                    else
                    {
                        // 1 0 0
                        Sub2069D44(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 512;
                        Sub2069D44(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 512;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                uint v15 = outputSpan1Int[1];
                uint v16 = outputSpan1Int[2];
                uint v17 = outputSpan1Int[3];
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[1] = v15;
                outputSpan2Int[2] = v16;
                outputSpan2Int[3] = v17;
                uint v20 = outputSpan1Int[256 / 2 + 1];
                uint v21 = outputSpan1Int[256 / 2 + 2];
                uint v22 = outputSpan1Int[256 / 2 + 3];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
                outputSpan2Int[256 / 2 + 1] = v20;
                outputSpan2Int[256 / 2 + 2] = v21;
                outputSpan2Int[256 / 2 + 3] = v22;
            }
        }

        // todo: same structure as others (10 doesn't branch into 101/100)
    void TestMisc::Sub2069D44(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        ushort v33 = wordBuf.Consume();
                        uint v34 = (uint)(v33 | ((static_cast<uint>(v33) << 16)));
                        outputSpan2Int[0] = v34;
                        outputSpan2Int[1] = v34;
                        outputSpan2Int[2] = v34;
                        outputSpan2Int[3] = v34;
                    }
                    else
                    {
                        // 1 1 0
                        // todo: like the section in Sub20679B0, but sets fewer values
                        byte v24 = byteBuf.Consume();
                        uint v25 = (uint)_dword206B2A0[v24];
                        ushort v26 = wordBuf.Consume();
                        int v27 = UInt32ToInt32(static_cast<uint>(v26 | ((static_cast<uint>(v26) << 16))));
                        uint prevOffset = v25 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v27;
                        outputSpan2Int[1] = outputSpan1Int[1] + (uint)v27;
                        outputSpan2Int[2] = outputSpan1Int[2] + (uint)v27;
                        outputSpan2Int[3] = outputSpan1Int[3] + (uint)v27;
                    }
                }
                else
                {
                    // 1 0
                    Sub206AC14(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                    outputPos += 8;
                    Sub206AC14(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                    outputPos -= 8;
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets even fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[1] = outputSpan1Int[1];
                outputSpan2Int[2] = outputSpan1Int[2];
                outputSpan2Int[3] = outputSpan1Int[3];
            }
        }

        // todo: same structure as Sub2068488 and Sub206A0B8
    void TestMisc::Sub2068B24(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        // todo: like the section in Sub20679B0, but sets fewer values
                        byte v77 = byteBuf.Consume();
                        uint v78 = (uint)_dword206B2A0[v77];
                        ushort v79 = wordBuf.Consume();
                        int v80 = UInt32ToInt32(static_cast<uint>(v79 | ((static_cast<uint>(v79) << 16))));
                        uint prevOffset = v78 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v80;
                        outputSpan2Int[1] = outputSpan1Int[1] + (uint)v80;
                        outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2] + (uint)v80;
                        outputSpan2Int[256 / 2 + 1] = outputSpan1Int[256 / 2 + 1] + (uint)v80;
                        outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2] + (uint)v80;
                        outputSpan2Int[256 * 2 / 2 + 1] = outputSpan1Int[256 * 2 / 2 + 1] + (uint)v80;
                        outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2] + (uint)v80;
                        outputSpan2Int[256 * 3 / 2 + 1] = outputSpan1Int[256 * 3 / 2 + 1] + (uint)v80;
                        outputSpan2Int[256 * 4 / 2] = outputSpan1Int[256 * 4 / 2] + (uint)v80;
                        outputSpan2Int[256 * 4 / 2 + 1] = outputSpan1Int[256 * 4 / 2 + 1] + (uint)v80;
                        outputSpan2Int[256 * 5 / 2] = outputSpan1Int[256 * 5 / 2] + (uint)v80;
                        outputSpan2Int[256 * 5 / 2 + 1] = outputSpan1Int[256 * 5 / 2 + 1] + (uint)v80;
                        outputSpan2Int[256 * 6 / 2] = outputSpan1Int[256 * 6 / 2] + (uint)v80;
                        outputSpan2Int[256 * 6 / 2 + 1] = outputSpan1Int[256 * 6 / 2 + 1] + (uint)v80;
                        outputSpan2Int[256 * 7 / 2] = outputSpan1Int[256 * 7 / 2] + (uint)v80;
                        outputSpan2Int[256 * 7 / 2 + 1] = outputSpan1Int[256 * 7 / 2 + 1] + (uint)v80;
                    }
                    else
                    {
                        // 1 1 0
                        ReadNextBit(value, intBuf);
                        if (_readBit != 0)
                        {
                            // 1 1 0 1
                            Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                            Sub2067388(wordBuf, outputSpan2Slice);
                            // normally advanced by 1536 bytes (768 words), this call advances another 512 bytes (256 words) = 2048 bytes (1024 words)
                            Sub2067388(wordBuf, outputSpan2Slice.Slice(1024));
                        }
                        else
                        {
                            // 1 1 0 0
                            assert(outputPos >= 512);
                            Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                            Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                            // todo: similar to the inlines in Sub Sub206A0B8 and Sub2068488 and Sub206935C
                            ushort v33 = wordBuf[0];
                            outputSpan2Slice[256 * 2 + 2] = v33;
                            outputSpan2Slice[256 * 2 + 3] = v33;
                            outputSpan2Slice[256 * 3 + 2] = v33;
                            outputSpan2Slice[256 * 3 + 3] = v33;
                            outputSpan2Slice[256 * 4 + 2] = v33;
                            outputSpan2Slice[256 * 4 + 3] = v33;
                            outputSpan2Slice[256 * 5 + 2] = v33;
                            outputSpan2Slice[256 * 5 + 3] = v33;
                            outputSpan2Slice[256 * 6 + 2] = v33;
                            outputSpan2Slice[256 * 6 + 3] = v33;
                            outputSpan2Slice[256 * 7 + 2] = v33;
                            outputSpan2Slice[256 * 7 + 3] = v33;
                            uint v34 = v33 | ((uint)(static_cast<uint>(v33) << 16));
                            uint v42 = v34 & 0x7BDF7BDF;
                            uint v43 = v34 & 0x4200420;
                            uint v46 = outputSpan2Prev[0];
                            uint v47 = v46 & 0x4200420;
                            v46 &= 0x7BDF7BDF;
                            uint v48 = v47 | ((uint)(v42 + v46) >> 1);
                            outputSpan2Slice[0] = (ushort)v48;
                            v48 &= 0x7BDF7BDF;
                            outputSpan2Prev[0] = (ushort)(v43 | ((v48 + v46) >> 1));
                            outputSpan2Slice[256] = (ushort)(v43 | ((v48 + v42) >> 1));
                            uint v49 = outputSpan2Prev[1];
                            v47 = (v47 & 0xFFFF0000) | (v49 & 0x420);
                            v49 &= 0x7BDF7BDF;
                            uint v50 = v43 | ((uint)(v42 + v49) >> 1);
                            outputSpan2Slice[1] = (ushort)v50;
                            v50 &= 0x7BDF7BDF;
                            outputSpan2Prev[1] = (ushort)(v47 | ((v50 + v49) >> 1));
                            outputSpan2Slice[256 + 1] = (ushort)(v47 | ((v50 + v42) >> 1));
                            uint v51 = outputSpan2Prev[2];
                            uint v52 = v51 & 0x4200420;
                            v51 &= 0x7BDF7BDF;
                            uint v53 = v52 | ((uint)(v42 + v51) >> 1);
                            outputSpan2Slice[2] = (ushort)v53;
                            v53 &= 0x7BDF7BDF;
                            outputSpan2Prev[2] = (ushort)(v43 | ((v53 + v51) >> 1));
                            outputSpan2Slice[256 + 2] = (ushort)(v43 | ((v53 + v42) >> 1));
                            uint v54 = outputSpan2Prev[3];
                            v52 = (v52 & 0xFFFF0000) | (v54 & 0x420);
                            v54 &= 0x7BDF7BDF;
                            uint v55 = v43 | ((uint)(v42 + v54) >> 1);
                            outputSpan2Slice[3] = (ushort)v55;
                            v55 &= 0x7BDF7BDF;
                            outputSpan2Prev[3] = (ushort)(v52 | ((v55 + v54) >> 1));
                            outputSpan2Slice[256 + 3] = (ushort)(v52 | ((v55 + v42) >> 1));
                            uint v56 = outputSpan2Prev[256 - 1];
                            uint v57 = v56 & 0x4200420;
                            v56 &= 0x7BDF7BDF;
                            uint v58 = v57 | ((uint)(v42 + v56) >> 1);
                            outputSpan2Slice[0] = (ushort)v58;
                            v58 &= 0x7BDF7BDF;
                            outputSpan2Prev[256 - 1] = (ushort)(v43 | ((v58 + v56) >> 1));
                            outputSpan2Slice[1] = (ushort)(v43 | ((v58 + v42) >> 1));
                            uint v59 = outputSpan2Slice[256 - 1];
                            v57 = (v57 & 0xFFFF0000) | (v59 & 0x420);
                            v59 &= 0x7BDF7BDF;
                            uint v60 = v43 | ((uint)(v42 + v59) >> 1);
                            outputSpan2Slice[256] = (ushort)v60;
                            v60 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 - 1] = (ushort)(v57 | ((v60 + v59) >> 1));
                            outputSpan2Slice[256 + 1] = (ushort)(v57 | ((v60 + v42) >> 1));
                            uint v61 = outputSpan2Slice[256 * 2 - 1];
                            uint v62 = v61 & 0x4200420;
                            v61 &= 0x7BDF7BDF;
                            uint v63 = v62 | ((uint)(v42 + v61) >> 1);
                            outputSpan2Slice[256 * 2] = (ushort)v63;
                            v63 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 2 - 1] = (ushort)(v43 | ((v63 + v61) >> 1));
                            outputSpan2Slice[256 * 2 + 1] = (ushort)(v43 | ((v63 + v42) >> 1));
                            uint v64 = outputSpan2Slice[256 * 3 - 1];
                            v62 = (v62 & 0xFFFF0000) | (v64 & 0x420);
                            v64 &= 0x7BDF7BDF;
                            uint v65 = v43 | ((uint)(v42 + v64) >> 1);
                            outputSpan2Slice[256 * 3] = (ushort)v65;
                            v65 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 3 - 1] = (ushort)(v62 | ((v65 + v64) >> 1));
                            outputSpan2Slice[256 * 3 + 1] = (ushort)(v62 | ((v65 + v42) >> 1));
                            uint v66 = outputSpan2Slice[256 * 4 - 1];
                            uint v67 = v66 & 0x4200420;
                            v66 &= 0x7BDF7BDF;
                            uint v68 = v67 | ((uint)(v42 + v66) >> 1);
                            outputSpan2Slice[256 * 4] = (ushort)v68;
                            v68 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 4 - 1] = (ushort)(v43 | ((v68 + v66) >> 1));
                            outputSpan2Slice[256 * 4 + 1] = (ushort)(v43 | ((v68 + v42) >> 1));
                            uint v69 = outputSpan2Slice[256 * 5 - 1];
                            v67 = (v67 & 0xFFFF0000) | (v69 & 0x420);
                            v69 &= 0x7BDF7BDF;
                            uint v70 = v43 | ((uint)(v42 + v69) >> 1);
                            outputSpan2Slice[256 * 5] = (ushort)v70;
                            v70 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 5 - 1] = (ushort)(v67 | ((v70 + v69) >> 1));
                            outputSpan2Slice[256 * 5 + 1] = (ushort)(v67 | ((v70 + v42) >> 1));
                            uint v71 = outputSpan2Slice[256 * 6 - 1];
                            uint v72 = v71 & 0x4200420;
                            v71 &= 0x7BDF7BDF;
                            uint v73 = v72 | ((uint)(v42 + v71) >> 1);
                            outputSpan2Slice[256 * 6] = (ushort)v73;
                            v73 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 6 - 1] = (ushort)(v43 | ((v73 + v71) >> 1));
                            outputSpan2Slice[256 * 6 + 1] = (ushort)(v43 | ((v73 + v42) >> 1));
                            uint v74 = outputSpan2Slice[256 * 7 - 1];
                            v72 = (v72 & 0xFFFF0000) | (v74 & 0x420);
                            v74 &= 0x7BDF7BDF;
                            uint v75 = v43 | ((uint)(v42 + v74) >> 1);
                            outputSpan2Slice[256 * 7] = (ushort)v75;
                            v75 &= 0x7BDF7BDF;
                            outputSpan2Slice[256 * 7 - 1] = (ushort)(v72 | ((v75 + v74) >> 1));
                            outputSpan2Slice[256 * 7 + 1] = (ushort)(v72 | ((v75 + v42) >> 1));
                            wordBuf = wordBuf.Slice(1);
                        }
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        Sub20697D0(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 4;
                        Sub20697D0(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 4;
                    }
                    else
                    {
                        // 1 0 0
                        Sub206A0B8(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 2048;
                        Sub206A0B8(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 2048;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[1] = outputSpan1Int[1];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
                outputSpan2Int[256 / 2 + 1] = outputSpan1Int[256 / 2 + 1];
                outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2];
                outputSpan2Int[256 * 2 / 2 + 1] = outputSpan1Int[256 * 2 / 2 + 1];
                outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2];
                outputSpan2Int[256 * 3 / 2 + 1] = outputSpan1Int[256 * 3 / 2 + 1];
                outputSpan2Int[256 * 4 / 2] = outputSpan1Int[256 * 4 / 2];
                outputSpan2Int[256 * 4 / 2 + 1] = outputSpan1Int[256 * 4 / 2 + 1];
                outputSpan2Int[256 * 5 / 2] = outputSpan1Int[256 * 5 / 2];
                outputSpan2Int[256 * 5 / 2 + 1] = outputSpan1Int[256 * 5 / 2 + 1];
                outputSpan2Int[256 * 6 / 2] = outputSpan1Int[256 * 6 / 2];
                outputSpan2Int[256 * 6 / 2 + 1] = outputSpan1Int[256 * 6 / 2 + 1];
                outputSpan2Int[256 * 7 / 2] = outputSpan1Int[256 * 7 / 2];
                outputSpan2Int[256 * 7 / 2 + 1] = outputSpan1Int[256 * 7 / 2 + 1];
            }
        }

        // todo: same structure as Sub206935C (same structure as Sub2068488 and Sub206A0B8, except 110 doesn't branch into 1101/1100)
    void TestMisc::Sub206A5A4(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        assert(outputPos >= 512);
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                        // todo: similar to the inlines in Sub Sub206A0B8 and Sub2068488 and Sub206935C and Sub2068B24 and Sub20679B0
                        ushort v31 = wordBuf[0];
                        uint v32 = v31 | ((uint)(static_cast<uint>(v31) << 16));
                        uint v35 = outputSpan2Prev[0];
                        uint v36 = v35 & 0x4200420;
                        v35 &= 0x7BDF7BDF;
                        uint v37 = v36 | (((v32 & 0x7BDF7BDF) + v35) >> 1);
                        outputSpan2Slice[0] = (ushort)v37;
                        v37 &= 0x7BDF7BDF;
                        outputSpan2Prev[0] = (ushort)(v32 & 0x420 | ((v37 + v35) >> 1));
                        outputSpan2Slice[256] = (ushort)(v32 & 0x420 | ((v37 + (v32 & 0x7BDF7BDF)) >> 1));
                        uint v38 = outputSpan2Prev[1];
                        v36 = (v36 & 0xFFFF0000) | (v38 & 0x420);
                        v38 &= 0x7BDF7BDF;
                        outputSpan2Slice[1] = (ushort)(v32 & 0x420 | (((v32 & 0x7BDF7BDF) + v38) >> 1));
                        uint v39 = (((v32 & 0x7BDF7BDF) + v38) >> 1) & 0x7BDF7BDF;
                        outputSpan2Prev[1] = (ushort)(v36 | ((v39 + v38) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v36 | ((v39 + (v32 & 0x7BDF7BDF)) >> 1));
                        uint v40 = outputSpan2Prev[2];
                        uint v41 = v40 & 0x4200420;
                        v40 &= 0x7BDF7BDF;
                        uint v42 = v41 | (((v32 & 0x7BDF7BDF) + v40) >> 1);
                        outputSpan2Slice[2] = (ushort)v42;
                        v42 &= 0x7BDF7BDF;
                        outputSpan2Prev[2] = (ushort)(v32 & 0x420 | ((v42 + v40) >> 1));
                        outputSpan2Slice[256 + 2] = (ushort)(v32 & 0x420 | ((v42 + (v32 & 0x7BDF7BDF)) >> 1));
                        uint v43 = outputSpan2Prev[3];
                        v41 = (v41 & 0xFFFF0000) | (v43 & 0x420);
                        v43 &= 0x7BDF7BDF;
                        outputSpan2Slice[3] = (ushort)(v32 & 0x420 | (((v32 & 0x7BDF7BDF) + v43) >> 1));
                        uint v44 = (((v32 & 0x7BDF7BDF) + v43) >> 1) & 0x7BDF7BDF;
                        outputSpan2Prev[3] = (ushort)(v41 | ((v44 + v43) >> 1));
                        outputSpan2Slice[256 + 3] = (ushort)(v41 | ((v44 + (v32 & 0x7BDF7BDF)) >> 1));
                        uint v45 = outputSpan2Prev[256 - 1];
                        uint v46 = v45 & 0x4200420;
                        v45 &= 0x7BDF7BDF;
                        uint v47 = v46 | (((v32 & 0x7BDF7BDF) + v45) >> 1);
                        outputSpan2Slice[0] = (ushort)v47;
                        v47 &= 0x7BDF7BDF;
                        outputSpan2Prev[256 - 1] = (ushort)(v32 & 0x420 | ((v47 + v45) >> 1));
                        outputSpan2Slice[1] = (ushort)(v32 & 0x420 | ((v47 + (v32 & 0x7BDF7BDF)) >> 1));
                        uint v48 = outputSpan2Slice[256 - 1];
                        v46 = (v46 & 0xFFFF0000) | (v48 & 0x420);
                        v48 &= 0x7BDF7BDF;
                        outputSpan2Slice[256] = (ushort)(v32 & 0x420 | (((v32 & 0x7BDF7BDF) + v48) >> 1));
                        uint v49 = (((v32 & 0x7BDF7BDF) + v48) >> 1) & 0x7BDF7BDF;
                        outputSpan2Slice[256 - 1] = (ushort)(v46 | ((v49 + v48) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v46 | ((v49 + (v32 & 0x7BDF7BDF)) >> 1));
                        wordBuf = wordBuf.Slice(1);
                    }
                    else
                    {
                        // 1 1 0
                        // todo: like the section in Sub20679B0, but sets fewer values
                        byte v23 = byteBuf.Consume();
                        uint v24 = (uint)_dword206B2A0[v23];
                        ushort v25 = wordBuf.Consume();
                        int v26 = UInt32ToInt32(static_cast<uint>(v25 | ((static_cast<uint>(v25) << 16))));
                        uint prevOffset = v24 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v26;
                        outputSpan2Int[1] = outputSpan1Int[1] + (uint)v26;
                        outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2] + (uint)v26;
                        outputSpan2Int[256 / 2 + 1] = outputSpan1Int[256 / 2 + 1] + (uint)v26;
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        Sub206AE88(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 4;
                        Sub206AE88(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 4;
                    }
                    else
                    {
                        // 1 0 0
                        Sub206AC14(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 512;
                        Sub206AC14(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 512;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets even fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[1] = outputSpan1Int[1];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
                outputSpan2Int[256 / 2 + 1] = outputSpan1Int[256 / 2 + 1];
            }
        }

        // todo: same structure as Sub206935C and Sub206A5A4 etc. (same structure as Sub2068488 and Sub206A0B8, except 110 doesn't branch into 1101/1100)
    void TestMisc::Sub20697D0(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        assert(outputPos >= 512);
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                        // todo: similar to the inlines in Sub Sub206A0B8 and Sub2068488 and Sub206935C and Sub2068B24 and Sub20679B0 etc.
                        ushort v41 = wordBuf[0];
                        uint v42 = v41 | ((uint)(static_cast<uint>(v41) << 16));
                        uint v44 = v42 & 0x7BDF7BDF;
                        uint v45 = v42 & 0x4200420;
                        uint v48 = outputSpan2Prev[0];
                        uint v49 = v48 & 0x4200420;
                        v48 &= 0x7BDF7BDF;
                        uint v50 = v49 | ((uint)(v44 + v48) >> 1);
                        outputSpan2Slice[0] = (ushort)v50;
                        v50 &= 0x7BDF7BDF;
                        outputSpan2Prev[0] = (ushort)(v45 | ((v50 + v48) >> 1));
                        outputSpan2Slice[256] = (ushort)(v45 | ((v50 + v44) >> 1));
                        uint v51 = outputSpan2Prev[1];
                        v49 = (v49 & 0xFFFF0000) | (v51 & 0x420);
                        v51 &= 0x7BDF7BDF;
                        uint v52 = v45 | ((uint)(v44 + v51) >> 1);
                        outputSpan2Slice[1] = (ushort)v52;
                        v52 &= 0x7BDF7BDF;
                        outputSpan2Prev[1] = (ushort)(v49 | ((v52 + v51) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v49 | ((v52 + v44) >> 1));
                        uint v53 = outputSpan2Prev[256 - 1];
                        uint v54 = v53 & 0x4200420;
                        v53 &= 0x7BDF7BDF;
                        uint v55 = v54 | ((uint)(v44 + v53) >> 1);
                        outputSpan2Slice[0] = (ushort)v55;
                        v55 &= 0x7BDF7BDF;
                        outputSpan2Prev[256 - 1] = (ushort)(v45 | ((v55 + v53) >> 1));
                        outputSpan2Slice[1] = (ushort)(v45 | ((v55 + v44) >> 1));
                        uint v56 = outputSpan2Slice[256 - 1];
                        v54 = (v54 & 0xFFFF0000) | (v56 & 0x420);
                        v56 &= 0x7BDF7BDF;
                        uint v57 = v45 | ((uint)(v44 + v56) >> 1);
                        outputSpan2Slice[256] = (ushort)v57;
                        v57 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 - 1] = (ushort)(v54 | ((v57 + v56) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v54 | ((v57 + v44) >> 1));
                        uint v58 = outputSpan2Slice[256 * 2 - 1];
                        uint v59 = v58 & 0x4200420;
                        v58 &= 0x7BDF7BDF;
                        uint v60 = v59 | ((uint)(v44 + v58) >> 1);
                        outputSpan2Slice[256 * 2] = (ushort)v60;
                        v60 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 * 2 - 1] = (ushort)(v45 | ((v60 + v58) >> 1));
                        outputSpan2Slice[256 * 2 + 1] = (ushort)(v45 | ((v60 + v44) >> 1));
                        uint v61 = outputSpan2Slice[256 * 3 - 1];
                        v59 = (v59 & 0xFFFF0000) | (v61 & 0x420);
                        v61 &= 0x7BDF7BDF;
                        uint v62 = v45 | ((uint)(v44 + v61) >> 1);
                        outputSpan2Slice[256 * 3] = (ushort)v62;
                        v62 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 * 3 - 1] = (ushort)(v59 | ((v62 + v61) >> 1));
                        outputSpan2Slice[256 * 3 + 1] = (ushort)(v59 | ((v62 + v44) >> 1));
                        uint v63 = outputSpan2Slice[256 * 4 - 1];
                        uint v64 = v63 & 0x4200420;
                        v63 &= 0x7BDF7BDF;
                        uint v65 = v64 | ((uint)(v44 + v63) >> 1);
                        outputSpan2Slice[256 * 4] = (ushort)v65;
                        v65 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 * 4 - 1] = (ushort)(v45 | ((v65 + v63) >> 1));
                        outputSpan2Slice[256 * 4 + 1] = (ushort)(v45 | ((v65 + v44) >> 1));
                        uint v66 = outputSpan2Slice[256 * 5 - 1];
                        v64 = (v64 & 0xFFFF0000) | (v66 & 0x420);
                        v66 &= 0x7BDF7BDF;
                        uint v67 = v45 | ((uint)(v44 + v66) >> 1);
                        outputSpan2Slice[256 * 5] = (ushort)v67;
                        v67 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 * 5 - 1] = (ushort)(v64 | ((v67 + v66) >> 1));
                        outputSpan2Slice[256 * 5 + 1] = (ushort)(v64 | ((v67 + v44) >> 1));
                        uint v68 = outputSpan2Slice[256 * 6 - 1];
                        uint v69 = v68 & 0x4200420;
                        v68 &= 0x7BDF7BDF;
                        uint v70 = v69 | ((uint)(v44 + v68) >> 1);
                        outputSpan2Slice[256 * 6] = (ushort)v70;
                        v70 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 * 6 - 1] = (ushort)(v45 | ((v70 + v68) >> 1));
                        outputSpan2Slice[256 * 6 + 1] = (ushort)(v45 | ((v70 + v44) >> 1));
                        uint v71 = outputSpan2Slice[256 * 7 - 1];
                        v69 = (v69 & 0xFFFF0000) | (v71 & 0x420);
                        v71 &= 0x7BDF7BDF;
                        uint v72 = v45 | ((uint)(v44 + v71) >> 1);
                        outputSpan2Slice[256 * 7] = (ushort)v72;
                        v72 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 * 7 - 1] = (ushort)(v69 | ((v72 + v71) >> 1));
                        outputSpan2Slice[256 * 7 + 1] = (ushort)(v69 | ((v72 + v44) >> 1));
                        wordBuf = wordBuf.Slice(1);
                    }
                    else
                    {
                        // 1 1 0
                        // todo: like the section in Sub20679B0, but sets fewer values
                        byte v28 = byteBuf.Consume();
                        uint v29 = (uint)_dword206B2A0[v28];
                        ushort v30 = wordBuf.Consume();
                        int v31 = UInt32ToInt32(static_cast<uint>(v30 | ((static_cast<uint>(v30) << 16))));
                        uint prevOffset = v29 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v31;
                        outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2] + (uint)v31;
                        outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2] + (uint)v31;
                        outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2] + (uint)v31;
                        outputSpan2Int[256 * 4 / 2] = outputSpan1Int[256 * 4 / 2] + (uint)v31;
                        outputSpan2Int[256 * 5 / 2] = outputSpan1Int[256 * 5 / 2] + (uint)v31;
                        outputSpan2Int[256 * 6 / 2] = outputSpan1Int[256 * 6 / 2] + (uint)v31;
                        outputSpan2Int[256 * 7 / 2] = outputSpan1Int[256 * 7 / 2] + (uint)v31;
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        Sub2069EC0(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 2;
                        Sub2069EC0(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 2;
                    }
                    else
                    {
                        // 1 0 0
                        Sub206A8C0(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 2048;
                        Sub206A8C0(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 2048;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
                outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2];
                outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2];
                outputSpan2Int[256 * 4 / 2] = outputSpan1Int[256 * 4 / 2];
                outputSpan2Int[256 * 5 / 2] = outputSpan1Int[256 * 5 / 2];
                outputSpan2Int[256 * 6 / 2] = outputSpan1Int[256 * 6 / 2];
                outputSpan2Int[256 * 7 / 2] = outputSpan1Int[256 * 7 / 2];
            }
        }

        // todo: similar structure to Sub206935C and Sub206A5A4 etc., except 10 doesn't branch into 101/100
    void TestMisc::Sub2069EC0(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        ushort v36 = wordBuf[0];
                        outputSpan2Slice[0] = v36;
                        outputSpan2Slice[256] = v36;
                        outputSpan2Slice[256 * 2] = v36;
                        outputSpan2Slice[256 * 3] = v36;
                        outputSpan2Slice[256 * 4] = v36;
                        outputSpan2Slice[256 * 5] = v36;
                        outputSpan2Slice[256 * 6] = v36;
                        outputSpan2Slice[256 * 7] = v36;
                        wordBuf = wordBuf.Slice(1);
                    }
                    else
                    {
                        // 1 1 0
                        byte v25 = byteBuf.Consume();
                        uint v27 = (uint)_dword206B2A0[v25];
                        ushort v26 = wordBuf.Consume();
                        uint prevOffset = v27 + outputPos;
                        Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        outputSpan2Slice[0] = (ushort)(outputSpan1Slice[0] + v26);
                        outputSpan2Slice[256] = (ushort)(outputSpan1Slice[256] + v26);
                        outputSpan2Slice[256 * 2] = (ushort)(outputSpan1Slice[256 * 2] + v26);
                        outputSpan2Slice[256 * 3] = (ushort)(outputSpan1Slice[256 * 3] + v26);
                        outputSpan2Slice[256 * 4] = (ushort)(outputSpan1Slice[256 * 4] + v26);
                        outputSpan2Slice[256 * 5] = (ushort)(outputSpan1Slice[256 * 5] + v26);
                        outputSpan2Slice[256 * 6] = (ushort)(outputSpan1Slice[256 * 6] + v26);
                        outputSpan2Slice[256 * 7] = (ushort)(outputSpan1Slice[256 * 7] + v26);
                    }
                }
                else
                {
                    // 1 0
                    Sub206AD40(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                    outputPos += 2048;
                    Sub206AD40(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                    outputPos -= 2048;
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: similar to the section in Sub206935C and others, but all the code is shared except for the buf1 pointer
                Span<ushort> outputSpan1Slice;
                Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                }
                else
                {
                    // 0 0
                    outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Slice[0] = outputSpan1Slice[0];
                outputSpan2Slice[256] = outputSpan1Slice[256];
                outputSpan2Slice[256 * 2] = outputSpan1Slice[256 * 2];
                outputSpan2Slice[256 * 3] = outputSpan1Slice[256 * 3];
                outputSpan2Slice[256 * 4] = outputSpan1Slice[256 * 4];
                outputSpan2Slice[256 * 5] = outputSpan1Slice[256 * 5];
                outputSpan2Slice[256 * 6] = outputSpan1Slice[256 * 6];
                outputSpan2Slice[256 * 7] = outputSpan1Slice[256 * 7];
            }
        }

        // todo: similar structure to Sub206935C and Sub206A5A4 etc., except 10 doesn't branch into 101/100
    void TestMisc::Sub206AD40(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        ushort v28 = wordBuf[0];
                        outputSpan2Slice[0] = v28;
                        outputSpan2Slice[256] = v28;
                        outputSpan2Slice[256 * 2] = v28;
                        outputSpan2Slice[256 * 3] = v28;
                        wordBuf = wordBuf.Slice(1);
                    }
                    else
                    {
                        // 1 1 0
                        byte v25 = byteBuf.Consume();
                        uint v27 = (uint)_dword206B2A0[v25];
                        ushort v26 = wordBuf.Consume();
                        uint prevOffset = v27 + outputPos;
                        Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        outputSpan2Slice[0] = (ushort)(outputSpan1Slice[0] + v26);
                        outputSpan2Slice[256] = (ushort)(outputSpan1Slice[256] + v26);
                        outputSpan2Slice[256 * 2] = (ushort)(outputSpan1Slice[256 * 2] + v26);
                        outputSpan2Slice[256 * 3] = (ushort)(outputSpan1Slice[256 * 3] + v26);

                    }
                }
                else
                {
                    // 1 0
                    Sub206B1B8(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                    outputPos += 1024;
                    Sub206B1B8(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                    outputPos -= 1024;
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: similar to that other section, but sets fewer values
                Span<ushort> outputSpan1Slice;
                Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                }
                else
                {
                    // 0 0
                    outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Slice[0] = outputSpan1Slice[0];
                outputSpan2Slice[256] = outputSpan1Slice[256];
                outputSpan2Slice[256 * 2] = outputSpan1Slice[256 * 2];
                outputSpan2Slice[256 * 3] = outputSpan1Slice[256 * 3];
            }
        }

        // todo: similar structure to Sub206935C and Sub206A5A4 etc.
    void TestMisc::Sub206AE88(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        assert(outputPos >= 512);
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                        // todo: similar to the inlines in Sub Sub206A0B8 and Sub2068488
                        uint v27 = wordBuf[0];
                        uint v28 = v27 | ((static_cast<uint>(v27) << 16));
                        uint v31 = outputSpan2Prev[0];
                        uint v32 = v31 & 0x4200420;
                        v31 &= 0x7BDF7BDFu;
                        uint v33 = v32 | (((v28 & 0x7BDF7BDFu) + v31) >> 1);
                        outputSpan2Slice[0] = (ushort)v33;
                        v33 &= 0x7BDF7BDFu;
                        outputSpan2Prev[0] = (ushort)(v28 & 0x420 | ((v33 + v31) >> 1));
                        outputSpan2Slice[256] = (ushort)(v28 & 0x420 | ((v33 + (v28 & 0x7BDF7BDF)) >> 1));
                        uint v34 = outputSpan2Prev[1];
                        v32 = (v32 & 0xFFFF0000) | (v34 & 0x420);
                        v34 &= 0x7BDF7BDFu;
                        outputSpan2Slice[1] = (ushort)(v28 & 0x420 | (((v28 & 0x7BDF7BDFu) + v34) >> 1));
                        uint v35 = (((v28 & 0x7BDF7BDFu) + v34) >> 1) & 0x7BDF7BDF;
                        outputSpan2Prev[1] = (ushort)(v32 | ((v35 + v34) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v32 | ((v35 + (v28 & 0x7BDF7BDF)) >> 1));
                        uint v36 = outputSpan2Prev[256 - 1];
                        uint v37 = v36 & 0x4200420;
                        v36 &= 0x7BDF7BDFu;
                        uint v38 = v37 | (((v28 & 0x7BDF7BDFu) + v36) >> 1);
                        outputSpan2Slice[0] = (ushort)v38;
                        v38 &= 0x7BDF7BDFu;
                        outputSpan2Prev[256 - 1] = (ushort)(v28 & 0x420 | ((v38 + v36) >> 1));
                        outputSpan2Slice[1] = (ushort)(v28 & 0x420 | ((v38 + (v28 & 0x7BDF7BDF)) >> 1));
                        uint v39 = outputSpan2Slice[256 - 1];
                        v37 = (v37 & 0xFFFF0000) | (v39 & 0x420);
                        v39 &= 0x7BDF7BDFu;
                        outputSpan2Slice[256] = (ushort)(v28 & 0x420 | (((v28 & 0x7BDF7BDFu) + v39) >> 1));
                        uint v40 = (((v28 & 0x7BDF7BDFu) + v39) >> 1) & 0x7BDF7BDF;
                        outputSpan2Slice[256 - 1] = (ushort)(v37 | ((v40 + v39) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v37 | ((v40 + (v28 & 0x7BDF7BDF)) >> 1));
                        wordBuf = wordBuf.Slice(1);
                    }
                    else
                    {
                        // 1 1 0
                        // todo: like the section in Sub20679B0, but sets even fewer values
                        byte v21 = byteBuf.Consume();
                        uint v22 = (uint)_dword206B2A0[v21];
                        ushort v23 = wordBuf.Consume();
                        int v24 = UInt32ToInt32(static_cast<uint>(v23 | ((static_cast<uint>(v23) << 16))));
                        uint prevOffset = v22 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v24;
                        outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2] + (uint)v24;
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        Sub206B1B8(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 2;
                        Sub206B1B8(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 2;
                    }
                    else
                    {
                        // 1 0 0
                        Sub206B0C4(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 512;
                        Sub206B0C4(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 512;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets even fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
            }
        }

        // todo: similar structure to Sub206935C and Sub206A5A4 etc.
    void TestMisc::Sub206AC14(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        ushort v26 = wordBuf.Consume();
                        uint v27 = (uint)(v26 | ((static_cast<uint>(v26) << 16)));
                        outputSpan2Int[0] = v27;
                        outputSpan2Int[1] = v27;
                    }
                    else
                    {
                        // 1 1 0
                        // todo: like the section in Sub20679B0, but sets even fewer values
                        byte v19 = byteBuf.Consume();
                        uint v20 = (uint)_dword206B2A0[v19];
                        ushort v21 = wordBuf.Consume();
                        int v22 = UInt32ToInt32(static_cast<uint>(v21 | ((static_cast<uint>(v21) << 16))));
                        uint prevOffset = v20 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v22;
                        outputSpan2Int[1] = outputSpan1Int[1] + (uint)v22;
                    }
                }
                else
                {
                    // 1 0
                    Sub206B0C4(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                    outputPos += 4;
                    Sub206B0C4(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                    outputPos -= 4;
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets even fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[1] = outputSpan1Int[1];
            }
        }

        // todo: same structure as others (10 doesn't branch into 101/100)
    void TestMisc::Sub206B0C4(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        outputSpan2Slice[0] = wordBuf.Consume();
                        outputSpan2Slice[1] = wordBuf.Consume();
                    }
                    else
                    {
                        // 1 1 0
                        ushort v23 = wordBuf.Consume();
                        int v24 = UInt32ToInt32(static_cast<uint>(v23 | ((static_cast<uint>(v23) << 16))));
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        outputSpan2Int[0] = (uint)v24;
                    }
                }
                else
                {
                    // 1 0
                    // todo: like the section in Sub20679B0, but sets yet fewer values
                    byte v16 = byteBuf.Consume();
                    uint v17 = (uint)_dword206B2A0[v16];
                    ushort v18 = wordBuf.Consume();
                    int v19 = UInt32ToInt32(static_cast<uint>(v18 | ((static_cast<uint>(v18) << 16))));
                    uint prevOffset = v17 + outputPos;
                    Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                    Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                    outputSpan2Int[0] = outputSpan1Int[0] + (uint)v19;
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets yet fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    outputSpan1Int = MemoryCast<ushort, uint>(outputSpan1Slice);
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
            }
        }

        // todo: same structure as others (10 doesn't branch into 101/100)
    void TestMisc::Sub206B1B8(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        outputSpan2Slice[0] = wordBuf.Consume();
                        outputSpan2Slice[256] = wordBuf.Consume();
                    }
                    else
                    {
                        // 1 1 0
                        ushort v22 = wordBuf.Consume();
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        outputSpan2Slice[0] = v22;
                        outputSpan2Slice[256] = v22;
                    }
                }
                else
                {
                    // 1 0
                    byte v25 = byteBuf.Consume();
                    uint v27 = (uint)_dword206B2A0[v25];
                    ushort v26 = wordBuf.Consume();
                    uint prevOffset = v27 + outputPos;
                    Span<ushort> outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                    Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                    outputSpan2Slice[0] = (ushort)(outputSpan1Slice[0] + v26);
                    outputSpan2Slice[256] = (ushort)(outputSpan1Slice[256] + v26);
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: similar to that other section, but sets even fewer values
                Span<ushort> outputSpan1Slice;
                Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(prevOffset));
                }
                else
                {
                    // 0 0
                    outputSpan1Slice = MemoryCast<byte, ushort>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Slice[0] = outputSpan1Slice[0];
                outputSpan2Slice[256] = outputSpan1Slice[256];
            }
        }

    void TestMisc::Sub2067388(Span<ushort>& wordBuf, Span<ushort> outputSpan2Slice)
        {
            ushort word0 = wordBuf[0];
            ushort word1 = wordBuf[1];
            int prevInt23 = UInt32ToInt32(static_cast<uint>(wordBuf[2])
                | (static_cast<uint>(wordBuf[3]) << 16));
            int bits0A = (word0 >> 10) & 0x1F; // bits 10-14
            int bits1A = (word1 >> 10) & 0x1F;
            int bits0B = (word0 >> 5) & 0x1F; // bits 5-9
            int bits1B = (word1 >> 5) & 0x1F;
            int bits0C = word0 & 0x1F; // bits 0-4
            int bits1C = word1 & 0x1F;
            wordBuf[2] = (ushort)(_byte2067320[2 * bits0C + bits1C] + 32 * (_byte2067320[2 * bits0B + bits1B] + 32 * _byte2067320[bits1A + 2 * bits0A]));
            wordBuf[3] = (ushort)(_byte2067320[2 * bits1C + bits0C] + 32 * (_byte2067320[2 * bits1B + bits0B] + 32 * _byte2067320[2 * bits1A + bits0A]));
            // todo: clean this up once it's confirmed the indices work this way and we don't need to un-align
            int wordIdx0 = UInt32ToInt32(Int32ToUInt32(prevInt23) << 1) & 6;
            int wordIdx1 = (prevInt23 >> 1) & 6;
            int wordIdx2 = (prevInt23 >> 3) & 6;
            int wordIdx3 = (prevInt23 >> 5) & 6;
            int wordIdx4 = (prevInt23 >> 7) & 6;
            int wordIdx5 = (prevInt23 >> 9) & 6;
            int wordIdx6 = (prevInt23 >> 11) & 6;
            int wordIdx7 = (prevInt23 >> 13) & 6;
            int wordIdx8 = (prevInt23 >> 15) & 6;
            int wordIdx9 = (prevInt23 >> 17) & 6;
            int wordIdxA = (prevInt23 >> 19) & 6;
            int wordIdxB = (prevInt23 >> 21) & 6;
            int wordIdxC = (prevInt23 >> 23) & 6;
            int wordIdxD = (prevInt23 >> 25) & 6;
            int wordIdxE = (prevInt23 >> 27) & 6;
            int wordIdxF = (prevInt23 >> 29) & 6;
            assert(wordIdx0 % 2 == 0);
            assert(wordIdx1 % 2 == 0);
            assert(wordIdx2 % 2 == 0);
            assert(wordIdx3 % 2 == 0);
            assert(wordIdx4 % 2 == 0);
            assert(wordIdx5 % 2 == 0);
            assert(wordIdx6 % 2 == 0);
            assert(wordIdx7 % 2 == 0);
            assert(wordIdx8 % 2 == 0);
            assert(wordIdx9 % 2 == 0);
            assert(wordIdxA % 2 == 0);
            assert(wordIdxB % 2 == 0);
            assert(wordIdxC % 2 == 0);
            assert(wordIdxD % 2 == 0);
            assert(wordIdxE % 2 == 0);
            assert(wordIdxF % 2 == 0);
            wordIdx0 /= 2;
            wordIdx1 /= 2;
            wordIdx2 /= 2;
            wordIdx3 /= 2;
            wordIdx4 /= 2;
            wordIdx5 /= 2;
            wordIdx6 /= 2;
            wordIdx7 /= 2;
            wordIdx8 /= 2;
            wordIdx9 /= 2;
            wordIdxA /= 2;
            wordIdxB /= 2;
            wordIdxC /= 2;
            wordIdxD /= 2;
            wordIdxE /= 2;
            wordIdxF /= 2;
            outputSpan2Slice[0] = wordBuf[wordIdx0];
            outputSpan2Slice[1] = wordBuf[wordIdx1];
            outputSpan2Slice[2] = wordBuf[wordIdx2];
            outputSpan2Slice[3] = wordBuf[wordIdx3];
            outputSpan2Slice[256] = wordBuf[wordIdx4];
            outputSpan2Slice[256 + 1] = wordBuf[wordIdx5];
            outputSpan2Slice[256 + 2] = wordBuf[wordIdx6];
            outputSpan2Slice[256 + 3] = wordBuf[wordIdx7];
            outputSpan2Slice[256 * 2] = wordBuf[wordIdx8];
            outputSpan2Slice[256 * 2 + 1] = wordBuf[wordIdx9];
            outputSpan2Slice[256 * 2 + 2] = wordBuf[wordIdxA];
            outputSpan2Slice[256 * 2 + 3] = wordBuf[wordIdxB];
            outputSpan2Slice[256 * 3] = wordBuf[wordIdxC];
            outputSpan2Slice[256 * 3 + 1] = wordBuf[wordIdxD];
            outputSpan2Slice[256 * 3 + 2] = wordBuf[wordIdxE];
            outputSpan2Slice[256 * 3 + 3] = wordBuf[wordIdxF];
            wordBuf = wordBuf.Slice(4);
        }

    void TestMisc::Sub20674E4(Span<ushort>& wordBuf, Span<byte> outputSpan2, uint outputPos)
        {
            // todo: this section is exactly the same as in Sub2067388
            ushort word0 = wordBuf[0];
            ushort word1 = wordBuf[1];
            int prevInt23 = UInt32ToInt32(static_cast<uint>(wordBuf[2])
                | (static_cast<uint>(wordBuf[3]) << 16));
            int bits0A = (word0 >> 10) & 0x1F; // bits 10-14
            int bits1A = (word1 >> 10) & 0x1F;
            int bits0B = (word0 >> 5) & 0x1F; // bits 5-9
            int bits1B = (word1 >> 5) & 0x1F;
            int bits0C = word0 & 0x1F; // bits 0-4
            int bits1C = word1 & 0x1F;
            wordBuf[2] = (ushort)(_byte2067320[2 * bits0C + bits1C] + 32 * (_byte2067320[2 * bits0B + bits1B] + 32 * _byte2067320[bits1A + 2 * bits0A]));
            wordBuf[3] = (ushort)(_byte2067320[2 * bits1C + bits0C] + 32 * (_byte2067320[2 * bits1B + bits0B] + 32 * _byte2067320[2 * bits1A + bits0A]));
            // todo: ^
            Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
            // todo: clean this up once it's confirmed the indices work this way and we don't need to un-align
            int wordIdx0 = UInt32ToInt32(Int32ToUInt32(prevInt23) << 1) & 6;
            int wordIdx1 = (prevInt23 >> 1) & 6;
            int wordIdx2 = (prevInt23 >> 3) & 6;
            int wordIdx3 = (prevInt23 >> 5) & 6;
            int wordIdx4 = (prevInt23 >> 7) & 6;
            int wordIdx5 = (prevInt23 >> 9) & 6;
            int wordIdx6 = (prevInt23 >> 11) & 6;
            int wordIdx7 = (prevInt23 >> 13) & 6;
            int wordIdx8 = (prevInt23 >> 15) & 6;
            int wordIdx9 = (prevInt23 >> 17) & 6;
            int wordIdxA = (prevInt23 >> 19) & 6;
            int wordIdxB = (prevInt23 >> 21) & 6;
            int wordIdxC = (prevInt23 >> 23) & 6;
            int wordIdxD = (prevInt23 >> 25) & 6;
            int wordIdxE = (prevInt23 >> 27) & 6;
            int wordIdxF = (prevInt23 >> 29) & 6;
            assert(wordIdx0 % 2 == 0);
            assert(wordIdx1 % 2 == 0);
            assert(wordIdx2 % 2 == 0);
            assert(wordIdx3 % 2 == 0);
            assert(wordIdx4 % 2 == 0);
            assert(wordIdx5 % 2 == 0);
            assert(wordIdx6 % 2 == 0);
            assert(wordIdx7 % 2 == 0);
            assert(wordIdx8 % 2 == 0);
            assert(wordIdx9 % 2 == 0);
            assert(wordIdxA % 2 == 0);
            assert(wordIdxB % 2 == 0);
            assert(wordIdxC % 2 == 0);
            assert(wordIdxD % 2 == 0);
            assert(wordIdxE % 2 == 0);
            assert(wordIdxF % 2 == 0);
            wordIdx0 /= 2;
            wordIdx1 /= 2;
            wordIdx2 /= 2;
            wordIdx3 /= 2;
            wordIdx4 /= 2;
            wordIdx5 /= 2;
            wordIdx6 /= 2;
            wordIdx7 /= 2;
            wordIdx8 /= 2;
            wordIdx9 /= 2;
            wordIdxA /= 2;
            wordIdxB /= 2;
            wordIdxC /= 2;
            wordIdxD /= 2;
            wordIdxE /= 2;
            wordIdxF /= 2;
            //buf_2_ptr_add_512 = (unsigned __int16 *)(buf_2_ptr + 512);
            uint v13 = (uint)outputSpan2Slice[256 - 1] & 0x7BDF;
            uint v14 = wordBuf[wordIdx0];
            uint v15 = v14 & 0x420;
            outputSpan2Slice[256 + 1] = (ushort)v14;
            v14 &= 0x7BDF7BDF;
            outputSpan2Slice[256] = (ushort)(v15 | ((uint)(v13 + v14) >> 1));
            uint v16 = wordBuf[wordIdx1];
            uint v17 = v16 & 0x420;
            outputSpan2Slice[256 + 3] = (ushort)v16;
            v16 &= 0x7BDF7BDF;
            outputSpan2Slice[256 + 2] = (ushort)(v17 | ((uint)(v14 + v16) >> 1));
            uint v18 = wordBuf[wordIdx2];
            uint v19 = v18 & 0x420;
            outputSpan2Slice[256 + 5] = (ushort)v18;
            v18 &= 0x7BDF7BDF;
            outputSpan2Slice[256 + 4] = (ushort)(v19 | ((uint)(v16 + v18) >> 1));
            uint v20 = wordBuf[wordIdx3];
            outputSpan2Slice[256 + 7] = (ushort)v20;
            outputSpan2Slice[256 + 6] = (ushort)(v20 & 0x420 | ((v18 + (v20 & 0x7BDF7BDF)) >> 1));
            uint v21 = (uint)outputSpan2Slice[256 * 3 - 1] & 0x7BDF;
            uint v22 = wordBuf[wordIdx4];
            uint v23 = v22 & 0x420;
            outputSpan2Slice[256 * 3 + 1] = (ushort)v22;
            v22 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 3] = (ushort)(v23 | ((uint)(v21 + v22) >> 1));
            uint v24 = wordBuf[wordIdx5];
            uint v25 = v24 & 0x420;
            outputSpan2Slice[256 * 3 + 3] = (ushort)v24;
            v24 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 3 + 2] = (ushort)(v25 | ((uint)(v22 + v24) >> 1));
            uint v26 = wordBuf[wordIdx6];
            uint v27 = v26 & 0x420;
            outputSpan2Slice[256 * 3 + 5] = (ushort)v26;
            v26 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 3 + 4] = (ushort)(v27 | ((uint)(v24 + v26) >> 1));
            uint v28 = wordBuf[wordIdx7];
            outputSpan2Slice[256 * 3 + 7] = (ushort)v28;
            outputSpan2Slice[256 * 3 + 6] = (ushort)(v28 & 0x420 | ((v26 + (v28 & 0x7BDF7BDF)) >> 1));
            uint v29 = (uint)outputSpan2Slice[256 * 5 - 1] & 0x7BDF;
            uint v30 = wordBuf[wordIdx8];
            uint v31 = v30 & 0x420;
            outputSpan2Slice[256 * 5 + 1] = (ushort)v30;
            v30 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 5] = (ushort)(v31 | ((uint)(v29 + v30) >> 1));
            uint v32 = wordBuf[wordIdx9];
            uint v33 = v32 & 0x420;
            outputSpan2Slice[256 * 5 + 3] = (ushort)v32;
            v32 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 5 + 2] = (ushort)(v33 | ((uint)(v30 + v32) >> 1));
            uint v34 = wordBuf[wordIdxA];
            uint v35 = v34 & 0x420;
            outputSpan2Slice[256 * 5 + 5] = (ushort)v34;
            v34 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 5 + 4] = (ushort)(v35 | ((uint)(v32 + v34) >> 1));
            uint v36 = wordBuf[wordIdxB];
            outputSpan2Slice[256 * 5 + 7] = (ushort)v36;
            outputSpan2Slice[256 * 5 + 6] = (ushort)(v36 & 0x420 | ((v34 + (v36 & 0x7BDF7BDF)) >> 1));
            uint v37 = (uint)outputSpan2Slice[256 * 7 - 1] & 0x7BDF;
            uint v38 = wordBuf[wordIdxC];
            uint v39 = v38 & 0x420;
            outputSpan2Slice[256 * 7 + 1] = (ushort)v38;
            v38 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 7] = (ushort)(v39 | ((uint)(v37 + v38) >> 1));
            uint v40 = wordBuf[wordIdxD];
            uint v41 = v40 & 0x420;
            outputSpan2Slice[256 * 7 + 3] = (ushort)v40;
            v40 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 7 + 2] = (ushort)(v41 | ((uint)(v38 + v40) >> 1));
            uint v42 = wordBuf[wordIdxE];
            uint v43 = v42 & 0x420;
            outputSpan2Slice[256 * 7 + 5] = (ushort)v42;
            v42 &= 0x7BDF7BDF;
            outputSpan2Slice[256 * 7 + 4] = (ushort)(v43 | ((uint)(v40 + v42) >> 1));
            uint v44 = wordBuf[wordIdxF];
            outputSpan2Slice[256 * 7 + 7] = (ushort)v44;
            outputSpan2Slice[256 * 7 + 6] = (ushort)(v44 & 0x420 | ((v42 + (v44 & 0x7BDF7BDF)) >> 1));
            assert(outputPos >= 512);
            Span<uint> outputSpan2Int = MemoryCast<ushort, uint>(outputSpan2Slice);
            Span<uint> outputSpan2IntPrev = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos - 512));
            uint v01 = outputSpan2Int[256 * 7 / 2];
            uint v46 = v01 & 0x4200420;
            uint v47 = v01 & 0x7BDF7BDF;
            uint v02 = outputSpan2Int[256 * 5 / 2];
            uint v48 = v02 & 0x4200420;
            uint v49 = v02 & 0x7BDF7BDF;
            outputSpan2Int[256 * 6 / 2] = v46 | ((uint)(v47 + v49) >> 1);
            uint v03 = outputSpan2Int[256 * 3 / 2];
            uint v50 = v03 & 0x4200420;
            uint v51 = v03 & 0x7BDF7BDF;
            outputSpan2Int[256 * 4 / 2] = v48 | ((uint)(v49 + v51) >> 1);
            uint v04 = outputSpan2Int[256 / 2];
            uint v52 = v04 & 0x4200420;
            uint v53 = v04 & 0x7BDF7BDF;
            outputSpan2Int[256 * 2 / 2] = v50 | ((uint)(v51 + v53) >> 1);
            outputSpan2Int[0] = v52 | ((v53 + (outputSpan2IntPrev[0] & 0x7BDF7BDF)) >> 1);
            uint v54 = outputSpan2Int[256 * 7 / 2 + 1];
            uint v55 = outputSpan2Int[256 * 5 / 2 + 1];
            uint v56 = v55 & 0x4200420;
            v55 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 6 / 2 + 1] = v54 & 0x4200420 | (((v54 & 0x7BDF7BDF) + v55) >> 1);
            uint v57 = outputSpan2Int[256 * 3 / 2 + 1];
            uint v58 = v57 & 0x4200420;
            v57 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 4 / 2 + 1] = v56 | ((uint)(v55 + v57) >> 1);
            uint v59 = outputSpan2Int[256 / 2 + 1];
            uint v60 = v59 & 0x4200420;
            v59 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 2 / 2 + 1] = v58 | ((uint)(v57 + v59) >> 1);
            outputSpan2Int[1] = v60 | ((v59 + (outputSpan2IntPrev[1] & 0x7BDF7BDF)) >> 1);
            uint v61 = outputSpan2Int[256 * 7 / 2 + 2];
            uint v62 = outputSpan2Int[256 * 5 / 2 + 2];
            uint v63 = v62 & 0x4200420;
            v62 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 6 / 2 + 2] = v61 & 0x4200420 | (((v61 & 0x7BDF7BDF) + v62) >> 1);
            uint v64 = outputSpan2Int[256 * 3 / 2 + 2];
            uint v65 = v64 & 0x4200420;
            v64 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 4 / 2 + 2] = v63 | ((uint)(v62 + v64) >> 1);
            uint v66 = outputSpan2Int[256 / 2 + 2];
            uint v67 = v66 & 0x4200420;
            v66 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 2 / 2 + 2] = v65 | ((uint)(v64 + v66) >> 1);
            outputSpan2Int[2] = v67 | ((v66 + (outputSpan2IntPrev[2] & 0x7BDF7BDF)) >> 1);
            uint v68 = outputSpan2Int[256 * 7 / 2 + 3];
            uint v69 = outputSpan2Int[256 * 5 / 2 + 3];
            uint v70 = v69 & 0x4200420;
            v69 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 6 / 2 + 3] = v68 & 0x4200420 | (((v68 & 0x7BDF7BDF) + v69) >> 1);
            uint v71 = outputSpan2Int[256 * 3 / 2 + 3];
            uint v72 = v71 & 0x4200420;
            v71 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 4 / 2 + 3] = v70 | ((uint)(v69 + v71) >> 1);
            uint v73 = outputSpan2Int[256 / 2 + 3];
            uint v74 = v73 & 0x4200420;
            v73 &= 0x7BDF7BDF;
            outputSpan2Int[256 * 2 / 2 + 3] = v72 | ((uint)(v71 + v73) >> 1);
            outputSpan2Int[3] = v74 | ((v73 + (outputSpan2IntPrev[3] & 0x7BDF7BDF)) >> 1);
            wordBuf = wordBuf.Slice(4);
        }

        // todo: same structure as Sub206935C (same structure as Sub2068488 and Sub206A0B8, except 110 doesn't branch into 1101/1100)
    void TestMisc::Sub206A8C0(uint& outputPos, uint& value, Span<byte>& outputSpan1, Span<byte>& outputSpan2,
            Span<uint>& intBuf, Span<ushort>& wordBuf, Span<byte>& byteBuf)
        {
            ReadNextBit(value, intBuf);
            if (_readBit != 0)
            {
                // 1
                ReadNextBit(value, intBuf);
                if (_readBit != 0)
                {
                    // 1 1
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 1 1
                        assert(outputPos >= 512);
                        Span<ushort> outputSpan2Slice = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos));
                        Span<ushort> outputSpan2Prev = MemoryCast<byte, ushort>(outputSpan2.Slice(outputPos - 512));
                        // todo: similar to the inlines in Sub Sub206A0B8 and Sub2068488
                        uint v37 = wordBuf[0];
                        uint v38 = v37 | ((static_cast<uint>(v37) << 16));
                        uint v40 = v38 & 0x7BDF7BDF;
                        uint v41 = v38 & 0x4200420;
                        uint v44 = outputSpan2Prev[0];
                        uint v45 = v44 & 0x4200420;
                        v44 &= 0x7BDF7BDF;
                        uint v46 = v45 | ((uint)(v40 + v44) >> 1);
                        outputSpan2Slice[0] = (ushort)v46;
                        v46 &= 0x7BDF7BDF;
                        outputSpan2Prev[0] = (ushort)(v41 | ((v46 + v44) >> 1));
                        outputSpan2Slice[256] = (ushort)(v41 | ((v46 + v40) >> 1));
                        uint v47 = outputSpan2Prev[1];
                        v45 = (v45 & 0xFFFF0000) | (v47 & 0x420);
                        v47 &= 0x7BDF7BDF;
                        uint v48 = v41 | ((uint)(v40 + v47) >> 1);
                        outputSpan2Slice[1] = (ushort)v48;
                        v48 &= 0x7BDF7BDF;
                        outputSpan2Prev[1] = (ushort)(v45 | ((v48 + v47) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v45 | ((v48 + v40) >> 1));
                        uint v49 = outputSpan2Prev[256 - 1];
                        uint v50 = v49 & 0x4200420;
                        v49 &= 0x7BDF7BDF;
                        uint v51 = v50 | ((uint)(v40 + v49) >> 1);
                        outputSpan2Slice[0] = (ushort)v51;
                        v51 &= 0x7BDF7BDF;
                        outputSpan2Prev[256 - 1] = (ushort)(v41 | ((v51 + v49) >> 1));
                        outputSpan2Slice[1] = (ushort)(v41 | ((v51 + v40) >> 1));
                        uint v52 = outputSpan2Slice[256 - 1];
                        v50 = (v50 & 0xFFFF0000) | (v52 & 0x420);
                        v52 &= 0x7BDF7BDF;
                        uint v53 = v41 | ((uint)(v40 + v52) >> 1);
                        outputSpan2Slice[256] = (ushort)v53;
                        v53 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 - 1] = (ushort)(v50 | ((v53 + v52) >> 1));
                        outputSpan2Slice[256 + 1] = (ushort)(v50 | ((v53 + v40) >> 1));
                        uint v54 = outputSpan2Slice[256 * 2 - 1];
                        uint v55 = v54 & 0x4200420;
                        v54 &= 0x7BDF7BDF;
                        uint v56 = v55 | ((uint)(v40 + v54) >> 1);
                        outputSpan2Slice[256 * 2] = (ushort)v56;
                        v56 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 * 2 - 1] = (ushort)(v41 | ((v56 + v54) >> 1));
                        outputSpan2Slice[256 * 2 + 1] = (ushort)(v41 | ((v56 + v40) >> 1));
                        uint v57 = outputSpan2Slice[256 * 3 - 1];
                        v55 = (v55 & 0xFFFF0000) | (v57 & 0x420);
                        v57 &= 0x7BDF7BDF;
                        uint v58 = v41 | ((uint)(v40 + v57) >> 1);
                        outputSpan2Slice[256 * 3] = (ushort)v58;
                        v58 &= 0x7BDF7BDF;
                        outputSpan2Slice[256 * 3 - 1] = (ushort)(v55 | ((v58 + v57) >> 1));
                        outputSpan2Slice[256 * 3 + 1] = (ushort)(v55 | ((v58 + v40) >> 1));
                        wordBuf = wordBuf.Slice(1);
                    }
                    else
                    {
                        // 1 1 0
                        // todo: like the section in Sub20679B0, but sets fewer values
                        byte v26 = byteBuf.Consume();
                        uint v27 = (uint)_dword206B2A0[v26];
                        ushort v28 = wordBuf.Consume();
                        int v29 = UInt32ToInt32(static_cast<uint>(v28 | ((static_cast<uint>(v28) << 16))));
                        uint prevOffset = v27 + outputPos;
                        Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                        Span<uint> outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                        outputSpan2Int[0] = outputSpan1Int[0] + (uint)v29;
                        outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2] + (uint)v29;
                        outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2] + (uint)v29;
                        outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2] + (uint)v29;
                    }
                }
                else
                {
                    // 1 0
                    ReadNextBit(value, intBuf);
                    if (_readBit != 0)
                    {
                        // 1 0 1
                        Sub206AD40(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 2;
                        Sub206AD40(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 2;
                    }
                    else
                    {
                        // 1 0 0
                        Sub206AE88(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos += 1024;
                        Sub206AE88(outputPos, value, outputSpan1, outputSpan2, intBuf, wordBuf, byteBuf);
                        outputPos -= 1024;
                    }
                }
            }
            else
            {
                // 0
                ReadNextBit(value, intBuf);
                // todo: same as the section in Sub206935C, but sets even fewer values
                // most of the code is shared between the 01+aligned and 00 code paths
                // (presumably because 00 is always aligned since it doesn't use an offset from the table)
                Span<uint> outputSpan1Int;
                Span<uint> outputSpan2Int = MemoryCast<byte, uint>(outputSpan2.Slice(outputPos));
                if (_readBit != 0)
                {
                    // 0 1
                    byte v14 = byteBuf.Consume();
                    uint v11 = (uint)_dword206B2A0[v14];
                    uint prevOffset = v11 + outputPos;
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(prevOffset));
                }
                else
                {
                    // 0 0
                    outputSpan1Int = MemoryCast<byte, uint>(outputSpan1.Slice(outputPos));
                }
                outputSpan2Int[0] = outputSpan1Int[0];
                outputSpan2Int[256 / 2] = outputSpan1Int[256 / 2];
                outputSpan2Int[256 * 2 / 2] = outputSpan1Int[256 * 2 / 2];
                outputSpan2Int[256 * 3 / 2] = outputSpan1Int[256 * 3 / 2];
            }
        }

    void TestMisc::ReadNextBit(uint& value, Span<uint>& span)
        {
            value = NextBit(value);
            if (value == 0)
            {
                value = NextValueCarry(span);
            }
        }

        // updates the carry flag, but does not include it in this calculation
    uint TestMisc::NextBit(uint value)
        {
            ulong ulongValue = (ulong)value;
            ulongValue += ulongValue;
            _readBit = ulongValue > std::numeric_limits<uint>::max() ? 1 : 0;
            return (uint)ulongValue;
        }

    uint TestMisc::NextValueCarry(Span<uint>& span)
        {
            ulong ulongValue = (ulong)span[0];
            ulongValue += ulongValue + (ulong)_readBit;
            _readBit = ulongValue > std::numeric_limits<uint>::max() ? 1 : 0;
            span = span.Slice(1);
            return (uint)ulongValue;
        }
    void TestMisc::TestCameraSequences()
    {
        std::unordered_set<int> ids;
        for (const auto& [name, meta] : Metadata::RoomMetadata)
        {
            (void)name;
            if (meta->EntityPath)
            {
                const auto entities = Read::GetEntities(
                    *meta->EntityPath, -1, meta->FirstHunt);
                for (const std::shared_ptr<Entity>& entity : *entities)
                {
                    if (entity->Type == EntityType::CameraSequence)
                    {
                        const auto typed =
                            std::dynamic_pointer_cast<EntityOf<CameraSequenceEntityData>>(entity);
                        if (!typed)
                        {
                            throw System::InvalidCastException();
                        }
                        const CameraSequenceEntityData data = typed->Data;
                        Entities::CamSeqEntity entityClass(data, nullptr);
                        if (ids.contains(data.SequenceId))
                        {
                            continue;
                        }
                        ids.insert(data.SequenceId);
                        const auto sequence = entityClass.Sequence();
                        if (!sequence)
                        {
                            throw System::NullReferenceException();
                        }
                        for (const auto& frame : sequence->Keyframes())
                        {
                            (void)frame;
                        }
                        Nop();
                    }
                }
            }
        }
        Nop();
    }

    void TestMisc::TestCameraSequenceFiles()
    {
        for (const std::string& filePath
            : EnumerateFiles(Paths::Combine(Paths::FileSystem(), "cameraEditor")))
        {
            const std::string name = PathGetFileName(filePath);
            if (name != "cameraEditBG.bin")
            {
                [[maybe_unused]] const auto seq = Formats::CameraSequence::Load(name, nullptr);
                Nop();
            }
        }
        Nop();
    }

    void TestMisc::TestAllCollision()
    {
        using namespace Formats::Collision;
        std::vector<std::pair<bool, std::shared_ptr<CollisionInstance>>> allCollision;
        for (const auto& [name, meta] : Metadata::RoomMetadata)
        {
            (void)name;
            if (!meta->Hybrid)
            {
                allCollision.emplace_back(true, Collision::GetCollision(meta.get()));
            }
        }
        for (const auto& [name, meta] : Metadata::ModelMetadata)
        {
            (void)name;
            if (meta.CollisionPath)
            {
                allCollision.emplace_back(false, Collision::GetCollision(&meta));
                if (meta.ExtraCollisionPath)
                {
                    allCollision.emplace_back(false, Collision::GetCollision(&meta, true));
                }
            }
        }
        for (const auto& [room, instance] : allCollision)
        {
            (void)room;
            if (const auto collision = std::dynamic_pointer_cast<MphCollisionInfo>(instance->Info))
            {
                for (const CollisionData& data : *collision->Data)
                {
                    (void)data;
                }
            }
            else if (const auto fhCollision =
                std::dynamic_pointer_cast<FhCollisionInfo>(instance->Info))
            {
                (void)fhCollision;
            }
        }
        Nop();
    }

    void TestMisc::TestAllFhCollision()
    {
        using namespace Formats::Collision;
        std::vector<std::pair<bool, std::shared_ptr<CollisionInstance>>> allCollision;
        for (const auto& [name, meta] : Metadata::RoomMetadata)
        {
            (void)name;
            if (meta->FirstHunt || meta->Hybrid)
            {
                allCollision.emplace_back(true, Collision::GetCollision(meta.get()));
            }
        }
        for (const auto& [name, meta] : Metadata::FirstHuntModels)
        {
            (void)name;
            if (meta.CollisionPath)
            {
                allCollision.emplace_back(false, Collision::GetCollision(&meta));
            }
        }
        for (const auto& [room, instance] : allCollision)
        {
            (void)room;
            const auto collision = std::dynamic_pointer_cast<FhCollisionInfo>(instance->Info);
            if (!collision)
            {
                throw System::InvalidCastException();
            }
        }
        Nop();
    }

    void TestMisc::ConvertRoomToMph(
        const std::string& room, std::optional<std::string> over)
    {
        const std::shared_ptr<RoomMetadata> meta = DictionaryAt(Metadata::RoomMetadata, room);
        std::shared_ptr<RoomMetadata> overMeta{};
        if (over)
        {
            overMeta = DictionaryAt(Metadata::RoomMetadata, *over);
        }
        assert(meta->EntityPath && meta->NodePath);

        const std::string folder = Paths::Combine(Paths::Export(), "_pack");
        const std::string fileSystem = meta->FirstHunt ? Paths::FhFileSystem() : Paths::FileSystem();

        std::cout << "Converting model...\n";
        auto [model, texture] = Utility::Repack::RepackRoomModel(room, true);
        const std::string modelPath = PathGetFileName(overMeta ? overMeta->ModelPath : meta->ModelPath);
        const std::string modelDest = Paths::Combine(folder, modelPath);
        const std::string texDest = Paths::Combine(
            folder,
            StringReplace(StringReplace(modelPath, "_Model.bin", "_Tex.bin"),
                "_model.bin", "_tex.bin"));
        FileWriteAllBytes(modelDest, model);
        FileWriteAllBytes(texDest, texture);

        std::cout << "Converting collision...\n";
        const std::vector<byte> collision = Utility::RepackCollision::RepackMphRoom(room);
        const std::string colDest = Paths::Combine(
            folder, PathGetFileName(overMeta ? overMeta->CollisionPath : meta->CollisionPath));
        FileWriteAllBytes(colDest, collision);

        std::cout << "Converting animation...\n";
        const std::string animSrc = Paths::Combine(fileSystem, meta->AnimationPath);
        const std::string animDest = Paths::Combine(
            folder, PathGetFileName(overMeta ? overMeta->AnimationPath : meta->AnimationPath));
        FileDelete(animDest);
        CopyFile(animSrc, animDest);

        if (meta->Hybrid)
        {
            std::cout << "Copying entities...\n";
            std::cout << "Copying nodedata...\n";
            const std::string entSrc = Paths::Combine(
                fileSystem, RequireString(meta->EntityPath, "path2"));
            const std::string nodeSrc = Paths::Combine(
                fileSystem, RequireString(meta->NodePath, "path2"));
            std::string entDest = Paths::Combine(
                folder, RequireString(meta->EntityPath, "path2"));
            std::string nodeDest = Paths::Combine(
                folder, RequireString(meta->NodePath, "path2"));
            if (overMeta)
            {
                assert(overMeta->EntityPath && overMeta->NodePath);
                entDest = Paths::Combine(
                    folder, RequireString(overMeta->EntityPath, "path2"));
                nodeDest = Paths::Combine(
                    folder, RequireString(overMeta->NodePath, "path2"));
            }
            FileDelete(entDest);
            FileDelete(nodeDest);
            CopyFile(entSrc, entDest);
            CopyFile(nodeSrc, nodeDest);
        }
        else
        {
            std::cout << "Converting entities...\n";
            const std::vector<byte> entity = Utility::Repack::RepackMphEntities(room);
            const std::string entDest = Paths::Combine(
                folder,
                PathGetFileName(overMeta
                    ? RequireString(overMeta->EntityPath, "path")
                    : RequireString(meta->EntityPath, "path")));
            FileWriteAllBytes(entDest, entity);
        }

        std::cout << "Creating archive...";
        const auto files = std::make_shared<const std::vector<std::string>>(
            std::vector<std::string>{animDest, colDest, modelDest});
        const std::string outPath = Paths::Combine(folder, "out.arc");
        Archive::Archiver::Archive(outPath, files);
        const std::string archiveName = overMeta ? overMeta->Archive : meta->Archive;
        std::cout << " Compressing...\n";
        (void)LZ10::Compress(
            outPath, StringReplace(outPath, "out.arc", archiveName + ".arc"));
        FileDelete(outPath);
        std::cout << "Done.\n";
        Nop();
    }

    void TestMisc::ConvertRoomToFh(
        const std::string& room, std::optional<std::string> over)
    {
        const std::shared_ptr<RoomMetadata> meta = DictionaryAt(Metadata::RoomMetadata, room);
        std::shared_ptr<RoomMetadata> overMeta{};
        if (over)
        {
            overMeta = DictionaryAt(Metadata::RoomMetadata, *over);
        }
        assert(meta->EntityPath && meta->NodePath);

        const std::string folder = Paths::Combine(Paths::Export(), "_pack");
        const std::string fileSystem = meta->FirstHunt ? Paths::FhFileSystem() : Paths::FileSystem();
        Utility::RepackFilter filter = Utility::RepackFilter::All;
        if (!meta->FirstHunt && !meta->Hybrid)
        {
            filter = meta->Multiplayer
                ? Utility::RepackFilter::Multiplayer
                : Utility::RepackFilter::SinglePlayer;
        }

        std::cout << "Converting model...\n";
        auto [model, ignoredTexture] = Utility::Repack::RepackRoomModel(room, false, filter);
        (void)ignoredTexture;
        const std::string modelPath = PathGetFileName(overMeta ? overMeta->ModelPath : meta->ModelPath);
        const std::string modelDest = Paths::Combine(folder, modelPath);
        FileWriteAllBytes(modelDest, model);

        std::cout << "Converting collision...\n";
        const std::vector<byte> collision = Utility::RepackCollision::RepackFhRoom(room, filter);
        const std::string colDest = Paths::Combine(
            folder, PathGetFileName(overMeta ? overMeta->CollisionPath : meta->CollisionPath));
        FileWriteAllBytes(colDest, collision);

        std::cout << "Converting animation...\n";
        const std::string animSrc = Paths::Combine(fileSystem, meta->AnimationPath);
        const std::string animDest = Paths::Combine(
            folder, PathGetFileName(overMeta ? overMeta->AnimationPath : meta->AnimationPath));
        FileDelete(animDest);
        CopyFile(animSrc, animDest);

        if (meta->Hybrid)
        {
            std::cout << "Copying entities...\n";
            std::cout << "Copying nodedata...\n";
            const std::string entSrc = Paths::Combine(
                fileSystem, RequireString(meta->EntityPath, "path2"));
            const std::string nodeSrc = Paths::Combine(
                fileSystem, RequireString(meta->NodePath, "path2"));
            std::string entDest = Paths::Combine(
                folder, RequireString(meta->EntityPath, "path2"));
            std::string nodeDest = Paths::Combine(
                folder, RequireString(meta->NodePath, "path2"));
            if (overMeta)
            {
                assert(overMeta->EntityPath && overMeta->NodePath);
                entDest = Paths::Combine(
                    folder, RequireString(overMeta->EntityPath, "path2"));
                nodeDest = Paths::Combine(
                    folder, RequireString(overMeta->NodePath, "path2"));
            }
            FileDelete(entDest);
            FileDelete(nodeDest);
            CopyFile(entSrc, entDest);
            CopyFile(nodeSrc, nodeDest);
        }
        else
        {
            std::cout << "Converting entities...\n";
            const std::vector<byte> entity =
                Utility::Repack::RepackFhEntities(room, filter);
            const std::string entDest = Paths::Combine(
                folder,
                PathGetFileName(overMeta
                    ? RequireString(overMeta->EntityPath, "path")
                    : RequireString(meta->EntityPath, "path")));
            FileWriteAllBytes(entDest, entity);
        }
        std::cout << "Done.\n";
        Nop();
    }

    void TestMisc::TestCameraShake()
    {
        for (int i = 0; i < 1; ++i)
        {
            Rng::DoCameraShake(204);
        }
        std::cout << '\n';
        const std::array<int, 3> chances{50, 50, 50};
        for (int chance : chances)
        {
            const bool spawn = Rng::GetRandomInt2(100) < static_cast<uint>(chance);
            std::cout << (spawn ? "True" : "False") << '\n';
            if (spawn)
            {
                (void)Rng::GetRandomInt2(1);
            }
        }
        std::string line;
        std::getline(std::cin, line);
    }

    void TestMisc::Nop() noexcept
    {
    }
}
