#include "MapTextureBake.hpp"
#include "MphTextures.hpp"

#include "../../NativeRuntime/Stb/Image.hpp"
#include "../../NativeRuntime/System/ZipArchive.hpp"

#include "Q3Bsp.hpp"
#include "ShaderAnim.hpp"
#include "MapTexturePack.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Number.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
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
using ::MphRead::NativeRuntime::RequireReference;
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
    // How much brighter a lit level texture is baked (Q3Import::Light).
    constexpr std::int32_t Overbright = 2;
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
        std::int32_t Width = 0;
        std::int32_t Height = 0;
        MphRead::Mods::MapGen::MapTexturePack::Entry::PixelFormat Format
            = MphRead::Mods::MapGen::MapTexturePack::Entry::PixelFormat::Palette8;
        std::string Meta;
    };

    // Quake's own rule for an image's size (R_CreateImage with
    // r_roundImagesDown): each side down to a power of two, here no larger
    // than NativeMaxSize and no smaller than 8.
    [[nodiscard]] std::int32_t NativeSide(std::int32_t side) noexcept
    {
        std::int32_t result = 8;
        while (result * 2 <= side
            && result * 2 <= MphRead::Mods::MapGen::MapTextureBake::NativeMaxSize)
        {
            result *= 2;
        }
        return result;
    }

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
        // skyparms' cloud height: Quake's 512 when it says "-".
        float CloudHeight = 512.0F;
        // cull none (or disable, twosided).
        bool TwoSided = false;
        // fogparms: red, green, blue and the depth it is opaque at.
        std::optional<std::array<float, 4>> Fog;
        // Its stages' images, in the order drawn, each with whether it is
        // laid over what is under it by its alpha (blendFunc blend) -- the
        // fire behind a grille, the texture with holes over it.
        struct Stage
        {
            std::string Map;
            bool Alpha = false;
            bool Opaque = false;
            // map $lightmap: where Quake lays the level's light, which
            // decides what of the rest it falls on.
            bool Lightmap = false;
            // blendFunc, as its two factors in lower case ("" for none).
            std::string Blend;
            bool Clamp = false;
            // animMap: its images in turn, so many a second.
            std::vector<std::string> Frames;
            float FrameRate = 0.0F;
            std::vector<MphRead::Mods::MapGen::ShaderAnim::TcMod> TcMods;
            std::optional<MphRead::Mods::MapGen::ShaderAnim::Wave> Fade;
        };
        std::vector<Stage> Stages;
    };

    // How a stage is laid over what is under it.
    enum class StageBlend
    {
        Opaque,
        Filter,
        Add,
        Alpha,
        Other,
    };

    [[nodiscard]] StageBlend BlendOf(const ShaderScript::Stage& stage)
    {
        const std::string& b = stage.Blend;
        if (b.empty() || b == "gl_one gl_zero")
        {
            return StageBlend::Opaque;
        }
        if (b == "gl_dst_color gl_zero" || b == "gl_zero gl_src_color")
        {
            return StageBlend::Filter;
        }
        if (b == "gl_one gl_one")
        {
            return StageBlend::Add;
        }
        if (b == "gl_src_alpha gl_one_minus_src_alpha")
        {
            return StageBlend::Alpha;
        }
        return StageBlend::Other;
    }

    [[nodiscard]] bool Moves(const ShaderScript::Stage& stage)
    {
        if (stage.Fade.has_value() || stage.Frames.size() > 1)
        {
            return true;
        }
        for (const auto& mod : stage.TcMods)
        {
            if (mod.Kind == "scroll" || mod.Kind == "rotate" || mod.Kind == "stretch" || mod.Kind == "turb")
            {
                return true;
            }
        }
        return false;
    }

    // What of a shader is drawn how: its base image (the stage the light
    // falls on, or for a shader with none -- a flame, a beam -- its first,
    // laid on by brightness) and the stages laid over it, each with whether
    // the light falls on it too.
    struct ShaderPlan
    {
        std::int32_t Base = -1;
        bool Translucent = false;
        std::vector<std::pair<std::int32_t, bool>> Layers;
        bool Animated = false;
    };

    [[nodiscard]] ShaderPlan MakePlan(const ShaderScript& script)
    {
        ShaderPlan plan;
        const auto& stages = script.Stages;
        for (std::size_t s = 0; s < stages.size(); ++s)
        {
            const StageBlend blend = BlendOf(stages[s]);
            if (!stages[s].Lightmap && (blend == StageBlend::Opaque || blend == StageBlend::Filter))
            {
                plan.Base = static_cast<std::int32_t>(s);
                break;
            }
        }
        if (plan.Base < 0)
        {
            for (std::size_t s = 0; s < stages.size(); ++s)
            {
                if (!stages[s].Lightmap)
                {
                    if (BlendOf(stages[s]) == StageBlend::Add)
                    {
                        plan.Base = static_cast<std::int32_t>(s);
                        plan.Translucent = true;
                    }
                    break;
                }
            }
        }
        if (plan.Base < 0)
        {
            return plan;
        }
        plan.Animated = Moves(stages[static_cast<std::size_t>(plan.Base)]);
        if (plan.Translucent)
        {
            return plan;
        }
        for (std::size_t s = static_cast<std::size_t>(plan.Base) + 1; s < stages.size(); ++s)
        {
            const StageBlend blend = BlendOf(stages[s]);
            if (stages[s].Lightmap || (blend != StageBlend::Add && blend != StageBlend::Alpha))
            {
                continue;
            }
            bool lit = false;
            for (std::size_t after = s + 1; after < stages.size(); ++after)
            {
                lit = lit || stages[after].Lightmap || BlendOf(stages[after]) == StageBlend::Filter;
            }
            plan.Layers.emplace_back(static_cast<std::int32_t>(s), lit);
            plan.Animated = plan.Animated || Moves(stages[s]);
        }
        return plan;
    }

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
            ShaderScript::Stage stage;
            bool blended = false;
            while (i < tokens.size() && depth > 0)
            {
                const std::string& token = tokens[i].Text;
                if (token == "{")
                {
                    ++depth;
                    if (depth == 2)
                    {
                        stage = ShaderScript::Stage{};
                        blended = false;
                    }
                }
                else if (token == "}")
                {
                    if (depth == 2 && (!stage.Map.empty() || stage.Lightmap))
                    {
                        stage.Opaque = !blended && !stage.Lightmap;
                        script.Stages.push_back(stage);
                    }
                    --depth;
                }
                else if (depth == 2 && Lower(token) == "blendfunc")
                {
                    blended = true;
                    std::string mode;
                    for (std::size_t k = i + 1; k < tokens.size() && tokens[k].Line == tokens[i].Line; ++k)
                    {
                        mode += Lower(tokens[k].Text) + " ";
                    }
                    if (mode == "gl_one gl_zero ")
                    {
                        blended = false;
                    }
                    stage.Alpha = mode == "blend " || mode == "gl_src_alpha gl_one_minus_src_alpha ";
                    std::string normal = mode.empty() ? mode : mode.substr(0, mode.size() - 1);
                    if (normal == "blend") normal = "gl_src_alpha gl_one_minus_src_alpha";
                    else if (normal == "add") normal = "gl_one gl_one";
                    else if (normal == "filter") normal = "gl_dst_color gl_zero";
                    stage.Blend = normal;
                }
                else if (depth == 2 && (Lower(token) == "tcmod" || Lower(token) == "rgbgen" || Lower(token) == "alphagen"))
                {
                    const std::string key = Lower(token);
                    std::vector<std::string> args;
                    for (std::size_t k = i + 1; k < tokens.size() && tokens[k].Line == tokens[i].Line
                        && tokens[k].Text != "{" && tokens[k].Text != "}"; ++k)
                    {
                        args.push_back(Lower(tokens[k].Text));
                    }
                    const auto number = [&args](std::size_t at)
                    {
                        float value = 0.0F;
                        if (at < args.size())
                        {
                            (void)::MphRead::NativeRuntime::SingleTryParseInvariant(args[at], value);
                        }
                        return value;
                    };
                    const auto wave = [&](std::size_t at)
                    {
                        MphRead::Mods::MapGen::ShaderAnim::Wave result;
                        result.Function = at < args.size() ? args[at] : "sin";
                        result.Base = number(at + 1);
                        result.Amplitude = number(at + 2);
                        result.Phase = number(at + 3);
                        result.Frequency = number(at + 4);
                        return result;
                    };
                    if (key == "tcmod" && !args.empty())
                    {
                        MphRead::Mods::MapGen::ShaderAnim::TcMod mod;
                        mod.Kind = args[0];
                        if (mod.Kind == "stretch")
                        {
                            mod.Wave = wave(1);
                        }
                        else if (mod.Kind == "turb" && args.size() > 1 && !args[1].empty()
                            && (args[1][0] < '0' || args[1][0] > '9') && args[1][0] != '.' && args[1][0] != '-')
                        {
                            // Written by some with a waveform first, which
                            // Quake does not read; its numbers follow.
                            for (std::size_t v = 0; v < 4; ++v) mod.Values[v] = number(v + 2);
                        }
                        else
                        {
                            for (std::size_t v = 0; v < mod.Values.size(); ++v) mod.Values[v] = number(v + 1);
                        }
                        if (mod.Kind == "scroll" || mod.Kind == "scale" || mod.Kind == "rotate"
                            || mod.Kind == "stretch" || mod.Kind == "turb" || mod.Kind == "transform")
                        {
                            stage.TcMods.push_back(std::move(mod));
                        }
                    }
                    else if (!args.empty() && args[0] == "wave")
                    {
                        stage.Fade = wave(1);
                    }
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
                    else if (depth == 1 && key == "fogparms")
                    {
                        // fogparms ( r g b ) distance, the brackets optional
                        // and spaced as anybody pleased.
                        std::array<float, 4> values{};
                        std::size_t found = 0;
                        for (std::size_t k = i + 1; k < tokens.size() && found < 4
                            && tokens[k].Line == tokens[i].Line; ++k)
                        {
                            std::string text = tokens[k].Text;
                            text.erase(std::remove(text.begin(), text.end(), '('), text.end());
                            text.erase(std::remove(text.begin(), text.end(), ')'), text.end());
                            float value = 0.0F;
                            if (!text.empty() && ::MphRead::NativeRuntime::SingleTryParseInvariant(text, value))
                            {
                                values[found++] = value;
                            }
                        }
                        if (found == 4)
                        {
                            script.Fog = values;
                        }
                    }
                    else if (depth == 1 && key == "cull")
                    {
                        const std::string* value = argument(i, 1);
                        script.TwoSided = value != nullptr
                            && (Lower(*value) == "none" || Lower(*value) == "disable" || Lower(*value) == "twosided");
                    }
                    else if (depth == 1 && key == "skyparms")
                    {
                        const std::string* value = argument(i, 1);
                        if (value != nullptr && *value != "-")
                        {
                            script.SkyBase = *value;
                        }
                        if (const std::string* height = argument(i, 2); height != nullptr)
                        {
                            const float cloud = static_cast<float>(std::atof(height->c_str()));
                            script.CloudHeight = cloud > 0.0F ? cloud : 512.0F;
                        }
                    }
                    else if (depth == 2
                        && (key == "map" || key == "clampmap" || key == "animmap"))
                    {
                        if (const std::string* first = argument(i, 1); first != nullptr && Lower(*first) == "$lightmap")
                        {
                            stage.Lightmap = true;
                        }
                        stage.Clamp = key == "clampmap";
                        if (key == "animmap")
                        {
                            if (const std::string* rate = argument(i, 1))
                            {
                                (void)::MphRead::NativeRuntime::SingleTryParseInvariant(*rate, stage.FrameRate);
                            }
                            for (std::size_t k = 2; const std::string* frame = argument(i, k); ++k)
                            {
                                stage.Frames.push_back(*frame);
                            }
                        }
                        const std::string* value = argument(i, key == "animmap" ? 2 : 1);
                        if (value != nullptr && !value->empty()
                            && (*value)[0] != '$' && (*value)[0] != '*')
                        {
                            if (script.FirstMap.empty())
                            {
                                script.FirstMap = *value;
                            }
                            stage.Map = *value;
                        }
                    }
                }
                ++i;
            }
            shaders.try_emplace(name, std::move(script));
        }
    }

    // A shader's base image with the stages laid over it (by their alpha,
    // or added, as Quake adds a glow) as one image, the way it looks
    // standing still -- fire behind a carved grille, a light's glow over
    // its fitting -- at the size of the topmost. A TGA, for the decoder that
    // reads the rest.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> Compose(
        const ShaderScript& script,
        std::int32_t base,
        const std::vector<std::int32_t>& overs,
        const std::function<std::optional<std::vector<std::uint8_t>>(const std::string&)>& read)
    {
        if (base < 0 || overs.empty())
        {
            return std::nullopt;
        }
        const auto load = [&](const std::string& map) -> std::optional<::MphRead::NativeRuntime::Image>
        {
            const auto raw = read(map);
            if (!raw)
            {
                return std::nullopt;
            }
            ::MphRead::NativeRuntime::Image image = ::MphRead::NativeRuntime::LoadImage(*raw, 4);
            if (image.Width <= 0 || image.Height <= 0
                || image.Pixels.size() < static_cast<std::size_t>(image.Width) * image.Height * 4)
            {
                return std::nullopt;
            }
            return image;
        };
        const auto under = load(script.Stages[static_cast<std::size_t>(base)].Map);
        const auto top = load(script.Stages[static_cast<std::size_t>(overs.back())].Map);
        if (!under || !top)
        {
            return std::nullopt;
        }
        const std::int32_t width = std::max(top->Width, under->Width);
        const std::int32_t height = std::max(top->Height, under->Height);
        std::vector<float> rgb(static_cast<std::size_t>(width) * height * 3);
        const auto sample = [](const ::MphRead::NativeRuntime::Image& image, std::int32_t x, std::int32_t y,
                                std::int32_t width, std::int32_t height)
        {
            const std::int32_t sx = std::min(image.Width - 1, x * image.Width / width);
            const std::int32_t sy = std::min(image.Height - 1, y * image.Height / height);
            return &image.Pixels[(static_cast<std::size_t>(sy) * image.Width + sx) * 4];
        };
        for (std::int32_t y = 0; y < height; ++y)
        {
            for (std::int32_t x = 0; x < width; ++x)
            {
                const std::uint8_t* p = sample(*under, x, y, width, height);
                for (std::size_t c = 0; c < 3; ++c)
                {
                    rgb[(static_cast<std::size_t>(y) * width + x) * 3 + c] = p[c];
                }
            }
        }
        for (const std::int32_t s : overs)
        {
            const ShaderScript::Stage& stage = script.Stages[static_cast<std::size_t>(s)];
            const auto layer = s == overs.back() ? top : load(stage.Map);
            if (!layer)
            {
                continue;
            }
            const bool add = BlendOf(stage) == StageBlend::Add;
            for (std::int32_t y = 0; y < height; ++y)
            {
                for (std::int32_t x = 0; x < width; ++x)
                {
                    const std::uint8_t* p = sample(*layer, x, y, width, height);
                    const float a = static_cast<float>(p[3]) / 255.0F;
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        float& out = rgb[(static_cast<std::size_t>(y) * width + x) * 3 + c];
                        out = add ? std::min(255.0F, out + static_cast<float>(p[c]))
                                  : out * (1.0F - a) + static_cast<float>(p[c]) * a;
                    }
                }
            }
        }
        std::vector<std::uint8_t> tga(18, 0);
        tga[2] = 2;
        tga[12] = static_cast<std::uint8_t>(width & 0xFF);
        tga[13] = static_cast<std::uint8_t>(width >> 8);
        tga[14] = static_cast<std::uint8_t>(height & 0xFF);
        tga[15] = static_cast<std::uint8_t>(height >> 8);
        tga[16] = 24;
        tga[17] = 0x20;
        tga.reserve(tga.size() + rgb.size());
        for (std::size_t p = 0; p < rgb.size(); p += 3)
        {
            for (const std::size_t c : {2U, 1U, 0U})
            {
                tga.push_back(static_cast<std::uint8_t>(std::clamp(std::lround(rgb[p + c]), 0L, 255L)));
            }
        }
        return tga;
    }

    // The same, for a shader that does not move: its base and every stage
    // laid over it. Nothing for a shader of one image.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> Compose(
        const ShaderScript& script,
        const std::function<std::optional<std::vector<std::uint8_t>>(const std::string&)>& read)
    {
        const ShaderPlan plan = MakePlan(script);
        if (plan.Base < 0 || plan.Translucent)
        {
            return std::nullopt;
        }
        std::vector<std::int32_t> overs;
        for (const auto& [stage, lit] : plan.Layers)
        {
            overs.push_back(stage);
        }
        return Compose(script, plan.Base, overs, read);
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

    [[nodiscard]] std::vector<std::uint8_t> Resample(
        const MphRead::Mods::MapGen::MapTextureBakeInterop::DecodedImage& image,
        std::int32_t outWidth, std::int32_t outHeight)
    {
        using namespace MphRead::Mods::MapGen::MapTextureBakeInterop;

        const std::int32_t width = image.Width;
        const std::int32_t height = image.Height;
        if (width <= 0 || height <= 0
            || image.Pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U)
        {
            return {};
        }

        const std::int32_t resultLength = UncheckedMultiply(
            UncheckedMultiply(outWidth, outHeight), 3);
        std::vector<std::uint8_t> result(ArrayLength(resultLength));

        for (std::int32_t y = 0; y < outHeight; ++y)
        {
            const std::int32_t y0
                = UncheckedMultiply(y, height) / outHeight;
            const std::int32_t y1 = std::max(
                UncheckedAdd(y0, 1),
                UncheckedMultiply(UncheckedAdd(y, 1), height) / outHeight);
            for (std::int32_t x = 0; x < outWidth; ++x)
            {
                const std::int32_t x0
                    = UncheckedMultiply(x, width) / outWidth;
                const std::int32_t x1 = std::max(
                    UncheckedAdd(x0, 1),
                    UncheckedMultiply(UncheckedAdd(x, 1), width) / outWidth);

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
                    UncheckedAdd(UncheckedMultiply(y, outWidth), x), 3);
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

    [[nodiscard]] std::vector<std::uint8_t> Decode(
        const std::vector<std::uint8_t>& raw, std::int32_t outWidth, std::int32_t outHeight)
    {
        return Resample(
            MphRead::Mods::MapGen::MapTextureBakeInterop::LoadRgb(raw), outWidth, outHeight);
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
            std::int32_t count,
            std::int32_t colours = PaletteSize)
    {
        std::vector<std::int32_t> indices(ArrayLength(count));
        for (std::int32_t i = 0; i < count; ++i)
        {
            indices[static_cast<std::size_t>(i)] = i;
        }

        std::vector<BoxRange> boxes;
        boxes.push_back(BoxRange{0, count});
        while (boxes.size() < static_cast<std::size_t>(colours))
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
        WriteU16(stream, 2);
        WriteU16(stream, static_cast<std::uint16_t>(entries.size()));

        for (const BakedEntry& entry : entries)
        {
            const std::string& encoded = entry.Name;
            WriteU16(stream, static_cast<std::uint16_t>(entry.Index));
            WriteU16(stream, static_cast<std::uint16_t>(entry.Width));
            WriteU16(stream, static_cast<std::uint16_t>(entry.Height));
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
            stream.put(static_cast<char>(entry.Format));
            WriteU16(stream, static_cast<std::uint16_t>(entry.Meta.size()));
            stream.write(entry.Meta.data(), static_cast<std::streamsize>(entry.Meta.size()));
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

    std::unordered_map<std::string, std::array<float, 4>> MapTextureBake::FogParms(
        const std::vector<std::string>& archivePaths)
    {
        using namespace MapTextureBakeInterop;
        std::unordered_map<std::string, std::array<float, 4>> result;
        for (const std::string& path : archivePaths)
        {
            if (!FileExists(path) || OrdinalIgnoreCaseEquals(PathGetExtension(path), ".bsp"))
            {
                continue;
            }
            std::shared_ptr<ZipArchive> archive = OpenZipRead(path);
            if (!archive)
            {
                continue;
            }
            std::unordered_map<std::string, ShaderScript> shaders;
            const std::size_t count = ZipEntryCount(*archive);
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::string lower = Lower(ZipEntryFullName(*archive, i));
                if (lower.starts_with("scripts/") && lower.ends_with(".shader"))
                {
                    const std::vector<std::uint8_t> bytes = ReadZipEntry(*archive, i);
                    ParseShaderScript(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), shaders);
                }
            }
            for (const auto& [name, script] : shaders)
            {
                if (script.Fog.has_value())
                {
                    result.try_emplace(name, *script.Fog);
                }
            }
        }
        return result;
    }

    std::shared_ptr<MapTextureBake::Result> MapTextureBake::Bake(
        const std::shared_ptr<Q3Bsp>& bsp,
        const std::shared_ptr<
            const std::vector<std::optional<std::string>>>& archivePaths,
        const std::optional<std::string>& outputPath,
        std::int32_t size,
        bool sky,
        const std::vector<std::pair<std::string, std::string>>* aliases,
        const std::vector<std::string>* portalImages,
        const std::vector<std::string>* modelShaders)
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
            if (script != shaders.end())
            {
                if (auto composed = Compose(script->second, [&](const std::string& map)
                        { return read(FindImage(files, map)); }))
                {
                    return composed;
                }
            }
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

        // A shader that moves (ShaderAnim): its base image, each stage laid
        // over it that moves or that a moving base would carry away, and an
        // animMap's frames, each an entry saying what it is. Its stages
        // that stand still over a still base are baked into the base, as
        // any other shader's are.
        namespace SA = MphRead::Mods::MapGen::ShaderAnim;
        using PixelFormat = MapTexturePack::Entry::PixelFormat;
        std::int32_t nextSource = AnimatedSourceIndex;
        const auto loadRgba = [&](const std::string& map) -> std::optional<::MphRead::NativeRuntime::Image>
        {
            const auto raw = read(FindImage(files, map));
            if (!raw)
            {
                return std::nullopt;
            }
            ::MphRead::NativeRuntime::Image image = ::MphRead::NativeRuntime::LoadImage(*raw, 4);
            if (image.Width <= 0 || image.Height <= 0
                || image.Pixels.size() < static_cast<std::size_t>(image.Width) * image.Height * 4)
            {
                return std::nullopt;
            }
            return image;
        };
        // An image in one of the pack's formats. Added light (A5I3) is drawn
        // as see-through as it is dark, its colour at full strength, which
        // over anything darker than it is what adding it gives.
        const auto encode = [](const ::MphRead::NativeRuntime::Image& image, std::int32_t width, std::int32_t height,
                                PixelFormat format, bool overbright, BakedEntry& entry)
        {
            const std::size_t count = static_cast<std::size_t>(width) * height;
            std::vector<std::uint8_t> rgb(count * 3);
            std::vector<std::uint8_t> alpha(count);
            for (std::int32_t y = 0; y < height; ++y)
            {
                const std::int32_t y0 = y * image.Height / height;
                const std::int32_t y1 = std::max(y0 + 1, (y + 1) * image.Height / height);
                for (std::int32_t x = 0; x < width; ++x)
                {
                    const std::int32_t x0 = x * image.Width / width;
                    const std::int32_t x1 = std::max(x0 + 1, (x + 1) * image.Width / width);
                    std::array<std::int32_t, 4> sum{};
                    std::int32_t n = 0;
                    for (std::int32_t sy = y0; sy < y1 && sy < image.Height; ++sy)
                    {
                        for (std::int32_t sx = x0; sx < x1 && sx < image.Width; ++sx)
                        {
                            const std::uint8_t* p = &image.Pixels[(static_cast<std::size_t>(sy) * image.Width + sx) * 4];
                            for (std::size_t c = 0; c < 4; ++c)
                            {
                                sum[c] += p[c];
                            }
                            ++n;
                        }
                    }
                    n = std::max(n, 1);
                    const std::size_t at = static_cast<std::size_t>(y) * width + x;
                    std::array<std::int32_t, 3> colour{sum[0] / n, sum[1] / n, sum[2] / n};
                    std::int32_t a = sum[3] / n;
                    if (format == PixelFormat::A5I3)
                    {
                        a = std::max(colour[0], std::max(colour[1], colour[2]));
                        for (std::int32_t& c : colour)
                        {
                            c = a > 0 ? std::min(255, c * 255 / a) : 0;
                        }
                    }
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        rgb[at * 3 + c] = static_cast<std::uint8_t>(std::min(255, colour[c] * (overbright ? Overbright : 1)));
                    }
                    alpha[at] = static_cast<std::uint8_t>(a);
                }
            }
            const std::int32_t colours = format == PixelFormat::Palette8 ? PaletteSize
                : (format == PixelFormat::A5I3 ? 8 : 32);
            auto [palette, indices] = Quantize(rgb, static_cast<std::int32_t>(count), colours);
            palette.resize(static_cast<std::size_t>(colours), 0);
            for (std::size_t p = 0; p < count; ++p)
            {
                if (format == PixelFormat::A5I3)
                {
                    indices[p] = static_cast<std::uint8_t>(((alpha[p] * 31 + 127) / 255) << 3 | (indices[p] & 7));
                }
                else if (format == PixelFormat::A3I5)
                {
                    indices[p] = static_cast<std::uint8_t>(((alpha[p] * 7 + 127) / 255) << 5 | (indices[p] & 31));
                }
            }
            entry.Width = width;
            entry.Height = height;
            entry.Palette = std::move(palette);
            entry.Pixels = std::move(indices);
            entry.Format = format;
        };
        // A stage's motion, its rates rounded to whole cycles of the room's
        // loop.
        const auto describe = [](const ShaderScript::Stage& stage, SA::EntryMeta& meta)
        {
            meta.Clamp = stage.Clamp;
            meta.TcMods = stage.TcMods;
            for (SA::TcMod& mod : meta.TcMods)
            {
                if (mod.Kind == "scroll")
                {
                    mod.Values[0] = SA::Looped(mod.Values[0]);
                    mod.Values[1] = SA::Looped(mod.Values[1]);
                }
                else if (mod.Kind == "rotate")
                {
                    mod.Values[0] = SA::Looped(mod.Values[0], 360.0F);
                }
                else if (mod.Kind == "turb")
                {
                    mod.Values[3] = SA::Looped(mod.Values[3]);
                }
                else if (mod.Kind == "stretch" && mod.Wave.has_value())
                {
                    mod.Wave->Frequency = SA::Looped(mod.Wave->Frequency);
                }
            }
            meta.Fade = stage.Fade;
            if (meta.Fade.has_value())
            {
                meta.Fade->Frequency = SA::Looped(meta.Fade->Frequency);
            }
            if (stage.Frames.size() > 1 && stage.FrameRate > 0.0F)
            {
                const float frames = static_cast<float>(stage.Frames.size());
                meta.FrameCount = static_cast<std::int32_t>(stage.Frames.size());
                meta.FrameRate = SA::Looped(stage.FrameRate / frames) * frames;
                if (meta.FrameRate <= 0.0F)
                {
                    meta.FrameCount = 0;
                }
            }
        };
        // An entry and the frames of its animMap after it.
        const auto addAnimated = [&](BakedEntry entry, const ShaderScript::Stage& stage,
                                     SA::EntryMeta meta, bool overbright, bool fades)
        {
            describe(stage, meta);
            if (!fades)
            {
                meta.Fade.reset();
            }
            entry.Meta = SA::Encode(meta);
            const std::int32_t owner = entry.Index;
            const std::int32_t width = entry.Width;
            const std::int32_t height = entry.Height;
            const PixelFormat format = entry.Format;
            const std::string name = entry.Name;
            entries.push_back(std::move(entry));
            for (std::int32_t k = 0; k < meta.FrameCount; ++k)
            {
                const auto image = loadRgba(stage.Frames[static_cast<std::size_t>(k)]);
                if (!image)
                {
                    continue;
                }
                BakedEntry frame;
                frame.Index = nextSource++;
                frame.Name = name + "#" + std::to_string(k);
                encode(*image, width, height, format, overbright, frame);
                SA::EntryMeta frameMeta;
                frameMeta.Role = "frame";
                frameMeta.Of = owner;
                frameMeta.Frame = k;
                frame.Meta = SA::Encode(frameMeta);
                entries.push_back(std::move(frame));
            }
        };
        const auto bakeAnimated = [&](std::int32_t index, const std::string& name, std::int32_t flags) -> bool
        {
            std::string lookup = name;
            if (aliases != nullptr)
            {
                for (const auto& [from, to] : *aliases)
                {
                    if (OrdinalIgnoreCaseEquals(from, name))
                    {
                        lookup = to;
                        break;
                    }
                }
            }
            const auto found = shaders.find(Lower(lookup));
            if (found == shaders.end())
            {
                return false;
            }
            const ShaderScript& script = found->second;
            const ShaderPlan plan = MakePlan(script);
            // A sky drawn by its box (six images) does not move here; one
            // without a box is Quake's cloud layers, scrolling over a dome
            // (Q3Import::AddCloudBox).
            const bool sky = (flags & Q3Bsp::SurfaceSky) != 0;
            if (sky)
            {
                bool box = false;
                for (std::string_view extension : Extensions)
                {
                    box = box || (!script.SkyBase.empty()
                        && FindFile(files, script.SkyBase + "_" + std::string(SkyBoxSuffixes[0]) + std::string(extension)) != nullptr);
                }
                if (box || !plan.Animated || plan.Translucent)
                {
                    return false;
                }
            }
            // A glow added after the lightmap (base x lightmap + glow): Quake
            // never darkens it, so it is not baked into the lit base but laid
            // over it unlit -- the bright strips of a light fitting.
            const auto glowsUnlit = [&](std::int32_t stage, bool lit)
            {
                return !lit && (flags & Q3Bsp::SurfaceNoLightmap) == 0
                    && BlendOf(script.Stages[static_cast<std::size_t>(stage)]) == StageBlend::Add;
            };
            const bool glow = !plan.Translucent && std::any_of(plan.Layers.begin(), plan.Layers.end(),
                [&](const auto& layer) { return glowsUnlit(layer.first, layer.second); });
            if (plan.Base < 0 || (!plan.Animated && !glow))
            {
                return false;
            }
            const ShaderScript::Stage& baseStage = script.Stages[static_cast<std::size_t>(plan.Base)];
            const bool baseMoves = Moves(baseStage);
            std::vector<std::int32_t> still;
            std::vector<std::pair<std::int32_t, bool>> moving;
            for (const auto& [stage, lit] : plan.Layers)
            {
                if (!baseMoves && !Moves(script.Stages[static_cast<std::size_t>(stage)])
                    && !glowsUnlit(stage, lit))
                {
                    still.push_back(stage);
                }
                else
                {
                    moving.emplace_back(stage, lit);
                }
            }
            std::optional<::MphRead::NativeRuntime::Image> baseImage;
            if (!still.empty())
            {
                if (const auto composed = Compose(script, plan.Base, still,
                        [&](const std::string& map) { return read(FindImage(files, map)); }))
                {
                    ::MphRead::NativeRuntime::Image image = ::MphRead::NativeRuntime::LoadImage(*composed, 4);
                    if (image.Width > 0 && image.Height > 0)
                    {
                        baseImage = std::move(image);
                    }
                }
            }
            if (!baseImage)
            {
                baseImage = loadRgba(baseStage.Frames.empty() ? baseStage.Map : baseStage.Frames.front());
            }
            if (!baseImage)
            {
                return false;
            }

            const bool unlit = (flags & Q3Bsp::SurfaceNoLightmap) != 0;
            BakedEntry base;
            base.Index = index;
            base.Name = name;
            SA::EntryMeta baseMeta;
            baseMeta.Of = index;
            baseMeta.TwoSided = script.TwoSided;
            if (sky)
            {
                baseMeta.CloudHeight = script.CloudHeight;
            }
            if (plan.Translucent)
            {
                encode(*baseImage, NativeSide(baseImage->Width), NativeSide(baseImage->Height),
                    PixelFormat::A5I3, false, base);
                baseMeta.Blend = "add";
                baseMeta.Lit = false;
            }
            else
            {
                encode(*baseImage, NativeSide(baseImage->Width), NativeSide(baseImage->Height),
                    PixelFormat::Palette8, !unlit, base);
            }
            // An opaque base's own wave is left out (ShaderAnim::EntryMeta's
            // Fade dims by see-through, which an opaque image is not); a
            // flame's is its glow.
            // A flame's wave fades its frames into the next stage's, which
            // is not drawn here: shown alone it would flicker.
            addAnimated(std::move(base), baseStage, baseMeta, !plan.Translucent && !unlit,
                plan.Translucent && baseStage.Frames.size() <= 1);

            std::int32_t order = 0;
            for (const auto& [stageIndex, lit] : moving)
            {
                const ShaderScript::Stage& stage = script.Stages[static_cast<std::size_t>(stageIndex)];
                const auto image = loadRgba(stage.Frames.empty() ? stage.Map : stage.Frames.front());
                if (!image)
                {
                    continue;
                }
                const bool add = BlendOf(stage) == StageBlend::Add;
                BakedEntry layer;
                layer.Index = nextSource++;
                layer.Name = name + "@" + std::to_string(order);
                encode(*image, NativeSide(image->Width), NativeSide(image->Height),
                    add ? PixelFormat::A5I3 : PixelFormat::A3I5, lit && !add, layer);
                SA::EntryMeta meta;
                meta.Role = "layer";
                meta.Of = index;
                meta.Order = order++;
                meta.Blend = add ? "add" : "alpha";
                meta.Lit = lit;
                meta.TwoSided = script.TwoSided;
                addAnimated(std::move(layer), stage, meta, lit && !add, true);
            }
            return true;
        };

        auto used = UsedTextures(bsp, sky);
        if (modelShaders != nullptr)
        {
            for (std::size_t k = 0; k < modelShaders->size(); ++k)
            {
                used.emplace_back(ModelSourceIndex + static_cast<std::int32_t>(k), (*modelShaders)[k], 0);
            }
        }
        for (const auto& [index, name, flags] : used)
        {
            if (bakeAnimated(index, name, flags))
            {
                continue;
            }
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
            DecodedImage image;
            if (raw.has_value())
            {
                image = LoadRgb(*raw);
            }
            // An image the level does not carry is painted with the
            // cartridge's own art of the same kind (MphTextures). Not a
            // shader with no lightmap that is not the sky -- a flare, a
            // light's glow, an effect, which painted opaque would be worse
            // than left out -- nor textures/common's editor markers.
            if ((image.Width <= 0 || image.Height <= 0)
                && ((flags & Q3Bsp::SurfaceNoLightmap) != 0 && (flags & Q3Bsp::SurfaceSky) == 0
                    || Lower(name).starts_with("textures/common/")))
            {
                missing->push_back(raw.has_value() ? name + " (undecodable)" : name);
                continue;
            }
            // A sky of one colour -- gfx/colors/black behind q3dm17 -- is a
            // backdrop Quake leaves to the level; this game gives it one of
            // the cartridge's skies instead, as it does a sky it lacks.
            if ((flags & Q3Bsp::SurfaceSky) != 0 && image.Width > 0 && image.Height > 0
                && image.Pixels.size() >= static_cast<std::size_t>(image.Width) * image.Height * 3)
            {
                std::array<std::uint8_t, 3> low{255, 255, 255};
                std::array<std::uint8_t, 3> high{0, 0, 0};
                for (std::size_t p = 0; p + 2 < image.Pixels.size(); p += 3)
                {
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        low[c] = std::min(low[c], image.Pixels[p + c]);
                        high[c] = std::max(high[c], image.Pixels[p + c]);
                    }
                }
                if (high[0] - low[0] <= 4 && high[1] - low[1] <= 4 && high[2] - low[2] <= 4)
                {
                    image = DecodedImage{};
                }
            }
            if (image.Width <= 0 || image.Height <= 0)
            {
                const std::int32_t contents = static_cast<std::size_t>(index) < bsp->Textures().size()
                    ? RequireReference(bsp->Textures().at(static_cast<std::size_t>(index))).Contents() : 0;
                std::optional<MphTextures::Image> standIn = MphTextures::StandIn(name, flags, contents);
                if (!standIn.has_value())
                {
                    missing->push_back(raw.has_value() ? name + " (undecodable)" : name);
                    continue;
                }
                substituted->push_back(name + " <- " + standIn->Source);
                image.Width = standIn->Width;
                image.Height = standIn->Height;
                image.Pixels = std::move(standIn->Pixels);
            }

            // At the size it was painted at, or the one asked for.
            std::int32_t width = size;
            std::int32_t height = size;
            if (size == NativeSize)
            {
                width = NativeSide(image.Width);
                height = NativeSide(image.Height);
            }
            // Overbright, as Quake draws it: a level's light reaches past
            // white -- twice a texture's brightness in full sun -- and this
            // game's vertex light stops at white. Its textures are baked
            // twice as bright and its light halved (Q3Import::Light), which
            // gives the light the room it has in Quake. Not a sky, which no
            // light falls on, nor a surface Quake draws at its own colour
            // without a lightmap (lava, a glowing panel).
            if ((flags & (Q3Bsp::SurfaceSky | Q3Bsp::SurfaceNoLightmap)) == 0)
            {
                for (std::uint8_t& value : image.Pixels)
                {
                    value = static_cast<std::uint8_t>(std::min(255, value * Overbright));
                }
            }
            const std::vector<std::uint8_t> decoded = Resample(image, width, height);
            if (decoded.empty())
            {
                missing->push_back(name + " (undecodable)");
                continue;
            }
            auto [palette, pixels] = Quantize(decoded, UncheckedMultiply(width, height));
            entries.push_back(BakedEntry{
                index,
                name,
                std::move(palette),
                std::move(pixels),
                width,
                height
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
                const std::vector<std::uint8_t> decoded = Decode(*raw, sideSize, sideSize);
                if (decoded.empty())
                {
                    break;
                }
                auto [palette, pixels] = Quantize(decoded, UncheckedMultiply(sideSize, sideSize));
                sides.push_back(BakedEntry{
                    SkyBoxSourceIndex + static_cast<std::int32_t>(side),
                    name + "#" + std::string(SkyBoxSuffixes[side]),
                    std::move(palette),
                    std::move(pixels),
                    sideSize,
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

        // Each portal's view as its camera sees it, once it has been
        // rendered; until then a dark glass, which is what a portal looks
        // like from afar.
        if (portalImages != nullptr)
        {
            for (std::size_t i = 0; i < portalImages->size(); ++i)
            {
                DecodedImage image;
                if (FileExists((*portalImages)[i]))
                {
                    std::ifstream file(PathFromUtf8((*portalImages)[i]), std::ios::binary);
                    const std::vector<std::uint8_t> bytes(
                        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    image = LoadRgb(bytes);
                }
                if (image.Width <= 0 || image.Height <= 0)
                {
                    image.Width = 8;
                    image.Height = 8;
                    image.Pixels.assign(8 * 8 * 3, 0);
                    for (std::size_t p = 0; p < image.Pixels.size(); p += 3)
                    {
                        image.Pixels[p] = 40;
                        image.Pixels[p + 1] = 52;
                        image.Pixels[p + 2] = 70;
                    }
                }
                const std::int32_t width = NativeSide(image.Width);
                const std::int32_t height = NativeSide(image.Height);
                const std::vector<std::uint8_t> decoded = Resample(image, width, height);
                auto [palette, pixels] = Quantize(decoded, UncheckedMultiply(width, height));
                entries.push_back(BakedEntry{
                    PortalSourceIndex + static_cast<std::int32_t>(i),
                    "portal#" + std::to_string(i),
                    std::move(palette),
                    std::move(pixels),
                    width,
                    height
                });
            }
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
