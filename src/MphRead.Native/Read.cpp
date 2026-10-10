#include "Read.hpp"

#include "Export/Collada.hpp"
#include "Formats/EntityEnemy.hpp"
#include "Metadata/Rooms.hpp"
#include "Mods/Headless.hpp"
#include "Program.hpp"
#include "Utility/Archive.hpp"
#include "Utility/Compress.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::Int64ToInt32;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;

namespace MphRead
{
    // Exact declaration seam for the already-owned Metadata counterpart. Read.cpp
    // cannot include Metadata.hpp together with Formats.hpp until the existing
    // duplicate PaletteData declaration in those prerequisite headers is closed.
    enum class MdlSuffix : std::int32_t
    {
        None,
        All,
        Model
    };

    enum class MetaDir : std::int32_t
    {
        Models,
        Hud,
        Stage,
        MainMenu,
        Logo,
        CharSelect,
        CreateJoin,
        GameOption,
        GamersCard,
        Keyboard,
        Keypad,
        MoviePlayer,
        MultiMaster,
        Multiplayer,
        PaxControls,
        Popup,
        Results,
        ScStartGame,
        StartGame,
        ToStart,
        TouchToStart,
        TouchToStart2,
        WifiCreate,
        WifiGames
    };

    class RecolorMetadata
    {
    public:
        const std::string Name;
        const std::string ModelPath;
        const std::string TexturePath;
        const std::string PalettePath;
        const std::optional<std::string> ReplacePath;
        const std::map<int, std::vector<int>> ReplaceIds;

        RecolorMetadata(std::string name, std::string modelPath);
        RecolorMetadata(std::string name, std::string modelPath, std::string texturePath);
        RecolorMetadata(std::string name, std::string modelPath, std::string texturePath,
            std::string palettePath, std::map<int, std::vector<int>> replaceIds = {},
            bool separateReplace = false);
    };

    class ModelMetadata
    {
        struct Values
        {
            std::string Name;
            std::string ModelPath;
            std::optional<std::string> AnimationPath;
            std::optional<std::string> AnimationShare;
            std::optional<std::string> CollisionPath;
            std::optional<std::string> ExtraCollisionPath;
            std::vector<RecolorMetadata> Recolors;
            bool UseLightSources = false;
            bool FirstHunt = false;
        };

        explicit ModelMetadata(Values values);

    public:
        const std::string Name;
        const std::string ModelPath;
        const std::optional<std::string> AnimationPath;
        const std::optional<std::string> AnimationShare;
        const std::optional<std::string> CollisionPath;
        const std::optional<std::string> ExtraCollisionPath;
        const std::vector<RecolorMetadata> Recolors;
        const bool UseLightSources;
        const bool FirstHunt;

        ModelMetadata(std::string name, std::string modelPath,
            std::optional<std::string> animationPath, std::optional<std::string> collisionPath,
            std::vector<RecolorMetadata> recolors,
            std::optional<std::string> animationShare = std::nullopt,
            bool useLightSources = false);
        ModelMetadata(std::string name, MetaDir dir,
            std::optional<std::string> anim = std::nullopt);
        ModelMetadata(std::string name, std::string texturePath, MetaDir dir);
        ModelMetadata(std::string name, std::optional<std::string> animationPath,
            std::optional<std::string> texturePath = std::nullopt);
        ModelMetadata(std::string name, std::string remove, bool animation = true,
            std::optional<std::string> animationPath = std::nullopt,
            bool collision = false, bool firstHunt = false);
        ModelMetadata(std::string name, std::vector<std::string> recolors,
            std::optional<std::string> remove = std::nullopt, bool animation = false,
            std::optional<std::string> animationPath = std::nullopt, bool texture = false,
            MdlSuffix mdlSuffix = MdlSuffix::None,
            std::optional<std::string> archive = std::nullopt,
            std::optional<std::string> recolorName = std::nullopt,
            std::optional<std::string> animationShare = std::nullopt,
            bool useLightSources = false, bool firstHunt = false,
            bool noUnderscore = false);
        ModelMetadata(std::string name, bool animation = true, bool collision = false,
            bool texture = false, std::optional<std::string> share = std::nullopt,
            MdlSuffix mdlSuffix = MdlSuffix::None,
            std::optional<std::string> archive = std::nullopt,
            std::optional<std::string> addToAnim = std::nullopt,
            bool firstHunt = false,
            std::optional<std::string> animationPath = std::nullopt,
            std::optional<std::string> extraCollision = std::nullopt);
        ModelMetadata(std::string name, std::string modelPath,
            std::optional<std::string> animationPath,
            std::optional<std::string> collisionPath, bool firstHunt = false);
    };

    namespace Metadata
    {
        extern const std::unordered_map<std::string, std::vector<PaletteData>> PowerPalettes;
        extern const std::array<std::pair<std::string, std::optional<std::string>>, 247> Effects;
        extern const std::unordered_map<SingleType, std::pair<std::string, std::string>> SingleParticles;

        [[nodiscard]] const ModelMetadata* GetModelByName(
            std::string_view name, MetaDir dir = MetaDir::Models) noexcept;
        [[nodiscard]] const ModelMetadata* GetFirstHuntModelByName(std::string_view name) noexcept;
        [[nodiscard]] std::pair<const ::MphRead::RoomMetadata*, int> GetRoomByName(std::string_view name);
    }

    namespace Mods
    {
        class DebugLog final
        {
        public:
            static void Line(std::string_view category, std::string_view value);
        };
    }

    namespace Utility
    {
        class Repack final
        {
        public:
            [[nodiscard]] static std::vector<std::uint8_t> RepackHook(
                const std::string& path, bool firstHunt);
        };
    }

    namespace Export
    {
        class Images final
        {
        public:
            static void ExportImages(const Model& model);
        };
    }
}

namespace
{
    using namespace MphRead;

    using ModelCache = std::unordered_map<std::string, std::shared_ptr<Model>>;
    using EffectCache = std::unordered_map<std::int32_t, std::shared_ptr<Effect>>;
    using ParticleKey = std::pair<std::string, std::string>;
    using ParticleCache = std::map<ParticleKey, std::shared_ptr<Particle>>;

    [[nodiscard]] ModelCache& Models()
    {
        static ModelCache cache;
        return cache;
    }

    [[nodiscard]] ModelCache& FhModels()
    {
        static ModelCache cache;
        return cache;
    }

    [[nodiscard]] EffectCache& EffectsCache()
    {
        static EffectCache cache;
        return cache;
    }

    [[nodiscard]] ParticleCache& Particles()
    {
        static ParticleCache cache;
        return cache;
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<const std::vector<T>> EmptyArray()
    {
        static const auto empty = std::make_shared<const std::vector<T>>();
        return empty;
    }

    [[nodiscard]] std::string MetaDirString(MetaDir dir)
    {
        static constexpr std::array<std::string_view, 24> names = {
            "Models", "Hud", "Stage", "MainMenu", "Logo", "CharSelect", "CreateJoin",
            "GameOption", "GamersCard", "Keyboard", "Keypad", "MoviePlayer", "MultiMaster",
            "Multiplayer", "PaxControls", "Popup", "Results", "ScStartGame", "StartGame",
            "ToStart", "TouchToStart", "TouchToStart2", "WifiCreate", "WifiGames"
        };
        const std::int32_t value = static_cast<std::int32_t>(dir);
        if (value >= 0 && value < static_cast<std::int32_t>(names.size()))
        {
            return std::string(names[static_cast<std::size_t>(value)]);
        }
        return std::to_string(value);
    }

    [[nodiscard]] std::string EntityTypeString(EntityType type)
    {
        return ::MphRead::ToString(type);
    }

    template <typename T>
    void AddRange(std::vector<T>& target, const std::shared_ptr<const std::vector<T>>& source)
    {
        if (!source)
        {
            throw System::NullReferenceException();
        }
        target.insert(target.end(), source->begin(), source->end());
    }

    template <typename T>
    void AddRange(std::vector<T>& target, const std::vector<T>& source)
    {
        target.insert(target.end(), source.begin(), source.end());
    }

    template <typename K, typename V>
    void DictionaryAdd(std::unordered_map<K, V>& values, K key, V value)
    {
        if (!values.emplace(std::move(key), std::move(value)).second)
        {
            throw std::invalid_argument("An item with the same key has already been added.");
        }
    }

    template <std::size_t N>
    [[nodiscard]] std::string MarshalString(
        const MphRead::NativeRuntime::ByValByteArray<N>& value)
    {
        if (value.IsNull())
        {
            throw System::ArgumentNullException("array");
        }
        std::string result;
        for (std::uint8_t byte : value)
        {
            if (byte == 0)
            {
                break;
            }
            if (byte <= 0x7F)
            {
                result.push_back(static_cast<char>(byte));
            }
            else
            {
                result.push_back(static_cast<char>(0xC0U | (byte >> 6)));
                result.push_back(static_cast<char>(0x80U | (byte & 0x3FU)));
            }
        }
        return result;
    }

    [[nodiscard]] std::uint8_t AtByte(std::span<const std::uint8_t> bytes, std::int64_t index)
    {
        if (index < 0 || static_cast<std::uint64_t>(index) >= bytes.size())
        {
            throw System::IndexOutOfRangeException();
        }
        return bytes[static_cast<std::size_t>(index)];
    }

    void AppendLatin1CodePoint(std::string& output, std::uint8_t byte)
    {
        if (byte <= 0x7F)
        {
            output.push_back(static_cast<char>(byte));
        }
        else
        {
            output.push_back(static_cast<char>(0xC0U | (byte >> 6)));
            output.push_back(static_cast<char>(0x80U | (byte & 0x3FU)));
        }
    }

    [[nodiscard]] std::string AsciiDecode(std::span<const std::uint8_t> bytes)
    {
        std::string output;
        output.reserve(bytes.size());
        for (std::uint8_t byte : bytes)
        {
            output.push_back(byte <= 0x7F ? static_cast<char>(byte) : '?');
        }
        return output;
    }

    [[nodiscard]] std::uint8_t RoundByteToEven(float value)
    {
        const float lower = std::floor(value);
        const float fraction = value - lower;
        float rounded = lower;
        if (fraction > 0.5F)
        {
            rounded = lower + 1.0F;
        }
        else if (fraction == 0.5F)
        {
            const auto integer = static_cast<std::int32_t>(lower);
            if ((integer & 1) != 0)
            {
                rounded = lower + 1.0F;
            }
        }
        return static_cast<std::uint8_t>(rounded);
    }

    [[nodiscard]] std::string NoNodeName(std::size_t index)
    {
        std::ostringstream stream;
        stream << "__no_node_" << std::setfill('0') << std::setw(2) << index;
        return stream.str();
    }

    [[nodiscard]] std::string FileNameWithoutExtension(const std::string& path)
    {
        return ::MphRead::NativeRuntime::PathGetFileNameWithoutExtension(path);
    }

    [[nodiscard]] std::size_t ManagedCapacity(std::int32_t capacity)
    {
        if (capacity < 0)
        {
            throw std::out_of_range("Non-negative number required. (Parameter 'capacity')");
        }
        return static_cast<std::size_t>(capacity);
    }
}

namespace MphRead
{
    bool Read::ApplyFixes = true;

    void Read::ClearCache()
    {
        Models().clear();
        FhModels().clear();
        EffectsCache().clear();
        Particles().clear();
    }

    std::vector<std::shared_ptr<Model>> Read::CachedModels()
    {
        std::vector<std::shared_ptr<Model>> result;
        result.reserve(Models().size() + FhModels().size());
        for (const auto& [name, model] : Models())
        {
            (void)name;
            result.push_back(model);
        }
        for (const auto& [name, model] : FhModels())
        {
            (void)name;
            result.push_back(model);
        }
        return result;
    }

    std::shared_ptr<ModelInstance> Read::GetModelInstance(
        const std::string& name, bool firstHunt, MetaDir dir, bool noCache)
    {
        std::shared_ptr<ModelInstance> inst = GetModelInstanceOrNull(name, firstHunt, dir, noCache);
        if (!inst)
        {
            throw ProgramException("No model with this name is known.");
        }
        return inst;
    }

    std::shared_ptr<ModelInstance> Read::GetModelInstanceOrNull(
        const std::string& name, bool firstHunt, MetaDir dir, bool noCache)
    {
        ModelCache& cache = firstHunt ? FhModels() : Models();
        std::shared_ptr<Model> model;
        const auto iterator = cache.find(name);
        if (!noCache && iterator != cache.end())
        {
            model = iterator->second;
        }
        else
        {
            model = GetModel(name, firstHunt, dir);
            if (!model)
            {
                return nullptr;
            }
            if (!noCache)
            {
                if (!cache.emplace(name, model).second)
                {
                    throw std::invalid_argument("An item with the same key has already been added.");
                }
            }
        }
        return std::make_shared<ModelInstance>(model);
    }

    std::shared_ptr<Model> Read::GetModel(const std::string& name, bool firstHunt, MetaDir dir)
    {
        Mods::DebugLog::Line("model", "reading \"" + name + "\" (dir=" + MetaDirString(dir)
            + (firstHunt ? ", first hunt)" : ")"));
        const ModelMetadata* meta = firstHunt
            ? Metadata::GetFirstHuntModelByName(name)
            : Metadata::GetModelByName(name, dir);
        if (meta == nullptr)
        {
            return nullptr;
        }
        return ReadModel(meta->Name, meta->ModelPath, meta->AnimationPath,
            meta->AnimationShare, meta->Recolors, meta->FirstHunt);
    }

    std::shared_ptr<ModelInstance> Read::GetRoomModelInstance(const std::string& name)
    {
        std::shared_ptr<ModelInstance> inst = GetRoomModelInstanceOrNull(name);
        if (!inst)
        {
            throw ProgramException("No room with this name is known.");
        }
        return inst;
    }

    std::shared_ptr<ModelInstance> Read::GetRoomModelInstanceOrNull(const std::string& name)
    {
        const auto [meta, unused] = Metadata::GetRoomByName(name);
        (void)unused;
        if (meta == nullptr)
        {
            return nullptr;
        }
        std::shared_ptr<Model> model;
        const auto iterator = Models().find(name);
        if (iterator != Models().end())
        {
            model = iterator->second;
        }
        else
        {
            model = GetRoomModel(*meta);
            if (!model)
            {
                return nullptr;
            }
            if (!Models().emplace(name, model).second)
            {
                throw std::invalid_argument("An item with the same key has already been added.");
            }
        }
        return std::make_shared<ModelInstance>(model);
    }

    std::shared_ptr<Model> Read::GetRoomModel(const RoomMetadata& meta)
    {
        std::vector<RecolorMetadata> recolors;
        recolors.emplace_back("default", meta.ModelPath, meta.TexturePath.value_or(meta.ModelPath));
        return ReadModel(meta.Name, meta.ModelPath, std::optional<std::string>(meta.AnimationPath),
            std::nullopt, recolors, meta.FirstHunt || meta.Hybrid);
    }

    void Read::RemoveModel(const std::string& name, bool firstHunt)
    {
        if (firstHunt)
        {
            FhModels().erase(name);
        }
        else
        {
            Models().erase(name);
        }
    }

    std::pair<std::int32_t, std::vector<std::uint8_t>> Read::ReadKanjiFont(bool singlePlayer)
    {
        const std::string path = Paths::Combine(Paths::FileSystem(), "stringTables_jp",
            singlePlayer ? "ingame_1bit.bin" : "inmulti_1bit.bin");
        const std::vector<std::uint8_t> storage = ReadBytes(path, false);
        const std::span<const std::uint8_t> bytes(storage);
        const std::int32_t count = SpanReadInt(bytes, 0);
        const std::uint16_t width = SpanReadUshort(bytes, 4);
#ifndef NDEBUG
        assert(width == 16);
#endif
        const std::uint16_t height = SpanReadUshort(bytes, 6);
        const std::int32_t outputCount = UncheckedMultiply(count, 128) / 4;
        if (outputCount < 0)
        {
            throw System::OverflowException();
        }
        std::vector<std::uint32_t> output(static_cast<std::size_t>(outputCount));
        static constexpr std::array<std::int32_t, 8> table = {128, 64, 32, 16, 8, 4, 2, 1};
        std::int32_t ch = 0;
        const std::int32_t rowBytes = UncheckedMultiply(2, static_cast<std::int32_t>(height));
        const std::int32_t byteEnd = UncheckedAdd(UncheckedMultiply(rowBytes, count), 8);
        for (std::int32_t c = 8; c < byteEnd; c = UncheckedAdd(c, rowBytes))
        {
            const std::span<const std::uint8_t> data = ReadDetail::Slice(bytes, c);
            for (std::int32_t y = 0; y < height; ++y)
            {
                for (std::int32_t x = 0; x < 16; ++x)
                {
                    const std::int32_t inc = height == 16 ? y : y + 3;
                    const std::int32_t offset = (inc & 7) + 16 * (inc / 8) + 8 * (x / 8);
                    const std::int32_t shift = 4 * (x & 7);
                    const std::int32_t mask = table[static_cast<std::size_t>(x & 7)];
                    const std::uint32_t value
                        = (AtByte(data, y * 2 + x / 8) & mask) != 0
                        ? (3U << shift) : 0U;
                    const std::int32_t outputIndex = UncheckedAdd(
                        UncheckedMultiply(ch, 128) / 4, offset);
                    output.at(static_cast<std::size_t>(outputIndex)) |= value;
                }
            }
            ch = UncheckedAdd(ch, 1);
        }
        const std::int32_t resultLength = UncheckedMultiply(
            static_cast<std::int32_t>(output.size()), 4);
        if (resultLength < 0)
        {
            throw System::OverflowException();
        }
        std::vector<std::uint8_t> result(static_cast<std::size_t>(resultLength));
        for (std::int32_t i = 0; i < resultLength; i = UncheckedAdd(i, 4))
        {
            const std::uint32_t value = output[static_cast<std::size_t>(i / 4)];
            result[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value & 0xFFU);
            result[static_cast<std::size_t>(i + 1)] = static_cast<std::uint8_t>((value & 0xFF00U) >> 8);
            result[static_cast<std::size_t>(i + 2)] = static_cast<std::uint8_t>((value & 0xFF0000U) >> 16);
            result[static_cast<std::size_t>(i + 3)] = static_cast<std::uint8_t>((value & 0xFF000000U) >> 24);
        }
        return {count, std::move(result)};
    }

    std::shared_ptr<Model> Read::ReadModelFile(const std::string& name,
        const std::string& modelPath, const std::optional<std::string>& animationPath)
    {
        const std::vector<RecolorMetadata> recolors{RecolorMetadata("default", modelPath)};
        return ReadModel(name, modelPath, animationPath, std::nullopt, recolors, false);
    }

    std::shared_ptr<Model> Read::ReadModel(
        const std::string& name, const std::string& modelPath,
        const std::optional<std::string>& animationPath,
        const std::optional<std::string>& animationShare,
        const std::vector<RecolorMetadata>& recolorMeta, bool firstHunt)
    {
        const std::string& root = firstHunt ? Paths::FhFileSystem() : Paths::FileSystem();
        const std::string path = Paths::Combine(root, modelPath);
        const std::vector<std::uint8_t> initialStorage = ReadBytes(path, firstHunt);
        const std::span<const std::uint8_t> initialBytes(initialStorage);
        const Header header = ReadStruct<Header>(ReadDetail::Slice(initialBytes, 0, Sizes::Header));
        const auto nodes = DoOffsets<RawNode>(initialBytes, header.NodeOffset, header.NodeCount);
        const auto meshes = DoOffsets<RawMesh>(initialBytes, header.MeshOffset, header.MeshCount);
        const auto dlists = DoOffsets<DisplayList>(initialBytes, header.DlistOffset, header.MeshCount);
        auto instructionLists = std::make_shared<std::vector<
            std::shared_ptr<const std::vector<std::shared_ptr<RenderInstruction>>>>>() ;
        instructionLists->reserve(dlists->size());
        for (const DisplayList& dlist : *dlists)
        {
            instructionLists->push_back(Mods::Headless::Active()
                ? EmptyArray<std::shared_ptr<RenderInstruction>>()
                : DoRenderInstructions(initialBytes, dlist));
        }
        const auto materials = DoOffsets<RawMaterial>(
            initialBytes, header.MaterialOffset, header.MaterialCount);
        auto recolors = std::make_shared<std::vector<std::shared_ptr<Recolor>>>();
        recolors->reserve(recolorMeta.size());

        for (const RecolorMetadata& meta : recolorMeta)
        {
            std::vector<std::uint8_t> modelOwned;
            std::span<const std::uint8_t> modelBytes = initialBytes;
            Header modelHeader = header;
            if (Paths::Combine(root, meta.ModelPath) != path)
            {
                modelOwned = ReadBytes(meta.ModelPath, firstHunt);
                modelBytes = modelOwned;
                modelHeader = ReadStruct<Header>(ReadDetail::Slice(modelBytes, 0, Sizes::Header));
            }

            auto texturesBase = DoOffsets<Texture>(
                modelBytes, modelHeader.TextureOffset, modelHeader.TextureCount);
            auto palettesBase = DoOffsets<Palette>(
                modelBytes, modelHeader.PaletteOffset, modelHeader.PaletteCount);
            auto textures = std::make_shared<std::vector<Texture>>(*texturesBase);
            auto palettes = std::make_shared<std::vector<Palette>>(*palettesBase);

            if (ApplyFixes)
            {
                if ((name == "Guardian_lod0" || name == "Guardian_lod1") && meta.Name != "pal_01")
                {
                    auto extraTex = std::make_shared<std::vector<Texture>>();
                    auto extraPal = std::make_shared<std::vector<Palette>>();
                    if (meta.Name == "pal_02" || meta.Name == "pal_03" || meta.Name == "pal_04")
                    {
                        *extraTex = *textures;
                        *extraPal = *palettes;
                        if (meta.Name == "pal_02")
                        {
                            extraTex->at(0) = extraTex->at(5);
                            extraTex->at(1) = extraTex->at(4);
                            extraPal->at(7) = extraPal->at(4);
                            extraPal->at(3) = extraPal->at(0);
                        }
                        else if (meta.Name == "pal_03")
                        {
                            extraTex->at(0) = extraTex->at(6);
                            extraTex->at(1) = extraTex->at(3);
                            extraPal->at(7) = extraPal->at(5);
                            extraPal->at(3) = extraPal->at(1);
                        }
                        else
                        {
                            extraTex->at(0) = extraTex->at(7);
                            extraTex->at(1) = extraTex->at(2);
                            extraPal->at(7) = extraPal->at(6);
                            extraPal->at(3) = extraPal->at(2);
                        }
                    }
                    else if (meta.Name == "pal_Team01" || meta.Name == "pal_Team02")
                    {
                        extraTex->push_back(textures->at(1));
                        extraTex->push_back(textures->at(0));
                        extraPal->push_back(Palette{});
                        extraPal->push_back(Palette{});
                        extraPal->push_back(Palette{});
                        extraPal->push_back(palettes->at(0));
                        extraPal->push_back(Palette{});
                        extraPal->push_back(Palette{});
                        extraPal->push_back(Palette{});
                        extraPal->push_back(palettes->at(1));
                    }
                    textures = std::move(extraTex);
                    palettes = std::move(extraPal);
                }
                else if (name == "Alimbic_Power" || name == "Generic_Power"
                    || name == "Ice_Power" || name == "Lava_Power" || name == "Ruins_Power")
                {
                    auto extraTex = std::make_shared<std::vector<Texture>>(*textures);
                    auto extraPal = std::make_shared<std::vector<Palette>>(*palettes);
                    if (name == "Alimbic_Power")
                    {
                        extraTex->at(1) = extraTex->at(0);
                        extraPal->at(1) = extraPal->at(0);
                    }
                    else if (name == "Lava_Power")
                    {
                        extraTex->at(1) = extraTex->at(2);
                        extraPal->at(1) = extraPal->at(2);
                    }
                    else if (name == "Ruins_Power")
                    {
                        extraTex->at(0) = extraTex->at(1);
                        extraPal->at(0) = extraPal->at(1);
                    }
                    textures = std::move(extraTex);
                    palettes = std::move(extraPal);
                }
            }

            std::vector<std::uint8_t> textureOwned;
            std::span<const std::uint8_t> textureBytes = modelBytes;
            if (meta.TexturePath != meta.ModelPath)
            {
                textureOwned = ReadBytes(meta.TexturePath, firstHunt);
                textureBytes = textureOwned;
            }
            std::vector<std::uint8_t> paletteOwned;
            std::span<const std::uint8_t> paletteBytes = textureBytes;
            if (meta.PalettePath != meta.TexturePath && meta.ReplaceIds.empty())
            {
                paletteOwned = ReadBytes(meta.PalettePath, firstHunt);
                paletteBytes = paletteOwned;
                const Header paletteHeader = ReadStruct<Header>(
                    ReadDetail::Slice(paletteBytes, 0, Sizes::Header));
                palettes = std::make_shared<std::vector<Palette>>(*DoOffsets<Palette>(
                    paletteBytes, paletteHeader.PaletteOffset, paletteHeader.PaletteCount));
            }

            auto textureData = std::make_shared<std::vector<
                std::shared_ptr<const std::vector<TextureData>>>>();
            textureData->reserve(textures->size());
            for (const Texture& texture : *textures)
            {
                textureData->push_back(Mods::Headless::Active()
                    ? EmptyArray<TextureData>() : GetTextureData(texture, textureBytes));
            }
            auto paletteData = std::make_shared<std::vector<
                std::shared_ptr<const std::vector<PaletteData>>>>();
            paletteData->reserve(palettes->size());
            for (const Palette& palette : *palettes)
            {
                paletteData->push_back(GetPaletteData(palette, paletteBytes));
            }

            if (ApplyFixes)
            {
                if (name == "Alimbic_Power" || name == "Generic_Power"
                    || name == "Ice_Power" || name == "Lava_Power")
                {
                    const std::shared_ptr<Model> ruins = GetModelInstance("Ruins_Power")->Model();
                    const std::shared_ptr<Recolor> recolor = ruins->Recolors->at(0);
                    const Texture newTexture = recolor->Textures->at(8);
                    const auto newTexData = recolor->TextureData->at(8);
                    const std::vector<PaletteData>& powerPalette = Metadata::PowerPalettes.at(name);
#ifndef NDEBUG
                    assert(powerPalette.size() == 8);
#endif
                    const auto newPalette = std::make_shared<const std::vector<PaletteData>>(powerPalette);
                    auto extraTex = std::make_shared<std::vector<Texture>>(*textures);
                    if (name == "Lava_Power")
                    {
                        textureData->push_back(newTexData);
                        extraTex->push_back(newTexture);
                    }
                    else
                    {
                        textureData->at(8) = newTexData;
                        extraTex->at(8) = newTexture;
                    }
                    auto extraPal = std::make_shared<std::vector<Palette>>(*palettes);
                    if (name == "Lava_Power")
                    {
                        paletteData->push_back(newPalette);
                        extraPal->push_back(Palette{});
                    }
                    else
                    {
                        paletteData->at(8) = newPalette;
                    }
                    textures = std::move(extraTex);
                    palettes = std::move(extraPal);
                }
            }
            else if (name == "Lava_Power")
            {
                auto extraTex = std::make_shared<std::vector<Texture>>(*textures);
                extraTex->push_back(Texture(TextureFormat::Palette8Bit, 1, 1));
                textureData->push_back(std::make_shared<const std::vector<TextureData>>(
                    std::initializer_list<TextureData>{TextureData(0, 255)}));
                auto extraPal = std::make_shared<std::vector<Palette>>(*palettes);
                extraPal->push_back(Palette{});
                paletteData->push_back(std::make_shared<const std::vector<PaletteData>>(
                    std::initializer_list<PaletteData>{PaletteData(0x7FFF)}));
                textures = std::move(extraTex);
                palettes = std::move(extraPal);
            }

            const std::string replacePath = meta.ReplacePath.value_or(meta.PalettePath);
            if (replacePath != meta.TexturePath && !meta.ReplaceIds.empty())
            {
                paletteOwned = ReadBytes(replacePath, firstHunt);
                paletteBytes = paletteOwned;
                const Header paletteHeader = ReadStruct<Header>(
                    ReadDetail::Slice(paletteBytes, 0, Sizes::Header));
                const auto replacePalettes = DoOffsets<Palette>(
                    paletteBytes, paletteHeader.PaletteOffset, paletteHeader.PaletteCount);
                std::vector<std::shared_ptr<const std::vector<PaletteData>>> replacePaletteData;
                replacePaletteData.reserve(replacePalettes->size());
                for (const Palette& palette : *replacePalettes)
                {
                    replacePaletteData.push_back(GetPaletteData(palette, paletteBytes));
                }
                for (std::size_t i = 0; i < replacePaletteData.size(); ++i)
                {
                    const auto ids = meta.ReplaceIds.find(static_cast<int>(i));
                    if (ids != meta.ReplaceIds.end())
                    {
                        for (int replaceId : ids->second)
                        {
                            paletteData->at(static_cast<std::size_t>(replaceId)) = replacePaletteData[i];
                        }
                    }
                }
            }
            recolors->push_back(std::make_shared<Recolor>(meta.Name, textures, palettes,
                textureData, paletteData));
        }

        auto textureMatrices = std::make_shared<std::vector<OpenTK::Mathematics::Matrix4>>();
        if (name == "AlimbicCapsule")
        {
#ifndef NDEBUG
            assert(header.TextureMatrixCount == 1);
#endif
            OpenTK::Mathematics::Matrix4 textureMatrix = OpenTK::Mathematics::Matrix4::Zero;
            textureMatrix.M21 = Fixed::ToFloat(-2048);
            textureMatrix.M31 = Fixed::ToFloat(410);
            textureMatrix.M32 = Fixed::ToFloat(-3891);
            textureMatrices->push_back(textureMatrix);
        }

        const auto nodeWeights = DoOffsets<std::int32_t>(
            initialBytes, header.NodeWeightOffset, header.NodeWeightCount);
        std::shared_ptr<AnimationResults> animations = LoadAnimation(
            name, animationPath, nodes, firstHunt);
        if (animationShare.has_value())
        {
            const std::shared_ptr<AnimationResults> shared = LoadAnimation(
                name, animationShare, nodes, firstHunt);
            animations->NodeAnimationGroups->insert(animations->NodeAnimationGroups->end(),
                shared->NodeAnimationGroups->begin(), shared->NodeAnimationGroups->end());
            animations->MaterialAnimationGroups->insert(animations->MaterialAnimationGroups->end(),
                shared->MaterialAnimationGroups->begin(), shared->MaterialAnimationGroups->end());
            animations->TexcoordAnimationGroups->insert(animations->TexcoordAnimationGroups->end(),
                shared->TexcoordAnimationGroups->begin(), shared->TexcoordAnimationGroups->end());
            animations->TextureAnimationGroups->insert(animations->TextureAnimationGroups->end(),
                shared->TextureAnimationGroups->begin(), shared->TextureAnimationGroups->end());
            animations->NodeGroupOffsets->insert(animations->NodeGroupOffsets->end(),
                shared->NodeGroupOffsets->begin(), shared->NodeGroupOffsets->end());
            animations->MaterialGroupOffsets->insert(animations->MaterialGroupOffsets->end(),
                shared->MaterialGroupOffsets->begin(), shared->MaterialGroupOffsets->end());
            animations->TexcoordGroupOffsets->insert(animations->TexcoordGroupOffsets->end(),
                shared->TexcoordGroupOffsets->begin(), shared->TexcoordGroupOffsets->end());
            animations->TextureGroupOffsets->insert(animations->TextureGroupOffsets->end(),
                shared->TextureGroupOffsets->begin(), shared->TextureGroupOffsets->end());
        }
        const auto nodePos = DoOffsets<Vector3Fx>(initialBytes, header.NodePosition, header.NodeCount);
        const auto nodeInitPos = DoOffsets<Vector3Fx>(
            initialBytes, header.NodeInitialPosition, header.NodeCount);
        std::shared_ptr<const std::vector<std::int32_t>> posCounts;
        if (header.NodePosCounts != 0 && header.NodeWeightCount == 0)
        {
            const std::int32_t nodeWeightCount
                = UncheckedAdd(ReadDetail::ManagedInt32(header.NodePosCounts), -Sizes::Header) / 4;
            posCounts = DoOffsets<std::int32_t>(initialBytes, header.NodePosCounts, nodeWeightCount);
        }
        else
        {
            posCounts = DoOffsets<std::int32_t>(
                initialBytes, header.NodePosCounts, header.NodeWeightCount);
        }
        std::int32_t maxIndex = -1;
        if (header.NodePosCounts != 0)
        {
            for (std::int32_t n = 0; n < header.NodeWeightCount; ++n)
            {
                for (std::int32_t i = 0; i < posCounts->at(static_cast<std::size_t>(n)); ++i)
                {
                    const std::int32_t posIndex = UncheckedAdd(
                        i, nodeWeights->at(static_cast<std::size_t>(n)));
                    maxIndex = std::max(maxIndex, posIndex);
                }
            }
        }
        const auto posScales = DoOffsets<Fixed>(
            initialBytes, header.NodePosScales, UncheckedAdd(maxIndex, 1));
        return std::make_shared<Model>(name, firstHunt, header, nodes, meshes, materials,
            dlists, instructionLists, animations, textureMatrices, recolors, nodeWeights,
            nodePos, nodeInitPos, posCounts, posScales);
    }

    std::shared_ptr<AnimationResults> Read::LoadAnimation(
        const std::string& model, const std::optional<std::string>& inputPath,
        const std::shared_ptr<const std::vector<RawNode>>& nodes, bool firstHunt)
    {
        const std::string temp = model;
        (void)temp;
        auto results = std::make_shared<AnimationResults>();
        if (!inputPath.has_value())
        {
            return results;
        }
        const std::string path = Paths::Combine(
            firstHunt ? Paths::FhFileSystem() : Paths::FileSystem(), *inputPath);
        const std::vector<std::uint8_t> storage = MphRead::NativeRuntime::FileReadAllBytes(path);
        const std::span<const std::uint8_t> bytes(storage);
        const AnimationHeader header = ReadStruct<AnimationHeader>(bytes);
        const auto nodeOffsets = DoOffsets<std::uint32_t>(bytes, header.NodeGroupOffset, header.Count);
        const auto materialOffsets = DoOffsets<std::uint32_t>(bytes, header.MaterialGroupOffset, header.Count);
        const auto texcoordOffsets = DoOffsets<std::uint32_t>(bytes, header.TexcoordGroupOffset, header.Count);
        const auto textureOffsets = DoOffsets<std::uint32_t>(bytes, header.TextureGroupOffset, header.Count);
        AddRange(*results->NodeGroupOffsets, nodeOffsets);
        AddRange(*results->MaterialGroupOffsets, materialOffsets);
        AddRange(*results->TexcoordGroupOffsets, texcoordOffsets);
        AddRange(*results->TextureGroupOffsets, textureOffsets);

        for (std::uint32_t offset : *nodeOffsets)
        {
            if (offset == 0)
            {
                results->NodeAnimationGroups->push_back(NodeAnimationGroup::Empty());
                continue;
            }
            const RawNodeAnimationGroup raw = DoOffset<RawNodeAnimationGroup>(bytes, offset);
            if (nodes->empty())
            {
#ifndef NDEBUG
                assert(offset == raw.ScaleLutOffset && offset == raw.RotateLutOffset
                    && offset == raw.TranslateLutOffset && offset == raw.AnimationOffset);
#endif
                results->NodeAnimationGroups->push_back(std::make_shared<NodeAnimationGroup>(raw,
                    std::make_shared<const std::vector<float>>(),
                    std::make_shared<const std::vector<float>>(),
                    std::make_shared<const std::vector<float>>(),
                    std::make_shared<const NodeAnimationDictionary>()));
                continue;
            }
#ifndef NDEBUG
            assert(offset > raw.AnimationOffset);
            assert((offset - raw.AnimationOffset) % Sizes::NodeAnimation == 0);
            assert(raw.RotateLutOffset > raw.ScaleLutOffset);
            assert((raw.RotateLutOffset - raw.ScaleLutOffset) % 4 == 0);
            assert(raw.TranslateLutOffset > raw.RotateLutOffset);
            assert((raw.TranslateLutOffset - raw.RotateLutOffset) % 2 == 0);
            assert(raw.AnimationOffset > raw.TranslateLutOffset);
            assert((raw.AnimationOffset - raw.TranslateLutOffset) % 4 == 0);
#endif
            const std::int32_t count = ReadDetail::ManagedInt32(offset - raw.AnimationOffset)
                / Sizes::NodeAnimation;
            const auto rawAnimations = DoOffsets<NodeAnimation>(bytes, raw.AnimationOffset, count);
            auto animations = std::make_shared<NodeAnimationDictionary>();
            for (std::size_t i = 0; i < rawAnimations->size(); ++i)
            {
                const std::string animationName = i < nodes->size()
                    ? nodes->at(i).NameString() : NoNodeName(i);
                DictionaryAdd(*animations, animationName, rawAnimations->at(i));
            }
            const std::int32_t scaleCount = ReadDetail::ManagedInt32(
                raw.RotateLutOffset - raw.ScaleLutOffset) / 4;
            const std::int32_t rotationCount = ReadDetail::ManagedInt32(
                raw.TranslateLutOffset - raw.RotateLutOffset) / 2;
            const std::int32_t translationCount = ReadDetail::ManagedInt32(
                raw.AnimationOffset - raw.TranslateLutOffset) / 4;
            auto scales = std::make_shared<std::vector<float>>();
            const auto scaleLut = DoOffsets<Fixed>(bytes, raw.ScaleLutOffset, scaleCount);
            for (const Fixed& value : *scaleLut)
            {
                scales->push_back(value.FloatValue());
            }
            auto rotations = std::make_shared<std::vector<float>>();
            rotations->reserve(ManagedCapacity(rotationCount));
            const auto rotateLut = DoOffsets<std::uint16_t>(bytes, raw.RotateLutOffset, rotationCount);
            for (std::uint16_t value : *rotateLut)
            {
                rotations->push_back(static_cast<float>(value) / 65536.0F
                    * 2.0F * std::numbers::pi_v<float>);
            }
            auto translations = std::make_shared<std::vector<float>>();
            const auto translateLut = DoOffsets<Fixed>(bytes, raw.TranslateLutOffset, translationCount);
            for (const Fixed& value : *translateLut)
            {
                translations->push_back(value.FloatValue());
            }
            results->NodeAnimationGroups->push_back(std::make_shared<NodeAnimationGroup>(
                raw, scales, rotations, translations, animations));
        }

        for (std::uint32_t offset : *materialOffsets)
        {
            if (offset == 0)
            {
                results->MaterialAnimationGroups->push_back(MaterialAnimationGroup::Empty());
                continue;
            }
            const RawMaterialAnimationGroup raw = DoOffset<RawMaterialAnimationGroup>(bytes, offset);
            if (raw.AnimationCount == 0)
            {
#ifndef NDEBUG
                assert(offset == raw.ColorLutOffset && offset == raw.AnimationOffset);
#endif
                results->MaterialAnimationGroups->push_back(std::make_shared<MaterialAnimationGroup>(raw,
                    std::make_shared<const std::vector<float>>(),
                    std::make_shared<const MaterialAnimationDictionary>()));
                continue;
            }
#ifndef NDEBUG
            assert(raw.AnimationOffset > raw.ColorLutOffset);
#endif
            const auto rawAnimations = DoOffsets<MaterialAnimation>(
                bytes, raw.AnimationOffset, ReadDetail::ManagedInt32(raw.AnimationCount));
            auto animations = std::make_shared<MaterialAnimationDictionary>();
            for (const MaterialAnimation& animation : *rawAnimations)
            {
                DictionaryAdd(*animations, animation.NameString(), animation);
            }
            const std::int32_t colorCount = ReadDetail::ManagedInt32(
                raw.AnimationOffset - raw.ColorLutOffset);
            auto colors = std::make_shared<std::vector<float>>();
            const auto colorLut = DoOffsets<std::uint8_t>(bytes, raw.ColorLutOffset, colorCount);
            for (std::uint8_t value : *colorLut)
            {
                colors->push_back(static_cast<float>(value));
            }
            results->MaterialAnimationGroups->push_back(
                std::make_shared<MaterialAnimationGroup>(raw, colors, animations));
        }

        for (std::uint32_t offset : *texcoordOffsets)
        {
            if (offset == 0)
            {
                results->TexcoordAnimationGroups->push_back(TexcoordAnimationGroup::Empty());
                continue;
            }
            const RawTexcoordAnimationGroup raw = DoOffset<RawTexcoordAnimationGroup>(bytes, offset);
            if (raw.AnimationCount == 0)
            {
#ifndef NDEBUG
                assert(offset == raw.ScaleLutOffset && offset == raw.RotateLutOffset
                    && offset == raw.TranslateLutOffset && offset == raw.AnimationOffset);
#endif
                results->TexcoordAnimationGroups->push_back(std::make_shared<TexcoordAnimationGroup>(raw,
                    std::make_shared<const std::vector<float>>(),
                    std::make_shared<const std::vector<float>>(),
                    std::make_shared<const std::vector<float>>(),
                    std::make_shared<const TexcoordAnimationDictionary>()));
                continue;
            }
#ifndef NDEBUG
            assert(raw.RotateLutOffset > raw.ScaleLutOffset);
            assert((raw.RotateLutOffset - raw.ScaleLutOffset) % 4 == 0);
            assert(raw.TranslateLutOffset > raw.RotateLutOffset);
            assert((raw.TranslateLutOffset - raw.RotateLutOffset) % 2 == 0);
            assert(raw.AnimationOffset > raw.TranslateLutOffset);
            assert((raw.AnimationOffset - raw.TranslateLutOffset) % 4 == 0);
#endif
            const auto rawAnimations = DoOffsets<TexcoordAnimation>(
                bytes, raw.AnimationOffset, ReadDetail::ManagedInt32(raw.AnimationCount));
            auto animations = std::make_shared<TexcoordAnimationDictionary>();
            for (const TexcoordAnimation& animation : *rawAnimations)
            {
                DictionaryAdd(*animations, animation.NameString(), animation);
            }
            const std::int32_t scaleCount = ReadDetail::ManagedInt32(
                raw.RotateLutOffset - raw.ScaleLutOffset) / 4;
            const std::int32_t rotationCount = ReadDetail::ManagedInt32(
                raw.TranslateLutOffset - raw.RotateLutOffset) / 2;
            std::int32_t translationCount = ReadDetail::ManagedInt32(
                raw.AnimationOffset - raw.TranslateLutOffset) / 4;
            auto scales = std::make_shared<std::vector<float>>();
            const auto scaleLut2 = DoOffsets<Fixed>(bytes, raw.ScaleLutOffset, scaleCount);
            for (const Fixed& value : *scaleLut2)
            {
                scales->push_back(value.FloatValue());
            }
            auto rotations = std::make_shared<std::vector<float>>();
            rotations->reserve(ManagedCapacity(rotationCount));
            const auto rotateLut2 = DoOffsets<std::uint16_t>(bytes, raw.RotateLutOffset, rotationCount);
            for (std::uint16_t value : *rotateLut2)
            {
                rotations->push_back(static_cast<float>(value) / 65536.0F
                    * 2.0F * std::numbers::pi_v<float>);
            }
            if (offset == 3704 && model == "GuardBot1")
            {
                translationCount = 26;
            }
            auto translations = std::make_shared<std::vector<float>>();
            const auto translateLut2 = DoOffsets<Fixed>(bytes, raw.TranslateLutOffset, translationCount);
            for (const Fixed& value : *translateLut2)
            {
                translations->push_back(value.FloatValue());
            }
            results->TexcoordAnimationGroups->push_back(std::make_shared<TexcoordAnimationGroup>(
                raw, scales, rotations, translations, animations));
        }

        for (std::uint32_t offset : *textureOffsets)
        {
            if (offset == 0)
            {
                results->TextureAnimationGroups->push_back(TextureAnimationGroup::Empty());
                continue;
            }
            const RawTextureAnimationGroup raw = DoOffset<RawTextureAnimationGroup>(bytes, offset);
            if (raw.AnimationCount == 0)
            {
#ifndef NDEBUG
                assert(offset == raw.FrameIndexOffset && offset == raw.TextureIdOffset
                    && offset == raw.PaletteIdOffset && offset == raw.AnimationOffset);
                assert(raw.FrameIndexCount == 0 && raw.TextureIdCount == 0 && raw.PaletteIdCount == 0);
#endif
                results->TextureAnimationGroups->push_back(std::make_shared<TextureAnimationGroup>(raw,
                    std::make_shared<const std::vector<std::uint16_t>>(),
                    std::make_shared<const std::vector<std::uint16_t>>(),
                    std::make_shared<const std::vector<std::uint16_t>>(),
                    std::make_shared<const TextureAnimationDictionary>()));
                continue;
            }
            const auto rawAnimations = DoOffsets<TextureAnimation>(
                bytes, raw.AnimationOffset, raw.AnimationCount);
            auto animations = std::make_shared<TextureAnimationDictionary>();
            for (const TextureAnimation& animation : *rawAnimations)
            {
                DictionaryAdd(*animations, MarshalString(animation.Name), animation);
            }
            const auto frameIndices = DoOffsets<std::uint16_t>(bytes, raw.FrameIndexOffset, raw.FrameIndexCount);
            const auto textureIds = DoOffsets<std::uint16_t>(bytes, raw.TextureIdOffset, raw.TextureIdCount);
            const auto paletteIds = DoOffsets<std::uint16_t>(bytes, raw.PaletteIdOffset, raw.PaletteIdCount);
            results->TextureAnimationGroups->push_back(std::make_shared<TextureAnimationGroup>(
                raw, frameIndices, textureIds, paletteIds, animations));
        }
        return results;
    }

    std::vector<std::uint8_t> Read::ReadBytes(const std::string& path, bool firstHunt)
    {
        return MphRead::NativeRuntime::FileReadAllBytes(Paths::Combine(
            firstHunt ? Paths::FhFileSystem() : Paths::FileSystem(), path));
    }

    std::shared_ptr<const std::vector<TextureData>> Read::GetTextureData(
        Texture texture, std::span<const std::uint8_t> textureBytes)
    {
        auto data = std::make_shared<std::vector<TextureData>>();
        std::int32_t pixelCount = std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(texture.Width) * static_cast<std::uint32_t>(texture.Height));
        std::int32_t entriesPerByte = 1;
        if (texture.Format == TextureFormat::Palette2Bit)
        {
            entriesPerByte = 4;
        }
        else if (texture.Format == TextureFormat::Palette4Bit)
        {
            entriesPerByte = 2;
        }
        if (pixelCount % entriesPerByte != 0)
        {
            throw ProgramException("Pixel count " + std::to_string(pixelCount)
                + " is not divisible by " + std::to_string(entriesPerByte) + ".");
        }
        pixelCount /= entriesPerByte;
        if (texture.Format == TextureFormat::DirectRgb)
        {
            for (std::int32_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                const std::int32_t byteOffset = UncheckedAdd(
                    ReadDetail::ManagedInt32(texture.ImageOffset), UncheckedMultiply(pixelIndex, 2));
                const std::uint16_t color = SpanReadUshort(textureBytes, byteOffset);
                data->push_back(TextureData(color, AlphaFromShort(color)));
            }
        }
        else
        {
            for (std::int32_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                const std::int32_t byteOffset = UncheckedAdd(
                    ReadDetail::ManagedInt32(texture.ImageOffset), pixelIndex);
                const std::uint8_t entry = AtByte(textureBytes, byteOffset);
                for (std::int32_t entryIndex = 0; entryIndex < entriesPerByte; ++entryIndex)
                {
                    std::uint32_t index = static_cast<std::uint32_t>(entry >>
                        (((pixelIndex * entriesPerByte + entryIndex) % entriesPerByte)
                            * (8 / entriesPerByte)));
                    std::uint8_t alpha = 255;
                    if (texture.Format == TextureFormat::Palette2Bit)
                    {
                        index &= 0x3U;
                    }
                    else if (texture.Format == TextureFormat::Palette4Bit)
                    {
                        index &= 0xFU;
                    }
                    else if (texture.Format == TextureFormat::PaletteA5I3)
                    {
                        index &= 0x7U;
                        alpha = AlphaFromA5I3(entry);
                    }
                    else if (texture.Format == TextureFormat::PaletteA3I5)
                    {
                        index &= 0x1FU;
                        alpha = AlphaFromA3I5(entry);
                    }
                    if (texture.Format == TextureFormat::Palette2Bit
                        || texture.Format == TextureFormat::Palette4Bit
                        || texture.Format == TextureFormat::Palette8Bit)
                    {
                        if (texture.Opaque == 0 && index == 0)
                        {
                            alpha = 0;
                        }
                    }
                    data->push_back(TextureData(index, alpha));
                }
            }
        }
        return data;
    }

    std::shared_ptr<const std::vector<PaletteData>> Read::GetPaletteData(
        Palette palette, std::span<const std::uint8_t> paletteBytes)
    {
        if (palette.Size % 2U != 0)
        {
            throw ProgramException("Palette size " + std::to_string(palette.Size)
                + " is not divisible by 2.");
        }
        const std::int32_t count = ReadDetail::ManagedInt32(palette.Size) / 2;
        auto data = std::make_shared<std::vector<PaletteData>>();
        data->reserve(ManagedCapacity(count));
        for (std::int32_t i = 0; i < static_cast<std::int64_t>(palette.Size) / 2; ++i)
        {
            const std::int32_t byteOffset = UncheckedAdd(
                ReadDetail::ManagedInt32(palette.Offset), UncheckedMultiply(i, 2));
            const std::uint16_t entry = SpanReadUshort(paletteBytes, byteOffset);
            data->push_back(PaletteData(entry));
        }
        return data;
    }

    std::uint8_t Read::AlphaFromShort(std::uint16_t value) noexcept
    {
        return (value & 0x8000U) == 0 ? 0 : 255;
    }

    std::uint8_t Read::AlphaFromA5I3(std::uint8_t value) noexcept
    {
        return RoundByteToEven(static_cast<float>(value >> 3) / 31.0F * 255.0F);
    }

    std::uint8_t Read::AlphaFromA3I5(std::uint8_t value) noexcept
    {
        return RoundByteToEven(static_cast<float>(value >> 5) / 7.0F * 255.0F);
    }

    std::shared_ptr<const std::vector<std::shared_ptr<Entity>>> Read::GetEntities(
        const std::string& inputPath, std::int32_t layerId, bool firstHunt, bool allowHook)
    {
        const std::string path = Paths::Combine(
            firstHunt ? Paths::FhFileSystem() : Paths::FileSystem(), inputPath);
        return GetEntitiesFromPath(path, layerId, firstHunt, allowHook);
    }

    std::shared_ptr<const std::vector<std::shared_ptr<Entity>>> Read::GetEntitiesFromPath(
        const std::string& inputPath, std::int32_t layerId, bool firstHunt, bool allowHook)
    {
        const std::string path = Paths::Combine(
            firstHunt ? Paths::FhFileSystem() : Paths::FileSystem(), inputPath);
        const std::vector<std::uint8_t> storage = allowHook
            ? Utility::Repack::RepackHook(path, firstHunt)
            : ReadBytes(path, firstHunt);
        const std::span<const std::uint8_t> bytes(storage);
        const std::uint32_t version = SpanReadUint(bytes, 0);
        if (version == 1)
        {
            return GetFirstHuntEntities(bytes);
        }
        if (version != 2)
        {
            throw ProgramException("Unexpected entity header version " + std::to_string(version) + ".");
        }
        auto entities = std::make_shared<std::vector<std::shared_ptr<Entity>>>();
        const EntityHeader header = ReadStruct<EntityHeader>(
            ReadDetail::Slice(bytes, 0, Sizes::EntityHeader));
        for (std::int32_t i = 0;; i = UncheckedAdd(i, 1))
        {
            const std::int32_t start = UncheckedAdd(
                Sizes::EntityHeader, UncheckedMultiply(Sizes::EntityEntry, i));
            const EntityEntry entry = ReadStruct<EntityEntry>(
                ReadDetail::Slice(bytes, start, Sizes::EntityEntry));
            if (entry.DataOffset == 0)
            {
                break;
            }
            const std::uint32_t layerBit = 1U << (static_cast<std::uint32_t>(layerId) & 31U);
            if (layerId == -1 || (static_cast<std::uint32_t>(entry.LayerMask) & layerBit) != 0)
            {
                entities->push_back(ReadEntity(bytes, entry));
            }
        }
#ifndef NDEBUG
        if (layerId != -1)
        {
            assert(entities->size() == header.Lengths[layerId]);
        }
#endif
        return entities;
    }

    std::shared_ptr<Entity> Read::ReadEntity(
        std::span<const std::uint8_t> bytes, EntityEntry entry)
    {
        const std::int32_t start = ReadDetail::ManagedInt32(entry.DataOffset);
        const EntityDataHeader header = ReadStruct<EntityDataHeader>(
            ReadDetail::Slice(bytes, start, Sizes::EntityDataHeader));
        const EntityType type = static_cast<EntityType>(header.Type);
        switch (type)
        {
        case EntityType::Platform: return ReadEntityOf<PlatformEntityData>(bytes, entry, header);
        case EntityType::Object: return ReadEntityOf<ObjectEntityData>(bytes, entry, header);
        case EntityType::PlayerSpawn: return ReadEntityOf<PlayerSpawnEntityData>(bytes, entry, header);
        case EntityType::Door: return ReadEntityOf<DoorEntityData>(bytes, entry, header);
        case EntityType::ItemSpawn: return ReadEntityOf<ItemSpawnEntityData>(bytes, entry, header);
        case EntityType::EnemySpawn: return ReadEntityOf<EnemySpawnEntityData>(bytes, entry, header);
        case EntityType::TriggerVolume: return ReadEntityOf<TriggerVolumeEntityData>(bytes, entry, header);
        case EntityType::AreaVolume: return ReadEntityOf<AreaVolumeEntityData>(bytes, entry, header);
        case EntityType::JumpPad: return ReadEntityOf<JumpPadEntityData>(bytes, entry, header);
        case EntityType::PointModule: return ReadEntityOf<PointModuleEntityData>(bytes, entry, header);
        case EntityType::MorphCamera: return ReadEntityOf<MorphCameraEntityData>(bytes, entry, header);
        case EntityType::OctolithFlag: return ReadEntityOf<OctolithFlagEntityData>(bytes, entry, header);
        case EntityType::FlagBase: return ReadEntityOf<FlagBaseEntityData>(bytes, entry, header);
        case EntityType::Teleporter: return ReadEntityOf<TeleporterEntityData>(bytes, entry, header);
        case EntityType::NodeDefense: return ReadEntityOf<NodeDefenseEntityData>(bytes, entry, header);
        case EntityType::LightSource: return ReadEntityOf<LightSourceEntityData>(bytes, entry, header);
        case EntityType::Artifact: return ReadEntityOf<ArtifactEntityData>(bytes, entry, header);
        case EntityType::CameraSequence: return ReadEntityOf<CameraSequenceEntityData>(bytes, entry, header);
        case EntityType::ForceField: return ReadEntityOf<ForceFieldEntityData>(bytes, entry, header);
        default:
            throw ProgramException("Invalid entity type " + EntityTypeString(type));
        }
    }

    std::shared_ptr<const std::vector<std::shared_ptr<Entity>>> Read::GetFirstHuntEntities(
        std::span<const std::uint8_t> bytes)
    {
        auto entities = std::make_shared<std::vector<std::shared_ptr<Entity>>>();
        for (std::int32_t i = 0;; i = UncheckedAdd(i, 1))
        {
            const std::int32_t start = UncheckedAdd(
                static_cast<std::int32_t>(sizeof(std::uint32_t)),
                UncheckedMultiply(Sizes::FhEntityEntry, i));
            const FhEntityEntry entry = ReadStruct<FhEntityEntry>(
                ReadDetail::Slice(bytes, start, Sizes::EntityEntry));
            if (entry.DataOffset == 0)
            {
                break;
            }
            entities->push_back(ReadFirstHuntEntity(bytes, entry));
        }
        return entities;
    }

    std::shared_ptr<Entity> Read::ReadFirstHuntEntity(
        std::span<const std::uint8_t> bytes, FhEntityEntry entry)
    {
        const std::int32_t start = ReadDetail::ManagedInt32(entry.DataOffset);
        const EntityDataHeader header = ReadStruct<EntityDataHeader>(
            ReadDetail::Slice(bytes, start, Sizes::EntityDataHeader));
        const EntityType type = static_cast<EntityType>(
            static_cast<std::uint16_t>(header.Type + 100));
        switch (type)
        {
        case EntityType::FhPlayerSpawn: return ReadFirstHuntEntityOf<PlayerSpawnEntityData>(bytes, entry, header);
        case EntityType::FhDoor: return ReadFirstHuntEntityOf<FhDoorEntityData>(bytes, entry, header);
        case EntityType::FhItemSpawn: return ReadFirstHuntEntityOf<FhItemSpawnEntityData>(bytes, entry, header);
        case EntityType::FhEnemySpawn: return ReadFirstHuntEntityOf<FhEnemySpawnEntityData>(bytes, entry, header);
        case EntityType::FhTriggerVolume: return ReadFirstHuntEntityOf<FhTriggerVolumeEntityData>(bytes, entry, header);
        case EntityType::FhAreaVolume: return ReadFirstHuntEntityOf<FhAreaVolumeEntityData>(bytes, entry, header);
        case EntityType::FhPlatform: return ReadFirstHuntEntityOf<FhPlatformEntityData>(bytes, entry, header);
        case EntityType::FhJumpPad: return ReadFirstHuntEntityOf<FhJumpPadEntityData>(bytes, entry, header);
        case EntityType::FhPointModule: return ReadFirstHuntEntityOf<PointModuleEntityData>(bytes, entry, header);
        case EntityType::FhMorphCamera: return ReadFirstHuntEntityOf<FhMorphCameraEntityData>(bytes, entry, header);
        default:
            throw ProgramException("Invalid entity type " + EntityTypeString(type));
        }
    }

    std::shared_ptr<Effect> Read::GetEffect(std::int32_t id)
    {
        const auto iterator = EffectsCache().find(id);
        return iterator == EffectsCache().end() ? nullptr : iterator->second;
    }

    std::shared_ptr<Effect> Read::LoadEffect(std::int32_t id, bool persistent)
    {
        if (id < 1 || id > static_cast<std::int32_t>(Metadata::Effects.size()))
        {
            throw ProgramException("Could not get particle.");
        }
        const auto& meta = Metadata::Effects.at(static_cast<std::size_t>(id));
        std::shared_ptr<Effect> effect = LoadEffectByName(id, meta.first, meta.second, persistent);
        effect->Persistent = effect->Persistent || persistent;
        return effect;
    }

    std::shared_ptr<Effect> Read::LoadEffectByName(
        std::int32_t id, const std::string& name,
        const std::optional<std::string>& archive, bool persistent)
    {
        const std::string path = archive.has_value()
            ? "_archives/" + *archive + "/" + name + "_PS.bin"
            : "effects/" + name + "_PS.bin";
        std::shared_ptr<Effect> effect = LoadEffectFromPath(id, path);
        for (const std::shared_ptr<EffectElement>& element : *effect->Elements)
        {
            if (element->ChildEffectId != 0)
            {
                (void)LoadEffect(static_cast<std::int32_t>(element->ChildEffectId), persistent);
            }
        }
        effect->Persistent = effect->Persistent || persistent;
        return effect;
    }

    std::shared_ptr<Effect> Read::LoadEffectFromPath(std::int32_t id, const std::string& path)
    {
        if (id != -1)
        {
            const auto iterator = EffectsCache().find(id);
            if (iterator != EffectsCache().end())
            {
                return iterator->second;
            }
        }
        const std::vector<std::uint8_t> storage = MphRead::NativeRuntime::FileReadAllBytes(
            Paths::Combine(Paths::FileSystem(), path));
        const std::span<const std::uint8_t> bytes(storage);
        const RawEffect rawEffect = ReadStruct<RawEffect>(bytes);
        auto funcs = std::make_shared<Effects::EffectFuncDictionary>();
        const auto funcOffsets = DoOffsets<std::uint32_t>(
            bytes, rawEffect.FuncOffset, rawEffect.FuncCount);
        for (std::uint32_t offset : *funcOffsets)
        {
            const std::uint32_t funcId = SpanReadUint(bytes, offset);
            const std::uint32_t paramOffset = SpanReadUint(bytes, offset + 4U);
#ifndef NDEBUG
            DebugValidateParams(funcId, offset, paramOffset);
#endif
            const std::uint32_t paramCount = (offset - paramOffset) / 4U;
            const auto parameters = DoOffsets<std::int32_t>(bytes, paramOffset, paramCount);
            if (!funcs->emplace(offset, std::make_shared<FxFuncInfo>(funcId, parameters)).second)
            {
                throw std::invalid_argument("An item with the same key has already been added.");
            }
        }
        const auto list2 = DoOffsets<std::uint32_t>(bytes, rawEffect.Offset2, rawEffect.Count2);
        const auto elementOffsets = DoOffsets<std::uint32_t>(
            bytes, rawEffect.ElementOffset, rawEffect.ElementCount);
        auto elements = std::make_shared<std::vector<std::shared_ptr<EffectElement>>>();
        elements->reserve(elementOffsets->size());
        for (std::uint32_t offset : *elementOffsets)
        {
            const RawEffectElement element = DoOffset<RawEffectElement>(bytes, offset);
            auto particles = std::make_shared<std::vector<std::shared_ptr<Particle>>>();
            particles->reserve(ManagedCapacity(ReadDetail::ManagedInt32(element.ParticleCount)));
            const auto nameOffsets = DoOffsets<std::uint32_t>(
                bytes, element.ParticleOffset, element.ParticleCount);
            for (std::uint32_t nameOffset : *nameOffsets)
            {
                particles->push_back(GetParticle(
                    element.ModelNameString(), ReadString(bytes, nameOffset, 16)));
            }
            auto actions = std::make_shared<Effects::EffectActionDictionary>();
            const auto metadata = DoOffsets<std::uint32_t>(
                bytes, element.FuncOffset, 2U * element.FuncCount);
            for (std::size_t i = 0; i < metadata->size(); i += 2U)
            {
                const std::uint32_t index = metadata->at(i);
                const std::uint32_t funcOffset = metadata->at(i + 1U);
                if (funcOffset != 0)
                {
                    const auto function = funcs->at(funcOffset);
                    if (!actions->emplace(static_cast<FuncAction>(index), function).second)
                    {
                        throw std::invalid_argument("An item with the same key has already been added.");
                    }
                }
            }
            elements->push_back(std::make_shared<EffectElement>(element, particles, funcs, actions));
        }
        auto effect = std::make_shared<Effect>(id, rawEffect, funcs, list2, elements, path);
        if (id != -1)
        {
            if (!EffectsCache().emplace(id, effect).second)
            {
                throw std::invalid_argument("An item with the same key has already been added.");
            }
        }
        return effect;
    }

    std::shared_ptr<Particle> Read::GetSingleParticle(SingleType type)
    {
        const auto iterator = Metadata::SingleParticles.find(type);
        if (iterator != Metadata::SingleParticles.end())
        {
            return GetParticle(iterator->second.first, iterator->second.second);
        }
        throw ProgramException("Could not get single particle.");
    }

    std::shared_ptr<Particle> Read::GetParticle(
        const std::string& modelName, const std::string& particleName)
    {
        const ParticleKey key{modelName, particleName};
        const auto cached = Particles().find(key);
        if (cached != Particles().end())
        {
            return cached->second;
        }
        const std::shared_ptr<ModelInstance> inst = GetModelInstance(modelName);
        const std::shared_ptr<Model> model = inst->Model();
        std::shared_ptr<Node> node;
        for (const std::shared_ptr<Node>& candidate : *model->Nodes)
        {
            if (candidate->Name == particleName)
            {
                node = candidate;
                break;
            }
        }
        if (modelName == "geo1" && particleName == "gib")
        {
            node.reset();
            for (const std::shared_ptr<Node>& candidate : *model->Nodes)
            {
                if (candidate->Name == "gib3")
                {
                    node = candidate;
                    break;
                }
            }
            if (!node)
            {
                throw std::invalid_argument("Sequence contains no matching element");
            }
        }
        if (node && node->MeshCount > 0)
        {
            const std::int32_t materialId = model->Meshes->at(
                static_cast<std::size_t>(node->MeshId / 2))->MaterialId;
            auto particle = std::make_shared<Particle>(particleName, model, node, materialId);
            if (!Particles().emplace(key, particle).second)
            {
                throw std::invalid_argument("An item with the same key has already been added.");
            }
            return particle;
        }
        throw ProgramException("Could not get particle.");
    }

    void Read::DebugValidateParams(
        std::uint32_t funcId, std::uint32_t funcOffset, std::uint32_t paramOffset)
    {
#ifndef NDEBUG
        assert(paramOffset == 0 || paramOffset < funcOffset);
        std::uint32_t count = 0;
        if (paramOffset != 0)
        {
            count = (funcOffset - paramOffset) / 4U;
        }
        switch (funcId)
        {
        case 1: case 5: case 8: case 9: case 11: case 22: case 23: case 24:
        case 25: case 26: case 29: case 31: case 32: case 35: case 43: case 44: case 45:
            assert(count == 0); break;
        case 39: case 42:
            assert(count == 1); break;
        case 13: case 14: case 15: case 16: case 17: case 19: case 20:
        case 46: case 47: case 48:
            assert(count == 2); break;
        case 4: case 40:
            assert(count == 3); break;
        case 49:
            assert(count == 4); break;
        case 41:
            assert(count >= 4); break;
        default:
            assert(false); break;
        }
#else
        (void)funcId;
        (void)funcOffset;
        (void)paramOffset;
#endif
    }

    void Read::Nop() noexcept
    {
    }

    std::shared_ptr<const std::vector<std::shared_ptr<RenderInstruction>>> Read::DoRenderInstructions(
        std::span<const std::uint8_t> bytes, DisplayList dlist)
    {
        if (dlist.Size % 4U != 0)
        {
            throw ProgramException("Dlist size " + std::to_string(dlist.Size) + " not divisible by 4.");
        }
        auto list = std::make_shared<std::vector<std::shared_ptr<RenderInstruction>>>();
        std::int32_t pointer = ReadDetail::ManagedInt32(dlist.Offset);
        const std::int32_t endPointer = UncheckedAdd(pointer, ReadDetail::ManagedInt32(dlist.Size));
        if (endPointer >= static_cast<std::int32_t>(bytes.size()))
        {
            throw ProgramException("End pointer " + std::to_string(endPointer)
                + " too large for dlist size " + std::to_string(bytes.size()) + ".");
        }
        while (pointer < endPointer)
        {
            std::uint32_t packed = SpanReadUint(bytes, std::ref(pointer));
            for (std::int32_t i = 0; i < 4; ++i)
            {
                const InstructionCode instruction = static_cast<InstructionCode>(
                    ((packed & 0xFFU) << 2U) + 0x400U);
                const std::int32_t arity = RenderInstruction::GetArity(instruction);
                auto arguments = std::make_shared<std::vector<std::uint32_t>>();
                arguments->reserve(ManagedCapacity(arity));
                for (std::int32_t j = 0; j < arity; ++j)
                {
                    arguments->push_back(SpanReadUint(bytes, std::ref(pointer)));
                }
                list->push_back(std::make_shared<RenderInstruction>(instruction, arguments));
                packed >>= 8U;
            }
        }
        return list;
    }

    std::int32_t Read::SpanReadInt(
        std::span<const std::uint8_t> bytes, std::reference_wrapper<std::int32_t> offset)
    {
        std::int32_t& current = offset.get();
        const std::int32_t result = ReadStruct<std::int32_t>(
            ReadDetail::Slice(bytes, current, static_cast<std::int32_t>(sizeof(std::int32_t))));
        current = UncheckedAdd(current, static_cast<std::int32_t>(sizeof(std::int32_t)));
        return result;
    }

    std::int32_t Read::SpanReadInt(std::span<const std::uint8_t> bytes, std::uint32_t offset)
    {
        return SpanReadInt(bytes, ReadDetail::ManagedInt32(offset));
    }

    std::int32_t Read::SpanReadInt(std::span<const std::uint8_t> bytes, std::int32_t offset)
    {
        return SpanReadInt(bytes, std::ref(offset));
    }

    std::uint32_t Read::SpanReadUint(
        std::span<const std::uint8_t> bytes, std::reference_wrapper<std::int32_t> offset)
    {
        std::int32_t& current = offset.get();
        const std::uint32_t result = ReadStruct<std::uint32_t>(
            ReadDetail::Slice(bytes, current, static_cast<std::int32_t>(sizeof(std::uint32_t))));
        current = UncheckedAdd(current, static_cast<std::int32_t>(sizeof(std::uint32_t)));
        return result;
    }

    std::uint32_t Read::SpanReadUint(std::span<const std::uint8_t> bytes, std::uint32_t offset)
    {
        return SpanReadUint(bytes, ReadDetail::ManagedInt32(offset));
    }

    std::uint32_t Read::SpanReadUint(std::span<const std::uint8_t> bytes, std::int32_t offset)
    {
        return SpanReadUint(bytes, std::ref(offset));
    }

    std::uint16_t Read::SpanReadUshort(
        std::span<const std::uint8_t> bytes, std::reference_wrapper<std::int32_t> offset)
    {
        std::int32_t& current = offset.get();
        const std::uint16_t result = ReadStruct<std::uint16_t>(
            ReadDetail::Slice(bytes, current, static_cast<std::int32_t>(sizeof(std::uint16_t))));
        current = UncheckedAdd(current, static_cast<std::int32_t>(sizeof(std::uint16_t)));
        return result;
    }

    std::uint16_t Read::SpanReadUshort(std::span<const std::uint8_t> bytes, std::uint32_t offset)
    {
        return SpanReadUshort(bytes, ReadDetail::ManagedInt32(offset));
    }

    std::uint16_t Read::SpanReadUshort(std::span<const std::uint8_t> bytes, std::int32_t offset)
    {
        return SpanReadUshort(bytes, std::ref(offset));
    }

    std::string Read::GetModelName(const std::string& input)
    {
        std::string path = input;
        std::size_t position = 0;
        while ((position = path.find("_mdl_", position)) != std::string::npos)
        {
            path.replace(position, 5, "_");
            ++position;
        }
        position = 0;
        if (path.find("_Model.bin") != std::string::npos)
        {
            while ((position = path.find("_Model.bin", position)) != std::string::npos)
            {
                path.erase(position, 10);
            }
        }
        else if (path.find("_model.bin") != std::string::npos)
        {
            while ((position = path.find("_model.bin", position)) != std::string::npos)
            {
                path.erase(position, 10);
            }
        }
        return FileNameWithoutExtension(path);
    }

    std::shared_ptr<const std::vector<std::uint32_t>> Read::DoListNullEnd(
        std::span<const std::uint8_t> bytes, std::uint32_t offset)
    {
        auto results = std::make_shared<std::vector<std::uint32_t>>();
        std::int32_t ioffset = ReadDetail::ManagedInt32(offset);
        if (offset != 0)
        {
            const std::int32_t size = static_cast<std::int32_t>(sizeof(std::uint32_t));
            for (std::uint32_t i = 0;; ++i)
            {
                (void)i;
                const std::uint32_t result = ReadStruct<std::uint32_t>(
                    ReadDetail::Slice(bytes, ioffset, size));
                if (result == 0)
                {
                    break;
                }
                results->push_back(result);
                ioffset = UncheckedAdd(ioffset, size);
            }
        }
        return results;
    }

    std::string Read::ReadString(
        std::span<const std::uint8_t> bytes, std::uint32_t offset, std::int32_t length)
    {
        return ReadString(bytes, ReadDetail::ManagedInt32(offset), length);
    }

    std::string Read::ReadString(
        std::span<const std::uint8_t> bytes, std::int32_t offset, std::int32_t length)
    {
        std::int32_t end = offset;
        for (std::int32_t i = 0; i < length; i = UncheckedAdd(i, 1))
        {
            if (AtByte(bytes, UncheckedAdd(offset, i)) == 0)
            {
                break;
            }
            end = UncheckedAdd(end, 1);
        }
        if (end == offset)
        {
            return {};
        }
        return AsciiDecode(ReadDetail::Slice(bytes, offset, end - offset));
    }

    std::string Read::ReadStringTable(
        std::span<const std::uint8_t> bytes, std::uint32_t offset, std::int32_t length)
    {
        return ReadStringTable(bytes, ReadDetail::ManagedInt32(offset), length);
    }

    std::string Read::ReadStringTable(
        std::span<const std::uint8_t> bytes, std::int32_t offset, std::int32_t length)
    {
        std::int32_t end = offset;
        for (std::int32_t i = 0; i < length; i = UncheckedAdd(i, 1))
        {
            if (AtByte(bytes, UncheckedAdd(offset, i)) == 0)
            {
                break;
            }
            end = UncheckedAdd(end, 1);
        }
        if (end == offset)
        {
            return {};
        }
        std::string result;
        for (std::int32_t i = offset; i < end; i = UncheckedAdd(i, 1))
        {
            AppendLatin1CodePoint(result, AtByte(bytes, i));
        }
        return result;
    }

    std::shared_ptr<const std::vector<std::string>> Read::ReadStrings(
        std::span<const std::uint8_t> bytes, std::int64_t offset, std::int32_t count)
    {
        return ReadStrings(bytes, Int64ToInt32(offset), count);
    }

    std::shared_ptr<const std::vector<std::string>> Read::ReadStrings(
        std::span<const std::uint8_t> bytes, std::int64_t offset, std::uint32_t count)
    {
        return ReadStrings(bytes, Int64ToInt32(offset), ReadDetail::ManagedInt32(count));
    }

    std::shared_ptr<const std::vector<std::string>> Read::ReadStrings(
        std::span<const std::uint8_t> bytes, std::uint32_t offset, std::uint32_t count)
    {
        return ReadStrings(bytes, ReadDetail::ManagedInt32(offset), ReadDetail::ManagedInt32(count));
    }

    std::shared_ptr<const std::vector<std::string>> Read::ReadStrings(
        std::span<const std::uint8_t> bytes, std::int32_t offset, std::int32_t count)
    {
        auto strings = std::make_shared<std::vector<std::string>>();
        strings->reserve(ManagedCapacity(count));
        while (static_cast<std::int32_t>(strings->size()) < count)
        {
            std::int32_t end = offset;
            std::uint8_t value = AtByte(bytes, end);
            while (value != 0)
            {
                end = UncheckedAdd(end, 1);
                value = AtByte(bytes, end);
            }
            if (end == offset)
            {
                strings->emplace_back();
            }
            else
            {
                strings->push_back(AsciiDecode(ReadDetail::Slice(bytes, offset, end - offset)));
            }
            offset = UncheckedAdd(end, 1);
        }
        return strings;
    }

    void Read::ExtractArchive(const std::string& path)
    {
        const std::filesystem::path input = ::MphRead::NativeRuntime::PathFromUtf8(path);
        const std::string name = ::MphRead::NativeRuntime::PathToUtf8(input.stem());
        const std::filesystem::path parent = input.has_parent_path() ? input.parent_path() : std::filesystem::path();
        const std::filesystem::path outputPath = std::filesystem::absolute(
            parent / ".." / "_archives" / ::MphRead::NativeRuntime::PathFromUtf8(name)).lexically_normal();
        const std::string output = ::MphRead::NativeRuntime::PathToUtf8(outputPath);
        try
        {
            std::int32_t filesWritten = 0;
            std::filesystem::create_directories(outputPath);
            std::cout << "Reading " << name << "...";
            const std::vector<std::uint8_t> bytes = MphRead::NativeRuntime::FileReadAllBytes(path);
            const std::string magic = AsciiDecode(ReadDetail::Slice(bytes, 0, 8));
            if (magic == Archive::Archiver::MagicString())
            {
                std::cout << " Extracting archive...";
                filesWritten = Archive::Archiver::Extract(path, output);
            }
            else if (AtByte(bytes, 0) == LZ10::MagicByte())
            {
                std::cout << " Decompressing...";
                const std::string compressedData(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size());
                std::istringstream compressed(
                    compressedData, std::ios::in | std::ios::binary);
                std::ostringstream decompressed(std::ios::out | std::ios::binary);
                (void)LZ10::Decompress(
                    compressed, static_cast<std::int64_t>(bytes.size()), decompressed);
                const std::string decompressedData = decompressed.str();
                std::cout << " Extracting archive...";
                const auto* decompressedBytes
                    = reinterpret_cast<const std::uint8_t*>(decompressedData.data());
                filesWritten = Archive::Archiver::Extract(
                    std::span<const std::uint8_t>(decompressedBytes, decompressedData.size()), output);
            }
            std::cout << std::endl;
            std::cout << "Extracted " << filesWritten << " file"
                << (filesWritten == 1 ? "" : "s") << "." << std::endl;
            if (filesWritten == 0)
            {
                throw ProgramException(name + " yielded no files.");
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << std::endl;
            std::cout << "Failed to extract archive " << name << ": "
                << ex.what() << std::endl;
            std::cout << "Verify an archive exists at " << path << "." << std::endl;
        }
        catch (...)
        {
            std::cout << std::endl;
            std::cout << "Failed to extract archive " << name
                << ": unknown exception" << std::endl;
            std::cout << "Verify an archive exists at " << path << "." << std::endl;
        }
    }

    void Read::ReadAndExport(const std::string& name, bool firstHunt, MetaDir dir)
    {
        std::shared_ptr<Model> model;
        if (const std::shared_ptr<ModelInstance> inst = GetModelInstanceOrNull(name, firstHunt, dir, true))
        {
            model = inst->Model();
        }
        if (!model)
        {
            if (const std::shared_ptr<ModelInstance> room = GetRoomModelInstanceOrNull(name))
            {
                model = room->Model();
            }
        }
        if (!model)
        {
            std::cout << "No model or room with the name " << name << " could be found." << std::endl;
            return;
        }
        try
        {
            Export::Images::ExportImages(*model);
            Export::Collada::ExportModel(*model);
            std::cout << "Exported successfully." << std::endl;
        }
        catch (...)
        {
            std::cout << "Failed to export model. Verify your export path is accessible." << std::endl;
        }
    }

    void Read::DumpEntityList(
        const std::shared_ptr<const std::vector<std::shared_ptr<Entity>>>& entities)
    {
        if (!entities)
        {
            throw System::NullReferenceException();
        }
        std::vector<EntityType> types;
        for (const std::shared_ptr<Entity>& entity : *entities)
        {
            if (std::find(types.begin(), types.end(), entity->Type) == types.end())
            {
                types.push_back(entity->Type);
            }
        }
        for (EntityType type : types)
        {
            std::size_t count = 0;
            for (const std::shared_ptr<Entity>& entity : *entities)
            {
                if (entity->Type == type)
                {
                    ++count;
                }
            }
            std::cout << count << "x " << EntityTypeString(type) << std::endl;
        }
        std::cout << std::endl;
        for (const std::shared_ptr<Entity>& entity : *entities)
        {
            std::cout << EntityTypeString(entity->Type) << std::endl;
        }
    }
}
