#pragma once

#include "Enums.hpp"
#include "Types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MphRead
{
    class EffectElement;
    class FxFuncInfo;
    class Material;
    class Mesh;
    class Model;
    class Node;
    class Particle;
    class Scene;

    enum class FuncAction : std::int32_t;

    namespace Formats::Collision
    {
        class EntityCollision;
    }
}

namespace MphRead::Effects
{
    enum class EffElemFlags : std::uint32_t;

    using EffectActionDictionary = std::unordered_map<
        MphRead::FuncAction, std::shared_ptr<MphRead::FxFuncInfo>>;
    using EffectFuncDictionary = std::unordered_map<
        std::uint32_t, std::shared_ptr<MphRead::FxFuncInfo>>;

    struct TimeValues
    {
        const float Global = 0.0F;
        const float Elapsed = 0.0F;
        const float Lifespan = 0.0F;

        constexpr TimeValues() noexcept = default;
        constexpr TimeValues(float global, float elapsed, float lifespan) noexcept
            : Global(global), Elapsed(elapsed), Lifespan(lifespan)
        {
        }

        TimeValues(const TimeValues&) noexcept = default;
        TimeValues& operator=(const TimeValues& other) noexcept;
    };

    class SingleParticle
    {
    public:
        std::shared_ptr<MphRead::Particle> ParticleDefinition{};
        OpenTK::Mathematics::Vector3 Position{};
        OpenTK::Mathematics::Vector3 Color{};
        float Alpha = 0.0F;
        float Scale = 0.0F;

        SingleParticle() = default;
        SingleParticle(const SingleParticle&) = delete;
        SingleParticle& operator=(const SingleParticle&) = delete;
        SingleParticle(SingleParticle&&) = delete;
        SingleParticle& operator=(SingleParticle&&) = delete;

        [[nodiscard]] bool ShouldDraw() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector2 Texcoord0() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Vertex0() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector2 Texcoord1() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Vertex1() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector2 Texcoord2() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Vertex2() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector2 Texcoord3() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Vertex3() const noexcept;

        void Process();
        void AddRenderItem(MphRead::Scene* scene);

    private:
        bool _shouldDraw = false;
        OpenTK::Mathematics::Vector2 _texcoord0{};
        OpenTK::Mathematics::Vector3 _vertex0{};
        OpenTK::Mathematics::Vector2 _texcoord1{};
        OpenTK::Mathematics::Vector3 _vertex1{};
        OpenTK::Mathematics::Vector2 _texcoord2{};
        OpenTK::Mathematics::Vector3 _vertex2{};
        OpenTK::Mathematics::Vector2 _texcoord3{};
        OpenTK::Mathematics::Vector3 _vertex3{};
    };

    class EffectFuncBase
    {
    public:
        EffectFuncBase() = default;
        EffectFuncBase(const EffectFuncBase&) = delete;
        EffectFuncBase& operator=(const EffectFuncBase&) = delete;
        EffectFuncBase(EffectFuncBase&&) = delete;
        EffectFuncBase& operator=(EffectFuncBase&&) = delete;
        virtual ~EffectFuncBase() = default;

        [[nodiscard]] virtual std::shared_ptr<const EffectActionDictionary> Actions() const;
        virtual void SetActions(std::shared_ptr<const EffectActionDictionary> value);

        [[nodiscard]] virtual std::shared_ptr<const EffectFuncDictionary> Funcs() const;
        virtual void SetFuncs(std::shared_ptr<const EffectFuncDictionary> value);

        void InvokeVecFunc(
            std::shared_ptr<MphRead::FxFuncInfo> info,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        [[nodiscard]] float InvokeFloatFunc(
            std::shared_ptr<MphRead::FxFuncInfo> info,
            TimeValues times);

        [[nodiscard]] static std::pair<std::int32_t, std::int32_t> GetFuncIds(
            EffElemFlags flags, std::int32_t drawType);

    protected:
        virtual void FxFunc01(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) = 0;
        virtual void FxFunc03(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) = 0;
        void FxFunc04(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc05(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc06(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc07(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc08(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc09(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc10(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        virtual void FxFunc11(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) = 0;
        void FxFunc13(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc14(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc15(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc16(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc17(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc18(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc19(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);
        void FxFunc20(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec);

        [[nodiscard]] float FxFunc21(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] virtual float FxFunc22(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc23(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc24(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc25(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc26(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc27(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc29(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc30(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc31(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc32(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc33(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc34(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc35(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc36(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc37(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc38(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;
        [[nodiscard]] virtual float FxFunc39(
            const std::vector<std::int32_t>& param, TimeValues times) = 0;

        [[nodiscard]] float FxFunc40(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc41(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc42(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc43(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc44(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc45(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc46(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc47(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc48(
            const std::vector<std::int32_t>& param, TimeValues times);
        [[nodiscard]] float FxFunc49(
            const std::vector<std::int32_t>& param, TimeValues times);

    private:
        [[nodiscard]] float InvokeFloatFunc(
            std::uint32_t funcId,
            const std::vector<std::int32_t>& parameters,
            TimeValues times);

        std::shared_ptr<const EffectActionDictionary> _actions{};
        std::shared_ptr<const EffectFuncDictionary> _funcs{};
    };

    enum class EffElemFlags : std::uint32_t
    {
        None = 0x0,
        UseTransform = 0x1,
        UseAcceleration = 0x2,
        UseMesh = 0x4,
        SpawnUnitVecs = 0x8,
        KeepAlive = 0x10,
        ParticleExtension = 0x20,
        CheckCollision = 0x40,
        SpawnChildEffect = 0x80,
        DestroyOnDetach = 0x100,
        ElementExtension = 0x80000,
        DrawEnabled = 0x100000
    };

    [[nodiscard]] constexpr EffElemFlags operator|(
        EffElemFlags left, EffElemFlags right) noexcept
    {
        return static_cast<EffElemFlags>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr EffElemFlags operator&(
        EffElemFlags left, EffElemFlags right) noexcept
    {
        return static_cast<EffElemFlags>(
            static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr EffElemFlags operator^(
        EffElemFlags left, EffElemFlags right) noexcept
    {
        return static_cast<EffElemFlags>(
            static_cast<std::uint32_t>(left) ^ static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr EffElemFlags operator~(EffElemFlags value) noexcept
    {
        return static_cast<EffElemFlags>(~static_cast<std::uint32_t>(value));
    }

    constexpr EffElemFlags& operator|=(EffElemFlags& left, EffElemFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr EffElemFlags& operator&=(EffElemFlags& left, EffElemFlags right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr EffElemFlags& operator^=(EffElemFlags& left, EffElemFlags right) noexcept
    {
        left = left ^ right;
        return left;
    }

    class EffectElementEntry;

    class EffectEntry
    {
    public:
        std::int32_t EffectId = 0;
        const std::shared_ptr<std::vector<std::shared_ptr<EffectElementEntry>>> Elements
            = std::make_shared<std::vector<std::shared_ptr<EffectElementEntry>>>();
        // A colour every particle of this effect is drawn in instead of its
        // own, at its own brightness: the Instagib muzzle flash in the
        // shooter's rail colour. Not set, the effect is drawn as authored.
        std::optional<OpenTK::Mathematics::Vector3> Tint{};

        EffectEntry() = default;
        EffectEntry(const EffectEntry&) = delete;
        EffectEntry& operator=(const EffectEntry&) = delete;
        EffectEntry(EffectEntry&&) = delete;
        EffectEntry& operator=(EffectEntry&&) = delete;

        [[nodiscard]] bool IsFinished() const;
        void Transform(
            OpenTK::Mathematics::Vector3 facing,
            OpenTK::Mathematics::Vector3 up,
            OpenTK::Mathematics::Vector3 position);
        void Transform(
            OpenTK::Mathematics::Vector3 position,
            OpenTK::Mathematics::Matrix4 transform);
        void SetElementExtension(bool set);
        void SetDrawEnabled(bool set);
        void SetReadOnlyField(std::int32_t index, float value);
        void ResetElements(float elapsedTime);

    private:
        void SetElementExtension();
        void ClearElementExtension();
        void SetDrawEnabled();
        void ClearDrawEnabled();
    };

    class EffectParticle;

    class EffectElementEntry : public EffectFuncBase
    {
    public:
        std::int32_t EffectId = 0;
        std::string EffectName{};
        std::string ElementName{};
        float CreationTime = 0.0F;
        float ExpirationTime = 0.0F;
        float DrainTime = 0.0F;
        float BufferTime = 0.0F;
        float Lifespan = 0.0F;
        EffElemFlags Flags = EffElemFlags::None;
        std::int32_t DrawType = 0;
        OpenTK::Mathematics::Matrix4 OwnTransform{};
        OpenTK::Mathematics::Matrix4 Transform{};
        OpenTK::Mathematics::Vector3 Acceleration{};
        bool Func39Called = false;
        float ParticleAmount = 0.0F;
        bool Expired = false;
        std::int32_t ChildEffectId = 0;
        // Overrides the colour of every particle, at its own brightness: set
        // by Scene::SpawnTintedEffect, or by the owning EffectEntry's Tint.
        std::optional<OpenTK::Mathematics::Vector3> Tint{};
        float RoField1 = 0.0F;
        float RoField2 = 0.0F;
        float RoField3 = 0.0F;
        float RoField4 = 0.0F;

        std::int32_t Parity = 0;
        const std::shared_ptr<std::vector<std::shared_ptr<MphRead::Particle>>> ParticleDefinitions
            = std::make_shared<std::vector<std::shared_ptr<MphRead::Particle>>>();
        const std::shared_ptr<std::vector<std::int32_t>> TextureBindingIds
            = std::make_shared<std::vector<std::int32_t>>();
        const std::shared_ptr<std::vector<std::shared_ptr<EffectParticle>>> Particles
            = std::make_shared<std::vector<std::shared_ptr<EffectParticle>>>();

        std::shared_ptr<MphRead::EffectElement> Definition{};
        std::shared_ptr<MphRead::Formats::Collision::EntityCollision> EntityCollision{};
        std::shared_ptr<MphRead::Effects::EffectEntry> EffectEntry{};
        std::shared_ptr<MphRead::Model> Model{};
        const std::shared_ptr<std::vector<std::shared_ptr<MphRead::Node>>> Nodes
            = std::make_shared<std::vector<std::shared_ptr<MphRead::Node>>>();

        EffectElementEntry() = default;
        EffectElementEntry(const EffectElementEntry&) = delete;
        EffectElementEntry& operator=(const EffectElementEntry&) = delete;
        EffectElementEntry(EffectElementEntry&&) = delete;
        EffectElementEntry& operator=(EffectElementEntry&&) = delete;

    protected:
        void FxFunc01(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) override;
        void FxFunc03(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) override;
        void FxFunc11(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) override;

        [[nodiscard]] float FxFunc22(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc23(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc24(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc25(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc26(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc27(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc29(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc30(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc31(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc32(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc33(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc34(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc35(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc36(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc37(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc38(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc39(
            const std::vector<std::int32_t>& param, TimeValues times) override;
    };

    class EffectParticle : public EffectFuncBase
    {
    public:
        float CreationTime = 0.0F;
        float ExpirationTime = 0.0F;
        float Lifespan = 0.0F;
        OpenTK::Mathematics::Vector3 Position{};
        OpenTK::Mathematics::Vector3 Speed{};
        float Scale = 0.0F;
        float Rotation = 0.0F;
        float Red = 0.0F;
        float Green = 0.0F;
        float Blue = 0.0F;
        float Alpha = 0.0F;
        std::int32_t ParticleId = 0;
        float PortionTotal = 0.0F;
        float RoField1 = 0.0F;
        float RoField2 = 0.0F;
        float RoField3 = 0.0F;
        float RoField4 = 0.0F;
        float RwField1 = 0.0F;
        float RwField2 = 0.0F;
        float RwField3 = 0.0F;
        float RwField4 = 0.0F;

        std::shared_ptr<EffectElementEntry> Owner{};
        std::int32_t MaterialId = 0;
        std::int32_t SetVecsId = 0;
        std::int32_t DrawId = 0;

        EffectParticle() = default;
        EffectParticle(const EffectParticle&) = delete;
        EffectParticle& operator=(const EffectParticle&) = delete;
        EffectParticle(EffectParticle&&) = delete;
        EffectParticle& operator=(EffectParticle&&) = delete;

        [[nodiscard]] OpenTK::Mathematics::Vector3 EffectVec1() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 EffectVec2() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 EffectVec3() const noexcept;
        [[nodiscard]] bool ShouldDraw() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Color() const noexcept;
        // This particle's colour after its effect's Tint, if it has one.
        [[nodiscard]] OpenTK::Mathematics::Vector3 TintedColor(OpenTK::Mathematics::Vector3 color) const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector2 Texcoord0() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Vertex0() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector2 Texcoord1() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Vertex1() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector2 Texcoord2() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Vertex2() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector2 Texcoord3() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Vertex3() const noexcept;
        [[nodiscard]] bool DrawNode() const noexcept;
        [[nodiscard]] MphRead::BillboardMode BillboardMode() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Matrix4 NodeTransform() const noexcept;

        [[nodiscard]] std::shared_ptr<const EffectFuncDictionary> Funcs() const override;
        void SetFuncs(std::shared_ptr<const EffectFuncDictionary> value) override;
        [[nodiscard]] std::shared_ptr<const EffectActionDictionary> Actions() const override;
        void SetActions(std::shared_ptr<const EffectActionDictionary> value) override;

        void InvokeSetVecsFunc(OpenTK::Mathematics::Matrix4 viewMatrix);
        void InvokeDrawFunc(float scaleFactor);
        void SetFuncIds();
        void AddRenderItem(MphRead::Scene* scene);

    protected:
        void FxFunc01(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) override;
        void FxFunc03(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) override;
        void FxFunc11(
            const std::vector<std::int32_t>& param,
            TimeValues times,
            OpenTK::Mathematics::Vector3& vec) override;

        [[nodiscard]] float FxFunc22(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc23(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc24(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc25(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc26(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc27(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc29(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc30(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc31(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc32(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc33(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc34(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc35(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc36(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc37(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc38(
            const std::vector<std::int32_t>& param, TimeValues times) override;
        [[nodiscard]] float FxFunc39(
            const std::vector<std::int32_t>& param, TimeValues times) override;

    private:
        OpenTK::Mathematics::Vector3 _effectVec1{};
        OpenTK::Mathematics::Vector3 _effectVec2{};
        OpenTK::Mathematics::Vector3 _effectVec3{};

        bool _shouldDraw = false;
        OpenTK::Mathematics::Vector3 _color{};
        OpenTK::Mathematics::Vector2 _texcoord0{};
        OpenTK::Mathematics::Vector3 _vertex0{};
        OpenTK::Mathematics::Vector2 _texcoord1{};
        OpenTK::Mathematics::Vector3 _vertex1{};
        OpenTK::Mathematics::Vector2 _texcoord2{};
        OpenTK::Mathematics::Vector3 _vertex2{};
        OpenTK::Mathematics::Vector2 _texcoord3{};
        OpenTK::Mathematics::Vector3 _vertex3{};
        bool _drawNode = false;
        MphRead::BillboardMode _billboardMode = MphRead::BillboardMode::None;
        OpenTK::Mathematics::Matrix4 _nodeTransform{};

        void SetVecsB0();
        void SetVecsBC();
        void SetVecsC0(OpenTK::Mathematics::Matrix4 viewMatrix);
        void SetVecsD4();
        void SetVecsD8();

        void DrawB8(float scaleFactor);
        void DrawC4(float scaleFactor);
        void DrawCC(float scaleFactor);
        void DrawD0(float scaleFactor);
        void DrawDC(float scaleFactor);
        void DrawShared(float scaleFactor, bool skipIfZeroSpeed);
    };
}
