#include "RawStructs.hpp"

#include "../../Formats/Formats.hpp"
#include "../../Program.hpp"
#include "../../Read.hpp"

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(DEBUG)
#define RAW_STRUCTS_DEBUG_ASSERT(condition) do { if (!(condition)) { std::abort(); } } while (false)
#else
#define RAW_STRUCTS_DEBUG_ASSERT(condition) do { } while (false)
#endif

namespace
{
    void WriteByte(std::vector<std::uint8_t>& bytes, std::uint8_t value)
    {
        bytes.push_back(value);
    }

    void WriteUInt16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void WriteInt16(std::vector<std::uint8_t>& bytes, std::int32_t value)
    {
        WriteUInt16(bytes, static_cast<std::uint16_t>(value));
    }

    void WriteUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    }

    void WriteInt32(std::vector<std::uint8_t>& bytes, std::int32_t value)
    {
        WriteUInt32(bytes, static_cast<std::uint32_t>(value));
    }

    void WriteFixedString(
        std::vector<std::uint8_t>& writer,
        std::optional<std::u16string_view> value,
        std::int32_t length)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length), 0);
        if (!value.has_value())
        {
            throw System::NullReferenceException();
        }
        const std::u16string_view text = *value;
        const std::size_t limit = static_cast<std::size_t>(length - 1);
        for (std::size_t i = 0; i < text.size() && i < limit; ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(text[i]);
        }
        writer.insert(writer.end(), bytes.begin(), bytes.end());
    }

    void WriteVector3Fx(
        std::vector<std::uint8_t>& writer,
        OpenTK::Mathematics::Vector3 value)
    {
        WriteInt32(writer, MphRead::Fixed::ToInt(value.X));
        WriteInt32(writer, MphRead::Fixed::ToInt(value.Y));
        WriteInt32(writer, MphRead::Fixed::ToInt(value.Z));
    }

    void WriteColorRgb(
        std::vector<std::uint8_t>& writer,
        MphRead::ColorRgb value)
    {
        WriteByte(writer, value.Red);
        WriteByte(writer, value.Green);
        WriteByte(writer, value.Blue);
    }

    template <typename T>
    [[nodiscard]] T Marshal(
        const std::vector<std::uint8_t>& bytes,
        std::string_view typeName)
    {
        const std::size_t size = sizeof(T);
        RAW_STRUCTS_DEBUG_ASSERT(bytes.size() == size);
        if (bytes.size() != size)
        {
            throw MphRead::ProgramException(
                "Built " + std::to_string(bytes.size())
                + " bytes for " + std::string(typeName)
                + ", which is " + std::to_string(size) + ".");
        }
        return MphRead::Read::ReadStruct<T>(
            std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    }
}

namespace MphRead::Mods::MapGen
{
    std::shared_ptr<Node> RawStructs::MakeNode(
        std::optional<std::u16string_view> name,
        std::int32_t meshCount,
        std::int32_t firstMeshId,
        std::int32_t parent,
        std::int32_t child,
        std::int32_t next)
    {
        std::vector<std::uint8_t> writer;
        writer.reserve(sizeof(RawNode));

        WriteFixedString(writer, name, 64);
        WriteInt16(writer, parent);
        WriteInt16(writer, child);
        WriteInt16(writer, next);
        WriteUInt16(writer, 0);
        WriteUInt32(writer, 1U);
        WriteUInt16(writer, static_cast<std::uint16_t>(meshCount));
        const std::uint32_t firstMeshOffset
            = static_cast<std::uint32_t>(firstMeshId) * 2U;
        WriteUInt16(writer, static_cast<std::uint16_t>(firstMeshOffset));
        WriteVector3Fx(
            writer,
            OpenTK::Mathematics::Vector3(1.0F, 1.0F, 1.0F));
        WriteInt16(writer, 0);
        WriteInt16(writer, 0);
        WriteInt16(writer, 0);
        WriteUInt16(writer, 0);
        WriteVector3Fx(writer, OpenTK::Mathematics::Vector3::Zero);
        WriteInt32(writer, 0);
        WriteVector3Fx(writer, OpenTK::Mathematics::Vector3::Zero);
        WriteVector3Fx(writer, OpenTK::Mathematics::Vector3::Zero);
        WriteByte(writer, static_cast<std::uint8_t>(BillboardMode::None));
        WriteByte(writer, 0);
        WriteUInt16(writer, 0);
        for (std::int32_t i = 0; i < 12; ++i)
        {
            WriteInt32(writer, 0);
        }
        for (std::int32_t i = 0; i < 12; ++i)
        {
            WriteUInt32(writer, 0U);
        }

        return std::make_shared<Node>(
            Marshal<RawNode>(writer, "RawNode"));
    }

    std::shared_ptr<Material> RawStructs::MakeMaterial(
        std::optional<std::u16string_view> name,
        std::int32_t textureId,
        std::int32_t paletteId,
        RepeatMode xRepeat,
        RepeatMode yRepeat,
        bool lighting,
        ColorRgb diffuse,
        ColorRgb ambient,
        RenderMode renderMode,
        CullingMode culling)
    {
        std::vector<std::uint8_t> writer;
        writer.reserve(sizeof(RawMaterial));

        WriteFixedString(writer, name, 64);
        WriteByte(writer, lighting ? 1U : 0U);
        WriteByte(writer, static_cast<std::uint8_t>(culling));
        WriteByte(writer, 31);
        WriteByte(writer, 0);
        WriteInt16(writer, paletteId);
        WriteInt16(writer, textureId);
        WriteByte(writer, static_cast<std::uint8_t>(xRepeat));
        WriteByte(writer, static_cast<std::uint8_t>(yRepeat));
        WriteColorRgb(writer, diffuse);
        WriteColorRgb(writer, ambient);
        WriteColorRgb(writer, ColorRgb(0, 0, 0));
        WriteByte(writer, 0);
        WriteUInt32(writer, static_cast<std::uint32_t>(PolygonMode::Modulate));
        WriteByte(writer, static_cast<std::uint8_t>(renderMode));
        WriteByte(writer, 0);
        WriteUInt16(writer, 0);
        WriteUInt32(writer, static_cast<std::uint32_t>(TexgenMode::None));
        WriteUInt16(writer, 0);
        WriteUInt16(writer, 0);
        WriteUInt32(writer, 0U);
        WriteInt32(writer, 4096);
        WriteInt32(writer, 4096);
        WriteUInt16(writer, 0);
        WriteUInt16(writer, 0);
        WriteInt32(writer, 0);
        WriteInt32(writer, 0);
        WriteUInt16(writer, 0);
        WriteUInt16(writer, 0);
        WriteByte(writer, 0);
        WriteByte(writer, 0);
        WriteUInt16(writer, 0);

        return std::make_shared<Material>(
            Marshal<RawMaterial>(writer, "RawMaterial"));
    }

    std::shared_ptr<Mesh> RawStructs::MakeMesh(
        std::int32_t materialId,
        std::int32_t dlistId)
    {
        std::vector<std::uint8_t> writer;
        writer.reserve(sizeof(RawMesh));

        WriteUInt16(writer, static_cast<std::uint16_t>(materialId));
        WriteUInt16(writer, static_cast<std::uint16_t>(dlistId));

        return std::make_shared<Mesh>(
            Marshal<RawMesh>(writer, "RawMesh"));
    }
}
