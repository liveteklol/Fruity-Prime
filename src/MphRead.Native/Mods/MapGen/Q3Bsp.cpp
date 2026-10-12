#include "Q3Bsp.hpp"

#include "../../Formats/Types.hpp"
#include "../../Program.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/HashCode.hpp"
#include "NativeRuntime/System/ZipArchive.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <mutex>
#include <random>
#include <sstream>
#include <string_view>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#endif

using ::MphRead::NativeRuntime::AppendUtf8;
using ::MphRead::NativeRuntime::FileReadAllBytes;
using ::MphRead::NativeRuntime::OperationStatus;
using ::MphRead::NativeRuntime::PathGetFileName;
using ::MphRead::NativeRuntime::RuneDecodeFromUtf8;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::MphRead::NativeRuntime::Utf16ToUtf8;
using ::MphRead::NativeRuntime::Utf8GetString;
using ::MphRead::NativeRuntime::Utf8Scalar;
using ::MphRead::NativeRuntime::Utf8ToUtf16;
using ::MphRead::NativeRuntime::Utf8ToUtf32;
using ::MphRead::NativeRuntime::Utf8ToWide;
using ::MphRead::NativeRuntime::WideToUtf8;

namespace
{
    using ByteVector = std::vector<std::uint8_t>;

    [[nodiscard]] std::uint16_t ReadU16(const ByteVector& bytes, std::size_t position)
    {
        if (position > bytes.size() || bytes.size() - position < 2)
        {
            throw System::IO::InvalidDataException("Unexpected end of data.");
        }
        return static_cast<std::uint16_t>(bytes[position])
            | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[position + 1]) << 8);
    }

    [[nodiscard]] std::uint32_t ReadU32(const ByteVector& bytes, std::size_t position)
    {
        if (position > bytes.size() || bytes.size() - position < 4)
        {
            throw System::IO::InvalidDataException("Unexpected end of data.");
        }
        return static_cast<std::uint32_t>(bytes[position])
            | (static_cast<std::uint32_t>(bytes[position + 1]) << 8)
            | (static_cast<std::uint32_t>(bytes[position + 2]) << 16)
            | (static_cast<std::uint32_t>(bytes[position + 3]) << 24);
    }

    [[nodiscard]] std::uint64_t ReadU64(const ByteVector& bytes, std::size_t position)
    {
        const std::uint64_t low = ReadU32(bytes, position);
        const std::uint64_t high = ReadU32(bytes, position + 4);
        return low | (high << 32);
    }

    [[nodiscard]] std::int32_t ReadI32(const ByteVector& bytes, std::size_t position)
    {
        if (position > bytes.size() || bytes.size() - position < sizeof(std::int32_t))
        {
            throw std::runtime_error("Unexpected end of data.");
        }
        std::int32_t value = 0;
        std::memcpy(&value, bytes.data() + position, sizeof(value));
        return value;
    }

    void WriteI32(ByteVector& bytes, std::size_t position, std::int32_t value)
    {
        if (position > bytes.size() || bytes.size() - position < sizeof(value))
        {
            throw std::out_of_range("Destination is too short.");
        }
        std::memcpy(bytes.data() + position, &value, sizeof(value));
    }

    [[nodiscard]] std::string DecodeAscii(const std::uint8_t* data, std::size_t count)
    {
        std::string result;
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            result.push_back(data[i] <= 0x7F ? static_cast<char>(data[i]) : '?');
        }
        return result;
    }

    [[nodiscard]] std::string DecodeAscii(const ByteVector& bytes, std::size_t offset, std::size_t count)
    {
        if (offset > bytes.size() || count > bytes.size() - offset)
        {
            throw System::ArgumentOutOfRangeException();
        }
        return DecodeAscii(bytes.data() + offset, count);
    }

    [[nodiscard]] std::string FileNameWithoutExtension(const std::string& path)
    {
        const std::string fileName = PathGetFileName(path);
        const std::size_t dot = fileName.find_last_of('.');
        return dot == std::string::npos ? fileName : fileName.substr(0, dot);
    }

    [[nodiscard]] std::string Extension(const std::string& path)
    {
        const std::string fileName = PathGetFileName(path);
        const std::size_t dot = fileName.find_last_of('.');
        if (dot == std::string::npos || dot + 1 == fileName.size()) return {};
        return fileName.substr(dot);
    }

    class ByteReader
    {
    public:
        explicit ByteReader(const ByteVector& bytes) noexcept : _bytes(bytes) {}

        void Position(std::int64_t value)
        {
            if (value < 0)
            {
                throw System::ArgumentOutOfRangeException();
            }
            _position = static_cast<std::uint64_t>(value);
        }

        [[nodiscard]] std::int32_t ReadInt32()
        {
            const std::uint32_t value = ReadExactU32();
            return std::bit_cast<std::int32_t>(value);
        }

        [[nodiscard]] float ReadSingle()
        {
            return std::bit_cast<float>(ReadExactU32());
        }

        [[nodiscard]] ByteVector ReadBytes(std::size_t count)
        {
            if (_position >= _bytes.size())
            {
                return {};
            }
            const std::size_t position = static_cast<std::size_t>(_position);
            const std::size_t actual = std::min(count, _bytes.size() - position);
            ByteVector result(
                _bytes.begin() + static_cast<std::ptrdiff_t>(position),
                _bytes.begin() + static_cast<std::ptrdiff_t>(position + actual));
            _position += actual;
            return result;
        }

        [[nodiscard]] std::string ReadChars(std::size_t count)
        {
            std::string result;
            std::size_t charCount = 0;
            while (charCount < count && _position < _bytes.size())
            {
                const std::size_t position = static_cast<std::size_t>(_position);
                const Utf8Scalar decoded = RuneDecodeFromUtf8(std::string_view(
                    reinterpret_cast<const char*>(_bytes.data()) + position, _bytes.size() - position));
                const char32_t scalar = decoded.Value;

                // BinaryReader's UTF-8 Decoder is invoked with flush:false. An
                // incomplete terminal sequence is consumed into decoder state but
                // emits no replacement character before end-of-stream is observed.
                _position += decoded.Length;
                if (decoded.Status == OperationStatus::NeedMoreData)
                {
                    break;
                }

                const std::size_t units = scalar > 0xFFFFU ? 2U : 1U;
                if (units > count - charCount)
                {
                    // Decoder.GetChars throws when a complete surrogate pair cannot
                    // fit in the remaining destination rather than splitting it.
                    throw System::ArgumentException();
                }
                AppendUtf8(result, scalar);
                charCount += units;
            }
            return result;
        }

    private:
        const ByteVector& _bytes;
        std::uint64_t _position = 0;

        [[nodiscard]] std::uint32_t ReadExactU32()
        {
            if (_position > _bytes.size() || _bytes.size() - static_cast<std::size_t>(_position) < 4)
            {
                throw System::IO::EndOfStreamException();
            }
            const std::uint32_t value = ReadU32(_bytes, static_cast<std::size_t>(_position));
            _position += 4;
            return value;
        }
    };

    [[nodiscard]] bool CultureLess(const std::string& left, const std::string& right)
    {
        return ::MphRead::NativeRuntime::StringCompareCurrentCulture(left, right) < 0;
    }

    template <typename T, typename Read>
    [[nodiscard]] std::vector<T> ReadLump(ByteReader& reader,
        std::pair<std::int32_t, std::int32_t> lump, std::int32_t size, Read read)
    {
        const std::int32_t count = lump.second / size;
        if (count < 0)
        {
            throw System::ArgumentOutOfRangeException();
        }
        std::vector<T> results;
        results.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
        {
            const std::int32_t position = UncheckedAdd(lump.first, UncheckedMultiply(i, size));
            reader.Position(position);
            results.push_back(read(reader));
        }
        return results;
    }
}

namespace MphRead::Mods::MapGen::Q3RecordRuntime
{
    std::uint32_t TypeHash(const std::type_info& type) noexcept
    {
        return static_cast<std::uint32_t>(::MphRead::NativeRuntime::HashCodeCombine(
            static_cast<std::int32_t>(type.hash_code()),
            static_cast<std::int32_t>(static_cast<std::uint64_t>(type.hash_code()) >> 32)));
    }

    std::uint32_t StringHash(const Q3String& value) noexcept
    {
        return value.HasValue()
            ? static_cast<std::uint32_t>(::MphRead::NativeRuntime::StringGetHashCode(value.Value()))
            : 0U;
    }

    std::uint32_t ReferenceHash(const void* value) noexcept
    {
        return static_cast<std::uint32_t>(::MphRead::NativeRuntime::ReferenceGetHashCode(value));
    }

    std::string IntString(std::int32_t value)
    {
        return ::MphRead::NativeRuntime::ToString(value);
    }

    std::string FloatString(float value)
    {
        return ::MphRead::NativeRuntime::ToString(value);
    }
}

namespace MphRead::Mods::MapGen
{
    const Q3UsedLumps Q3Bsp::UsedLumps{};

    bool Q3Bsp::Open(float x, float y, float z) const
    {
        if (_nodes.empty() || _leafClusters.empty())
        {
            return true;
        }
        std::int32_t index = 0;
        for (std::size_t guard = 0; index >= 0 && guard < _nodes.size(); ++guard)
        {
            if (static_cast<std::size_t>(index) >= _nodes.size())
            {
                return true;
            }
            const Node& node = _nodes[static_cast<std::size_t>(index)];
            if (node.Plane < 0 || static_cast<std::size_t>(node.Plane) >= Planes().size() || !Planes()[static_cast<std::size_t>(node.Plane)])
            {
                return true;
            }
            const Q3Plane& plane = *Planes()[static_cast<std::size_t>(node.Plane)];
            const float side = plane.X() * x + plane.Y() * y + plane.Z() * z - plane.Distance();
            index = side >= 0.0F ? node.Front : node.Back;
        }
        if (index >= 0)
        {
            return true;
        }
        const std::size_t leaf = static_cast<std::size_t>(-(index + 1));
        return leaf >= _leafClusters.size() || _leafClusters[leaf] >= 0;
    }

    std::size_t Q3StringHash::operator()(const std::string& value) const noexcept
    {
        return ::MphRead::NativeRuntime::OrdinalIgnoreCaseHash{}(value);
    }

    bool Q3StringEqual::operator()(const std::string& left, const std::string& right) const noexcept
    {
        return ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(left, right);
    }

    Q3Texture::Q3Texture(Q3String name, std::int32_t flags, std::int32_t contents) noexcept
        : _name(std::move(name)), _flags(flags), _contents(contents) {}
    Q3Texture::Q3Texture(std::nullptr_t, std::int32_t flags, std::int32_t contents) noexcept
        : Q3Texture(Q3String(nullptr), flags, contents) {}
    Q3Texture::Q3Texture(std::string name, std::int32_t flags, std::int32_t contents)
        : Q3Texture(Q3String(std::move(name)), flags, contents) {}
    Q3Texture::Q3Texture(const char* name, std::int32_t flags, std::int32_t contents)
        : Q3Texture(Q3String(name), flags, contents) {}
    const Q3String& Q3Texture::Name() const noexcept { return _name; }
    std::int32_t Q3Texture::Flags() const noexcept { return _flags; }
    std::int32_t Q3Texture::Contents() const noexcept { return _contents; }

    Q3Plane::Q3Plane(float x, float y, float z, float distance) noexcept
        : _x(x), _y(y), _z(z), _distance(distance) {}
    float Q3Plane::X() const noexcept { return _x; }
    float Q3Plane::Y() const noexcept { return _y; }
    float Q3Plane::Z() const noexcept { return _z; }
    float Q3Plane::Distance() const noexcept { return _distance; }

    Q3Brush::Q3Brush(std::int32_t firstSide, std::int32_t sideCount, std::int32_t texture) noexcept
        : _firstSide(firstSide), _sideCount(sideCount), _texture(texture) {}
    std::int32_t Q3Brush::FirstSide() const noexcept { return _firstSide; }
    std::int32_t Q3Brush::SideCount() const noexcept { return _sideCount; }
    std::int32_t Q3Brush::Texture() const noexcept { return _texture; }

    Q3BrushSide::Q3BrushSide(std::int32_t plane, std::int32_t texture) noexcept
        : _plane(plane), _texture(texture) {}
    std::int32_t Q3BrushSide::Plane() const noexcept { return _plane; }
    std::int32_t Q3BrushSide::Texture() const noexcept { return _texture; }

    Q3Vertex::Q3Vertex(Q3FloatArray position, Q3FloatArray surface, Q3FloatArray normal, Q3ByteArray color) noexcept
        : _position(std::move(position)), _surface(std::move(surface)),
          _normal(std::move(normal)), _color(std::move(color)) {}
    Q3FloatArray Q3Vertex::Position() const noexcept { return _position; }
    Q3FloatArray Q3Vertex::Surface() const noexcept { return _surface; }
    Q3FloatArray Q3Vertex::Normal() const noexcept { return _normal; }
    Q3ByteArray Q3Vertex::Color() const noexcept { return _color; }

    Q3Face::Q3Face(std::int32_t texture, std::int32_t effect, std::int32_t type,
        std::int32_t vertex, std::int32_t vertexCount, std::int32_t meshVert,
        std::int32_t meshVertCount, Q3FloatArray normal, Q3IntArray size) noexcept
        : _texture(texture), _effect(effect), _type(type), _vertex(vertex),
          _vertexCount(vertexCount), _meshVert(meshVert), _meshVertCount(meshVertCount),
          _normal(std::move(normal)), _size(std::move(size)) {}
    std::int32_t Q3Face::Texture() const noexcept { return _texture; }
    std::int32_t Q3Face::Effect() const noexcept { return _effect; }
    std::int32_t Q3Face::Type() const noexcept { return _type; }
    std::int32_t Q3Face::Vertex() const noexcept { return _vertex; }
    std::int32_t Q3Face::VertexCount() const noexcept { return _vertexCount; }
    std::int32_t Q3Face::MeshVert() const noexcept { return _meshVert; }
    std::int32_t Q3Face::MeshVertCount() const noexcept { return _meshVertCount; }
    Q3FloatArray Q3Face::Normal() const noexcept { return _normal; }
    Q3IntArray Q3Face::Size() const noexcept { return _size; }

    Q3Model::Q3Model(Q3FloatArray mins, Q3FloatArray maxs, std::int32_t face,
        std::int32_t faceCount, std::int32_t brush, std::int32_t brushCount) noexcept
        : _mins(std::move(mins)), _maxs(std::move(maxs)), _face(face),
          _faceCount(faceCount), _brush(brush), _brushCount(brushCount) {}
    Q3FloatArray Q3Model::Mins() const noexcept { return _mins; }
    Q3FloatArray Q3Model::Maxs() const noexcept { return _maxs; }
    std::int32_t Q3Model::Face() const noexcept { return _face; }
    std::int32_t Q3Model::FaceCount() const noexcept { return _faceCount; }
    std::int32_t Q3Model::Brush() const noexcept { return _brush; }
    std::int32_t Q3Model::BrushCount() const noexcept { return _brushCount; }

    const Q3Bsp::TextureList& Q3Bsp::Textures() const noexcept { return _textures.Get(); }
    const Q3Bsp::PlaneList& Q3Bsp::Planes() const noexcept { return _planes.Get(); }
    const Q3Bsp::BrushList& Q3Bsp::Brushes() const noexcept { return _brushes.Get(); }
    const Q3Bsp::BrushSideList& Q3Bsp::BrushSides() const noexcept { return _brushSides.Get(); }
    const Q3Bsp::VertexList& Q3Bsp::Vertices() const noexcept { return _vertices.Get(); }
    const Q3Bsp::MeshVertList& Q3Bsp::MeshVerts() const noexcept { return _meshVerts.Get(); }
    const Q3Bsp::FaceList& Q3Bsp::Faces() const noexcept { return _faces.Get(); }
    const Q3Bsp::ModelList& Q3Bsp::Models() const noexcept { return _models.Get(); }
    const Q3Bsp::EntityList& Q3Bsp::Entities() const noexcept { return _entities.Get(); }

    std::vector<std::uint8_t> Q3Bsp::Trim(const std::vector<std::uint8_t>& bsp)
    {
        return Trim(&bsp);
    }

    std::vector<std::uint8_t> Q3Bsp::Trim(const std::vector<std::uint8_t>* bspReference)
    {
        if (bspReference == nullptr)
        {
            throw System::NullReferenceException();
        }
        const std::vector<std::uint8_t>& bsp = *bspReference;
        constexpr std::size_t HeaderSize = 8 + 17 * 8;
        if (bsp.size() < HeaderSize)
        {
            throw ProgramException("Not a Quake 3 level: too short to hold a header.");
        }
        ByteVector output;
        output.reserve(bsp.size() / 4);
        output.insert(output.end(), bsp.begin(), bsp.begin() + static_cast<std::ptrdiff_t>(HeaderSize));
        for (std::int32_t i = 0; i < 17; ++i)
        {
            const std::int32_t offset = ReadI32(bsp, 8 + static_cast<std::size_t>(i) * 8);
            const std::int32_t length = ReadI32(bsp, 8 + static_cast<std::size_t>(i) * 8 + 4);
            const bool used = std::find(UsedLumps.begin(), UsedLumps.end(), i) != UsedLumps.end();
            const std::int32_t end = UncheckedAdd(offset, length);
            if (!used || offset < 0 || length <= 0
                || (end > 0 && static_cast<std::uint64_t>(end) > bsp.size()))
            {
                WriteI32(output, 8 + static_cast<std::size_t>(i) * 8,
                    static_cast<std::int32_t>(output.size()));
                WriteI32(output, 8 + static_cast<std::size_t>(i) * 8 + 4, 0);
                continue;
            }
            WriteI32(output, 8 + static_cast<std::size_t>(i) * 8,
                static_cast<std::int32_t>(output.size()));
            WriteI32(output, 8 + static_cast<std::size_t>(i) * 8 + 4, length);
            const std::size_t begin = static_cast<std::size_t>(offset);
            const std::size_t count = static_cast<std::size_t>(length);
            if (begin > bsp.size() || count > bsp.size() - begin)
            {
                throw System::ArgumentOutOfRangeException();
            }
            output.insert(output.end(),
                bsp.begin() + static_cast<std::ptrdiff_t>(begin),
                bsp.begin() + static_cast<std::ptrdiff_t>(begin + count));
            while (output.size() % 4 != 0)
            {
                output.push_back(0);
            }
        }
        return output;
    }

    std::shared_ptr<Q3Bsp> Q3Bsp::Load(
        const std::string& source, const std::optional<std::string>& mapName)
    {
        return Load(&source, mapName);
    }

    std::shared_ptr<Q3Bsp> Q3Bsp::Load(
        const std::string* source, const std::optional<std::string>& mapName)
    {
        return Parse(ReadLevel(source, mapName));
    }

    std::vector<std::uint8_t> Q3Bsp::ReadLevel(
        const std::string& source, const std::optional<std::string>& mapName)
    {
        return ReadLevel(&source, mapName);
    }

    std::vector<std::uint8_t> Q3Bsp::ReadLevel(
        const std::string* source, const std::optional<std::string>& mapName)
    {
        const std::string sourceText = source == nullptr ? std::string{} : *source;
        if (source == nullptr || !MphRead::NativeRuntime::FileExists(sourceText))
        {
            throw ProgramException("No such file: " + sourceText);
        }
        if (::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(Extension(sourceText), ".bsp"))
        {
            return FileReadAllBytes(sourceText);
        }
        const auto archive = ::MphRead::NativeRuntime::ZipArchive::OpenRead(sourceText);
        const auto& entries = archive->Entries();
        std::vector<const ::MphRead::NativeRuntime::ZipArchiveEntry*> maps;
        for (const auto& entry : entries)
        {
            if (::MphRead::NativeRuntime::StringEndsWithOrdinalIgnoreCase(entry->FullName(), ".bsp"))
            {
                maps.push_back(entry.get());
            }
        }
        if (maps.empty())
        {
            throw ProgramException(PathGetFileName(sourceText) + " contains no .bsp.");
        }
        const ::MphRead::NativeRuntime::ZipArchiveEntry* selected = nullptr;
        if (!mapName.has_value())
        {
            selected = maps.front();
        }
        else
        {
            for (const auto* entry : maps)
            {
                if (::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(FileNameWithoutExtension(entry->FullName()), *mapName))
                {
                    selected = entry;
                    break;
                }
            }
        }
        if (selected == nullptr)
        {
            std::vector<std::string> available;
            available.reserve(maps.size());
            for (const auto* entry : maps)
            {
                available.push_back(FileNameWithoutExtension(entry->FullName()));
            }
            std::stable_sort(available.begin(), available.end(), CultureLess);
            std::string joined;
            for (std::size_t i = 0; i < available.size(); ++i)
            {
                if (i != 0) joined += ", ";
                joined += available[i];
            }
            throw ProgramException(PathGetFileName(sourceText) + " has no map " + *mapName + ". It has: " + joined);
        }
        return selected->ReadAllBytes();
    }

    std::vector<std::string> Q3Bsp::ListMaps(const std::string& source)
    {
        return ListMaps(&source);
    }

    std::vector<std::string> Q3Bsp::ListMaps(const std::string* source)
    {
        if (source == nullptr)
        {
            throw System::NullReferenceException();
        }
        const std::string& sourceText = *source;
        if (::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(Extension(sourceText), ".bsp"))
        {
            return {FileNameWithoutExtension(sourceText)};
        }
        const auto archive = ::MphRead::NativeRuntime::ZipArchive::OpenRead(sourceText);
        const auto& entries = archive->Entries();
        std::vector<std::string> maps;
        for (const auto& entry : entries)
        {
            if (::MphRead::NativeRuntime::StringEndsWithOrdinalIgnoreCase(entry->FullName(), ".bsp"))
            {
                maps.push_back(FileNameWithoutExtension(entry->FullName()));
            }
        }
        std::stable_sort(maps.begin(), maps.end(), CultureLess);
        return maps;
    }

    std::shared_ptr<Q3Bsp> Q3Bsp::Parse(const std::vector<std::uint8_t>& bytes)
    {
        ByteReader reader(bytes);
        const std::string magic = reader.ReadChars(4);
        const std::int32_t version = reader.ReadInt32();
        if (magic != "IBSP" || version != 46)
        {
            throw ProgramException("Not a Quake 3 level (magic " + magic
                + ", version " + std::to_string(version) + ").");
        }
        std::array<std::pair<std::int32_t, std::int32_t>, 17> offsets{};
        for (auto& offset : offsets)
        {
            offset = {reader.ReadInt32(), reader.ReadInt32()};
        }
        auto bsp = std::make_shared<Q3Bsp>();
        const std::int32_t entityOffset = offsets[0].first;
        const std::int32_t entityLength = offsets[0].second;
        if (entityOffset < 0 || entityLength < 0
            || static_cast<std::uint64_t>(entityOffset) > bytes.size()
            || static_cast<std::uint64_t>(entityLength) > bytes.size() - static_cast<std::size_t>(entityOffset))
        {
            throw System::ArgumentOutOfRangeException();
        }
        bsp->_entities = ParseEntities(DecodeAscii(bytes,
            static_cast<std::size_t>(entityOffset), static_cast<std::size_t>(entityLength)));

        bsp->_textures = ReadLump<std::shared_ptr<Q3Texture>>(reader, offsets[1], 72,
            [](ByteReader& r)
            {
                ByteVector nameBytes = r.ReadBytes(64);
                std::string name = DecodeAscii(nameBytes.data(), nameBytes.size());
                // A C string: whatever follows the terminator is whatever the
                // compiler's buffer held before (q3map2 2.5 leaves it there),
                // not padding, so trimming trailing zeros is not enough.
                const std::size_t terminator = name.find('\0');
                if (terminator != std::string::npos) name.resize(terminator);
                const std::int32_t flags = r.ReadInt32();
                const std::int32_t contents = r.ReadInt32();
                return std::make_shared<Q3Texture>(name, flags, contents);
            });
        bsp->_planes = ReadLump<std::shared_ptr<Q3Plane>>(reader, offsets[2], 16,
            [](ByteReader& r)
            {
                const float x = r.ReadSingle();
                const float y = r.ReadSingle();
                const float z = r.ReadSingle();
                const float distance = r.ReadSingle();
                return std::make_shared<Q3Plane>(x, y, z, distance);
            });
        bsp->_models = ReadLump<std::shared_ptr<Q3Model>>(reader, offsets[7], 40,
            [](ByteReader& r)
            {
                auto mins = std::make_shared<std::vector<float>>(std::initializer_list<float>{
                    r.ReadSingle(), r.ReadSingle(), r.ReadSingle()});
                auto maxs = std::make_shared<std::vector<float>>(std::initializer_list<float>{
                    r.ReadSingle(), r.ReadSingle(), r.ReadSingle()});
                const std::int32_t face = r.ReadInt32();
                const std::int32_t faceCount = r.ReadInt32();
                const std::int32_t brush = r.ReadInt32();
                const std::int32_t brushCount = r.ReadInt32();
                return std::make_shared<Q3Model>(mins, maxs, face, faceCount, brush, brushCount);
            });
        bsp->_brushes = ReadLump<std::shared_ptr<Q3Brush>>(reader, offsets[8], 12,
            [](ByteReader& r)
            {
                const std::int32_t firstSide = r.ReadInt32();
                const std::int32_t sideCount = r.ReadInt32();
                const std::int32_t texture = r.ReadInt32();
                return std::make_shared<Q3Brush>(firstSide, sideCount, texture);
            });
        bsp->_brushSides = ReadLump<std::shared_ptr<Q3BrushSide>>(reader, offsets[9], 8,
            [](ByteReader& r)
            {
                const std::int32_t plane = r.ReadInt32();
                const std::int32_t texture = r.ReadInt32();
                return std::make_shared<Q3BrushSide>(plane, texture);
            });
        bsp->_vertices = ReadLump<std::shared_ptr<Q3Vertex>>(reader, offsets[10], 44,
            [](ByteReader& r)
            {
                auto position = std::make_shared<std::vector<float>>(std::initializer_list<float>{
                    r.ReadSingle(), r.ReadSingle(), r.ReadSingle()});
                auto surface = std::make_shared<std::vector<float>>(std::initializer_list<float>{
                    r.ReadSingle(), r.ReadSingle()});
                const float lightmapS = r.ReadSingle();
                const float lightmapT = r.ReadSingle();
                auto normal = std::make_shared<std::vector<float>>(std::initializer_list<float>{
                    r.ReadSingle(), r.ReadSingle(), r.ReadSingle()});
                auto color = std::make_shared<std::vector<std::uint8_t>>(r.ReadBytes(4));
                auto vertex = std::make_shared<Q3Vertex>(position, surface, normal, color);
                vertex->Lightmap = {lightmapS, lightmapT};
                return vertex;
            });
        bsp->_meshVerts = ReadLump<std::int32_t>(reader, offsets[11], 4,
            [](ByteReader& r) { return r.ReadInt32(); });
        bsp->_faces = ReadLump<std::shared_ptr<Q3Face>>(reader, offsets[13], 104,
            [](ByteReader& r)
            {
                const std::int32_t texture = r.ReadInt32();
                const std::int32_t effect = r.ReadInt32();
                const std::int32_t type = r.ReadInt32();
                const std::int32_t vertex = r.ReadInt32();
                const std::int32_t vertexCount = r.ReadInt32();
                const std::int32_t meshVert = r.ReadInt32();
                const std::int32_t meshVertCount = r.ReadInt32();
                const std::int32_t lightmapIndex = r.ReadInt32();
                std::array<std::int32_t, 4> lightmapRect{};
                for (std::int32_t& value : lightmapRect)
                {
                    value = r.ReadInt32();
                }
                (void)r.ReadBytes(12 + 24);
                auto normal = std::make_shared<std::vector<float>>(std::initializer_list<float>{
                    r.ReadSingle(), r.ReadSingle(), r.ReadSingle()});
                auto size = std::make_shared<std::vector<std::int32_t>>(std::initializer_list<std::int32_t>{
                    r.ReadInt32(), r.ReadInt32()});
                auto face = std::make_shared<Q3Face>(texture, effect, type, vertex, vertexCount,
                    meshVert, meshVertCount, normal, size);
                face->LightmapIndex = lightmapIndex;
                face->LightmapRect = lightmapRect;
                return face;
            });
        // The lightmaps as they are, and the fog volumes.
        if (offsets[14].first >= 0 && offsets[14].second > 0
            && static_cast<std::uint64_t>(offsets[14].first) + static_cast<std::uint64_t>(offsets[14].second) <= bytes.size())
        {
            bsp->_lightmaps.assign(bytes.begin() + offsets[14].first,
                bytes.begin() + offsets[14].first + offsets[14].second);
        }
        bsp->_nodes = ReadLump<Node>(reader, offsets[3], 36,
            [](ByteReader& r)
            {
                Node node;
                node.Plane = r.ReadInt32();
                node.Front = r.ReadInt32();
                node.Back = r.ReadInt32();
                return node;
            });
        bsp->_leafClusters = ReadLump<std::int32_t>(reader, offsets[4], 48,
            [](ByteReader& r) { return r.ReadInt32(); });
        bsp->_fogs = ReadLump<Fog>(reader, offsets[12], 72,
            [](ByteReader& r)
            {
                const std::vector<std::uint8_t> raw = r.ReadBytes(64);
                std::string shader;
                for (const std::uint8_t ch : raw)
                {
                    if (ch == 0)
                    {
                        break;
                    }
                    shader.push_back(static_cast<char>(ch));
                }
                Fog fog;
                fog.Shader = shader;
                fog.Brush = r.ReadInt32();
                fog.VisibleSide = r.ReadInt32();
                return fog;
            });
        return bsp;
    }

    Q3Bsp::EntityList Q3Bsp::ParseEntities(const std::string& text)
    {
        EntityList results;
        std::shared_ptr<Q3Entity> current;
        std::string token;
        std::vector<std::string> tokens;
        bool inString = false;
        for (char c : text)
        {
            if (c == '"')
            {
                if (inString)
                {
                    tokens.push_back(token);
                    token.clear();
                }
                inString = !inString;
                continue;
            }
            if (inString)
            {
                token.push_back(c);
                continue;
            }
            if (c == '{')
            {
                current = std::make_shared<Q3Entity>();
                tokens.clear();
            }
            else if (c == '}')
            {
                if (current)
                {
                    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2)
                    {
                        (*current)[tokens[i]] = tokens[i + 1];
                    }
                    results.push_back(current);
                    current.reset();
                }
                tokens.clear();
            }
        }
        return results;
    }
}
