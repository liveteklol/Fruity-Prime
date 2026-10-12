#include "Q3Md3.hpp"

#include "Q3Bsp.hpp"
#include "Q3Movers.hpp"
#include "../../NativeRuntime/System/Number.hpp"
#include "../../NativeRuntime/System/ZipArchive.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>

namespace MphRead::Mods::MapGen
{
    namespace
    {
        using OpenTK::Mathematics::Vector2;
        using OpenTK::Mathematics::Vector3;

        template <typename T>
        [[nodiscard]] T Read(const std::vector<std::uint8_t>& bytes, std::size_t at)
        {
            T value{};
            if (at + sizeof(T) <= bytes.size())
            {
                std::memcpy(&value, bytes.data() + at, sizeof(T));
            }
            return value;
        }

        [[nodiscard]] std::string Lower(std::string text)
        {
            for (char& c : text)
            {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            return text;
        }

        [[nodiscard]] std::optional<std::vector<std::uint8_t>> Find(
            const std::vector<std::shared_ptr<::MphRead::NativeRuntime::ZipArchive>>& archives,
            const std::string& path)
        {
            const std::string wanted = Lower(path);
            for (const auto& archive : archives)
            {
                for (std::size_t i = 0; archive != nullptr && i < archive->Count(); ++i)
                {
                    if (Lower(archive->FullName(i)) == wanted)
                    {
                        return archive->Read(i);
                    }
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] const std::string* Value(const Q3Entity& entity, const std::string& key)
        {
            for (const auto& [name, value] : entity)
            {
                if (Q3StringEqual{}(name, key))
                {
                    return &value;
                }
            }
            return nullptr;
        }

        [[nodiscard]] Vector3 Origin(const Q3Entity& entity)
        {
            Vector3 origin(0.0F, 0.0F, 0.0F);
            if (const std::string* text = Value(entity, "origin"))
            {
                std::array<float, 3> values{};
                std::size_t found = 0;
                std::size_t start = 0;
                while (found < 3 && start < text->size())
                {
                    const std::size_t end = std::min(text->find(' ', start), text->size());
                    if (end > start && ::MphRead::NativeRuntime::SingleTryParseInvariant(
                            std::string_view(*text).substr(start, end - start), values[found]))
                    {
                        ++found;
                    }
                    start = end + 1;
                }
                if (found == 3)
                {
                    origin = Vector3(values[0], values[1], values[2]);
                }
            }
            return origin;
        }

        // An MD3's first frame (md3.h): each surface's triangles and texture
        // coordinates, its first shader's name.
        [[nodiscard]] std::vector<Q3Md3::Surface> Parse(const std::vector<std::uint8_t>& bytes, Vector3 offset)
        {
            std::vector<Q3Md3::Surface> surfaces;
            if (bytes.size() < 108 || std::memcmp(bytes.data(), "IDP3", 4) != 0)
            {
                return surfaces;
            }
            const std::int32_t count = Read<std::int32_t>(bytes, 84);
            std::size_t at = static_cast<std::size_t>(Read<std::int32_t>(bytes, 100));
            for (std::int32_t s = 0; s < count && at + 108 <= bytes.size(); ++s)
            {
                if (std::memcmp(bytes.data() + at, "IDP3", 4) != 0)
                {
                    break;
                }
                const std::int32_t shaders = Read<std::int32_t>(bytes, at + 76);
                const std::int32_t vertices = Read<std::int32_t>(bytes, at + 80);
                const std::int32_t triangles = Read<std::int32_t>(bytes, at + 84);
                const std::size_t triangleAt = at + static_cast<std::size_t>(Read<std::int32_t>(bytes, at + 88));
                const std::size_t shaderAt = at + static_cast<std::size_t>(Read<std::int32_t>(bytes, at + 92));
                const std::size_t stAt = at + static_cast<std::size_t>(Read<std::int32_t>(bytes, at + 96));
                const std::size_t xyzAt = at + static_cast<std::size_t>(Read<std::int32_t>(bytes, at + 100));
                const std::size_t end = at + static_cast<std::size_t>(Read<std::int32_t>(bytes, at + 104));
                Q3Md3::Surface surface;
                if (shaders > 0 && shaderAt + 64 <= bytes.size())
                {
                    std::string name(reinterpret_cast<const char*>(bytes.data() + shaderAt),
                        strnlen(reinterpret_cast<const char*>(bytes.data() + shaderAt), 64));
                    const std::size_t dot = name.find_last_of('.');
                    const std::size_t slash = name.find_last_of('/');
                    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                    {
                        name.resize(dot);
                    }
                    surface.Shader = Lower(name);
                }
                for (std::int32_t t = 0; t < triangles; ++t)
                {
                    std::array<Vector3, 3> points{};
                    std::array<Vector2, 3> uvs{};
                    bool valid = true;
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        const std::int32_t index = Read<std::int32_t>(bytes, triangleAt + static_cast<std::size_t>(t) * 12 + k * 4);
                        if (index < 0 || index >= vertices)
                        {
                            valid = false;
                            break;
                        }
                        const std::size_t xyz = xyzAt + static_cast<std::size_t>(index) * 8;
                        points[k] = Vector3(
                            Read<std::int16_t>(bytes, xyz) / 64.0F + offset.X,
                            Read<std::int16_t>(bytes, xyz + 2) / 64.0F + offset.Y,
                            Read<std::int16_t>(bytes, xyz + 4) / 64.0F + offset.Z);
                        const std::size_t st = stAt + static_cast<std::size_t>(index) * 8;
                        uvs[k] = Vector2(Read<float>(bytes, st), Read<float>(bytes, st + 4));
                    }
                    if (valid)
                    {
                        surface.Triangles.push_back(points);
                        surface.Texcoords.push_back(uvs);
                    }
                }
                if (!surface.Shader.empty() && !surface.Triangles.empty())
                {
                    surfaces.push_back(std::move(surface));
                }
                if (end <= at)
                {
                    break;
                }
                at = end;
            }
            return surfaces;
        }
    }

    std::vector<Q3Md3::Placed> Q3Md3::Load(const Q3Bsp* bsp, const std::vector<std::string>& archives)
    {
        std::vector<Placed> placed;
        if (bsp == nullptr)
        {
            return placed;
        }
        std::vector<std::shared_ptr<::MphRead::NativeRuntime::ZipArchive>> open;
        for (std::size_t m = 1; m < bsp->Models().size(); ++m)
        {
            const std::int32_t model = static_cast<std::int32_t>(m);
            if (!Q3Movers::IsMover(bsp, model))
            {
                continue;
            }
            const std::string name = "*" + std::to_string(model);
            for (const std::shared_ptr<Q3Entity>& entity : bsp->Entities())
            {
                const std::string* brush = entity == nullptr ? nullptr : Value(*entity, "model");
                const std::string* md3 = brush == nullptr || *brush != name ? nullptr : Value(*entity, "model2");
                if (md3 == nullptr)
                {
                    continue;
                }
                if (open.empty())
                {
                    for (const std::string& path : archives)
                    {
                        try
                        {
                            open.push_back(::MphRead::NativeRuntime::ZipArchive::OpenRead(path));
                        }
                        catch (...)
                        {
                        }
                    }
                }
                if (const auto bytes = Find(open, *md3))
                {
                    // Where Quake draws it: at the entity's origin, which is
                    // where the brush model is too, unless it is stored
                    // about it (then the mover's node puts both there).
                    const Vector3 offset = Q3Movers::IsRelative(bsp, model) ? Vector3(0.0F, 0.0F, 0.0F) : Origin(*entity);
                    Placed one;
                    one.Model = model;
                    one.Surfaces = Parse(*bytes, offset);
                    if (!one.Surfaces.empty())
                    {
                        placed.push_back(std::move(one));
                    }
                }
                break;
            }
        }
        return placed;
    }

    std::vector<std::string> Q3Md3::Shaders(const std::vector<Placed>& placed)
    {
        std::vector<std::string> shaders;
        for (const Placed& one : placed)
        {
            for (const Surface& surface : one.Surfaces)
            {
                if (std::find(shaders.begin(), shaders.end(), surface.Shader) == shaders.end())
                {
                    shaders.push_back(surface.Shader);
                }
            }
        }
        return shaders;
    }
}
