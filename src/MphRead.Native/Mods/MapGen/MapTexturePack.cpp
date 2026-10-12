#include "MapTexturePack.hpp"

#include "../../Program.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <limits>
#include <stdexcept>
#include <streambuf>
#include <string_view>
#include <utility>

using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathGetFileName;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::Utf8GetString;

namespace
{
    void AppendReplacement(std::string& output)
    {
        output.push_back(static_cast<char>(0xEF));
        output.push_back(static_cast<char>(0xBF));
        output.push_back(static_cast<char>(0xBD));
    }

    [[nodiscard]] bool IsContinuation(std::uint8_t value) noexcept
    {
        return (value & 0xC0U) == 0x80U;
    }

    class BinaryReader final
    {
    public:
        explicit BinaryReader(std::istream& stream) noexcept : _stream(stream)
        {
        }

        [[nodiscard]] std::uint16_t ReadUInt16()
        {
            std::vector<std::uint8_t> bytes = ReadBytes(2);
            if (bytes.size() != 2)
            {
                throw std::runtime_error("Unable to read beyond the end of the stream.");
            }
            return static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[0])
                | (static_cast<std::uint16_t>(bytes[1]) << 8));
        }

        [[nodiscard]] std::vector<std::uint8_t> ReadBytes(std::int32_t count)
        {
            if (count < 0)
            {
                throw std::out_of_range("count");
            }

            std::vector<std::uint8_t> result(static_cast<std::size_t>(count));
            std::size_t total = 0;
            while (total < result.size())
            {
                const std::size_t remaining = result.size() - total;
                const std::size_t maximum = static_cast<std::size_t>(
                    std::numeric_limits<std::streamsize>::max());
                const std::streamsize request = static_cast<std::streamsize>(
                    std::min(remaining, maximum));
                _stream.read(reinterpret_cast<char*>(result.data() + total), request);
                const std::streamsize read = _stream.gcount();
                if (read > 0)
                {
                    total += static_cast<std::size_t>(read);
                }
                if (read != request)
                {
                    if (_stream.bad())
                    {
                        throw std::ios_base::failure("I/O error while reading stream.");
                    }
                    break;
                }
            }
            result.resize(total);
            return result;
        }

    private:
        std::istream& _stream;
    };

    class ReadOnlyMemoryBuffer final : public std::streambuf
    {
    public:
        explicit ReadOnlyMemoryBuffer(const std::vector<std::uint8_t>& bytes) noexcept
        {
            if (bytes.empty())
            {
                setg(&_empty, &_empty, &_empty);
                return;
            }

            const char* data = reinterpret_cast<const char*>(bytes.data());
            char* begin = const_cast<char*>(data);
            setg(begin, begin, begin + bytes.size());
        }

    private:
        char _empty = 0;
    };

    [[nodiscard]] std::int32_t PixelCount(std::uint16_t width, std::uint16_t height) noexcept
    {
        const std::uint32_t product = static_cast<std::uint32_t>(width)
            * static_cast<std::uint32_t>(height);
        if (product <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return static_cast<std::int32_t>(product);
        }
        return static_cast<std::int32_t>(
            static_cast<std::int64_t>(product) - (static_cast<std::int64_t>(1) << 32));
    }
}

namespace MphRead::Mods::MapGen
{
    MapTexturePack::Entry::Entry(
        std::int32_t sourceIndex,
        std::string name,
        std::uint16_t width,
        std::uint16_t height,
        std::vector<std::uint16_t> palette,
        std::vector<std::uint8_t> pixels,
        PixelFormat format,
        std::string meta)
        : _sourceIndex(sourceIndex),
          _name(std::move(name)),
          _width(width),
          _height(height),
          _palette(std::move(palette)),
          _pixels(std::move(pixels)),
          _format(format),
          _meta(std::move(meta))
    {
    }

    MapTexturePack::Entry::PixelFormat MapTexturePack::Entry::Format() const noexcept
    {
        return _format;
    }

    const std::string& MapTexturePack::Entry::Meta() const noexcept
    {
        return _meta;
    }

    std::int32_t MapTexturePack::Entry::SourceIndex() const noexcept
    {
        return _sourceIndex;
    }

    const std::string& MapTexturePack::Entry::Name() const noexcept
    {
        return _name;
    }

    std::uint16_t MapTexturePack::Entry::Width() const noexcept
    {
        return _width;
    }

    std::uint16_t MapTexturePack::Entry::Height() const noexcept
    {
        return _height;
    }

    const std::vector<std::uint16_t>& MapTexturePack::Entry::Palette() const noexcept
    {
        return _palette;
    }

    const std::vector<std::uint8_t>& MapTexturePack::Entry::Pixels() const noexcept
    {
        return _pixels;
    }

    std::int32_t MapTexturePack::SourceIndexDictionary::Count() const noexcept
    {
        return static_cast<std::int32_t>(_values.size());
    }

    bool MapTexturePack::SourceIndexDictionary::TryGetValue(
        std::int32_t key,
        std::int32_t& value) const noexcept
    {
        for (const Value& pair : _values)
        {
            if (pair.first == key)
            {
                value = pair.second;
                return true;
            }
        }
        value = 0;
        return false;
    }

    const std::int32_t& MapTexturePack::SourceIndexDictionary::operator[](std::int32_t key) const
    {
        for (const Value& pair : _values)
        {
            if (pair.first == key)
            {
                return pair.second;
            }
        }
        throw std::out_of_range("The given key was not present in the dictionary.");
    }

    MapTexturePack::SourceIndexDictionary::const_iterator
    MapTexturePack::SourceIndexDictionary::begin() const noexcept
    {
        return _values.begin();
    }

    MapTexturePack::SourceIndexDictionary::const_iterator
    MapTexturePack::SourceIndexDictionary::end() const noexcept
    {
        return _values.end();
    }

    void MapTexturePack::SourceIndexDictionary::Set(std::int32_t key, std::int32_t value)
    {
        for (Value& pair : _values)
        {
            if (pair.first == key)
            {
                pair.second = value;
                return;
            }
        }
        _values.emplace_back(key, value);
    }

    MapTexturePack::MapTexturePack(std::vector<Entry> entries)
        : _entries(std::move(entries))
    {
        for (std::size_t i = 0; i < _entries.size(); ++i)
        {
            _bySourceIndex.Set(
                _entries[i].SourceIndex(),
                static_cast<std::int32_t>(i));
        }
    }

    const std::vector<MapTexturePack::Entry>& MapTexturePack::Entries() const noexcept
    {
        return _entries;
    }

    const MapTexturePack::SourceIndexDictionary& MapTexturePack::BySourceIndex() const noexcept
    {
        return _bySourceIndex;
    }

    MapTexturePack MapTexturePack::Load(
        const std::vector<std::uint8_t>& bytes,
        const std::string& name)
    {
        ReadOnlyMemoryBuffer buffer(bytes);
        std::istream memory(&buffer);
        return Load(memory, name);
    }

    MapTexturePack MapTexturePack::Load(const std::string& path)
    {
        std::ifstream stream(PathFromUtf8(path), std::ios::in | std::ios::binary);
        if (!stream.is_open())
        {
            throw std::ios_base::failure("Could not open file for reading: " + path);
        }
        stream.exceptions(std::ios::badbit);
        return Load(stream, PathGetFileName(path));
    }

    MapTexturePack MapTexturePack::Load(std::istream& stream, const std::string& path)
    {
        BinaryReader reader(stream);
        const std::vector<std::uint8_t> magic = reader.ReadBytes(4);
        if (magic.size() != 4
            || magic[0] != static_cast<std::uint8_t>('F')
            || magic[1] != static_cast<std::uint8_t>('P')
            || magic[2] != static_cast<std::uint8_t>('T')
            || magic[3] != static_cast<std::uint8_t>('X'))
        {
            throw ProgramException(PathGetFileName(path) + " is not a texture pack.");
        }

        const std::uint16_t version = reader.ReadUInt16();
        // 2 adds each entry's pixel format and what it is to its shader
        // (ShaderAnim).
        if (version != 1 && version != 2)
        {
            throw ProgramException(
                PathGetFileName(path) + " is version " + std::to_string(version)
                + "; this build reads 1 and 2.");
        }

        const std::int32_t count = reader.ReadUInt16();
        std::vector<Entry> entries;
        entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
        {
            const std::uint16_t sourceIndex = reader.ReadUInt16();
            const std::uint16_t width = reader.ReadUInt16();
            const std::uint16_t height = reader.ReadUInt16();
            const std::int32_t paletteLength = reader.ReadUInt16();
            const std::int32_t nameLength = reader.ReadUInt16();
            const std::string name = Utf8GetString(reader.ReadBytes(nameLength));

            std::vector<std::uint16_t> palette(static_cast<std::size_t>(paletteLength));
            for (std::int32_t p = 0; p < paletteLength; ++p)
            {
                palette[static_cast<std::size_t>(p)] = reader.ReadUInt16();
            }

            std::vector<std::uint8_t> pixels = reader.ReadBytes(PixelCount(width, height));
            Entry::PixelFormat format = Entry::PixelFormat::Palette8;
            std::string meta;
            if (version >= 2)
            {
                const std::vector<std::uint8_t> formatByte = reader.ReadBytes(1);
                if (formatByte.size() != 1 || formatByte[0] > 2)
                {
                    throw ProgramException(PathGetFileName(path) + " has an entry of an unknown pixel format.");
                }
                format = static_cast<Entry::PixelFormat>(formatByte[0]);
                meta = Utf8GetString(reader.ReadBytes(reader.ReadUInt16()));
            }
            entries.emplace_back(
                static_cast<std::int32_t>(sourceIndex),
                name,
                width,
                height,
                std::move(palette),
                std::move(pixels),
                format,
                std::move(meta));
        }
        return MapTexturePack(std::move(entries));
    }
}
