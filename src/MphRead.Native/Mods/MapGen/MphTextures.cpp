#include "MphTextures.hpp"

#include "../../Formats/Formats.hpp"
#include "../../Read.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "Q3Bsp.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedListAt;
using ::MphRead::NativeRuntime::RequireReference;

namespace
{
    using MphRead::Mods::MapGen::MphTextures;
    using MphRead::Mods::MapGen::Q3Bsp;

    struct Asset final
    {
        const char* Room;
        const char* Material;
    };

    struct Family final
    {
        const char* Name;
        // Words any of which in the shader's name put it in this family.
        std::vector<std::string_view> Words;
        std::vector<Asset> Assets;
    };

    // Quake 3's contents bits for its liquids (surfaceparm lava, slime,
    // water).
    constexpr std::int32_t ContentsLava = 0x8;
    constexpr std::int32_t ContentsSlime = 0x10;
    constexpr std::int32_t ContentsWater = 0x20;

    // The cartridge's art by what it is, as -mapmaterials lists it. The
    // families are tried in this order, so "lava" wins over "floor" and
    // "trim" over "wall"; a shader none of them names is a wall.
    [[nodiscard]] const std::vector<Family>& Families()
    {
        static const std::vector<Family> families{
            {"sky", {"sky", "space", "stars"},
                {{"MP8 FIRE CONTROL", "space"}, {"MP5 FUEL SLUICE", "space"}, {"MP13 ACCELERATOR", "space"}}},
            {"lava", {"lava", "magma", "fire", "hell"},
                {{"MP7 PROCESSOR CORE", "lava2"}, {"UNIT1 ALINOS LANDFALL", "Lava1"}, {"MP4 HIGHGROUND", "Lava"}}},
            {"ice", {"slime", "water", "liquid", "ice", "snow", "frost", "fog"},
                {{"MP13 ACCELERATOR", "ice"}, {"MP5 FUEL SLUICE", "snow"}, {"UNIT 4 ARCTERRA BASE", "snow"}}},
            {"glass", {"glass", "window", "crystal"},
                {{"MP8 FIRE CONTROL", "glass"}}},
            {"grate", {"grate", "grill", "mesh", "vent", "fence"},
                {{"MP7 PROCESSOR CORE", "trim_grating"}, {"AD2 MAGMA VENTS", "TRIM_grateing"}, {"MP8 FIRE CONTROL", "vent"}}},
            {"pipe", {"pipe", "tube", "cable", "conduit"},
                {{"MP7 PROCESSOR CORE", "big_pipe"}, {"MP5 FUEL SLUICE", "tube"}, {"MP8 FIRE CONTROL", "tube_wall"}}},
            {"light", {"light", "lamp", "glow", "flare", "neon", "lt_"},
                {{"MP8 FIRE CONTROL", "light_big_blue"}, {"MP13 ACCELERATOR", "trim_light"}, {"MP5 FUEL SLUICE", "light_2"},
                    {"MP7 PROCESSOR CORE", "light_45"}}},
            {"trim", {"trim", "border", "edge", "strip", "rim", "step", "stair"},
                {{"MP8 FIRE CONTROL", "skinny_gray"}, {"MP13 ACCELERATOR", "trim_gray_skinny"}, {"AD2 MAGMA VENTS", "TRIM_bolted"},
                    {"MP7 PROCESSOR CORE", "wall_dk_trim"}}},
            {"metal", {"metal", "steel", "support", "beam", "girder", "panel", "box", "crate", "door", "button", "tech", "mach", "pewter", "rust", "iron", "chrome", "copper", "bronze"},
                {{"MP7 PROCESSOR CORE", "metal"}, {"MP8 FIRE CONTROL", "metal_boxes"}, {"AD2 MAGMA VENTS", "panel_rivets"},
                    {"MP7 PROCESSOR CORE", "base_metal_sml"}, {"MP13 ACCELERATOR", "panel_graynotch"}}},
            {"rock", {"rock", "stone", "cliff", "cave", "organic", "marble", "skull", "gothic"},
                {{"MP7 PROCESSOR CORE", "rockwall"}, {"MP13 ACCELERATOR", "rock_gradient"}, {"AD2 MAGMA VENTS", "rock_reg"},
                    {"UNIT1 ALINOS LANDFALL", "Block"}}},
            {"sand", {"sand", "dirt", "grass", "mud", "earth", "terrain"},
                {{"AD2 MAGMA VENTS", "sand"}, {"MP4 HIGHGROUND", "new_sandC"}, {"AD2 MAGMA VENTS", "FLOOR_ground_2"}}},
            {"floor", {"floor", "ground", "tile", "plate", "pad", "deck", "flr"},
                {{"MP8 FIRE CONTROL", "floor_metroid"}, {"MP7 PROCESSOR CORE", "floor_tile"}, {"MP7 PROCESSOR CORE", "floor_pattern"},
                    {"MP13 ACCELERATOR", "floor_roomc"}, {"AD2 MAGMA VENTS", "FLOOR_concrete"}}},
            {"wall", {},
                {{"MP8 FIRE CONTROL", "black_wall"}, {"MP8 FIRE CONTROL", "wall_circutbox"}, {"MP7 PROCESSOR CORE", "wall_teethed"},
                    {"MP13 ACCELERATOR", "wall_squares"}, {"MP5 FUEL SLUICE", "metroid_wall"}, {"MP13 ACCELERATOR", "wall_metroid"}}},
        };
        return families;
    }

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

    // FNV-1a: the same name always picks the same one of a family.
    [[nodiscard]] std::uint32_t Hash(std::string_view value) noexcept
    {
        std::uint32_t hash = 2166136261U;
        for (const char ch : value)
        {
            hash ^= static_cast<std::uint8_t>(ch);
            hash *= 16777619U;
        }
        return hash;
    }

    [[nodiscard]] const Family& Classify(
        const std::string& shader, std::int32_t flags, std::int32_t contents)
    {
        const std::vector<Family>& families = Families();
        const auto named = [&](std::string_view family) -> const Family&
        {
            for (const Family& candidate : families)
            {
                if (family == candidate.Name)
                {
                    return candidate;
                }
            }
            return families.back();
        };
        if ((flags & Q3Bsp::SurfaceSky) != 0)
        {
            return named("sky");
        }
        if ((contents & ContentsLava) != 0)
        {
            return named("lava");
        }
        if ((contents & (ContentsSlime | ContentsWater)) != 0)
        {
            return named("ice");
        }
        // The last part of the name says what the surface is ("gothic_floor/
        // blocks15" is a floor), the folders what set it came from.
        const std::string name = Lower(shader);
        const std::size_t slash = name.find_last_of('/');
        const std::string leaf = slash == std::string::npos ? name : name.substr(slash + 1);
        for (const std::string* part : {&leaf, &name})
        {
            for (const Family& family : families)
            {
                for (std::string_view word : family.Words)
                {
                    if (part->find(word) != std::string::npos)
                    {
                        return family;
                    }
                }
            }
        }
        return families.back();
    }

    [[nodiscard]] std::optional<MphTextures::Image> Load(const Asset& asset)
    {
        static std::mutex lock;
        static std::map<std::pair<std::string, std::string>, std::optional<MphTextures::Image>> cache;
        const std::lock_guard<std::mutex> guard(lock);
        const auto key = std::make_pair(std::string(asset.Room), std::string(asset.Material));
        if (const auto found = cache.find(key); found != cache.end())
        {
            return found->second;
        }

        std::optional<MphTextures::Image> result;
        try
        {
            const std::shared_ptr<MphRead::ModelInstance> instance
                = MphRead::Read::GetRoomModelInstance(asset.Room);
            if (instance)
            {
                const std::shared_ptr<MphRead::Model> model = instance->Model();
                const std::shared_ptr<MphRead::Material> material
                    = RequireReference(model).GetMaterialByName(asset.Material);
                if (material && material->TextureId >= 0 && model->Recolors)
                {
                    const MphRead::Recolor& recolor = RequireReference(ManagedListAt(*model->Recolors, 0));
                    const MphRead::Texture& texture
                        = ManagedListAt(RequireReference(recolor.Textures), material->TextureId);
                    const std::vector<MphRead::ColorRgba> pixels
                        = model->GetPixels(material->TextureId, material->PaletteId, 0);
                    MphTextures::Image image;
                    image.Width = texture.Width;
                    image.Height = texture.Height;
                    image.Source = std::string(asset.Room) + "/" + asset.Material;
                    image.Pixels.reserve(pixels.size() * 3U);
                    // A see-through texel shows the dark behind it.
                    for (const MphRead::ColorRgba& pixel : pixels)
                    {
                        image.Pixels.push_back(static_cast<std::uint8_t>(pixel.Red * pixel.Alpha / 255));
                        image.Pixels.push_back(static_cast<std::uint8_t>(pixel.Green * pixel.Alpha / 255));
                        image.Pixels.push_back(static_cast<std::uint8_t>(pixel.Blue * pixel.Alpha / 255));
                    }
                    if (image.Width > 0 && image.Height > 0
                        && image.Pixels.size() == static_cast<std::size_t>(image.Width) * image.Height * 3U)
                    {
                        result = std::move(image);
                    }
                }
            }
        }
        catch (...)
        {
            // A room the game files lack is no answer; the next one may be.
        }
        cache.emplace(key, result);
        return result;
    }
}

namespace MphRead::Mods::MapGen
{
    std::optional<MphTextures::Image> MphTextures::StandIn(
        const std::string& shader, std::int32_t flags, std::int32_t contents)
    {
        // A see-through effect -- a teleporter's energy, fog, a flame, a
        // beam -- painted with an opaque wall would be a wall where there was
        // none: it is left out instead.
        const std::string lower = Lower(shader);
        for (std::string_view word : {"energy", "transparen", "fog", "flame", "beam", "flare", "spark", "smoke", "portal"})
        {
            if (lower.find(word) != std::string::npos)
            {
                return std::nullopt;
            }
        }
        const Family& family = Classify(shader, flags, contents);
        const std::size_t count = family.Assets.size();
        const std::size_t first = Hash(Lower(shader)) % count;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (std::optional<Image> image = Load(family.Assets[(first + i) % count]))
            {
                return image;
            }
        }
        return std::nullopt;
    }
}
