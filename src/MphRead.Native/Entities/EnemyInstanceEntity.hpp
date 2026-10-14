#pragma once

#include "../Formats/Culling.hpp"
#include "../Formats/Formats.hpp"
#include "EntityBase.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace MphRead
{
    class BeamProjectileArray;
}

namespace MphRead::Entities
{
    class BeamProjectileEntity;
    class BombEntity;
    class EnemyInstanceEntity;

    enum class EnemyFlags : std::uint16_t
    {
        Visible = 1,
        NoHomingNc = 2,
        NoHomingCo = 4,
        Invincible = 8,
        NoBombDamage = 0x10,
        CollidePlayer = 0x20,
        CollideBeam = 0x40,
        NoMaxDistance = 0x80,
        OnRadar = 0x100,
        Static = 0x200
    };

    [[nodiscard]] constexpr EnemyFlags operator|(EnemyFlags left, EnemyFlags right) noexcept
    {
        return static_cast<EnemyFlags>(
            static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr EnemyFlags operator&(EnemyFlags left, EnemyFlags right) noexcept
    {
        return static_cast<EnemyFlags>(
            static_cast<std::uint16_t>(left) & static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr EnemyFlags operator^(EnemyFlags left, EnemyFlags right) noexcept
    {
        return static_cast<EnemyFlags>(
            static_cast<std::uint16_t>(left) ^ static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr EnemyFlags operator~(EnemyFlags value) noexcept
    {
        return static_cast<EnemyFlags>(
            static_cast<std::uint16_t>(~static_cast<std::uint16_t>(value)));
    }

    constexpr EnemyFlags& operator|=(EnemyFlags& left, EnemyFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr EnemyFlags& operator&=(EnemyFlags& left, EnemyFlags right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr EnemyFlags& operator^=(EnemyFlags& left, EnemyFlags right) noexcept
    {
        left = left ^ right;
        return left;
    }

    enum class Effectiveness : std::uint8_t
    {
        Zero,
        Half,
        Normal,
        Double
    };

    struct EnemyInstanceEntityData
    {
        const MphRead::EnemyType Type;
        EntityBase* const Spawner;
        // The same enemy's name on every machine, for a homing shot's target.
        const std::uint64_t ModStableTargetId;

        EnemyInstanceEntityData() noexcept
            : Type{}, Spawner(nullptr), ModStableTargetId(0)
        {
        }

        EnemyInstanceEntityData(MphRead::EnemyType type, EntityBase* spawner,
            std::uint64_t modStableTargetId = 0) noexcept
            : Type(type), Spawner(spawner), ModStableTargetId(modStableTargetId)
        {
        }
    };

    template <typename T>
        requires std::is_convertible_v<T*, EnemyInstanceEntity*>
    struct EnemyBehavior;

    template <typename T>
        requires std::is_convertible_v<T*, EnemyInstanceEntity*>
    struct EnemySubroutine;

    class EnemyInstanceEntity : public EntityBase
    {
    public:
        class FlagsProperty final
        {
        public:
            FlagsProperty() noexcept = default;

            FlagsProperty& operator=(EnemyFlags value) noexcept
            {
                _value = value;
                return *this;
            }

            FlagsProperty& operator|=(EnemyFlags value) noexcept
            {
                _value |= value;
                return *this;
            }

            FlagsProperty& operator&=(EnemyFlags value) noexcept
            {
                _value &= value;
                return *this;
            }

            FlagsProperty& operator^=(EnemyFlags value) noexcept
            {
                _value ^= value;
                return *this;
            }

            [[nodiscard]] operator EnemyFlags() const noexcept
            {
                return _value;
            }

            [[nodiscard]] EnemyFlags operator()() const noexcept
            {
                return _value;
            }

        private:
            EnemyFlags _value{};
        };

        EnemyInstanceEntity(EnemyInstanceEntityData data,
            Formats::Culling::NodeRef nodeRef, Scene* scene);

        EnemyInstanceEntity(const EnemyInstanceEntity&) = delete;
        EnemyInstanceEntity& operator=(const EnemyInstanceEntity&) = delete;
        EnemyInstanceEntity(EnemyInstanceEntity&&) = delete;
        EnemyInstanceEntity& operator=(EnemyInstanceEntity&&) = delete;

        [[nodiscard]] std::uint16_t Health() const noexcept;
        [[nodiscard]] std::uint16_t HealthMax() const noexcept;
        [[nodiscard]] std::uint8_t StateA() const noexcept;
        [[nodiscard]] std::uint8_t StateB() const noexcept;
        [[nodiscard]] CollisionVolume HurtVolume() const noexcept;
        [[nodiscard]] MphRead::EnemyType EnemyType() const noexcept;
        [[nodiscard]] EntityBase* Owner() const noexcept;
        [[nodiscard]] std::uint64_t ModStableTargetId() const noexcept { return _data.ModStableTargetId; }
        [[nodiscard]] std::int32_t HealthbarMessageId() const noexcept;

        std::array<bool, 8> HitPlayers{};
        std::array<Effectiveness, 9> BeamEffectiveness{};
        FlagsProperty Flags{};

        static void DestroyBeams();

        void Initialize() override;
        void Destroy() override;
        void GetPosition(OpenTK::Mathematics::Vector3& position) override;
        void GetVectors(OpenTK::Mathematics::Vector3& position,
            OpenTK::Mathematics::Vector3& up,
            OpenTK::Mathematics::Vector3& facing) override;
        [[nodiscard]] bool GetTargetable() override;
        [[nodiscard]] bool Process() override;
        void GetDrawInfo() override;
        void GetDisplayVolumes() override;

        void ClearHitPlayers();
        void SetHealth(std::uint16_t health) noexcept;
        [[nodiscard]] bool CheckHitByBomb(BombEntity* bomb);
        void TakeDamage(std::uint32_t damage, EntityBase* source);
        [[nodiscard]] Effectiveness GetEffectiveness(BeamType beam) const;

        [[nodiscard]] static OpenTK::Mathematics::Vector3 RotateVector(
            OpenTK::Mathematics::Vector3 vec,
            OpenTK::Mathematics::Vector3 axis,
            float angle);
        [[nodiscard]] static bool SeekTargetVector(
            OpenTK::Mathematics::Vector3 target,
            OpenTK::Mathematics::Vector3& current,
            OpenTK::Mathematics::Vector3 axis,
            std::uint16_t& steps,
            float angle);
        [[nodiscard]] bool SeekTargetFacing(
            OpenTK::Mathematics::Vector3 target,
            OpenTK::Mathematics::Vector3 up,
            std::uint16_t& steps,
            float angle);

    protected:
        const EnemyInstanceEntityData _data;
        std::uint16_t _timeSinceDamage = 510;
        std::uint16_t _health = 20;
        std::uint16_t _healthMax = 20;
        EntityBase* _owner = nullptr;
        CollisionVolume _hurtVolume{};
        CollisionVolume _hurtVolumeInit{};
        std::uint8_t _state1 = 0;
        std::uint8_t _state2 = 0;
        std::uint8_t _subId = 0;
        OpenTK::Mathematics::Vector3 _prevPos = OpenTK::Mathematics::Vector3::Zero;
        OpenTK::Mathematics::Vector3 _speed = OpenTK::Mathematics::Vector3::Zero;
        float _boundingRadius = 0.0F;
        std::shared_ptr<ManagedArray<std::function<void()>>> _stateProcesses{};

        static std::shared_ptr<MphRead::BeamProjectileArray> _beams;

        [[nodiscard]] OpenTK::Mathematics::Vector3 FixParallelVectors(
            OpenTK::Mathematics::Vector3 facing,
            OpenTK::Mathematics::Vector3 up) const;
        [[nodiscard]] bool ContactDamagePlayer(std::uint32_t damage, bool knockback);
        [[nodiscard]] virtual bool BaseProcess();
        void DrawGeneric();
        virtual void EnemyInitialize();
        virtual void EnemyProcess();
        [[nodiscard]] virtual bool EnemyGetDrawInfo();
        virtual void Detach();
        [[nodiscard]] virtual bool EnemyTakeDamage(EntityBase* source);
        void CallStateProcess();
        void SetHealthbarMessageId(std::int32_t value) noexcept;

        template <typename T>
            requires std::is_convertible_v<T*, EnemyInstanceEntity*>
        [[nodiscard]] bool CallSubroutine(
            std::type_identity_t<std::span<const EnemySubroutine<T>>> subroutines, T* enemy);

        [[nodiscard]] bool HandleBlockingCollision(
            OpenTK::Mathematics::Vector3 position,
            CollisionVolume volume,
            bool updateSpeed);
        [[nodiscard]] bool HandleBlockingCollision(
            OpenTK::Mathematics::Vector3 position,
            CollisionVolume volume,
            bool updateSpeed,
            bool& withGround,
            bool& withWall);

    private:
        bool _onlyMoveHurtVolume = false;
        bool _noIneffectiveEffect = false;
        std::int32_t _healthbarMessageId = 0;

        void DoMovement();
        void UpdateHurtVolume();
        void PlayEnemySfx(std::int32_t sfx, bool noUpdate);
    };

    template <typename T>
        requires std::is_convertible_v<T*, EnemyInstanceEntity*>
    struct EnemyBehavior
    {
        const std::uint8_t NextState;
        const std::function<bool(T*)> Function;

        EnemyBehavior()
            : NextState(0), Function{}
        {
        }

        EnemyBehavior(std::uint8_t nextState, std::function<bool(T*)> function)
            : NextState(nextState), Function(std::move(function))
        {
        }
    };

    template <typename T>
        requires std::is_convertible_v<T*, EnemyInstanceEntity*>
    struct EnemySubroutine
    {
        const std::vector<EnemyBehavior<T>>* const Behaviors;

        EnemySubroutine() noexcept
            : Behaviors(nullptr)
        {
        }

        explicit EnemySubroutine(const std::vector<EnemyBehavior<T>>& behaviors) noexcept
            : Behaviors(&behaviors)
        {
        }
    };

    template <typename T>
        requires std::is_convertible_v<T*, EnemyInstanceEntity*>
    bool EnemyInstanceEntity::CallSubroutine(
        std::type_identity_t<std::span<const EnemySubroutine<T>>> subroutines, T* enemy)
    {
        assert(enemy == this);
        if (static_cast<std::size_t>(_subId) >= subroutines.size())
        {
            throw System::ArgumentOutOfRangeException();
        }
        const EnemySubroutine<T>& subroutine = subroutines[static_cast<std::size_t>(_subId)];
        if (subroutine.Behaviors == nullptr)
        {
            throw System::NullReferenceException();
        }
        const std::vector<EnemyBehavior<T>>& behaviors = *subroutine.Behaviors;
        if (behaviors.empty())
        {
            return false;
        }
        std::size_t index = 0;
        while (true)
        {
            const std::function<bool(T*)>& function = behaviors[index].Function;
            if (!function)
            {
                throw System::NullReferenceException();
            }
            if (function(enemy))
            {
                break;
            }
            ++index;
            if (index >= behaviors.size())
            {
                return false;
            }
        }
        _state2 = behaviors[index].NextState;
        return true;
    }
}
