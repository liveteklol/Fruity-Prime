#pragma once

#include "../Mods/Network/NetShotDiagnostics.hpp"

#include "../Formats/CollisionDetection.hpp"
#include "../Formats/Culling.hpp"
#include "../Formats/Effects.hpp"
#include "EntityBase.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace MphRead
{
    class EquipInfo;
    class WeaponInfo;
}

namespace MphRead::Mods::Network
{
    class NetPlayerLifecycle;
}

namespace MphRead::Entities
{
    class PlayerEntity;
    enum class Effectiveness : std::uint8_t;

    enum class BeamFlags : std::uint16_t
    {
        None = 0x0,
        Collided = 0x1,
        Charged = 0x2,
        Homing = 0x4,
        Ricochet = 0x8,
        SelfDamage = 0x10,
        ForceEffect = 0x20,
        Continuous = 0x40,
        Destroyable = 0x80,
        HasModel = 0x100,
        RadiusIndex1 = 0x200,
        RadiusIndex2 = 0x400,
        LifeDrain = 0x800,
        SurfaceCollision = 0x1000,
        DestroyMuzzle = 0x2000
    };

    [[nodiscard]] constexpr BeamFlags operator|(BeamFlags left, BeamFlags right) noexcept
    {
        return static_cast<BeamFlags>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
    }
    [[nodiscard]] constexpr BeamFlags operator&(BeamFlags left, BeamFlags right) noexcept
    {
        return static_cast<BeamFlags>(static_cast<std::uint16_t>(left) & static_cast<std::uint16_t>(right));
    }
    [[nodiscard]] constexpr BeamFlags operator^(BeamFlags left, BeamFlags right) noexcept
    {
        return static_cast<BeamFlags>(static_cast<std::uint16_t>(left) ^ static_cast<std::uint16_t>(right));
    }
    [[nodiscard]] constexpr BeamFlags operator~(BeamFlags value) noexcept
    {
        return static_cast<BeamFlags>(static_cast<std::uint16_t>(~static_cast<std::uint16_t>(value)));
    }
    constexpr BeamFlags& operator|=(BeamFlags& left, BeamFlags right) noexcept
    {
        left = left | right;
        return left;
    }
    constexpr BeamFlags& operator&=(BeamFlags& left, BeamFlags right) noexcept
    {
        left = left & right;
        return left;
    }
    constexpr BeamFlags& operator^=(BeamFlags& left, BeamFlags right) noexcept
    {
        left = left ^ right;
        return left;
    }

    enum class BeamSpawnFlags : std::uint8_t
    {
        None = 0x0,
        DoubleDamage = 0x1,
        Charged = 0x2,
        NoMuzzle = 0x4,
        PrimeHunter = 0x8,
        DestroyMuzzle = 0x10
    };

    [[nodiscard]] constexpr BeamSpawnFlags operator|(BeamSpawnFlags left, BeamSpawnFlags right) noexcept
    {
        return static_cast<BeamSpawnFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }
    [[nodiscard]] constexpr BeamSpawnFlags operator&(BeamSpawnFlags left, BeamSpawnFlags right) noexcept
    {
        return static_cast<BeamSpawnFlags>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }
    constexpr BeamSpawnFlags& operator|=(BeamSpawnFlags& left, BeamSpawnFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    enum class BeamResultFlags : std::uint8_t
    {
        NoSpawn = 0x0,
        Spawned = 0x1,
        Homing = 0x2
    };

    [[nodiscard]] constexpr BeamResultFlags operator|(BeamResultFlags left, BeamResultFlags right) noexcept
    {
        return static_cast<BeamResultFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }
    [[nodiscard]] constexpr BeamResultFlags operator&(BeamResultFlags left, BeamResultFlags right) noexcept
    {
        return static_cast<BeamResultFlags>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }
    constexpr BeamResultFlags& operator|=(BeamResultFlags& left, BeamResultFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    class BeamProjectileEntity : public EntityBase
    {
    public:
        explicit BeamProjectileEntity(Scene* scene);

        BeamProjectileEntity(const BeamProjectileEntity&) = delete;
        BeamProjectileEntity& operator=(const BeamProjectileEntity&) = delete;
        BeamProjectileEntity(BeamProjectileEntity&&) = delete;
        BeamProjectileEntity& operator=(BeamProjectileEntity&&) = delete;

        [[nodiscard]] BeamFlags Flags() const noexcept;
        void SetFlags(BeamFlags value) noexcept;
        // The authority frame the shooter's world was at when this shot was
        // launched -- the one thing that identifies a shot across two
        // machines. Stamped by Mods.Network.NetUnlagged on every machine that
        // spawns it. Zero for anything nobody aimed.
        std::uint32_t ModLaunchFrame = 0;
        [[nodiscard]] const Mods::Network::ShotKey& ModLaunchKey() const noexcept { return _modLaunchKey; }
        // Spawn's firing phase must survive until a Shock Coil beam tests an enemy.
        std::uint64_t ModContinuousPhase = 0;
        bool ModHasSharedContinuousPhase = false;
        std::uint16_t ModLaunchMatch = 0;
        std::uint64_t ModLaunchAuthority = 0;
        std::uint16_t ModLaunchGeneration = 0;
        std::uint16_t ModLaunchLife = 0;
        [[nodiscard]] BeamType Beam() const noexcept;
        void SetBeam(BeamType value) noexcept;
        [[nodiscard]] BeamType BeamKind() const noexcept;
        void SetBeamKind(BeamType value) noexcept;
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Velocity() const noexcept;
        void SetVelocity(::OpenTK::Mathematics::Vector3 value) noexcept;
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Acceleration() const noexcept;
        void SetAcceleration(::OpenTK::Mathematics::Vector3 value) noexcept;
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 BackPosition() const noexcept;
        void SetBackPosition(::OpenTK::Mathematics::Vector3 value) noexcept;
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 SpawnPosition() const noexcept;
        void SetSpawnPosition(::OpenTK::Mathematics::Vector3 value) noexcept;
        [[nodiscard]] std::array<::OpenTK::Mathematics::Vector3, 10>& PastPositions() noexcept;
        [[nodiscard]] const std::array<::OpenTK::Mathematics::Vector3, 10>& PastPositions() const noexcept;
        [[nodiscard]] std::int32_t DrawFuncId() const noexcept;
        void SetDrawFuncId(std::int32_t value) noexcept;
        [[nodiscard]] float Age() const noexcept;
        void SetAge(float value) noexcept;
        [[nodiscard]] float Lifespan() const noexcept;
        void SetLifespan(float value) noexcept;
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Color() const noexcept;
        void SetColor(::OpenTK::Mathematics::Vector3 value) noexcept;
        [[nodiscard]] std::uint8_t CollisionEffect() const noexcept;
        void SetCollisionEffect(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t DamageDirType() const noexcept;
        void SetDamageDirType(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t SplashDamageType() const noexcept;
        void SetSplashDamageType(std::uint8_t value) noexcept;
        [[nodiscard]] float Homing() const noexcept;
        void SetHoming(float value) noexcept;
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Direction() const noexcept;
        void SetDirection(::OpenTK::Mathematics::Vector3 value) noexcept;
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Right() const noexcept;
        void SetRight(::OpenTK::Mathematics::Vector3 value) noexcept;
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Up() const noexcept;
        void SetUp(::OpenTK::Mathematics::Vector3 value) noexcept;
        [[nodiscard]] float Damage() const noexcept;
        void SetDamage(float value) noexcept;
        [[nodiscard]] float HeadshotDamage() const noexcept;
        void SetHeadshotDamage(float value) noexcept;
        [[nodiscard]] float SplashDamage() const noexcept;
        void SetSplashDamage(float value) noexcept;
        [[nodiscard]] float SplashRadius() const noexcept;
        void SetSplashRadius(float value) noexcept;
        [[nodiscard]] float MaxDistance() const noexcept;
        void SetMaxDistance(float value) noexcept;
        [[nodiscard]] Affliction Afflictions() const noexcept;
        void SetAfflictions(Affliction value) noexcept;
        [[nodiscard]] std::shared_ptr<EntityBase> Owner() const noexcept;
        void SetOwner(std::shared_ptr<EntityBase> value) noexcept;
        void ReleaseReferences() noexcept;
        [[nodiscard]] std::shared_ptr<WeaponInfo> RicochetWeapon() const noexcept;
        void SetRicochetWeapon(std::shared_ptr<WeaponInfo> value) noexcept;
        [[nodiscard]] std::shared_ptr<Effects::EffectEntry> Effect() const noexcept;
        void SetEffect(std::shared_ptr<Effects::EffectEntry> value) noexcept;
        [[nodiscard]] std::shared_ptr<Effects::EffectEntry> MuzzleEffect() const noexcept;
        void SetMuzzleEffect(std::shared_ptr<Effects::EffectEntry> value) noexcept;
        [[nodiscard]] std::shared_ptr<EntityBase> Target() const noexcept;
        void SetTarget(std::shared_ptr<EntityBase> value) noexcept;
        [[nodiscard]] std::shared_ptr<EquipInfo> Equip() const noexcept;
        void SetEquip(std::shared_ptr<EquipInfo> value) noexcept;
        [[nodiscard]] std::int32_t DamageInterpolation() const noexcept;
        void SetDamageInterpolation(std::int32_t value) noexcept;
        [[nodiscard]] std::int32_t SpeedInterpolation() const noexcept;
        void SetSpeedInterpolation(std::int32_t value) noexcept;
        [[nodiscard]] float SpeedDecayTime() const noexcept;
        void SetSpeedDecayTime(float value) noexcept;
        [[nodiscard]] float Speed() const noexcept;
        void SetSpeed(float value) noexcept;
        [[nodiscard]] float InitialSpeed() const noexcept;
        void SetInitialSpeed(float value) noexcept;
        [[nodiscard]] float FinalSpeed() const noexcept;
        void SetFinalSpeed(float value) noexcept;
        [[nodiscard]] float DamageDirMag() const noexcept;
        void SetDamageDirMag(float value) noexcept;
        [[nodiscard]] float RicochetLossH() const noexcept;
        void SetRicochetLossH(float value) noexcept;
        [[nodiscard]] float RicochetLossV() const noexcept;
        void SetRicochetLossV(float value) noexcept;
        [[nodiscard]] float CylinderRadius() const noexcept;
        void SetCylinderRadius(float value) noexcept;

        void Initialize() override;
        void Reposition(::OpenTK::Mathematics::Vector3 offset);
        [[nodiscard]] bool Process() override;
        void OnCollision(Formats::CollisionResult colRes, EntityBase* colWith);
        void GetDrawInfo() override;
        void Destroy() override;
        void SpawnDamageEffect(Effectiveness effectiveness);

        [[nodiscard]] static BeamResultFlags Spawn(
            std::shared_ptr<EntityBase> owner,
            std::shared_ptr<EquipInfo> equip,
            ::OpenTK::Mathematics::Vector3 position,
            ::OpenTK::Mathematics::Vector3 direction,
            BeamSpawnFlags spawnFlags,
            Formats::Culling::NodeRef nodeRef,
            Scene* scene,
            BeamProjectileEntity* parent = nullptr);

    protected:
        [[nodiscard]] ::OpenTK::Mathematics::Matrix4 GetModelTransform(
            ModelInstance& inst, std::int32_t index) override;

    private:
        friend class ::MphRead::Mods::Network::NetPlayerLifecycle;
        void ModLaunchKey(const Mods::Network::ShotKey& value) noexcept { _modLaunchKey = value; }

        Mods::Network::ShotKey _modLaunchKey{};
        void CheckCollision();
        void ProcessRicochet(Formats::CollisionResult colRes);
        void PlayRicochetSfx();
        void StopHomingSfx();
        void PlayBeamHitSfx();
        void CheckSplashDamage(EntityBase* colWith);
        [[nodiscard]] float GetInterpolatedValue(
            std::int32_t type, float value1, float value2, float ratio) const;
        void Draw00();
        void Draw01();
        void Draw02();
        void Draw03();
        void Draw06();
        void Draw07();
        void Draw09();
        void Draw10();
        void Draw17();
        void DrawTrail1(float height);
        void DrawTrail2(float height, std::int32_t segments);
        void DrawTrail3(float height);
        void DrawTrail4(float height, float range, std::int32_t segments);
        [[nodiscard]] static std::shared_ptr<BeamProjectileEntity> ChooseBeamSlot(
            const std::shared_ptr<EquipInfo>& equip,
            const std::shared_ptr<EntityBase>& owner);
        [[nodiscard]] static bool CheckHomingTargets(
            const std::shared_ptr<BeamProjectileEntity>& beam,
            const std::shared_ptr<EquipInfo>& equip,
            Scene* scene);
        void SpawnIceWave(const std::shared_ptr<WeaponInfo>& weapon, float chargePct);
        void CheckIceWaveCollision(float angle);
        void CheckIceWaveCollision(
            PlayerEntity& player,
            ::OpenTK::Mathematics::Vector3 position,
            float angleCos,
            bool halfturret);
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 GetDamageDirection(
            ::OpenTK::Mathematics::Vector3 beamPos,
            ::OpenTK::Mathematics::Vector3 targetPos) const;
        void SpawnCollisionEffect(Formats::CollisionResult colRes, bool noSplat);
        void SpawnSniperBeam();
        [[nodiscard]] static ::OpenTK::Mathematics::Vector3 GetCrossVector(
            ::OpenTK::Mathematics::Vector3 up);

        BeamFlags _flags = BeamFlags::None;
        BeamType _beam = BeamType::PowerBeam;
        BeamType _beamKind = BeamType::PowerBeam;
        ::OpenTK::Mathematics::Vector3 _velocity{};
        ::OpenTK::Mathematics::Vector3 _acceleration{};
        ::OpenTK::Mathematics::Vector3 _backPosition{};
        ::OpenTK::Mathematics::Vector3 _spawnPosition{};
        std::array<::OpenTK::Mathematics::Vector3, 10> _pastPositions{};
        std::int32_t _drawFuncId = 0;
        float _age = 0.0F;
        float _lifespan = 0.0F;
        ::OpenTK::Mathematics::Vector3 _color{};
        std::uint8_t _collisionEffect = 0;
        std::uint8_t _damageDirType = 0;
        std::uint8_t _splashDamageType = 0;
        float _homing = 0.0F;
        ::OpenTK::Mathematics::Vector3 _direction{};
        ::OpenTK::Mathematics::Vector3 _right{};
        ::OpenTK::Mathematics::Vector3 _up{};
        float _damage = 0.0F;
        float _headshotDamage = 0.0F;
        float _splashDamage = 0.0F;
        float _splashRadius = 0.0F;
        float _maxDistance = 0.0F;
        Affliction _afflictions = Affliction::None;
        std::shared_ptr<EntityBase> _owner{};
        std::shared_ptr<WeaponInfo> _ricochetWeapon{};
        std::shared_ptr<Effects::EffectEntry> _effect{};
        std::shared_ptr<Effects::EffectEntry> _muzzleEffect{};
        std::shared_ptr<EntityBase> _target{};
        std::shared_ptr<EquipInfo> _equip{};
        std::int32_t _damageInterpolation = 0;
        std::int32_t _speedInterpolation = 0;
        float _speedDecayTime = 0.0F;
        float _speed = 0.0F;
        float _initialSpeed = 0.0F;
        float _finalSpeed = 0.0F;
        float _damageDirMag = 0.0F;
        float _ricochetLossH = 0.0F;
        float _ricochetLossV = 0.0F;
        float _cylinderRadius = 0.0F;

        static const std::shared_ptr<EquipInfo> _ricochetEquip;
        static const std::array<EntityType, 5> _homingTargetTypes;
        static const std::array<std::array<std::uint8_t, 12>, 11> _terSplat1P;

        std::shared_ptr<ModelInstance> _trailModel{};
        std::int32_t _bindingId = 0;
    };
}
