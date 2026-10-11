#pragma once

#include "Formats/Model.hpp"
#include "Utility/Compress.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace MphRead
{
    enum class MetaDir : std::int32_t;
    class RoomMetadata;
    class RecolorMetadata;

    class AnimationResults
    {
    public:
        const std::shared_ptr<std::vector<std::shared_ptr<NodeAnimationGroup>>> NodeAnimationGroups
            = std::make_shared<std::vector<std::shared_ptr<NodeAnimationGroup>>>();
        const std::shared_ptr<std::vector<std::shared_ptr<MaterialAnimationGroup>>> MaterialAnimationGroups
            = std::make_shared<std::vector<std::shared_ptr<MaterialAnimationGroup>>>();
        const std::shared_ptr<std::vector<std::shared_ptr<TexcoordAnimationGroup>>> TexcoordAnimationGroups
            = std::make_shared<std::vector<std::shared_ptr<TexcoordAnimationGroup>>>();
        const std::shared_ptr<std::vector<std::shared_ptr<TextureAnimationGroup>>> TextureAnimationGroups
            = std::make_shared<std::vector<std::shared_ptr<TextureAnimationGroup>>>();
        const std::shared_ptr<std::vector<std::uint32_t>> NodeGroupOffsets
            = std::make_shared<std::vector<std::uint32_t>>();
        const std::shared_ptr<std::vector<std::uint32_t>> MaterialGroupOffsets
            = std::make_shared<std::vector<std::uint32_t>>();
        const std::shared_ptr<std::vector<std::uint32_t>> TexcoordGroupOffsets
            = std::make_shared<std::vector<std::uint32_t>>();
        const std::shared_ptr<std::vector<std::uint32_t>> TextureGroupOffsets
            = std::make_shared<std::vector<std::uint32_t>>();

        AnimationResults() = default;
        AnimationResults(const AnimationResults&) = delete;
        AnimationResults& operator=(const AnimationResults&) = delete;
        AnimationResults(AnimationResults&&) = delete;
        AnimationResults& operator=(AnimationResults&&) = delete;
    };

    namespace ReadDetail
    {
        [[noreturn]] inline void ThrowRange()
        {
            throw std::out_of_range("Specified argument was out of the range of valid values.");
        }

        [[nodiscard]] inline std::span<const std::uint8_t> Slice(
            std::span<const std::uint8_t> bytes, std::int32_t start, std::int32_t length)
        {
            if (start < 0 || length < 0)
            {
                ThrowRange();
            }
            const std::size_t offset = static_cast<std::size_t>(start);
            const std::size_t count = static_cast<std::size_t>(length);
            if (offset > bytes.size() || count > bytes.size() - offset)
            {
                ThrowRange();
            }
            return bytes.subspan(offset, count);
        }

        [[nodiscard]] inline std::span<const std::uint8_t> Slice(
            std::span<const std::uint8_t> bytes, std::int32_t start)
        {
            if (start < 0 || static_cast<std::size_t>(start) > bytes.size())
            {
                ThrowRange();
            }
            return bytes.subspan(static_cast<std::size_t>(start));
        }

        template <typename T>
        inline void MaterializeByValArrays(T& value, std::span<const std::uint8_t> bytes)
        {
            if constexpr (requires(T& object, const std::uint8_t* pointer)
                { object.Name.SetMarshaledBytes(pointer); })
            {
                value.Name.SetMarshaledBytes(bytes.data() + offsetof(T, Name));
            }
            if constexpr (requires(T& object, const std::uint8_t* pointer)
                { object.ModelName.SetMarshaledBytes(pointer); })
            {
                value.ModelName.SetMarshaledBytes(bytes.data() + offsetof(T, ModelName));
            }
            if constexpr (requires(T& object, const std::uint8_t* pointer)
                { object.NodeName.SetMarshaledBytes(pointer); })
            {
                value.NodeName.SetMarshaledBytes(bytes.data() + offsetof(T, NodeName));
            }
            if constexpr (requires(T& object, const std::uint8_t* pointer)
                { object.Id.SetMarshaledBytes(pointer); })
            {
                value.Id.SetMarshaledBytes(bytes.data() + offsetof(T, Id));
            }
            if constexpr (requires(T& object, const std::uint8_t* pointer)
                { object.Filename.SetMarshaledBytes(pointer); })
            {
                value.Filename.SetMarshaledBytes(bytes.data() + offsetof(T, Filename));
            }
        }

        // How many bytes the type occupies unmarshalled. It is sizeof(T)
        // unless the type says otherwise, which it does when its C++ layout is
        // not its ABI layout -- a managed array member, say, which is a
        // pointer here and bytes on the wire.
        template <typename T>
        [[nodiscard]] inline constexpr std::size_t MarshaledSize() noexcept
        {
            if constexpr (requires { T::MarshaledSize; })
            {
                return T::MarshaledSize;
            }
            else
            {
                return sizeof(T);
            }
        }

        template <typename T>
        [[nodiscard]] inline T MarshalRead(std::span<const std::uint8_t> bytes)
        {
            constexpr std::size_t size = MarshaledSize<T>();
            if (bytes.size() < size)
            {
                ThrowRange();
            }
            if constexpr (requires(const std::array<std::uint8_t, size>& raw)
                { T::FromMarshaledBytes(raw); })
            {
                std::array<std::uint8_t, size> raw{};
                std::memcpy(raw.data(), bytes.data(), size);
                return T::FromMarshaledBytes(raw);
            }
            else
            {
                T value{};
                std::memcpy(static_cast<void*>(std::addressof(value)), bytes.data(), size);
                MaterializeByValArrays(value, bytes.first(size));
                return value;
            }
        }

        [[nodiscard]] constexpr std::int32_t ManagedInt32(std::uint32_t value) noexcept
        {
            return std::bit_cast<std::int32_t>(value);
        }

        [[nodiscard]] constexpr std::uint32_t ManagedUInt32(std::int32_t value) noexcept
        {
            return std::bit_cast<std::uint32_t>(value);
        }
    }

    class Read final
    {
    public:
        static bool ApplyFixes;

        Read() = delete;
        Read(const Read&) = delete;
        Read& operator=(const Read&) = delete;

        static void ClearCache();
        [[nodiscard]] static std::vector<std::shared_ptr<Model>> CachedModels();
        [[nodiscard]] static std::shared_ptr<ModelInstance> GetModelInstance(
            const std::string& name, bool firstHunt = false,
            MetaDir dir = static_cast<MetaDir>(0), bool noCache = false);
        [[nodiscard]] static std::shared_ptr<ModelInstance> GetRoomModelInstance(const std::string& name);
        // A model by file path (and its animation file, if any), outside the
        // metadata: the front end's menu widgets name their files directly.
        // Not cached; the caller keeps what it reads.
        [[nodiscard]] static std::shared_ptr<Model> ReadModelFile(const std::string& name,
            const std::string& modelPath, const std::optional<std::string>& animationPath);
        static void RemoveModel(const std::string& name, bool firstHunt = false);
        [[nodiscard]] static std::pair<std::int32_t, std::vector<std::uint8_t>> ReadKanjiFont(bool singlePlayer);

        [[nodiscard]] static std::vector<std::uint8_t> ReadBytes(const std::string& path, bool firstHunt);

        [[nodiscard]] static std::shared_ptr<const std::vector<std::shared_ptr<Entity>>> GetEntities(
            const std::string& path, std::int32_t layerId, bool firstHunt, bool allowHook = false);
        [[nodiscard]] static std::shared_ptr<const std::vector<std::shared_ptr<Entity>>> GetEntitiesFromPath(
            const std::string& path, std::int32_t layerId, bool firstHunt, bool allowHook = false);

        [[nodiscard]] static std::shared_ptr<Effect> GetEffect(std::int32_t id);
        [[nodiscard]] static std::shared_ptr<Effect> LoadEffect(std::int32_t id, bool persistent);
        [[nodiscard]] static std::shared_ptr<Effect> LoadEffectByName(
            std::int32_t id, const std::string& name,
            const std::optional<std::string>& archive, bool persistent);
        [[nodiscard]] static std::shared_ptr<Particle> GetSingleParticle(SingleType type);

        [[nodiscard]] static std::int32_t SpanReadInt(
            std::span<const std::uint8_t> bytes, std::reference_wrapper<std::int32_t> offset);
        [[nodiscard]] static std::int32_t SpanReadInt(
            std::span<const std::uint8_t> bytes, std::uint32_t offset);
        [[nodiscard]] static std::int32_t SpanReadInt(
            std::span<const std::uint8_t> bytes, std::int32_t offset);

        [[nodiscard]] static std::uint32_t SpanReadUint(
            std::span<const std::uint8_t> bytes, std::reference_wrapper<std::int32_t> offset);
        [[nodiscard]] static std::uint32_t SpanReadUint(
            std::span<const std::uint8_t> bytes, std::uint32_t offset);
        [[nodiscard]] static std::uint32_t SpanReadUint(
            std::span<const std::uint8_t> bytes, std::int32_t offset);

        [[nodiscard]] static std::uint16_t SpanReadUshort(
            std::span<const std::uint8_t> bytes, std::reference_wrapper<std::int32_t> offset);
        [[nodiscard]] static std::uint16_t SpanReadUshort(
            std::span<const std::uint8_t> bytes, std::uint32_t offset);
        [[nodiscard]] static std::uint16_t SpanReadUshort(
            std::span<const std::uint8_t> bytes, std::int32_t offset);

        template <typename T>
        [[nodiscard]] static T DoOffset(std::span<const std::uint8_t> bytes, std::int32_t offset)
        {
            return DoOffset<T>(bytes, ReadDetail::ManagedUInt32(offset));
        }

        template <typename T>
        [[nodiscard]] static T DoOffset(std::span<const std::uint8_t> bytes, std::uint32_t offset)
        {
            const auto values = DoOffsets<T>(bytes, offset, 1);
            if (values->empty())
            {
                throw std::out_of_range("Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
            }
            return (*values)[0];
        }

        template <typename T>
        [[nodiscard]] static std::shared_ptr<const std::vector<T>> DoOffsets(
            std::span<const std::uint8_t> bytes, std::int32_t offset, std::int32_t count)
        {
            return DoOffsets<T>(bytes, ReadDetail::ManagedUInt32(offset), count);
        }

        template <typename T>
        [[nodiscard]] static std::shared_ptr<const std::vector<T>> DoOffsets(
            std::span<const std::uint8_t> bytes, std::int32_t offset, std::uint32_t count)
        {
            return DoOffsets<T>(bytes, ReadDetail::ManagedUInt32(offset), ReadDetail::ManagedInt32(count));
        }

        template <typename T>
        [[nodiscard]] static std::shared_ptr<const std::vector<T>> DoOffsets(
            std::span<const std::uint8_t> bytes, std::uint32_t offset, std::uint32_t count)
        {
            return DoOffsets<T>(bytes, offset, ReadDetail::ManagedInt32(count));
        }

        template <typename T>
        [[nodiscard]] static std::shared_ptr<const std::vector<T>> DoOffsets(
            std::span<const std::uint8_t> bytes, std::uint32_t offset, std::int32_t count)
        {
            if (count < 0)
            {
                throw std::out_of_range("Non-negative number required. (Parameter 'capacity')");
            }
            auto results = std::make_shared<std::vector<T>>();
            results->reserve(static_cast<std::size_t>(count));
            if (offset != 0)
            {
                std::int32_t ioffset = ReadDetail::ManagedInt32(offset);
                const std::int32_t size = static_cast<std::int32_t>(sizeof(T));
                for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(count); ++i)
                {
                    results->push_back(ReadStruct<T>(ReadDetail::Slice(bytes, ioffset, size)));
                    ioffset = std::bit_cast<std::int32_t>(
                        static_cast<std::uint32_t>(ioffset) + static_cast<std::uint32_t>(size));
                }
            }
            return results;
        }

        [[nodiscard]] static std::shared_ptr<const std::vector<std::uint32_t>> DoListNullEnd(
            std::span<const std::uint8_t> bytes, std::uint32_t offset);

        template <typename T>
        [[nodiscard]] static T ReadStruct(std::span<const std::uint8_t> bytes)
        {
            return ReadDetail::MarshalRead<T>(bytes);
        }

        template <typename T>
        [[nodiscard]] static T ReadStruct(const void* pointer)
        {
            if (pointer == nullptr)
            {
                throw System::ArgumentNullException("ptr");
            }
            return ReadDetail::MarshalRead<T>(std::span<const std::uint8_t>(
                static_cast<const std::uint8_t*>(pointer), sizeof(T)));
        }

        [[nodiscard]] static std::string ReadString(
            std::span<const std::uint8_t> bytes, std::uint32_t offset,
            std::int32_t length = std::numeric_limits<std::int32_t>::max());
        [[nodiscard]] static std::string ReadString(
            std::span<const std::uint8_t> bytes, std::int32_t offset,
            std::int32_t length = std::numeric_limits<std::int32_t>::max());
        [[nodiscard]] static std::string ReadStringTable(
            std::span<const std::uint8_t> bytes, std::uint32_t offset,
            std::int32_t length = std::numeric_limits<std::int32_t>::max());
        [[nodiscard]] static std::string ReadStringTable(
            std::span<const std::uint8_t> bytes, std::int32_t offset,
            std::int32_t length = std::numeric_limits<std::int32_t>::max());

        [[nodiscard]] static std::shared_ptr<const std::vector<std::string>> ReadStrings(
            std::span<const std::uint8_t> bytes, std::int64_t offset, std::int32_t count);
        [[nodiscard]] static std::shared_ptr<const std::vector<std::string>> ReadStrings(
            std::span<const std::uint8_t> bytes, std::int64_t offset, std::uint32_t count);
        [[nodiscard]] static std::shared_ptr<const std::vector<std::string>> ReadStrings(
            std::span<const std::uint8_t> bytes, std::uint32_t offset, std::uint32_t count);
        [[nodiscard]] static std::shared_ptr<const std::vector<std::string>> ReadStrings(
            std::span<const std::uint8_t> bytes, std::int32_t offset, std::int32_t count);

        static void ExtractArchive(const std::string& path);
        static void ReadAndExport(const std::string& name, bool firstHunt = false,
            MetaDir dir = static_cast<MetaDir>(0));

    private:
        [[nodiscard]] static std::shared_ptr<ModelInstance> GetModelInstanceOrNull(
            const std::string& name, bool firstHunt, MetaDir dir, bool noCache);
        [[nodiscard]] static std::shared_ptr<Model> GetModel(
            const std::string& name, bool firstHunt, MetaDir dir);
        [[nodiscard]] static std::shared_ptr<ModelInstance> GetRoomModelInstanceOrNull(const std::string& name);
        [[nodiscard]] static std::shared_ptr<Model> GetRoomModel(const RoomMetadata& meta);
        [[nodiscard]] static std::shared_ptr<Model> ReadModel(
            const std::string& name, const std::string& modelPath,
            const std::optional<std::string>& animationPath,
            const std::optional<std::string>& animationShare,
            const std::vector<RecolorMetadata>& recolorMeta, bool firstHunt);
        [[nodiscard]] static std::shared_ptr<AnimationResults> LoadAnimation(
            const std::string& model, const std::optional<std::string>& path,
            const std::shared_ptr<const std::vector<RawNode>>& nodes, bool firstHunt);
        [[nodiscard]] static std::shared_ptr<const std::vector<TextureData>> GetTextureData(
            Texture texture, std::span<const std::uint8_t> textureBytes);
        [[nodiscard]] static std::shared_ptr<const std::vector<PaletteData>> GetPaletteData(
            Palette palette, std::span<const std::uint8_t> paletteBytes);
        [[nodiscard]] static std::uint8_t AlphaFromShort(std::uint16_t value) noexcept;
        [[nodiscard]] static std::uint8_t AlphaFromA5I3(std::uint8_t value) noexcept;
        [[nodiscard]] static std::uint8_t AlphaFromA3I5(std::uint8_t value) noexcept;
        [[nodiscard]] static std::shared_ptr<Entity> ReadEntity(
            std::span<const std::uint8_t> bytes, EntityEntry entry);
        [[nodiscard]] static std::shared_ptr<const std::vector<std::shared_ptr<Entity>>> GetFirstHuntEntities(
            std::span<const std::uint8_t> bytes);
        [[nodiscard]] static std::shared_ptr<Entity> ReadFirstHuntEntity(
            std::span<const std::uint8_t> bytes, FhEntityEntry entry);

        template <typename T>
        [[nodiscard]] static std::shared_ptr<Entity> ReadEntityOf(
            std::span<const std::uint8_t> bytes, EntityEntry entry, EntityDataHeader header)
        {
            const std::int32_t start = ReadDetail::ManagedInt32(entry.DataOffset);
#ifndef NDEBUG
            if (entry.Length != ReadDetail::MarshaledSize<T>())
            {
                std::abort();
            }
#endif
            T data = ReadStruct<T>(ReadDetail::Slice(bytes, start, entry.Length));
            return std::make_shared<EntityOf<T>>(entry, static_cast<EntityType>(header.Type),
                header.EntityId, data, header);
        }

        template <typename T>
        [[nodiscard]] static std::shared_ptr<Entity> ReadFirstHuntEntityOf(
            std::span<const std::uint8_t> bytes, FhEntityEntry entry, EntityDataHeader header)
        {
            const std::int32_t start = ReadDetail::ManagedInt32(entry.DataOffset);
            T data = ReadStruct<T>(ReadDetail::Slice(bytes, start, static_cast<std::int32_t>(sizeof(T))));
            return std::make_shared<EntityOf<T>>(entry,
                static_cast<EntityType>(static_cast<std::uint16_t>(header.Type + 100)),
                header.EntityId, data, header);
        }

        [[nodiscard]] static std::shared_ptr<Effect> LoadEffectFromPath(std::int32_t id, const std::string& path);
        [[nodiscard]] static std::shared_ptr<Particle> GetParticle(
            const std::string& modelName, const std::string& particleName);
        static void DebugValidateParams(std::uint32_t funcId, std::uint32_t funcOffset, std::uint32_t paramOffset);
        static void Nop() noexcept;
        [[nodiscard]] static std::shared_ptr<const std::vector<std::shared_ptr<RenderInstruction>>> DoRenderInstructions(
            std::span<const std::uint8_t> bytes, DisplayList dlist);
        [[nodiscard]] static std::string GetModelName(const std::string& path);
        static void DumpEntityList(const std::shared_ptr<const std::vector<std::shared_ptr<Entity>>>& entities);
    };
}
