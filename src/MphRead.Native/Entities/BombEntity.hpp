#pragma once

#include "../Formats/Enums.hpp"
#include "EntityBase.hpp"

#include <cstdint>
#include <memory>

namespace MphRead::Effects
{
    class EffectEntry;
}

namespace MphRead::Entities
{
    class PlayerEntity;
    class EnemyInstanceEntity;

    enum class BombFlags : std::uint8_t
    {
        None = 0x0,
        Exploding = 0x1,
        Exploded = 0x2,
        HasModel = 0x4
    };

    [[nodiscard]] constexpr BombFlags operator|(BombFlags left, BombFlags right) noexcept
    {
        return static_cast<BombFlags>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr BombFlags operator&(BombFlags left, BombFlags right) noexcept
    {
        return static_cast<BombFlags>(
            static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr BombFlags operator^(BombFlags left, BombFlags right) noexcept
    {
        return static_cast<BombFlags>(
            static_cast<std::uint8_t>(left) ^ static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr BombFlags operator~(BombFlags value) noexcept
    {
        return static_cast<BombFlags>(
            static_cast<std::uint8_t>(~static_cast<std::uint8_t>(value)));
    }

    constexpr BombFlags& operator|=(BombFlags& left, BombFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr BombFlags& operator&=(BombFlags& left, BombFlags right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr BombFlags& operator^=(BombFlags& left, BombFlags right) noexcept
    {
        left = left ^ right;
        return left;
    }

    class BombEntity : public EntityBase
    {
    public:
        explicit BombEntity(Scene* scene);

        BombEntity(const BombEntity&) = delete;
        BombEntity& operator=(const BombEntity&) = delete;
        BombEntity(BombEntity&&) = delete;
        BombEntity& operator=(BombEntity&&) = delete;

        [[nodiscard]] BombFlags Flags() const noexcept;
        [[nodiscard]] PlayerEntity* Owner() const noexcept;
        [[nodiscard]] MphRead::BombType BombType() const noexcept;

        [[nodiscard]] std::int32_t BombIndex() const noexcept;
        void SetBombIndex(std::int32_t value) noexcept;

        [[nodiscard]] std::int32_t Countdown() const noexcept;
        void SetCountdown(std::int32_t value) noexcept;

        [[nodiscard]] float Radius() const noexcept;
        void SetRadius(float value) noexcept;

        [[nodiscard]] float SelfRadius() const noexcept;
        void SetSelfRadius(float value) noexcept;

        [[nodiscard]] std::uint16_t Damage() const noexcept;
        void SetDamage(std::uint16_t value) noexcept;

        [[nodiscard]] std::uint16_t EnemyDamage() const noexcept;
        void SetEnemyDamage(std::uint16_t value) noexcept;

        [[nodiscard]] std::shared_ptr<Effects::EffectEntry> Effect() const noexcept;

        void Initialize() override;
        void Reposition(OpenTK::Mathematics::Vector3 offset);
        [[nodiscard]] bool Process() override;
        void GetDrawInfo() override;
        void Destroy() override;

        void PlaySpawnSfx();

        // Mods.Network.NetBombs: the bomb's sequence on its owner's machine;
        // 0 for one nobody numbered, and again once destroyed (pooled).
        std::uint32_t ModSequence = 0;
        // Mods.Network.NetBombs: a copy's bomb moved toward where its owner
        // has it, turned the way it moves as one homing in does.
        void ModMoveTo(OpenTK::Mathematics::Vector3 position);

        [[nodiscard]] static std::shared_ptr<BombEntity> Spawn(
            PlayerEntity* owner,
            OpenTK::Mathematics::Matrix4 transform,
            Scene* scene);

    private:
        void LockjawCheckTargeting(PlayerEntity& player, EntityBase*& hitEntity);
        [[nodiscard]] bool LockjawCheckSnare(OpenTK::Mathematics::Vector3 position);
        void SetTarget(EntityBase* target);
        void ProcessTargeting();
        void DrawLockjawTrail(
            OpenTK::Mathematics::Vector3 point1,
            OpenTK::Mathematics::Vector3 point2,
            float height,
            std::int32_t segments,
            std::int32_t targetBombIndex);

        BombFlags _flags = BombFlags::None;
        PlayerEntity* _owner = nullptr;
        MphRead::BombType _bombType = MphRead::BombType::MorphBall;
        std::int32_t _bombIndex = 0;

        EntityBase* _target = nullptr;
        // Adventure enemies can leave the scene while the chain is homing.
        // Keep them alive until the target is released; do not retain players,
        // since a self-targeted chain is owned by its player.
        std::shared_ptr<EnemyInstanceEntity> _enemyTarget{};
        OpenTK::Mathematics::Vector3 _speed = OpenTK::Mathematics::Vector3::Zero;

        std::int32_t _countdown = 0;
        float _radius = 0.0F;
        float _selfRadius = 0.0F;
        std::uint16_t _damage = 0;
        std::uint16_t _enemyDamage = 0;

        std::shared_ptr<Effects::EffectEntry> _effect{};
        std::shared_ptr<ModelInstance> _trailModel{};
        std::int32_t _bindingId = 0;
        std::uint64_t _lockjawVisualTick = 0;

    public:
        // Map-audit hooks stay inert outside -maptest -drawrate checks. The
        // RNG check is scoped here because other draw work advances effects.
        [[nodiscard]] std::int32_t ModLockjawTrailBindingId() const noexcept { return _bindingId; }
        inline static bool ModAuditLockjawDrawRng = false;
        inline static std::int32_t ModLockjawDrawRngChanges = 0;
    };
}
