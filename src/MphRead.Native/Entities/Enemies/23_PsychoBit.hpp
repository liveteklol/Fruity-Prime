#pragma once

#include "../../Formats/Culling.hpp"
#include "../../Formats/Effects.hpp"
#include "../EnemyInstanceEntity.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace MphRead
{
    class EquipInfo;
}

namespace MphRead::Entities
{
    class EnemySpawnEntity;
}

namespace MphRead::Mods::Diagnostics
{
    class HomingSpawnFixture;
}

namespace MphRead::Entities::Enemies
{
    struct Enemy23Values
    {
        std::uint16_t HealthMax = 0;
        std::uint16_t BeamDamage = 0;
        std::uint16_t SplashDamage = 0;
        std::uint16_t ContactDamage = 0;
        std::int32_t MinSpeedFactor1 = 0;
        std::int32_t MaxSpeedFactor1 = 0;
        std::int32_t MinSpeedFactor2 = 0;
        std::int32_t MaxSpeedFactor2 = 0;
        std::int32_t RangeMaxCosine = 0;
        std::int32_t Unknown1C = 0;
        std::int32_t Unused20 = 0;
        std::uint16_t DelayTime = 0;
        std::uint16_t ShotTime = 0;
        std::int32_t Unused28 = 0;
        std::uint16_t MinShots = 0;
        std::uint16_t MaxShots = 0;
        std::uint16_t DoubleSpeedSteps = 0;
        std::uint16_t AimSteps = 0;
        std::uint16_t SpeedSteps = 0;
        std::int16_t ScanId = 0;
        std::int32_t Effectiveness = 0;
    };

    class Enemy23Entity : public EnemyInstanceEntity
    {
        friend class ::MphRead::Mods::Diagnostics::HomingSpawnFixture;
    public:
        Enemy23Entity(EnemyInstanceEntityData data,
            Formats::Culling::NodeRef nodeRef, Scene* scene);

        Enemy23Entity(const Enemy23Entity&) = delete;
        Enemy23Entity& operator=(const Enemy23Entity&) = delete;
        Enemy23Entity(Enemy23Entity&&) = delete;
        Enemy23Entity& operator=(Enemy23Entity&&) = delete;

        [[nodiscard]] bool Behavior00();
        [[nodiscard]] bool Behavior01();
        [[nodiscard]] bool Behavior02();
        [[nodiscard]] bool Behavior03();
        [[nodiscard]] bool Behavior04();
        [[nodiscard]] bool Behavior05();
        [[nodiscard]] bool Behavior06();
        [[nodiscard]] bool Behavior07();
        [[nodiscard]] bool Behavior08();
        [[nodiscard]] bool Behavior09();
        [[nodiscard]] bool Behavior10();
        [[nodiscard]] bool Behavior11();
        [[nodiscard]] bool Behavior12();

        [[nodiscard]] static bool Behavior00(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior01(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior02(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior03(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior04(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior05(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior06(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior07(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior08(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior09(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior10(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior11(Enemy23Entity* enemy);
        [[nodiscard]] static bool Behavior12(Enemy23Entity* enemy);

        void Destroy() override;

    protected:
        void EnemyInitialize() override;
        void EnemyProcess() override;
        [[nodiscard]] bool EnemyTakeDamage(EntityBase* source) override;

    private:
        static const std::array<std::int32_t, 11> _recolors;

        EnemySpawnEntity* const _spawner;
        Enemy23Values _values{};
        CollisionVolume _homeVolume{};
        CollisionVolume _nearVolume{};
        CollisionVolume _rangeVolume{};

        OpenTK::Mathematics::Vector3 _curFacing{};
        OpenTK::Mathematics::Vector3 _moveTarget{};
        OpenTK::Mathematics::Vector3 _moveStart{};
        float _roamAngleSign = 1.0F;
        std::uint16_t _delayTimer = 0;
        std::uint16_t _shotCount = 0;
        std::uint16_t _shotTimer = 0;
        bool _damaged = false;
        float _speedFactor = 0.0F;
        float _speedInc = 0.0F;
        bool _reachedTarget = false;
        float _moveDistSqr = 0.0F;
        float _moveDistSqrHalf = 0.0F;
        bool _increaseSpeed = false;
        std::uint16_t _camSeqDelayTimer = 0;

        std::uint16_t _reachTargetHackTimer = 0;

        OpenTK::Mathematics::Vector3 _eyePos{};
        OpenTK::Mathematics::Vector3 _aimVec{1.0F, 0.0F, 0.0F};
        OpenTK::Mathematics::Vector3 _targetVec{};
        OpenTK::Mathematics::Vector3 _crossVec{1.0F, 0.0F, 0.0F};
        float _aimAngleStep = 0.0F;
        std::uint16_t _aimSteps = 0;

        std::shared_ptr<EquipInfo> _equipInfo{};
        std::int32_t _ammo = 1000;
        std::shared_ptr<Effects::EffectEntry> _effect{};

        void SpawnEffect();
        void UpdateMoveTarget(OpenTK::Mathematics::Vector3 targetPoint);
        void PickRoamTarget();
        void UpdateSpeed(float min, float max);
        void UpdateFacing();
        void CheckReachedTarget();
        void MoveAway();

        void State00();
        void State01();
        void State02();
        void State03();
        void State04();
        void State05();
        void State06();
        void State07();
        void State08();
        void State09();
        void State10();
    };
}
