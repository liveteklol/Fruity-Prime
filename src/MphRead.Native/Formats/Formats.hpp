#pragma once

#include "Culling.hpp"
#include "Effects.hpp"
#include "Entity.hpp"
#include "Enums.hpp"
#include "RawFormats.hpp"
#include "Types.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <coroutine>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MphRead::Entities
{
    class EntityBase;
}

namespace MphRead::NativeRuntime
{
    template <typename T>
    class CoroutineSequence final
    {
    public:
        struct promise_type;
        using Handle = std::coroutine_handle<promise_type>;

        struct promise_type
        {
            std::optional<T> Current{};
            std::exception_ptr Exception{};

            [[nodiscard]] CoroutineSequence get_return_object() noexcept
            {
                return CoroutineSequence(Handle::from_promise(*this));
            }

            [[nodiscard]] std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            [[nodiscard]] std::suspend_always final_suspend() noexcept
            {
                return {};
            }

            std::suspend_always yield_value(T value)
            {
                Current.emplace(std::move(value));
                return {};
            }

            void return_void() noexcept
            {
            }

            void unhandled_exception() noexcept
            {
                Exception = std::current_exception();
            }
        };

        CoroutineSequence(const CoroutineSequence&) = delete;
        CoroutineSequence& operator=(const CoroutineSequence&) = delete;

        CoroutineSequence(CoroutineSequence&& other) noexcept
            : _handle(std::exchange(other._handle, {}))
        {
        }

        CoroutineSequence& operator=(CoroutineSequence&& other) noexcept
        {
            if (this != std::addressof(other))
            {
                if (_handle)
                {
                    _handle.destroy();
                }
                _handle = std::exchange(other._handle, {});
            }
            return *this;
        }

        ~CoroutineSequence()
        {
            if (_handle)
            {
                _handle.destroy();
            }
        }

        [[nodiscard]] bool MoveNext()
        {
            if (!_handle || _handle.done())
            {
                return false;
            }

            _handle.promise().Current.reset();
            _handle.resume();
            if (_handle.promise().Exception)
            {
                std::rethrow_exception(_handle.promise().Exception);
            }
            return !_handle.done();
        }

        [[nodiscard]] const T& Current() const
        {
            return *_handle.promise().Current;
        }

    private:
        explicit CoroutineSequence(Handle handle) noexcept
            : _handle(handle)
        {
        }

        Handle _handle{};
    };
}

namespace MphRead
{
    template <typename T>
    class Enumerable final
    {
    public:
        using Factory = std::function<NativeRuntime::CoroutineSequence<T>()>;

        explicit Enumerable(Factory factory)
            : _factory(std::move(factory))
        {
        }

        class Iterator final
        {
        public:
            explicit Iterator(NativeRuntime::CoroutineSequence<T> sequence)
                : _sequence(std::move(sequence)),
                  _finished(!_sequence.MoveNext())
            {
            }

            Iterator(const Iterator&) = delete;
            Iterator& operator=(const Iterator&) = delete;
            Iterator(Iterator&&) noexcept = default;
            Iterator& operator=(Iterator&&) noexcept = default;

            [[nodiscard]] T operator*() const
            {
                return _sequence.Current();
            }

            Iterator& operator++()
            {
                _finished = !_sequence.MoveNext();
                return *this;
            }

            void operator++(int)
            {
                ++*this;
            }

            [[nodiscard]] friend bool operator==(
                const Iterator& iterator, std::default_sentinel_t) noexcept
            {
                return iterator._finished;
            }

            [[nodiscard]] friend bool operator!=(
                const Iterator& iterator, std::default_sentinel_t sentinel) noexcept
            {
                return !(iterator == sentinel);
            }

        private:
            NativeRuntime::CoroutineSequence<T> _sequence;
            bool _finished = true;
        };

        [[nodiscard]] Iterator begin() const
        {
            return Iterator(_factory());
        }

        [[nodiscard]] std::default_sentinel_t end() const noexcept
        {
            return {};
        }

    private:
        Factory _factory;
    };

    class Model;

    class Node : public std::enable_shared_from_this<Node>
    {
    public:
        const std::string Name;
        const std::int32_t ParentIndex;
        const std::int32_t ChildIndex;
        const std::int32_t NextIndex;
        bool Enabled = false;
        bool AnimIgnoreParent = false;
        bool AnimIgnoreChild = false;
        const std::int32_t MeshCount;
        const std::int32_t MeshId;
        OpenTK::Mathematics::Vector3 Scale{};
        OpenTK::Mathematics::Vector3 Angle{};
        OpenTK::Mathematics::Vector3 Position{};
        const float BoundingRadius;
        const OpenTK::Mathematics::Vector3 MinBounds;
        const OpenTK::Mathematics::Vector3 MaxBounds;
        const MphRead::BillboardMode BillboardMode;
        OpenTK::Mathematics::Matrix4 Transform{
            OpenTK::Mathematics::Vector4(1.0F, 0.0F, 0.0F, 0.0F),
            OpenTK::Mathematics::Vector4(0.0F, 1.0F, 0.0F, 0.0F),
            OpenTK::Mathematics::Vector4(0.0F, 0.0F, 1.0F, 0.0F),
            OpenTK::Mathematics::Vector4(0.0F, 0.0F, 0.0F, 1.0F)};
        std::optional<OpenTK::Mathematics::Matrix4> BeforeTransform{};
        std::optional<OpenTK::Mathematics::Matrix4> AfterTransform{};
        OpenTK::Mathematics::Matrix4 Animation{
            OpenTK::Mathematics::Vector4(1.0F, 0.0F, 0.0F, 0.0F),
            OpenTK::Mathematics::Vector4(0.0F, 1.0F, 0.0F, 0.0F),
            OpenTK::Mathematics::Vector4(0.0F, 0.0F, 1.0F, 0.0F),
            OpenTK::Mathematics::Vector4(0.0F, 0.0F, 0.0F, 1.0F)};

        std::int32_t RoomPartId = -1;
        bool RoomPartActive = true;

        const std::shared_ptr<ManagedArray<float>> Bounds
            = std::make_shared<ManagedArray<float>>(6);

        explicit Node(RawNode raw);
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;
        Node(Node&&) = delete;
        Node& operator=(Node&&) = delete;

        [[nodiscard]] Enumerable<std::int32_t> GetMeshIds() const;
        [[nodiscard]] Enumerable<std::int32_t> GetAllMeshIds(
            std::shared_ptr<const std::vector<std::shared_ptr<Node>>> nodes,
            bool root) const;
    };

    enum class SelectionType : std::int32_t;

    class Mesh
    {
    public:
        const std::int32_t MaterialId;
        const std::int32_t DlistId;

        bool Visible = true;
        std::optional<OpenTK::Mathematics::Vector4> PlaceholderColor{};

        SelectionType Selection{};

        explicit Mesh(RawMesh raw);
        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&&) = delete;
        Mesh& operator=(Mesh&&) = delete;

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector4> OverrideColor() const;
    };

    enum class SelectionType : std::int32_t
    {
        None = 0,
        Selected = 1,
        Parent = 2,
        Child = 3
    };

    enum class MatAnimFlags : std::uint8_t;

    class Material
    {
    public:
        const std::string Name;
        std::uint8_t Lighting = 0;
        const std::uint8_t InitLighting;
        CullingMode Culling = CullingMode::Neither;
        std::uint8_t Alpha = 0;
        float CurrentAlpha = 0.0F;
        std::uint8_t Wireframe = 0;
        const std::int32_t TextureId;
        const std::int32_t PaletteId;
        std::int32_t TextureBindingId = 0;
        std::int32_t CurrentTextureId = 0;
        std::int32_t CurrentPaletteId = 0;
        const RepeatMode XRepeat;
        const RepeatMode YRepeat;
        ColorRgb Diffuse{};
        ColorRgb Ambient{};
        const ColorRgb Specular;
        OpenTK::Mathematics::Vector3 CurrentDiffuse{};
        OpenTK::Mathematics::Vector3 CurrentAmbient{};
        OpenTK::Mathematics::Vector3 CurrentSpecular{};
        MphRead::PolygonMode PolygonMode = MphRead::PolygonMode::Modulate;
        MphRead::RenderMode RenderMode = MphRead::RenderMode::Normal;
        // Added to what is behind it, as Quake's GL_ONE GL_ONE stages are: a
        // translated Quake 3 material named "q<n>+..." (MapPacker).
        bool Additive = false;
        MatAnimFlags AnimationFlags{};
        MphRead::TexgenMode TexgenMode = MphRead::TexgenMode::None;
        std::int32_t TexcoordAnimationId = 0;
        std::int32_t MatrixId = 0;
        const float ScaleS;
        const float ScaleT;
        const float TranslateS;
        const float TranslateT;
        const float RotateZ;

        explicit Material(RawMaterial raw);
        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;
        Material(Material&&) = delete;
        Material& operator=(Material&&) = delete;
    };

    struct TextureData
    {
        const std::uint32_t Data = 0;
        const std::uint8_t Alpha = 0;

        constexpr TextureData() noexcept = default;
        constexpr TextureData(std::uint32_t data, std::uint8_t alpha) noexcept
            : Data(data), Alpha(alpha)
        {
        }

        TextureData(const TextureData&) noexcept = default;
        TextureData& operator=(const TextureData& other) noexcept;
    };

    struct PaletteData
    {
        const std::uint16_t Data = 0;

        constexpr PaletteData() noexcept = default;
        constexpr explicit PaletteData(std::uint16_t data) noexcept
            : Data(data)
        {
        }

        PaletteData(const PaletteData&) noexcept = default;
        PaletteData& operator=(const PaletteData& other) noexcept;
    };

    using NodeAnimationDictionary = std::unordered_map<std::string, NodeAnimation>;
    using TexcoordAnimationDictionary = std::unordered_map<std::string, TexcoordAnimation>;
    using TextureAnimationDictionary = std::unordered_map<std::string, TextureAnimation>;
    using MaterialAnimationDictionary = std::unordered_map<std::string, MaterialAnimation>;

    class NodeAnimationGroup
    {
    public:
        const std::int32_t FrameCount = 0;
        std::int32_t CurrentFrame = 0;
        const std::int32_t Count = 0;
        const std::shared_ptr<const std::vector<float>> Scales{};
        const std::shared_ptr<const std::vector<float>> Rotations{};
        const std::shared_ptr<const std::vector<float>> Translations{};
        const std::shared_ptr<const NodeAnimationDictionary> Animations{};

        NodeAnimationGroup(
            RawNodeAnimationGroup raw,
            std::shared_ptr<const std::vector<float>> scales,
            std::shared_ptr<const std::vector<float>> rotations,
            std::shared_ptr<const std::vector<float>> translations,
            std::shared_ptr<const NodeAnimationDictionary> animations);
        NodeAnimationGroup(const NodeAnimationGroup&) = delete;
        NodeAnimationGroup& operator=(const NodeAnimationGroup&) = delete;
        NodeAnimationGroup(NodeAnimationGroup&&) = delete;
        NodeAnimationGroup& operator=(NodeAnimationGroup&&) = delete;

        [[nodiscard]] static std::shared_ptr<NodeAnimationGroup> Empty();

    private:
        NodeAnimationGroup();
    };

    class TexcoordAnimationGroup
    {
    public:
        const std::int32_t FrameCount = 0;
        std::int32_t CurrentFrame = 0;
        const std::int32_t UnusedFrame = 0;
        const std::int32_t Count = 0;
        const std::shared_ptr<const std::vector<float>> Scales{};
        const std::shared_ptr<const std::vector<float>> Rotations{};
        const std::shared_ptr<const std::vector<float>> Translations{};
        const std::shared_ptr<const TexcoordAnimationDictionary> Animations{};

        TexcoordAnimationGroup(
            RawTexcoordAnimationGroup raw,
            std::shared_ptr<const std::vector<float>> scales,
            std::shared_ptr<const std::vector<float>> rotations,
            std::shared_ptr<const std::vector<float>> translations,
            std::shared_ptr<const TexcoordAnimationDictionary> animations);
        TexcoordAnimationGroup(const TexcoordAnimationGroup&) = delete;
        TexcoordAnimationGroup& operator=(const TexcoordAnimationGroup&) = delete;
        TexcoordAnimationGroup(TexcoordAnimationGroup&&) = delete;
        TexcoordAnimationGroup& operator=(TexcoordAnimationGroup&&) = delete;

        [[nodiscard]] static std::shared_ptr<TexcoordAnimationGroup> Empty();

    private:
        TexcoordAnimationGroup();
    };

    class TextureAnimationGroup
    {
    public:
        const std::int32_t FrameCount = 0;
        std::int32_t CurrentFrame = 0;
        const std::int32_t UnusedFrame = 0;
        const std::int32_t Count = 0;
        const std::shared_ptr<const std::vector<std::uint16_t>> FrameIndices{};
        const std::shared_ptr<const std::vector<std::uint16_t>> TextureIds{};
        const std::shared_ptr<const std::vector<std::uint16_t>> PaletteIds{};
        const std::shared_ptr<const TextureAnimationDictionary> Animations{};
        const std::uint16_t UnusedA = 0;

        TextureAnimationGroup(
            RawTextureAnimationGroup raw,
            std::shared_ptr<const std::vector<std::uint16_t>> frameIndices,
            std::shared_ptr<const std::vector<std::uint16_t>> textureIds,
            std::shared_ptr<const std::vector<std::uint16_t>> paletteIds,
            std::shared_ptr<const TextureAnimationDictionary> animations);
        TextureAnimationGroup(const TextureAnimationGroup&) = delete;
        TextureAnimationGroup& operator=(const TextureAnimationGroup&) = delete;
        TextureAnimationGroup(TextureAnimationGroup&&) = delete;
        TextureAnimationGroup& operator=(TextureAnimationGroup&&) = delete;

        [[nodiscard]] static std::shared_ptr<TextureAnimationGroup> Empty();

    private:
        TextureAnimationGroup();
    };

    class MaterialAnimationGroup
    {
    public:
        const std::int32_t FrameCount = 0;
        std::int32_t CurrentFrame = 0;
        const std::int32_t UnusedFrame = 0;
        const std::int32_t Count = 0;
        const std::shared_ptr<const std::vector<float>> Colors{};
        const std::shared_ptr<const MaterialAnimationDictionary> Animations{};

        MaterialAnimationGroup(
            RawMaterialAnimationGroup raw,
            std::shared_ptr<const std::vector<float>> colors,
            std::shared_ptr<const MaterialAnimationDictionary> animations);
        MaterialAnimationGroup(const MaterialAnimationGroup&) = delete;
        MaterialAnimationGroup& operator=(const MaterialAnimationGroup&) = delete;
        MaterialAnimationGroup(MaterialAnimationGroup&&) = delete;
        MaterialAnimationGroup& operator=(MaterialAnimationGroup&&) = delete;

        [[nodiscard]] static std::shared_ptr<MaterialAnimationGroup> Empty();

    private:
        MaterialAnimationGroup();
    };

    class FxFuncInfo
    {
    public:
        const std::uint32_t FuncId;
        const std::shared_ptr<const std::vector<std::int32_t>> Parameters;

        FxFuncInfo(
            std::uint32_t funcId,
            std::shared_ptr<const std::vector<std::int32_t>> parameters);
        FxFuncInfo(const FxFuncInfo&) = delete;
        FxFuncInfo& operator=(const FxFuncInfo&) = delete;
        FxFuncInfo(FxFuncInfo&&) = delete;
        FxFuncInfo& operator=(FxFuncInfo&&) = delete;
    };

    class EffectElement;
    class Particle;

    class Effect
    {
    public:
        const std::int32_t Id;
        const std::string Name;
        const std::uint32_t Field0;
        const std::shared_ptr<const Effects::EffectFuncDictionary> Funcs;
        const std::shared_ptr<const std::vector<std::uint32_t>> List2;
        const std::shared_ptr<const std::vector<std::shared_ptr<EffectElement>>> Elements;
        bool Persistent = false;

        Effect(
            std::int32_t id,
            RawEffect raw,
            std::shared_ptr<const Effects::EffectFuncDictionary> funcs,
            std::shared_ptr<const std::vector<std::uint32_t>> list2,
            std::shared_ptr<const std::vector<std::shared_ptr<EffectElement>>> elements,
            std::string name);
        Effect(const Effect&) = delete;
        Effect& operator=(const Effect&) = delete;
        Effect(Effect&&) = delete;
        Effect& operator=(Effect&&) = delete;
    };

    class EffectElement
    {
    public:
        const std::string Name;
        const std::string ModelName;
        const std::shared_ptr<const std::vector<std::shared_ptr<Particle>>> Particles;
        const Effects::EffElemFlags Flags;
        const OpenTK::Mathematics::Vector3 Acceleration;
        const std::uint32_t ChildEffectId;
        const float Lifespan;
        const float DrainTime;
        const float BufferTime;
        const std::int32_t DrawType;
        const std::shared_ptr<const Effects::EffectActionDictionary> Actions;
        const std::shared_ptr<const Effects::EffectFuncDictionary> Funcs;

        EffectElement(
            RawEffectElement raw,
            std::shared_ptr<const std::vector<std::shared_ptr<Particle>>> particles,
            std::shared_ptr<const Effects::EffectFuncDictionary> funcs,
            std::shared_ptr<const Effects::EffectActionDictionary> actions);
        EffectElement(const EffectElement&) = delete;
        EffectElement& operator=(const EffectElement&) = delete;
        EffectElement(EffectElement&&) = delete;
        EffectElement& operator=(EffectElement&&) = delete;

    private:
        struct Init;

        explicit EffectElement(Init init);
        [[nodiscard]] static Init BuildInit(
            RawEffectElement raw,
            std::shared_ptr<const std::vector<std::shared_ptr<Particle>>> particles,
            std::shared_ptr<const Effects::EffectFuncDictionary> funcs,
            std::shared_ptr<const Effects::EffectActionDictionary> actions);
    };

    class Particle
    {
    public:
        const std::string Name;
        const std::shared_ptr<MphRead::Model> Model;
        const std::shared_ptr<MphRead::Node> Node;
        const std::int32_t MaterialId;

        Particle(
            std::string name,
            std::shared_ptr<MphRead::Model> model,
            std::shared_ptr<MphRead::Node> node,
            std::int32_t materialId);
        Particle(const Particle&) = delete;
        Particle& operator=(const Particle&) = delete;
        Particle(Particle&&) = delete;
        Particle& operator=(Particle&&) = delete;
    };

    enum class FuncAction : std::int32_t
    {
        SetParticleId = 9,
        IncreaseParticleAmount = 14,
        SetNewParticleSpeed = 15,
        SetNewParticlePosition = 16,
        SetNewParticleLifespan = 17,
        UpdateParticleSpeed = 18,
        SetParticleAlpha = 19,
        SetParticleRed = 20,
        SetParticleGreen = 21,
        SetParticleBlue = 22,
        SetParticleScale = 23,
        SetParticleRotation = 24,
        SetParticleRoField1 = 25,
        SetParticleRoField2 = 26,
        SetParticleRoField3 = 27,
        SetParticleRoField4 = 28,
        SetParticleRwField1 = 29,
        SetParticleRwField2 = 30,
        SetParticleRwField3 = 31,
        SetParticleRwField4 = 32
    };

    class StringTableEntry
    {
    public:
        const std::string Id;
        const char16_t Prefix;
        const std::string Value1;
        const std::string Value2;
        const std::uint8_t Speed;
        const char16_t Category;

        const std::string String1;
        const std::string String2;

        StringTableEntry(
            RawStringTableEntry raw,
            char16_t prefix,
            std::string value1,
            std::string value2);

        StringTableEntry(
            std::string id,
            char16_t prefix,
            std::string value1,
            std::string value2,
            std::uint8_t speed,
            char16_t category);
        StringTableEntry(const StringTableEntry&) = delete;
        StringTableEntry& operator=(const StringTableEntry&) = delete;
        StringTableEntry(StringTableEntry&&) = delete;
        StringTableEntry& operator=(StringTableEntry&&) = delete;
    };

    class Entity
    {
    public:
        const std::shared_ptr<const std::string> NodeName;
        const std::uint16_t LayerMask;
        const std::uint16_t Length;
        const EntityType Type;
        const std::int16_t EntityId;
        const bool FirstHunt;

        const OpenTK::Mathematics::Vector3 Position;
        const OpenTK::Mathematics::Vector3 UpVector;
        const OpenTK::Mathematics::Vector3 FacingVector;

        Entity(
            EntityEntry entry,
            EntityType type,
            std::int16_t entityId,
            EntityDataHeader header);

        Entity(
            FhEntityEntry entry,
            EntityType type,
            std::int16_t entityId,
            EntityDataHeader header);

        Entity(const Entity&) = delete;
        Entity& operator=(const Entity&) = delete;
        Entity(Entity&&) = delete;
        Entity& operator=(Entity&&) = delete;
        virtual ~Entity() = default;

        [[nodiscard]] virtual std::int16_t GetParentId();
        [[nodiscard]] virtual std::int16_t GetChildId();

    };

    template <typename T>
    class EntityOf : public Entity
    {
    public:
        const T Data;

        EntityOf(
            EntityEntry entry,
            EntityType type,
            std::int16_t entityId,
            T data,
            EntityDataHeader header)
            : Entity(entry, type, entityId, header),
              Data(data)
        {
        }

        EntityOf(
            FhEntityEntry entry,
            EntityType type,
            std::int16_t entityId,
            T data,
            EntityDataHeader header)
            : Entity(entry, type, entityId, header),
              Data(data)
        {
        }

        [[nodiscard]] std::int16_t GetParentId() override
        {
            if constexpr (std::is_same_v<T, TriggerVolumeEntityData>)
            {
                return Data.ParentId;
            }
            else if constexpr (std::is_same_v<T, AreaVolumeEntityData>)
            {
                return Data.ParentId;
            }
            else if constexpr (std::is_same_v<T, FhTriggerVolumeEntityData>)
            {
                return Data.ParentId;
            }
            else if constexpr (std::is_same_v<T, PointModuleEntityData>)
            {
                if (Data.PrevId == Data.Header.EntityId)
                {
                    return -1;
                }
                return Data.PrevId;
            }
            return Entity::GetParentId();
        }

        [[nodiscard]] std::int16_t GetChildId() override
        {
            if constexpr (std::is_same_v<T, TriggerVolumeEntityData>)
            {
                return Data.ChildId;
            }
            else if constexpr (std::is_same_v<T, AreaVolumeEntityData>)
            {
                return Data.ChildId;
            }
            else if constexpr (std::is_same_v<T, FhTriggerVolumeEntityData>)
            {
                return Data.ChildId;
            }
            else if constexpr (std::is_same_v<T, PointModuleEntityData>)
            {
                if (Data.NextId == Data.Header.EntityId)
                {
                    return -1;
                }
                return Data.NextId;
            }
            return Entity::GetChildId();
        }
    };

    class CollisionVolume
    {
    public:
        VolumeType Type{};

        union
        {
            OpenTK::Mathematics::Vector3 BoxVector1;
            OpenTK::Mathematics::Vector3 CylinderVector;
            OpenTK::Mathematics::Vector3 SpherePosition;
        };

        union
        {
            OpenTK::Mathematics::Vector3 BoxVector2;
            OpenTK::Mathematics::Vector3 CylinderPosition;
            float SphereRadius;
        };

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
        union
        {
            OpenTK::Mathematics::Vector3 BoxVector3;
            struct
            {
                float CylinderRadius;
                float CylinderDot;
            };
        };
#else
#error "CollisionVolume requires compiler support for anonymous struct members to preserve C# explicit-layout field names."
#endif

        OpenTK::Mathematics::Vector3 BoxPosition;
        float BoxDot1;
        float BoxDot2;
        float BoxDot3;

        CollisionVolume() noexcept;
        // ValueType.Equals on this blittable, explicit-layout struct: the bytes.
        [[nodiscard]] bool Equals(const CollisionVolume& other) const noexcept
        {
            return std::memcmp(this, &other, sizeof(CollisionVolume)) == 0;
        }
        explicit CollisionVolume(RawCollisionVolume raw);
        explicit CollisionVolume(FhRawCollisionVolume raw);
        CollisionVolume(
            OpenTK::Mathematics::Vector3 vec1,
            OpenTK::Mathematics::Vector3 vec2,
            OpenTK::Mathematics::Vector3 vec3,
            OpenTK::Mathematics::Vector3 pos,
            float dot1,
            float dot2,
            float dot3) noexcept;
        CollisionVolume(
            OpenTK::Mathematics::Vector3 vec,
            OpenTK::Mathematics::Vector3 pos,
            float rad,
            float dot) noexcept;
        CollisionVolume(
            OpenTK::Mathematics::Vector3 pos,
            float rad) noexcept;

        CollisionVolume(const CollisionVolume& other) noexcept;
        CollisionVolume& operator=(const CollisionVolume& other) noexcept;

        [[nodiscard]] static CollisionVolume Transform(
            CollisionVolume vol,
            OpenTK::Mathematics::Matrix4 transform);
        [[nodiscard]] static CollisionVolume Transform(
            RawCollisionVolume vol,
            OpenTK::Mathematics::Matrix4 transform);
        [[nodiscard]] static CollisionVolume Transform(
            FhRawCollisionVolume vol,
            OpenTK::Mathematics::Matrix4 transform);

        [[nodiscard]] static CollisionVolume Move(
            CollisionVolume vol,
            OpenTK::Mathematics::Vector3 position);
        [[nodiscard]] static CollisionVolume Move(
            RawCollisionVolume vol,
            OpenTK::Mathematics::Vector3 position);
        [[nodiscard]] static CollisionVolume Move(
            FhRawCollisionVolume vol,
            OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] bool TestPoint(OpenTK::Mathematics::Vector3 point) const;
        [[nodiscard]] OpenTK::Mathematics::Vector3 GetCenter() const;
    };

    static_assert(sizeof(CollisionVolume) == 64);
    static_assert(offsetof(CollisionVolume, Type) == 0);
    static_assert(offsetof(CollisionVolume, BoxVector1) == 4);
    static_assert(offsetof(CollisionVolume, BoxVector2) == 16);
    static_assert(offsetof(CollisionVolume, BoxVector3) == 28);
    static_assert(offsetof(CollisionVolume, BoxPosition) == 40);
    static_assert(offsetof(CollisionVolume, BoxDot1) == 52);
    static_assert(offsetof(CollisionVolume, BoxDot2) == 56);
    static_assert(offsetof(CollisionVolume, BoxDot3) == 60);
    static_assert(offsetof(CollisionVolume, CylinderVector) == 4);
    static_assert(offsetof(CollisionVolume, CylinderPosition) == 16);
    static_assert(offsetof(CollisionVolume, CylinderRadius) == 28);
    static_assert(offsetof(CollisionVolume, CylinderDot) == 32);
    static_assert(offsetof(CollisionVolume, SpherePosition) == 4);
    static_assert(offsetof(CollisionVolume, SphereRadius) == 16);

    class DisplayVolume
    {
    public:
        const CollisionVolume Volume;

    protected:
        OpenTK::Mathematics::Vector3 _color1 = OpenTK::Mathematics::Vector3::Zero;
        OpenTK::Mathematics::Vector3 _color2 = OpenTK::Mathematics::Vector3::Zero;

    public:
        [[nodiscard]] OpenTK::Mathematics::Vector3 Color1() const noexcept
        {
            return _color1;
        }

        [[nodiscard]] OpenTK::Mathematics::Vector3 Color2() const noexcept
        {
            return _color2;
        }

        DisplayVolume(
            RawCollisionVolume volume,
            OpenTK::Mathematics::Matrix4 transform);
        DisplayVolume(
            FhRawCollisionVolume volume,
            OpenTK::Mathematics::Matrix4 transform);
        DisplayVolume(
            CollisionVolume volume,
            OpenTK::Mathematics::Matrix4 transform);
        DisplayVolume(
            RawCollisionVolume volume,
            OpenTK::Mathematics::Vector3 position);
        DisplayVolume(
            FhRawCollisionVolume volume,
            OpenTK::Mathematics::Vector3 position);
        DisplayVolume(
            CollisionVolume volume,
            OpenTK::Mathematics::Vector3 position);

        DisplayVolume(const DisplayVolume&) = delete;
        DisplayVolume& operator=(const DisplayVolume&) = delete;
        virtual ~DisplayVolume() = default;

        [[nodiscard]] virtual std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const = 0;

    protected:
        void SetColor1(OpenTK::Mathematics::Vector3 color) noexcept
        {
            _color1 = color;
        }

        void SetColor2(OpenTK::Mathematics::Vector3 color) noexcept
        {
            _color2 = color;
        }
    };

    class MorphCameraDisplay : public DisplayVolume
    {
    public:
        MorphCameraDisplay(
            std::shared_ptr<EntityOf<MorphCameraEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);
        MorphCameraDisplay(
            std::shared_ptr<EntityOf<FhMorphCameraEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const override;
    };

    class JumpPadDisplay : public DisplayVolume
    {
    public:
        const OpenTK::Mathematics::Vector3 Vector;
        const float Speed;
        const bool Active;

        JumpPadDisplay(
            std::shared_ptr<EntityOf<JumpPadEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);
        JumpPadDisplay(
            std::shared_ptr<EntityOf<FhJumpPadEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const override;
    };

    class ObjectDisplay : public DisplayVolume
    {
    public:
        ObjectDisplay(
            std::shared_ptr<EntityOf<ObjectEntityData>> entity,
            OpenTK::Mathematics::Matrix4 transform);

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const override;
    };

    class FlagBaseDisplay : public DisplayVolume
    {
    public:
        FlagBaseDisplay(
            std::shared_ptr<EntityOf<FlagBaseEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const override;
    };

    class NodeDefenseDisplay : public DisplayVolume
    {
    public:
        NodeDefenseDisplay(
            std::shared_ptr<EntityOf<NodeDefenseEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const override;
    };

    class TriggerVolumeDisplay : public DisplayVolume
    {
    public:
        TriggerVolumeDisplay(
            std::shared_ptr<EntityOf<TriggerVolumeEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);
        TriggerVolumeDisplay(
            std::shared_ptr<EntityOf<FhTriggerVolumeEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const override;
    };

    class AreaVolumeDisplay : public DisplayVolume
    {
    public:
        AreaVolumeDisplay(
            std::shared_ptr<EntityOf<AreaVolumeEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);
        AreaVolumeDisplay(
            std::shared_ptr<EntityOf<FhAreaVolumeEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const override;
    };

    class LightSource : public DisplayVolume
    {
    public:
        const bool Light1Enabled;
        const OpenTK::Mathematics::Vector3 Light1Vector;
        const bool Light2Enabled;
        const OpenTK::Mathematics::Vector3 Light2Vector;

        LightSource(
            std::shared_ptr<EntityOf<LightSourceEntityData>> entity,
            OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> GetColor(
            std::int32_t index) const override;
    };

    class CameraSequenceKeyframe
    {
    public:
        const OpenTK::Mathematics::Vector3 Position;
        const OpenTK::Mathematics::Vector3 ToTarget;
        const float Roll;
        const float Fov;
        const float MoveTime;
        const float HoldTime;
        const float FadeInTime;
        const float FadeOutTime;
        const FadeType FadeInType;
        const FadeType FadeOutType;
        const std::uint8_t PrevFrameInfluence;
        const std::uint8_t AfterFrameInfluence;
        const bool UseEntityTransform;
        const std::int16_t PosEntityType;
        const std::int16_t PosEntityId;
        const std::int16_t TargetEntityType;
        const std::int16_t TargetEntityId;
        const std::int16_t MessageTargetType;
        const std::int16_t MessageTargetId;
        const std::uint16_t MessageId;
        const std::uint16_t MessageParam;
        const float Easing;
        const std::string NodeName;

        std::shared_ptr<Entities::EntityBase> PositionEntity{};
        std::shared_ptr<Entities::EntityBase> TargetEntity{};
        std::shared_ptr<Entities::EntityBase> MessageTarget{};
        Formats::Culling::NodeRef NodeRef{};

        explicit CameraSequenceKeyframe(RawCameraSequenceKeyframe raw);
        CameraSequenceKeyframe(const CameraSequenceKeyframe&) = delete;
        CameraSequenceKeyframe& operator=(const CameraSequenceKeyframe&) = delete;
        CameraSequenceKeyframe(CameraSequenceKeyframe&&) = delete;
        CameraSequenceKeyframe& operator=(CameraSequenceKeyframe&&) = delete;
    };

    enum class GameMode : std::uint8_t
    {
        None = 0,
        SinglePlayer = 2,
        Battle = 3,
        BattleTeams = 4,
        Survival = 5,
        SurvivalTeams = 6,
        Capture = 7,
        Bounty = 8,
        BountyTeams = 9,
        Nodes = 10,
        NodesTeams = 11,
        Defender = 12,
        DefenderTeams = 13,
        PrimeHunter = 14,
        Unknown15 = 15
    };

    // GameMode.ToString().
    [[nodiscard]] std::string ToString(GameMode value);
    // Enum.IsDefined(typeof(GameMode), value).
    [[nodiscard]] bool IsDefinedGameMode(std::uint64_t value);
    // Enum.TryParse<GameMode>(value, ignoreCase, out result).
    [[nodiscard]] bool TryParse(std::string_view value, bool ignoreCase, GameMode& result);

    enum class MatAnimFlags : std::uint8_t
    {
        None = 0x0,
        DisableColor = 0x1,
        DisableAlpha = 0x2
    };

    [[nodiscard]] constexpr MatAnimFlags operator|(MatAnimFlags left, MatAnimFlags right) noexcept
    {
        return static_cast<MatAnimFlags>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr MatAnimFlags operator&(MatAnimFlags left, MatAnimFlags right) noexcept
    {
        return static_cast<MatAnimFlags>(
            static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr MatAnimFlags operator^(MatAnimFlags left, MatAnimFlags right) noexcept
    {
        return static_cast<MatAnimFlags>(
            static_cast<std::uint8_t>(left) ^ static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr MatAnimFlags operator~(MatAnimFlags value) noexcept
    {
        return static_cast<MatAnimFlags>(~static_cast<std::uint8_t>(value));
    }

    constexpr MatAnimFlags& operator|=(MatAnimFlags& left, MatAnimFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr MatAnimFlags& operator&=(MatAnimFlags& left, MatAnimFlags right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr MatAnimFlags& operator^=(MatAnimFlags& left, MatAnimFlags right) noexcept
    {
        left = left ^ right;
        return left;
    }

    enum class NodeLayer : std::uint16_t
    {
        None = 0x0,
        MultiplayerLod0 = 0x8,
        MultiplayerLod1 = 0x10,
        MultiplayerU = 0x20,
        Unknown40 = 0x40,
        Unknown1000 = 0x1000,
        CaptureTheFlag = 0x4000
    };

    [[nodiscard]] constexpr NodeLayer operator|(NodeLayer left, NodeLayer right) noexcept
    {
        return static_cast<NodeLayer>(
            static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr NodeLayer operator&(NodeLayer left, NodeLayer right) noexcept
    {
        return static_cast<NodeLayer>(
            static_cast<std::uint16_t>(left) & static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr NodeLayer operator^(NodeLayer left, NodeLayer right) noexcept
    {
        return static_cast<NodeLayer>(
            static_cast<std::uint16_t>(left) ^ static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr NodeLayer operator~(NodeLayer value) noexcept
    {
        return static_cast<NodeLayer>(~static_cast<std::uint16_t>(value));
    }

    constexpr NodeLayer& operator|=(NodeLayer& left, NodeLayer right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr NodeLayer& operator&=(NodeLayer& left, NodeLayer right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr NodeLayer& operator^=(NodeLayer& left, NodeLayer right) noexcept
    {
        left = left ^ right;
        return left;
    }

    enum class BossFlags : std::int32_t
    {
        Unspecified = -1,
        None = 0x0,
        Unit1B1Kill = 0x1,
        Unit1B1Done = 0x2,
        Unit1B2Kill = 0x4,
        Unit1B2Done = 0x8,
        Unit2B1Kill = 0x10,
        Unit2B1Done = 0x20,
        Unit2B2Kill = 0x40,
        Unit2B2Done = 0x80,
        Unit3B1Kill = 0x100,
        Unit3B1Done = 0x200,
        Unit3B2Kill = 0x400,
        Unit3B2Done = 0x800,
        Unit4B1Kill = 0x1000,
        Unit4B1Done = 0x2000,
        Unit4B2Kill = 0x4000,
        Unit4B2Done = 0x8000,
        Gorea1Kill = 0x10000,
        All = 0x55555
    };

    [[nodiscard]] constexpr BossFlags operator|(BossFlags left, BossFlags right) noexcept
    {
        return static_cast<BossFlags>(
            static_cast<std::int32_t>(left) | static_cast<std::int32_t>(right));
    }

    [[nodiscard]] constexpr BossFlags operator&(BossFlags left, BossFlags right) noexcept
    {
        return static_cast<BossFlags>(
            static_cast<std::int32_t>(left) & static_cast<std::int32_t>(right));
    }

    [[nodiscard]] constexpr BossFlags operator^(BossFlags left, BossFlags right) noexcept
    {
        return static_cast<BossFlags>(
            static_cast<std::int32_t>(left) ^ static_cast<std::int32_t>(right));
    }

    [[nodiscard]] constexpr BossFlags operator~(BossFlags value) noexcept
    {
        return static_cast<BossFlags>(~static_cast<std::int32_t>(value));
    }

    constexpr BossFlags& operator|=(BossFlags& left, BossFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr BossFlags& operator&=(BossFlags& left, BossFlags right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr BossFlags& operator^=(BossFlags& left, BossFlags right) noexcept
    {
        left = left ^ right;
        return left;
    }

    enum class AreaState : std::int32_t
    {
        None = 0,
        Escape = 1,
        Clear = 2
    };

    enum class InstructionCode : std::uint32_t
    {
        NOP = 0x400,
        MTX_RESTORE = 0x450,
        COLOR = 0x480,
        NORMAL = 0x484,
        TEXCOORD = 0x488,
        VTX_16 = 0x48C,
        VTX_10 = 0x490,
        VTX_XY = 0x494,
        VTX_XZ = 0x498,
        VTX_YZ = 0x49C,
        VTX_DIFF = 0x4A0,
        DIF_AMB = 0x4C0,
        BEGIN_VTXS = 0x500,
        END_VTXS = 0x504
    };

    class RenderInstruction
    {
    public:
        const InstructionCode Code;
        const std::shared_ptr<std::vector<std::uint32_t>> Arguments;

        RenderInstruction(
            InstructionCode code,
            std::shared_ptr<std::vector<std::uint32_t>> arguments);
        RenderInstruction(const RenderInstruction&) = delete;
        RenderInstruction& operator=(const RenderInstruction&) = delete;
        RenderInstruction(RenderInstruction&&) = delete;
        RenderInstruction& operator=(RenderInstruction&&) = delete;

        [[nodiscard]] static std::int32_t GetArity(InstructionCode code);

    private:
        static const std::shared_ptr<const std::unordered_map<InstructionCode, std::int32_t>> _arityMap;
    };

    class Ver final
    {
    public:
        inline static const std::string A76E0 = "A76E0";
        inline static const std::string AMHE0 = "AMHE0";
        inline static const std::string AMHE1 = "AMHE1";
        inline static const std::string AMHP0 = "AMHP0";
        inline static const std::string AMHP1 = "AMHP1";
        inline static const std::string AMHJ0 = "AMHJ0";
        inline static const std::string AMHJ1 = "AMHJ1";
        inline static const std::string AMHK0 = "AMHK0";
        inline static const std::string NTRJ0 = "NTRJ0";

        inline static const std::string AMFE0 = "AMFE0";
        inline static const std::string AMFP0 = "AMFP0";

        Ver() = delete;
    };

    class Paths final
    {
    public:
        static std::string MphKey;
        static std::string FhKey;

        [[nodiscard]] static std::string FileSystem();
        [[nodiscard]] static std::string FhFileSystem();
        [[nodiscard]] static std::string Export();

        [[nodiscard]] static bool IsMphAmericas() noexcept;
        [[nodiscard]] static bool IsMphEurope() noexcept;
        [[nodiscard]] static bool IsMphJapan() noexcept;
        [[nodiscard]] static bool IsMphKorea() noexcept;

        [[nodiscard]] static const std::unordered_map<std::string, std::string>& AllPaths();

        static void SetPath(std::string key, std::string path);
        static void UpdatePaths();
        static void ChooseMphPath();
        static void ChooseFhPath();

        [[nodiscard]] static std::string Combine(
            std::string path1, std::string path2);
        [[nodiscard]] static std::string Combine(
            std::string path1, std::string path2, std::string path3);
        [[nodiscard]] static std::string Combine(
            std::string path1, std::string path2,
            std::string path3, std::string path4);
        [[nodiscard]] static std::string Combine(std::vector<std::string>& paths);

        Paths() = delete;

    private:
        static std::unordered_map<std::string, std::string> _allPaths;

        [[nodiscard]] static std::string Absolute(const std::string& path);
        [[nodiscard]] static std::string Replace(std::string path);
    };

    class CollectionExtensions final
    {
    public:
        template <typename T>
        [[nodiscard]] static std::span<const T> Slice(
            std::span<const T> source, std::uint32_t start)
        {
            const std::int32_t converted = ManagedInt32(start);
            if (converted < 0
                || static_cast<std::size_t>(converted) > source.size())
            {
                ThrowSpanRange();
            }
            return source.subspan(static_cast<std::size_t>(converted));
        }

        template <typename T>
        [[nodiscard]] static std::span<T> Slice(
            std::span<T> source, std::uint32_t start)
        {
            const std::int32_t converted = ManagedInt32(start);
            if (converted < 0
                || static_cast<std::size_t>(converted) > source.size())
            {
                ThrowSpanRange();
            }
            return source.subspan(static_cast<std::size_t>(converted));
        }

        template <typename T>
        [[nodiscard]] static std::span<const T> Slice(
            std::span<const T> source, std::uint32_t start, std::uint32_t length)
        {
            return SliceChecked(source, ManagedInt32(start), ManagedInt32(length), true);
        }

        template <typename T>
        [[nodiscard]] static std::span<const T> Slice(
            std::span<const T> source, std::int32_t start, std::uint32_t length)
        {
            return SliceChecked(source, start, ManagedInt32(length), true);
        }

        template <typename T>
        [[nodiscard]] static std::span<const T> Slice(
            std::span<const T> source, std::uint32_t start, std::int32_t length)
        {
            return SliceChecked(source, ManagedInt32(start), length, true);
        }

        template <typename T>
        [[nodiscard]] static std::span<const T> Slice(
            std::span<const T> source, std::int64_t start, std::int32_t length)
        {
            return SliceChecked(source, ManagedInt32(start), length, true);
        }

        template <typename T>
        [[nodiscard]] static std::span<const T> Slice(
            std::span<const T> source, std::int64_t start, std::uint32_t length)
        {
            return SliceChecked(source, ManagedInt32(start), ManagedInt32(length), true);
        }

        template <typename T>
        [[nodiscard]] static std::span<T> Slice(
            std::span<T> source, std::int64_t start)
        {
            const std::int32_t converted = ManagedInt32(start);
            if (converted < 0
                || static_cast<std::size_t>(converted) > source.size())
            {
                ThrowSpanRange();
            }
            return source.subspan(static_cast<std::size_t>(converted));
        }

        template <typename T>
        [[nodiscard]] static T Consume(std::span<T>& span)
        {
            if (span.empty())
            {
                ThrowSpanIndex();
            }
            T value = span[0];
            span = span.subspan(1);
            return value;
        }

        template <typename T>
        [[nodiscard]] static T Consume(std::span<const T>& span)
        {
            if (span.empty())
            {
                ThrowSpanIndex();
            }
            T value = span[0];
            span = span.subspan(1);
            return value;
        }

        template <typename TRange, typename TPredicate>
        [[nodiscard]] static std::int32_t IndexOf(
            const TRange& source, TPredicate&& predicate)
        {
            std::int32_t index = 0;
            for (const auto& item : source)
            {
                if (std::invoke(predicate, item))
                {
                    return index;
                }
                index = std::bit_cast<std::int32_t>(
                    std::bit_cast<std::uint32_t>(index) + 1U);
            }
            return -1;
        }

        template <typename TRange, typename TPredicate>
        [[nodiscard]] static std::int32_t IndexOf(
            const std::shared_ptr<TRange>& source, TPredicate&& predicate)
        {
            if (!source)
            {
                throw System::NullReferenceException();
            }
            return IndexOf(*source, std::forward<TPredicate>(predicate));
        }

        template <typename TRange, typename TPredicate, typename TResult>
        [[nodiscard]] static bool TryFind(
            const std::shared_ptr<TRange>& source,
            TPredicate&& predicate,
            TResult& result)
        {
            if (!source)
            {
                throw System::ArgumentNullException("source");
            }
            for (const auto& item : *source)
            {
                if (std::invoke(predicate, item))
                {
                    result = item;
                    return static_cast<bool>(result);
                }
            }
            result = TResult{};
            return false;
        }

        CollectionExtensions() = delete;

    private:
        [[nodiscard]] static constexpr std::int32_t ManagedInt32(
            std::uint32_t value) noexcept
        {
            return std::bit_cast<std::int32_t>(value);
        }

        [[nodiscard]] static constexpr std::int32_t ManagedInt32(
            std::int64_t value) noexcept
        {
            return std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(value)));
        }

        [[noreturn]] static void ThrowSpanRange();
        [[noreturn]] static void ThrowSpanIndex();

        template <typename T>
        [[nodiscard]] static std::span<const T> SliceChecked(
            std::span<const T> source,
            std::int32_t start,
            std::int32_t length,
            bool)
        {
            if (start < 0 || length < 0
                || static_cast<std::size_t>(start) > source.size()
                || static_cast<std::size_t>(length)
                    > source.size() - static_cast<std::size_t>(start))
            {
                ThrowSpanRange();
            }
            return source.subspan(
                static_cast<std::size_t>(start),
                static_cast<std::size_t>(length));
        }
    };

    class Frozen final
    {
    public:
        template <typename TKey, typename TValue>
        [[nodiscard]] static std::shared_ptr<const std::unordered_map<TKey, TValue>> Create(
            const std::vector<std::pair<TKey, TValue>>& list)
        {
            auto result = std::make_shared<std::unordered_map<TKey, TValue>>();
            result->reserve(list.size());
            for (const auto& pair : list)
            {
                const auto [_, inserted] = result->emplace(pair.first, pair.second);
                if (!inserted)
                {
                    throw std::invalid_argument(
                        "An item with the same key has already been added.");
                }
            }
            return result;
        }

        Frozen() = delete;
    };
}
