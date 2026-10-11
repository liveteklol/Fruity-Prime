#include "MapTextureBake.hpp"

#include "../../NativeRuntime/Stb/Image.hpp"
#include "../../NativeRuntime/System/ZipArchive.hpp"

#include "Q3Bsp.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../../NativeRuntime/System/Sort.hpp"

using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::FileInfoLength;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathGetExtension;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;

using ::MphRead::NativeRuntime::ManagedSort;
namespace MphRead::Mods::MapGen::MapTextureBakeInterop
{
    // ZipFile.OpenRead and ReFuel.Stb's RGB decode, as MapTextureBake.cs calls
    // them.
    using ZipArchive = ::MphRead::NativeRuntime::ZipArchive;

    struct DecodedImage final
    {
        std::int32_t Width = 0;
        std::int32_t Height = 0;
        std::vector<std::uint8_t> Pixels;
    };

    [[nodiscard]] inline std::shared_ptr<ZipArchive> OpenZipRead(const std::string& path)
    {
        return ZipArchive::OpenRead(path);
    }

    [[nodiscard]] inline std::size_t ZipEntryCount(const ZipArchive& archive)
    {
        return archive.Count();
    }

    [[nodiscard]] inline std::string ZipEntryFullName(
        const ZipArchive& archive, std::size_t index)
    {
        return archive.FullName(index);
    }

    [[nodiscard]] inline std::vector<std::uint8_t> ReadZipEntry(
        const ZipArchive& archive, std::size_t index)
    {
        return archive.Read(index);
    }

    [[nodiscard]] inline DecodedImage LoadRgb(std::span<const std::uint8_t> bytes)
    {
        // LoadImage, not LoadPng: a level's art is JPEG and TGA, and a PNG-only
        // decoder answered every one of them with an empty image, which baked
        // to a single black palette entry -- every converted map drew black.
        const ::MphRead::NativeRuntime::Image image = ::MphRead::NativeRuntime::LoadImage(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()), 3);
        DecodedImage result;
        result.Width = image.Width;
        result.Height = image.Height;
        result.Pixels = image.Pixels;
        return result;
    }
}

namespace
{
    using MphRead::Mods::MapGen::MapTextureBakeInterop::DecodedImage;
    using MphRead::Mods::MapGen::MapTextureBakeInterop::ZipArchive;

    constexpr std::int32_t PaletteSize = 256;
    constexpr std::array<std::string_view, 7> SkySuffixes{
        "_1", "_2", "_ft", "_bk", "_lf", "_rt", "_up"
    };
    constexpr std::array<std::string_view, 4> Extensions{
        ".tga", ".jpg", ".jpeg", ".png"
    };

    [[nodiscard]] std::int32_t ManagedShiftRight(
        std::int32_t value, unsigned count) noexcept
    {
        count &= 31U;
        if (count == 0)
        {
            return value;
        }
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        std::uint32_t shifted = bits >> count;
        if ((bits & 0x80000000U) != 0)
        {
            shifted |= 0xFFFFFFFFU << (32U - count);
        }
        return std::bit_cast<std::int32_t>(shifted);
    }

    [[nodiscard]] std::int32_t ManagedShiftLeft(
        std::int32_t value, unsigned count) noexcept
    {
        const std::uint32_t shifted
            = std::bit_cast<std::uint32_t>(value) << (count & 31U);
        return std::bit_cast<std::int32_t>(shifted);
    }

    [[nodiscard]] std::int32_t ManagedOr(
        std::int32_t left, std::int32_t right) noexcept
    {
        return std::bit_cast<std::int32_t>(
            std::bit_cast<std::uint32_t>(left)
            | std::bit_cast<std::uint32_t>(right));
    }

    [[nodiscard]] std::size_t ArrayLength(std::int32_t value)
    {
        if (value < 0)
        {
            throw System::OverflowException();
        }
        return static_cast<std::size_t>(value);
    }

    [[nodiscard]] bool OrdinalIgnoreCaseEquals(
        const std::string& left, const std::string& right) noexcept
    {
        return MphRead::Mods::MapGen::Q3StringEqual{}(left, right);
    }

    class ArchiveGuard final
    {
    public:
        std::vector<std::shared_ptr<ZipArchive>> Values;

        ~ArchiveGuard()
        {
            for (std::shared_ptr<ZipArchive>& archive : Values)
            {
                archive.reset();
            }
        }

        ArchiveGuard() = default;
        ArchiveGuard(const ArchiveGuard&) = delete;
        ArchiveGuard& operator=(const ArchiveGuard&) = delete;
    };

    struct FileEntry final
    {
        ZipArchive* Archive = nullptr;
        std::size_t Index = 0;
        std::string FullName;
    };

    struct BakedEntry final
    {
        std::int32_t Index = 0;
        std::string Name;
        std::vector<std::uint16_t> Palette;
        std::vector<std::uint8_t> Pixels;
        std::int32_t Size = 0;
    };

    // Quake's own sky box order is rt bk lf ft up dn; this is the order of
    // the axes they face, +X -X +Y -Y +Z -Z in Quake's space (tr_sky.c's
    // sky_texorder applied to its st_to_vec).
    constexpr std::array<std::string_view, 6> SkyBoxSuffixes{
        "rt", "lf", "bk", "ft", "up", "dn"
    };

    [[nodiscard]] std::string Lower(std::string_view value)
    {
        std::string result(value);
        for (char& ch : result)
        {
            if (ch >= 'A' && ch <= 'Z')
            {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        return result;
    }

    // Every file in every archive, first archive first, looked up without
    // regard to case. A list scanned per lookup was fine for one level's .pk3
    // and is quadratic for a game's texture paks, which hold thousands.
    struct FileIndex final
    {
        std::vector<FileEntry> Entries;
        std::unordered_map<std::string, std::size_t> ByName;

        bool Add(FileEntry entry)
        {
            std::string key = Lower(entry.FullName);
            if (ByName.contains(key))
            {
                return false;
            }
            ByName.emplace(std::move(key), Entries.size());
            Entries.push_back(std::move(entry));
            return true;
        }
    };

    [[nodiscard]] const FileEntry* FindFile(
        const FileIndex& files, const std::string& name)
    {
        const auto found = files.ByName.find(Lower(name));
        return found == files.ByName.end()
            ? nullptr
            : std::addressof(files.Entries[found->second]);
    }

    // What a .shader script says a shader draws: the first image any of its
    // stages maps, the editor's picture of it, and for a sky the base name of
    // its six box sides. A Quake level names shaders, not images, and a
    // shader whose image lives under another name -- every light, every jump
    // pad, every sky -- has nothing at its own name to find.
    struct ShaderScript final
    {
        std::string FirstMap;
        std::string EditorImage;
        std::string SkyBase;
    };

    struct ShaderToken final
    {
        std::string Text;
        std::int32_t Line = 0;
    };

    [[nodiscard]] std::vector<ShaderToken> TokenizeShader(std::string_view text)
    {
        std::vector<ShaderToken> tokens;
        std::int32_t line = 0;
        std::size_t i = 0;
        while (i < text.size())
        {
            const char ch = text[i];
            if (ch == '\n')
            {
                ++line;
                ++i;
            }
            else if (ch == ' ' || ch == '\t' || ch == '\r')
            {
                ++i;
            }
            else if (ch == '/' && i + 1 < text.size() && text[i + 1] == '/')
            {
                while (i < text.size() && text[i] != '\n')
                {
                    ++i;
                }
            }
            else if (ch == '/' && i + 1 < text.size() && text[i + 1] == '*')
            {
                i += 2;
                while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
                {
                    if (text[i] == '\n')
                    {
                        ++line;
                    }
                    ++i;
                }
                i += 2;
            }
            else if (ch == '{' || ch == '}')
            {
                tokens.push_back(ShaderToken{std::string(1, ch), line});
                ++i;
            }
            else if (ch == '"')
            {
                const std::size_t start = ++i;
                while (i < text.size() && text[i] != '"' && text[i] != '\n')
                {
                    ++i;
                }
                tokens.push_back(ShaderToken{std::string(text.substr(start, i - start)), line});
                if (i < text.size() && text[i] == '"')
                {
                    ++i;
                }
            }
            else
            {
                const std::size_t start = i;
                while (i < text.size()
                    && text[i] != ' ' && text[i] != '\t' && text[i] != '\r'
                    && text[i] != '\n' && text[i] != '{' && text[i] != '}')
                {
                    ++i;
                }
                tokens.push_back(ShaderToken{std::string(text.substr(start, i - start)), line});
            }
        }
        return tokens;
    }

    // First definition of a name wins, archives in the order given -- the
    // level's own scripts before anything a texture pak brings.
    void ParseShaderScript(
        std::string_view text,
        std::unordered_map<std::string, ShaderScript>& shaders)
    {
        const std::vector<ShaderToken> tokens = TokenizeShader(text);
        std::size_t i = 0;
        const auto argument = [&tokens](std::size_t at, std::size_t offset) -> const std::string*
        {
            const std::size_t index = at + offset;
            if (index < tokens.size()
                && tokens[index].Line == tokens[at].Line
                && tokens[index].Text != "{" && tokens[index].Text != "}")
            {
                return std::addressof(tokens[index].Text);
            }
            return nullptr;
        };
        while (i < tokens.size())
        {
            const std::string name = Lower(tokens[i].Text);
            ++i;
            if (i >= tokens.size() || tokens[i].Text != "{")
            {
                continue;
            }
            ++i;
            ShaderScript script;
            std::int32_t depth = 1;
            while (i < tokens.size() && depth > 0)
            {
                const std::string& token = tokens[i].Text;
                if (token == "{")
                {
                    ++depth;
                }
                else if (token == "}")
                {
                    --depth;
                }
                else
                {
                    const std::string key = Lower(token);
                    if (depth == 1 && key == "qer_editorimage")
                    {
                        if (const std::string* value = argument(i, 1))
                        {
                            script.EditorImage = *value;
                        }
                    }
                    else if (depth == 1 && key == "skyparms")
                    {
                        const std::string* value = argument(i, 1);
                        if (value != nullptr && *value != "-")
                        {
                            script.SkyBase = *value;
                        }
                    }
                    else if (depth == 2 && script.FirstMap.empty()
                        && (key == "map" || key == "clampmap" || key == "animmap"))
                    {
                        const std::string* value = argument(i, key == "animmap" ? 2 : 1);
                        if (value != nullptr && !value->empty()
                            && (*value)[0] != '$' && (*value)[0] != '*')
                        {
                            script.FirstMap = *value;
                        }
                    }
                }
                ++i;
            }
            shaders.try_emplace(name, std::move(script));
        }
    }

    [[nodiscard]] std::vector<std::tuple<std::int32_t, std::string, std::int32_t>>
        UsedTextures(
            const std::shared_ptr<MphRead::Mods::MapGen::Q3Bsp>& bsp,
            bool sky)
    {
        using namespace MphRead::Mods::MapGen;

        if (!bsp)
        {
            throw System::NullReferenceException();
        }

        std::unordered_set<std::int32_t> seen;
        std::vector<std::tuple<std::int32_t, std::string, std::int32_t>> results;
        for (const std::shared_ptr<Q3Face>& faceRef : bsp->Faces())
        {
            if (!faceRef)
            {
                throw System::NullReferenceException();
            }
            const Q3Face& face = *faceRef;
            if (face.Type() != 1 && face.Type() != 2 && face.Type() != 3)
            {
                continue;
            }

            const std::int32_t textureIndex = face.Texture();
            if (!seen.emplace(textureIndex).second)
            {
                continue;
            }

            const Q3Bsp::TextureList& textures = bsp->Textures();
            if (textureIndex < 0
                || static_cast<std::uint64_t>(textureIndex) >= textures.size())
            {
                throw System::ArgumentOutOfRangeException();
            }
            const std::shared_ptr<Q3Texture>& textureRef
                = textures[static_cast<std::size_t>(textureIndex)];
            if (!textureRef)
            {
                throw System::NullReferenceException();
            }
            const Q3Texture& texture = *textureRef;
            if ((texture.Flags()
                & (Q3Bsp::SurfaceNoDraw
                    | Q3Bsp::SurfaceHint
                    | Q3Bsp::SurfaceSkip)) != 0)
            {
                continue;
            }
            if ((texture.Flags() & Q3Bsp::SurfaceSky) != 0 && !sky)
            {
                continue;
            }
            results.emplace_back(textureIndex, texture.Name(), texture.Flags());
        }

        std::sort(results.begin(), results.end(),
            [](const auto& left, const auto& right)
            {
                return std::get<0>(left) < std::get<0>(right);
            });
        return results;
    }

    // An image path out of a script, which may or may not carry the
    // extension of the file that answers it: Quake swaps .tga for .jpg.
    [[nodiscard]] const FileEntry* FindImage(
        const FileIndex& files, const std::string& path)
    {
        std::string stem = path;
        const std::size_t dot = stem.find_last_of('.');
        const std::size_t slash = stem.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        {
            stem.resize(dot);
        }
        for (std::string_view extension : Extensions)
        {
            if (const FileEntry* file = FindFile(files, stem + std::string(extension)))
            {
                return file;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool IsImage(const std::string& lowerName) noexcept
    {
        for (std::string_view extension : Extensions)
        {
            if (lowerName.size() > extension.size()
                && lowerName.ends_with(extension))
            {
                return true;
            }
        }
        return false;
    }

    // A shader nothing answers, painted with the image beside it in the same
    // folder whose name shares the longest start with its own -- the
    // metaltech12final a level drew with, from the metaltech06final a pak
    // has. Only for lit, solid-looking surfaces: a shader with no lightmap is
    // a flare, a sky or a light's glow, and painting one of those opaque is
    // worse than leaving it out. Never from textures/common, whose images are
    // the editor's markers for caulk and clip.
    [[nodiscard]] const FileEntry* Substitute(
        const FileIndex& files, const std::string& name)
    {
        const std::string lower = Lower(name);
        const std::size_t slash = lower.find_last_of('/');
        if (slash == std::string::npos || lower.starts_with("textures/common/"))
        {
            return nullptr;
        }
        const std::string folder = lower.substr(0, slash + 1);
        const std::string stem = lower.substr(slash + 1);
        const FileEntry* best = nullptr;
        std::string bestName;
        std::size_t bestShared = 0;
        for (const auto& [key, index] : files.ByName)
        {
            if (!key.starts_with(folder)
                || key.find('/', folder.size()) != std::string::npos
                || !IsImage(key))
            {
                continue;
            }
            const std::string candidate = key.substr(folder.size());
            std::size_t shared = 0;
            while (shared < candidate.size() && shared < stem.size()
                && candidate[shared] == stem[shared])
            {
                ++shared;
            }
            if (best == nullptr || shared > bestShared
                || (shared == bestShared && key < bestName))
            {
                best = std::addressof(files.Entries[index]);
                bestName = key;
                bestShared = shared;
            }
        }
        return best;
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> Find(
        const FileIndex& files, const std::string& name)
    {
        using namespace MphRead::Mods::MapGen::MapTextureBakeInterop;

        for (std::size_t suffixIndex = 0;
            suffixIndex <= SkySuffixes.size(); ++suffixIndex)
        {
            const std::string_view suffix = suffixIndex == 0
                ? std::string_view()
                : SkySuffixes[suffixIndex - 1];
            for (std::string_view extension : Extensions)
            {
                std::string candidate = name;
                candidate += suffix;
                candidate += extension;
                if (const FileEntry* file = FindFile(files, candidate))
                {
                    return ReadZipEntry(*file->Archive, file->Index);
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::uint8_t AtByte(
        const std::vector<std::uint8_t>& values, std::int32_t index)
    {
        if (index < 0
            || static_cast<std::uint64_t>(index) >= values.size())
        {
            throw System::IndexOutOfRangeException();
        }
        return values[static_cast<std::size_t>(index)];
    }

    void SetByte(
        std::vector<std::uint8_t>& values,
        std::int32_t index,
        std::uint8_t value)
    {
        if (index < 0
            || static_cast<std::uint64_t>(index) >= values.size())
        {
            throw System::IndexOutOfRangeException();
        }
        values[static_cast<std::size_t>(index)] = value;
    }

    [[nodiscard]] std::vector<std::uint8_t> Decode(
        const std::vector<std::uint8_t>& raw, std::int32_t size)
    {
        using namespace MphRead::Mods::MapGen::MapTextureBakeInterop;

        DecodedImage image = LoadRgb(raw);
        const std::int32_t width = image.Width;
        const std::int32_t height = image.Height;
        if (width <= 0 || height <= 0
            || image.Pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U)
        {
            return {};
        }

        const std::int32_t resultLength = UncheckedMultiply(
            UncheckedMultiply(size, size), 3);
        std::vector<std::uint8_t> result(ArrayLength(resultLength));

        for (std::int32_t y = 0; y < size; ++y)
        {
            const std::int32_t y0
                = UncheckedMultiply(y, height) / size;
            const std::int32_t y1 = std::max(
                UncheckedAdd(y0, 1),
                UncheckedMultiply(UncheckedAdd(y, 1), height) / size);
            for (std::int32_t x = 0; x < size; ++x)
            {
                const std::int32_t x0
                    = UncheckedMultiply(x, width) / size;
                const std::int32_t x1 = std::max(
                    UncheckedAdd(x0, 1),
                    UncheckedMultiply(UncheckedAdd(x, 1), width) / size);

                std::int32_t r = 0;
                std::int32_t g = 0;
                std::int32_t b = 0;
                std::int32_t count = 0;
                for (std::int32_t sy = y0;
                    sy < y1 && sy < height; ++sy)
                {
                    for (std::int32_t sx = x0;
                        sx < x1 && sx < width; ++sx)
                    {
                        const std::int32_t offset = UncheckedMultiply(
                            UncheckedAdd(UncheckedMultiply(sy, width), sx), 3);
                        r = UncheckedAdd(r, AtByte(image.Pixels, offset));
                        g = UncheckedAdd(g, AtByte(
                            image.Pixels, UncheckedAdd(offset, 1)));
                        b = UncheckedAdd(b, AtByte(
                            image.Pixels, UncheckedAdd(offset, 2)));
                        count = UncheckedAdd(count, 1);
                    }
                }

                const std::int32_t target = UncheckedMultiply(
                    UncheckedAdd(UncheckedMultiply(y, size), x), 3);
                const std::int32_t divisor = std::max<std::int32_t>(1, count);
                SetByte(result, target,
                    static_cast<std::uint8_t>(r / divisor));
                SetByte(result, UncheckedAdd(target, 1),
                    static_cast<std::uint8_t>(g / divisor));
                SetByte(result, UncheckedAdd(target, 2),
                    static_cast<std::uint8_t>(b / divisor));
            }
        }
        return result;
    }

    struct BoxRange final
    {
        std::int32_t Start = 0;
        std::int32_t Length = 0;
    };

    class PixelComparison final
    {
    public:
        PixelComparison(
            const std::vector<std::uint8_t>& rgb,
            std::int32_t channel) noexcept
            : _rgb(rgb), _channel(channel)
        {
        }

        [[nodiscard]] std::int32_t operator()(
            std::int32_t left, std::int32_t right) const
        {
            const std::int32_t leftOffset = UncheckedAdd(
                UncheckedMultiply(left, 3), _channel);
            const std::int32_t rightOffset = UncheckedAdd(
                UncheckedMultiply(right, 3), _channel);
            const std::int32_t a = AtByte(_rgb, leftOffset);
            const std::int32_t b = AtByte(_rgb, rightOffset);
            return a < b ? -1 : (a > b ? 1 : 0);
        }

    private:
        const std::vector<std::uint8_t>& _rgb;
        std::int32_t _channel;
    };

    void DotNetArraySort(
        std::vector<std::int32_t>& values,
        std::int32_t start,
        std::int32_t length,
        const PixelComparison& compare)
    {
        if (start < 0 || length < 0
            || static_cast<std::uint64_t>(start)
                + static_cast<std::uint64_t>(length)
                > values.size())
        {
            throw std::out_of_range(
                "Offset and length were out of bounds for the array.");
        }
        if (length <= 1)
        {
            return;
        }

        std::span<std::int32_t> keys(
            values.data() + static_cast<std::size_t>(start),
            static_cast<std::size_t>(length));
        ManagedSort(keys, compare);
    }

    [[nodiscard]] std::pair<
        std::vector<std::uint16_t>,
        std::vector<std::uint8_t>>
        Quantize(
            const std::vector<std::uint8_t>& rgb,
            std::int32_t size)
    {
        const std::int32_t count = UncheckedMultiply(size, size);
        std::vector<std::int32_t> indices(ArrayLength(count));
        for (std::int32_t i = 0; i < count; ++i)
        {
            indices[static_cast<std::size_t>(i)] = i;
        }

        std::vector<BoxRange> boxes;
        boxes.push_back(BoxRange{0, count});
        while (boxes.size() < static_cast<std::size_t>(PaletteSize))
        {
            std::int32_t widest = -1;
            std::int32_t widestSpread = 0;
            std::int32_t widestChannel = 0;
            for (std::size_t i = 0; i < boxes.size(); ++i)
            {
                const BoxRange box = boxes[i];
                if (box.Length < 2)
                {
                    continue;
                }
                for (std::int32_t channel = 0; channel < 3; ++channel)
                {
                    std::int32_t low = 255;
                    std::int32_t high = 0;
                    const std::int32_t end = UncheckedAdd(
                        box.Start, box.Length);
                    for (std::int32_t j = box.Start; j < end; ++j)
                    {
                        const std::int32_t index = indices.at(
                            static_cast<std::size_t>(j));
                        const std::int32_t offset = UncheckedAdd(
                            UncheckedMultiply(index, 3), channel);
                        const std::int32_t value = AtByte(rgb, offset);
                        low = std::min(low, value);
                        high = std::max(high, value);
                    }
                    const std::int32_t spread = high - low;
                    if (spread > widestSpread)
                    {
                        widestSpread = spread;
                        widest = static_cast<std::int32_t>(i);
                        widestChannel = channel;
                    }
                }
            }

            if (widest < 0 || widestSpread == 0)
            {
                break;
            }

            const BoxRange selected = boxes.at(
                static_cast<std::size_t>(widest));
            DotNetArraySort(
                indices,
                selected.Start,
                selected.Length,
                PixelComparison(rgb, widestChannel));

            const std::int32_t half = selected.Length / 2;
            boxes[static_cast<std::size_t>(widest)]
                = BoxRange{selected.Start, half};
            boxes.push_back(BoxRange{
                UncheckedAdd(selected.Start, half),
                selected.Length - half
            });
        }

        const std::size_t paletteLength
            = std::max<std::size_t>(1, boxes.size());
        std::vector<std::uint16_t> palette(paletteLength);
        std::vector<std::uint8_t> lookup(ArrayLength(count));

        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            const BoxRange box = boxes[i];
            std::int32_t r = 0;
            std::int32_t g = 0;
            std::int32_t b = 0;
            const std::int32_t end = UncheckedAdd(box.Start, box.Length);
            for (std::int32_t j = box.Start; j < end; ++j)
            {
                const std::int32_t index = indices.at(
                    static_cast<std::size_t>(j));
                const std::int32_t offset = UncheckedMultiply(index, 3);
                r = UncheckedAdd(r, AtByte(rgb, offset));
                g = UncheckedAdd(g, AtByte(rgb, UncheckedAdd(offset, 1)));
                b = UncheckedAdd(b, AtByte(rgb, UncheckedAdd(offset, 2)));
            }
            const std::int32_t divisor
                = std::max<std::int32_t>(1, box.Length);
            r /= divisor;
            g /= divisor;
            b /= divisor;

            const std::int32_t packed = ManagedOr(
                ManagedOr(
                    ManagedShiftLeft(ManagedShiftRight(b, 3), 10),
                    ManagedShiftLeft(ManagedShiftRight(g, 3), 5)),
                ManagedShiftRight(r, 3));
            palette[i] = static_cast<std::uint16_t>(
                std::bit_cast<std::uint32_t>(packed));

            for (std::int32_t j = box.Start; j < end; ++j)
            {
                const std::int32_t index = indices.at(
                    static_cast<std::size_t>(j));
                if (index < 0
                    || static_cast<std::uint64_t>(index) >= lookup.size())
                {
                    throw System::IndexOutOfRangeException();
                }
                lookup[static_cast<std::size_t>(index)]
                    = static_cast<std::uint8_t>(i);
            }
        }
        return {std::move(palette), std::move(lookup)};
    }

    void WriteU16(std::ostream& stream, std::uint16_t value)
    {
        const std::array<char, 2> bytes{
            static_cast<char>(value & 0xFFU),
            static_cast<char>((value >> 8) & 0xFFU)
        };
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    [[nodiscard]] std::string RequirePath(
        const std::optional<std::string>& value)
    {
        if (!value.has_value())
        {
            throw System::ArgumentNullException("path");
        }
        return *value;
    }

    void EnsureOutputDirectory(
        const std::optional<std::string>& outputPath)
    {
        const std::string path = RequirePath(outputPath);
        if (path.empty())
        {
            throw System::ArgumentException();
        }
        if (path.find('\0') != std::string::npos)
        {
            throw System::ArgumentException();
        }

        const std::filesystem::path absolute
            = std::filesystem::absolute(PathFromUtf8(path)).lexically_normal();
        if (absolute == absolute.root_path())
        {
            throw System::ArgumentNullException("path");
        }
        const std::filesystem::path directory = absolute.parent_path();
        if (directory.empty())
        {
            throw System::ArgumentNullException("path");
        }
        std::filesystem::create_directories(directory);
    }

    void WritePack(
        const std::string& outputPath,
        const std::vector<BakedEntry>& entries)
    {
        std::ofstream stream(
            PathFromUtf8(outputPath),
            std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::failbit | std::ios::badbit);

        static constexpr std::array<char, 4> Magic{'F', 'P', 'T', 'X'};
        stream.write(Magic.data(), static_cast<std::streamsize>(Magic.size()));
        WriteU16(stream, 1);
        WriteU16(stream, static_cast<std::uint16_t>(entries.size()));

        for (const BakedEntry& entry : entries)
        {
            const std::string& encoded = entry.Name;
            WriteU16(stream, static_cast<std::uint16_t>(entry.Index));
            WriteU16(stream, static_cast<std::uint16_t>(entry.Size));
            WriteU16(stream, static_cast<std::uint16_t>(entry.Size));
            WriteU16(stream, static_cast<std::uint16_t>(entry.Palette.size()));
            WriteU16(stream, static_cast<std::uint16_t>(encoded.size()));
            stream.write(
                encoded.data(),
                static_cast<std::streamsize>(encoded.size()));
            for (std::uint16_t colour : entry.Palette)
            {
                WriteU16(stream, colour);
            }
            if (!entry.Pixels.empty())
            {
                stream.write(
                    reinterpret_cast<const char*>(entry.Pixels.data()),
                    static_cast<std::streamsize>(entry.Pixels.size()));
            }
        }
        stream.close();
    }

    [[nodiscard]] std::shared_ptr<const std::vector<std::string>>
        EmptyStrings()
    {
        static const auto value
            = std::make_shared<const std::vector<std::string>>();
        return value;
    }
}

namespace MphRead::Mods::MapGen
{
    MapTextureBake::Result::Result()
        : Missing(EmptyStrings()),
          Substituted(EmptyStrings())
    {
    }

    std::shared_ptr<MapTextureBake::Result> MapTextureBake::Bake(
        const std::shared_ptr<Q3Bsp>& bsp,
        const std::shared_ptr<
            const std::vector<std::optional<std::string>>>& archivePaths,
        const std::optional<std::string>& outputPath,
        std::int32_t size,
        bool sky,
        const std::vector<std::pair<std::string, std::string>>* aliases)
    {
        using namespace MapTextureBakeInterop;

        ArchiveGuard archives;
        if (!archivePaths)
        {
            throw System::NullReferenceException();
        }

        for (const std::optional<std::string>& path : *archivePaths)
        {
            if (FileExists(path)
                && !OrdinalIgnoreCaseEquals(
                    PathGetExtension(*path), ".bsp"))
            {
                archives.Values.push_back(OpenZipRead(*path));
            }
        }

        FileIndex files;
        for (const std::shared_ptr<ZipArchive>& archive : archives.Values)
        {
            if (!archive)
            {
                throw System::NullReferenceException();
            }
            const std::size_t count = ZipEntryCount(*archive);
            for (std::size_t i = 0; i < count; ++i)
            {
                (void)files.Add(FileEntry{
                    archive.get(), i, ZipEntryFullName(*archive, i)
                });
            }
        }

        std::unordered_map<std::string, ShaderScript> shaders;
        for (const FileEntry& file : files.Entries)
        {
            const std::string lower = Lower(file.FullName);
            if (lower.starts_with("scripts/") && lower.ends_with(".shader"))
            {
                const std::vector<std::uint8_t> bytes
                    = ReadZipEntry(*file.Archive, file.Index);
                ParseShaderScript(
                    std::string_view(
                        reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                    shaders);
            }
        }

        const auto read = [](const FileEntry* file)
            -> std::optional<std::vector<std::uint8_t>>
        {
            if (file == nullptr)
            {
                return std::nullopt;
            }
            return ReadZipEntry(*file->Archive, file->Index);
        };

        // The order Quake itself looks in: a script defining the name, then
        // an image at the name, with the recipe's own answer ahead of both.
        const auto resolve = [&](const std::string& name)
            -> std::optional<std::vector<std::uint8_t>>
        {
            const auto script = shaders.find(Lower(name));
            if (script != shaders.end() && !script->second.FirstMap.empty())
            {
                if (auto raw = read(FindImage(files, script->second.FirstMap)))
                {
                    return raw;
                }
            }
            if (auto raw = Find(files, name))
            {
                return raw;
            }
            if (script != shaders.end() && !script->second.SkyBase.empty())
            {
                if (auto raw = Find(files, script->second.SkyBase))
                {
                    return raw;
                }
            }
            if (script != shaders.end() && !script->second.EditorImage.empty())
            {
                return read(FindImage(files, script->second.EditorImage));
            }
            return std::nullopt;
        };

        std::vector<BakedEntry> entries;
        auto missing = std::make_shared<std::vector<std::string>>();
        auto substituted = std::make_shared<std::vector<std::string>>();
        for (const auto& [index, name, flags] : UsedTextures(bsp, sky))
        {
            std::optional<std::vector<std::uint8_t>> raw;
            if (aliases != nullptr)
            {
                for (const auto& [from, to] : *aliases)
                {
                    if (OrdinalIgnoreCaseEquals(from, name))
                    {
                        raw = resolve(to);
                        if (!raw.has_value())
                        {
                            raw = read(FindImage(files, to));
                        }
                        break;
                    }
                }
            }
            if (!raw.has_value())
            {
                raw = resolve(name);
            }
            if (!raw.has_value()
                && (flags & (Q3Bsp::SurfaceNoLightmap | Q3Bsp::SurfaceSky)) == 0)
            {
                if (const FileEntry* file = Substitute(files, name))
                {
                    raw = read(file);
                    substituted->push_back(name + " <- " + file->FullName);
                }
            }
            if (!raw.has_value())
            {
                missing->push_back(name);
                continue;
            }

            // An image nothing here can decode is missing, not black.
            const std::vector<std::uint8_t> decoded = Decode(*raw, size);
            if (decoded.empty())
            {
                missing->push_back(name + " (undecodable)");
                continue;
            }
            auto [palette, pixels] = Quantize(decoded, size);
            entries.push_back(BakedEntry{
                index,
                name,
                std::move(palette),
                std::move(pixels),
                size
            });
        }

        // The sky box, as Quake draws it: six images, one a side, at the
        // resolution they were painted at (up to SkyBoxMaxSize). The one
        // image baked above for the sky shader is what a level with no box
        // falls back to; with these six the importer builds the box itself.
        for (const auto& [index, name, flags] : UsedTextures(bsp, sky))
        {
            if ((flags & Q3Bsp::SurfaceSky) == 0)
            {
                continue;
            }
            const auto script = shaders.find(Lower(name));
            if (script == shaders.end() || script->second.SkyBase.empty())
            {
                continue;
            }
            std::vector<BakedEntry> sides;
            for (std::size_t side = 0; side < SkyBoxSuffixes.size(); ++side)
            {
                std::optional<std::vector<std::uint8_t>> raw;
                for (std::string_view extension : Extensions)
                {
                    if (const FileEntry* file = FindFile(files,
                            script->second.SkyBase + "_" + std::string(SkyBoxSuffixes[side])
                                + std::string(extension)))
                    {
                        raw = read(file);
                        break;
                    }
                }
                if (!raw.has_value())
                {
                    break;
                }
                const DecodedImage image = LoadRgb(*raw);
                std::int32_t sideSize = 8;
                while (sideSize * 2 <= std::min(image.Width, image.Height)
                    && sideSize * 2 <= SkyBoxMaxSize)
                {
                    sideSize *= 2;
                }
                const std::vector<std::uint8_t> decoded = Decode(*raw, sideSize);
                if (decoded.empty())
                {
                    break;
                }
                auto [palette, pixels] = Quantize(decoded, sideSize);
                sides.push_back(BakedEntry{
                    SkyBoxSourceIndex + static_cast<std::int32_t>(side),
                    name + "#" + std::string(SkyBoxSuffixes[side]),
                    std::move(palette),
                    std::move(pixels),
                    sideSize
                });
            }
            if (sides.size() == SkyBoxSuffixes.size())
            {
                for (BakedEntry& entry : sides)
                {
                    entries.push_back(std::move(entry));
                }
            }
            break;
        }

        EnsureOutputDirectory(outputPath);
        const std::string path = RequirePath(outputPath);
        WritePack(path, entries);

        auto result = std::make_shared<Result>();
        result->Baked = static_cast<std::int32_t>(entries.size());
        result->Missing = missing;
        result->Substituted = substituted;
        result->Bytes = FileInfoLength(path);
        return result;
    }
}
