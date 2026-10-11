#pragma once

#include <source_location>

#define MPHREAD_PLAYER_ENTITY_CANONICAL_HEADER 1

#ifndef MPHREAD_PLAYER_AI_MEMBERS
#define MPHREAD_PLAYER_AI_MEMBERS \
public: \
    class PlayerAiData; \
    std::shared_ptr<PlayerAiData> AiData{}; \
    std::shared_ptr<::MphRead::Formats::NodeData3> ClosestNode{}; \
    [[nodiscard]] std::int32_t BotLevel() const noexcept { return _botLevel; } \
    void SetBotLevel(std::int32_t value) noexcept { _botLevel = value; } \
private: \
    std::int32_t _botLevel = 0;
#endif

#include "DynamicLightEntity.hpp"
#include "DialancheNativeCollision.hpp"
#include "WeavelLungeInput.hpp"
#include "WeavelOwnedTurret.hpp"
#include "WeavelReplicaTransition.hpp"
#include "PlayerCamera.hpp"
#include "PlayerCollision.hpp"
#include "PlayerDialog.hpp"
#include "PlayerDraw.hpp"
#include "PlayerHud.hpp"
#include "PlayerInput.hpp"
#include "PlayerPause.hpp"
#include "PlayerProcess.hpp"
#include "PlayerScan.hpp"
#include "PlayerSound.hpp"
#include "../../Mods/Chat/PlayerEntityChatHud.hpp"
#include "../../Mods/Input/AimAssist/AimAssistWorld.hpp"
#include "../../Mods/Input/PlayerEntityHaptics.hpp"
#include "../../Mods/Network/PlayerEntityNetAim.hpp"
#include "../../Mods/Network/PlayerEntityNetHud.hpp"
#include "../../Mods/Render/PlayerEntityAmmoClear.hpp"
#include "../../Mods/Render/GoldenCapture.hpp"
#include "../../Mods/Render/PlayerEntityEndScreen.hpp"
#include "../../Mods/Render/PlayerEntityStylusHud.hpp"
#include "../../Mods/Render/PlayerEntityTeamScoreboard.hpp"
#include "../../Mods/Render/PlayerEntityMapPick.hpp"
#include "../../Mods/Render/PlayerEntityVoteHud.hpp"
#include "../EnemyInstanceEntity.hpp"
#include "../../Formats/Culling.hpp"
#include "../../Formats/Formats.hpp"
#include "../../Formats/Model.hpp"
#include "../../Formats/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace MphRead::Mods::Diagnostics { class WeavelAltFormCheck; }
namespace MphRead::Mods::Diagnostics { class AimCheck; }

namespace MphRead
{
    class BeamProjectileArray;
    class EquipInfo;
    class WeaponInfo;
    class Scene;

    namespace Formats
    {
        class NodeData3;
    }

    namespace Effects
    {
        class EffectEntry;
    }
}

namespace MphRead::Mods::Network
{
    class NetCombatCheck;
    class DialancheCombatCheck;
    class SpireAltPoseCheck;
}

namespace MphRead::Mods::Input
{
    class PointerCheck;
}

namespace MphRead::Entities
{
    class BeamProjectileEntity;
    class BombEntity;
    class EnemySpawnEntity;
    class MorphCameraEntity;
    class OctolithFlagEntity;
    class JumpPadEntity;
    class HalfturretEntity;

    enum class LoadFlags : std::uint8_t
    {
        None = 0,
        Connected = 0x1,
        WasConnected = 0x2,
        Disconnected = 0x4,
        Initial = 0x8,
        Unknown4 = 0x10,
        Active = 0x20,
        SlotActive = 0x40,
        Spawned = 0x80
    };

    // LoadFlags.ToString(): a [Flags] enum.
    [[nodiscard]] std::string ToString(LoadFlags value);

    [[nodiscard]] constexpr LoadFlags operator|(LoadFlags left, LoadFlags right) noexcept
    {
        return static_cast<LoadFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }
    [[nodiscard]] constexpr LoadFlags operator&(LoadFlags left, LoadFlags right) noexcept
    {
        return static_cast<LoadFlags>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
    }
    [[nodiscard]] constexpr LoadFlags operator^(LoadFlags left, LoadFlags right) noexcept
    {
        return static_cast<LoadFlags>(static_cast<std::uint8_t>(left) ^ static_cast<std::uint8_t>(right));
    }
    [[nodiscard]] constexpr LoadFlags operator~(LoadFlags value) noexcept
    {
        return static_cast<LoadFlags>(static_cast<std::uint8_t>(~static_cast<std::uint8_t>(value)));
    }
    constexpr LoadFlags& operator|=(LoadFlags& left, LoadFlags right) noexcept { return left = left | right; }
    constexpr LoadFlags& operator&=(LoadFlags& left, LoadFlags right) noexcept { return left = left & right; }
    constexpr LoadFlags& operator^=(LoadFlags& left, LoadFlags right) noexcept { return left = left ^ right; }

    class AvailableArray
    {
    public:
        void ClearAll() noexcept;
        void SetAll() noexcept;
        void Set(std::uint16_t value) noexcept;
        void CopyFrom(const AvailableArray& source) noexcept;

        [[nodiscard]] bool& operator[](std::int32_t key);
        [[nodiscard]] const bool& operator[](std::int32_t key) const;
        [[nodiscard]] bool& operator[](MphRead::BeamType key);
        [[nodiscard]] const bool& operator[](MphRead::BeamType key) const;
        void Set(std::int32_t key, bool value);
        void Set(MphRead::BeamType key, bool value);

    private:
        std::array<bool, 9> _array{};
    };

    enum class PlayerAnimation : std::int8_t
    {
        None = -1,
        Morph = 0,
        Flourish = 1,
        WalkForward = 2,
        Unmorph = 3,
        DamageBack = 4,
        DamageFront = 5,
        DamageLeft = 6,
        DamageRight = 7,
        Idle = 8,
        LandNeutral = 9,
        LandLeft = 10,
        LandRight = 11,
        JumpNeutral = 12,
        JumpBack = 13,
        JumpForward = 14,
        JumpLeft = 15,
        JumpRight = 16,
        Unused17 = 17,
        WalkBackward = 18,
        Spawn = 19,
        WalkLeft = 20,
        WalkRight = 21,
        Turn = 22,
        Charge = 23,
        ChargeShoot = 24,
        Shoot = 25
    };

    enum class GunAnimation : std::uint8_t
    {
        FullCharge = 0,
        ChargeShot = 1,
        Charging = 2,
        Idle = 3,
        Switch = 4,
        FullChargeMissile = 5,
        ChargingMissile = 6,
        MissileClose = 7,
        MissileOpen = 8,
        Unknown9 = 9,
        MissileShot = 10,
        UpDown = 11,
        Shot = 12
    };

    enum class TraceAltAnim : std::uint8_t
    {
        Idle = 0, Attack = 1, MoveLeft = 2, MoveForward = 3, MoveRight = 4, MoveBackward = 5
    };
    enum class WeavelAltAnim : std::uint8_t
    {
        Idle = 0, Attack = 1, MoveLeft = 2, MoveForward = 3, MoveRight = 4, Turn = 5, MoveBackward = 6
    };
    enum class KandenAltAnim : std::uint8_t { Idle = 0, TailOut = 1, TailIn = 2 };
    enum class NoxusAltAnim : std::uint8_t { Extend = 0 };
    enum class SyluxAltAnim : std::uint8_t { Idle = 0 };
    enum class SpireAltAnim : std::uint8_t { Attack = 0 };

    enum class DamageFlags : std::int32_t
    {
        None = 0,
        NoDmgInvuln = 1,
        IgnoreInvuln = 2,
        Death = 4,
        Halfturret = 8,
        Headshot = 0x10,
        Deathalt = 0x20,
        Burn = 0x40,
        NoSfx = 0x80,
        FromAlt = 0x100
    };

    [[nodiscard]] constexpr DamageFlags operator|(DamageFlags left, DamageFlags right) noexcept
    {
        return static_cast<DamageFlags>(static_cast<std::int32_t>(left) | static_cast<std::int32_t>(right));
    }
    [[nodiscard]] constexpr DamageFlags operator&(DamageFlags left, DamageFlags right) noexcept
    {
        return static_cast<DamageFlags>(static_cast<std::int32_t>(left) & static_cast<std::int32_t>(right));
    }
    [[nodiscard]] constexpr DamageFlags operator^(DamageFlags left, DamageFlags right) noexcept
    {
        return static_cast<DamageFlags>(static_cast<std::int32_t>(left) ^ static_cast<std::int32_t>(right));
    }
    [[nodiscard]] constexpr DamageFlags operator~(DamageFlags value) noexcept
    {
        return static_cast<DamageFlags>(~static_cast<std::int32_t>(value));
    }
    constexpr DamageFlags& operator|=(DamageFlags& left, DamageFlags right) noexcept { return left = left | right; }
    constexpr DamageFlags& operator&=(DamageFlags& left, DamageFlags right) noexcept { return left = left & right; }
    constexpr DamageFlags& operator^=(DamageFlags& left, DamageFlags right) noexcept { return left = left ^ right; }

    enum class PlayerFlags1 : std::uint32_t
    {
        None = 0,
        Standing = 1,
        StandingPrevious = 2,
        NoUnmorph = 4,
        NoUnmorphPrevious = 8,
        CollidingLateral = 0x10,
        OnAcid = 0x20,
        OnLava = 0x40,
        CollidingEntity = 0x80,
        UsedJump = 0x100,
        AltForm = 0x200,
        AltFormPrevious = 0x400,
        Morphing = 0x800,
        Unmorphing = 0x1000,
        Strafing = 0x2000,
        FreeLook = 0x4000,
        FreeLookPrevious = 0x8000,
        ShotUncharged = 0x10000,
        ShotMissile = 0x20000,
        ShotCharged = 0x40000,
        GunOpenAnimation = 0x80000,
        Grounded = 0x100000,
        GroundedPrevious = 0x200000,
        Walking = 0x400000,
        MovingBiped = 0x800000,
        NoAimInput = 0x1000000,
        WeaponMenuOpen = 0x2000000,
        Boosting = 0x4000000,
        CanTouchBoost = 0x8000000,
        UsedJumpPad = 0x10000000,
        AltDirOverride = 0x20000000,
        DrawGunSmoke = 0x80000000
    };

    [[nodiscard]] constexpr PlayerFlags1 operator|(PlayerFlags1 left, PlayerFlags1 right) noexcept
    {
        return static_cast<PlayerFlags1>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr PlayerFlags1 operator&(PlayerFlags1 left, PlayerFlags1 right) noexcept
    {
        return static_cast<PlayerFlags1>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr PlayerFlags1 operator^(PlayerFlags1 left, PlayerFlags1 right) noexcept
    {
        return static_cast<PlayerFlags1>(static_cast<std::uint32_t>(left) ^ static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr PlayerFlags1 operator~(PlayerFlags1 value) noexcept
    {
        return static_cast<PlayerFlags1>(~static_cast<std::uint32_t>(value));
    }
    constexpr PlayerFlags1& operator|=(PlayerFlags1& left, PlayerFlags1 right) noexcept { return left = left | right; }
    constexpr PlayerFlags1& operator&=(PlayerFlags1& left, PlayerFlags1 right) noexcept { return left = left & right; }
    constexpr PlayerFlags1& operator^=(PlayerFlags1& left, PlayerFlags1 right) noexcept { return left = left ^ right; }

    enum class PlayerFlags2 : std::uint32_t
    {
        None = 0,
        ChargeEffect = 1,
        HideModel = 2,
        Shooting = 4,
        AltAttack = 8,
        BipedStuck = 0x10,
        Halfturret = 0x20,
        Cloaking = 0x40,
        GravityOverride = 0x80,
        NoFormSwitch = 0x100,
        BipedLock = 0x200,
        AltFormGravity = 0x400,
        Lod1 = 0x800,
        DrawnThirdPerson = 0x1000,
        RadarReveal = 0x2000,
        RadarRevealPrevious = 0x4000,
        SpireClimbing = 0x8000,
        NoShotsFired = 0x10000,
        UnequipOmegaCannon = 0x20000,
        Spectating = 0x40000
    };

    [[nodiscard]] constexpr PlayerFlags2 operator|(PlayerFlags2 left, PlayerFlags2 right) noexcept
    {
        return static_cast<PlayerFlags2>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr PlayerFlags2 operator&(PlayerFlags2 left, PlayerFlags2 right) noexcept
    {
        return static_cast<PlayerFlags2>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr PlayerFlags2 operator^(PlayerFlags2 left, PlayerFlags2 right) noexcept
    {
        return static_cast<PlayerFlags2>(static_cast<std::uint32_t>(left) ^ static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr PlayerFlags2 operator~(PlayerFlags2 value) noexcept
    {
        return static_cast<PlayerFlags2>(~static_cast<std::uint32_t>(value));
    }
    constexpr PlayerFlags2& operator|=(PlayerFlags2& left, PlayerFlags2 right) noexcept { return left = left | right; }
    constexpr PlayerFlags2& operator&=(PlayerFlags2& left, PlayerFlags2 right) noexcept { return left = left & right; }
    constexpr PlayerFlags2& operator^=(PlayerFlags2& left, PlayerFlags2 right) noexcept { return left = left ^ right; }

    enum class AbilityFlags : std::int16_t
    {
        None = 0,
        AltForm = 1,
        SpaceJump = 2,
        Bombs = 4,
        Boost = 0x40,
        NoxusAltAttack = 0x100,
        SpireAltAttack = 0x200,
        TraceAltAttack = 0x400,
        WeavelAltAttack = 0x1000
    };

    [[nodiscard]] constexpr AbilityFlags operator|(AbilityFlags left, AbilityFlags right) noexcept
    {
        return static_cast<AbilityFlags>(static_cast<std::int16_t>(left) | static_cast<std::int16_t>(right));
    }
    [[nodiscard]] constexpr AbilityFlags operator&(AbilityFlags left, AbilityFlags right) noexcept
    {
        return static_cast<AbilityFlags>(static_cast<std::int16_t>(left) & static_cast<std::int16_t>(right));
    }
    [[nodiscard]] constexpr AbilityFlags operator^(AbilityFlags left, AbilityFlags right) noexcept
    {
        return static_cast<AbilityFlags>(static_cast<std::int16_t>(left) ^ static_cast<std::int16_t>(right));
    }
    [[nodiscard]] constexpr AbilityFlags operator~(AbilityFlags value) noexcept
    {
        return static_cast<AbilityFlags>(static_cast<std::int16_t>(~static_cast<std::int16_t>(value)));
    }
    constexpr AbilityFlags& operator|=(AbilityFlags& left, AbilityFlags right) noexcept { return left = left | right; }
    constexpr AbilityFlags& operator&=(AbilityFlags& left, AbilityFlags right) noexcept { return left = left & right; }
    constexpr AbilityFlags& operator^=(AbilityFlags& left, AbilityFlags right) noexcept { return left = left ^ right; }

    enum class FhPlayerFlags : std::uint32_t
    {
        None = 0,
        Standing = 1,
        StandingPrevious = 2,
        CollidingLateral = 4,
        UsedJump = 8,
        AltForm = 0x10,
        AltFormPrevious = 0x20,
        FreeStrafe = 0x40,
        LockedOn = 0x80,
        AutoLockOn = 0x100,
        FreeLook = 0x200,
        FreeLookPrevious = 0x400,
        ShotUncharged = 0x800,
        ShotMissile = 0x1000,
        ShotCharged = 0x2000,
        GunOpenAnimation = 0x4000,
        Grounded = 0x8000,
        GroundedPrevious = 0x10000,
        Walking = 0x20000,
        MovingBiped = 0x40000,
        NoAttack = 0x80000,
        Boosting = 0x100000,
        Targeted = 0x200000,
        UsedJumpPad = 0x400000,
        AltDirOverride = 0x800000,
        CenterAltFormCamera = 0x1000000,
        DrawGunSmoke = 0x2000000,
        DrawMuzzleEffect = 0x4000000,
        DrawBallDeath = 0x8000000,
        HideModel = 0x10000000
    };


    [[nodiscard]] constexpr FhPlayerFlags operator|(FhPlayerFlags left, FhPlayerFlags right) noexcept
    {
        return static_cast<FhPlayerFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr FhPlayerFlags operator&(FhPlayerFlags left, FhPlayerFlags right) noexcept
    {
        return static_cast<FhPlayerFlags>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr FhPlayerFlags operator^(FhPlayerFlags left, FhPlayerFlags right) noexcept
    {
        return static_cast<FhPlayerFlags>(static_cast<std::uint32_t>(left) ^ static_cast<std::uint32_t>(right));
    }
    [[nodiscard]] constexpr FhPlayerFlags operator~(FhPlayerFlags value) noexcept
    {
        return static_cast<FhPlayerFlags>(~static_cast<std::uint32_t>(value));
    }
    constexpr FhPlayerFlags& operator|=(FhPlayerFlags& left, FhPlayerFlags right) noexcept { return left = left | right; }
    constexpr FhPlayerFlags& operator&=(FhPlayerFlags& left, FhPlayerFlags right) noexcept { return left = left & right; }
    constexpr FhPlayerFlags& operator^=(FhPlayerFlags& left, FhPlayerFlags right) noexcept { return left = left ^ right; }

    struct PlayerValues final
    {
        const MphRead::Hunter Hunter{};
        const std::int32_t WalkBipedTraction = 0;
        const std::int32_t StrafeBipedTraction = 0;
        const std::int32_t WalkSpeedCap = 0;
        const std::int32_t StrafeSpeedCap = 0;
        const std::int32_t AltMinHSpeed = 0;
        const std::int32_t BoostSpeedCap = 0;
        const std::int32_t BipedGravity = 0;
        const std::int32_t AltAirGravity = 0;
        const std::int32_t AltGroundGravity = 0;
        const std::int32_t JumpSpeed = 0;
        const std::int32_t WalkSpeedFactor = 0;
        const std::int32_t AltGroundSpeedFactor = 0;
        const std::int32_t StrafeSpeedFactor = 0;
        const std::int32_t AirSpeedFactor = 0;
        const std::int32_t StandSpeedFactor = 0;
        const std::int32_t RollAltTraction = 0;
        const std::int32_t AltColRadius = 0;
        const std::int32_t AltColYPos = 0;
        const std::uint16_t BoostChargeMin = 0;
        const std::uint16_t BoostChargeMax = 0;
        const std::int32_t BoostSpeedMin = 0;
        const std::int32_t BoostSpeedMax = 0;
        const std::int32_t AltHSpeedCapIncrement = 0;
        const std::int32_t Field58 = 0;
        const std::int32_t Field5C = 0;
        const std::int32_t WalkBobMax = 0;
        const std::int32_t AimDistance = 0;
        const std::uint16_t CamSwitchTime = 0;
        const std::uint16_t Padding6A = 0;
        const std::int32_t NormalFov = 0;
        const std::int32_t ZoomSensitivityFactor = 0;
        const std::int32_t AimYOffset = 0;
        const std::int32_t Field78 = 0;
        const std::int32_t Field7C = 0;
        const std::int32_t Field80 = 0;
        const std::int32_t Field84 = 0;
        const std::int32_t Field88 = 0;
        const std::int32_t Field8C = 0;
        const std::int32_t Field90 = 0;
        const std::int32_t MinPickupHeight = 0;
        const std::int32_t MaxPickupHeight = 0;
        const std::int32_t BipedColRadius = 0;
        const std::int32_t LockOnTolerance = 0;
        const std::int32_t LockOnMinDistance = 0;
        const std::int32_t LockOnMaxDistance = 0;
        const std::int16_t DamageInvuln = 0;
        const std::uint16_t DamageFlashTime = 0;
        const std::int32_t FieldB0 = 0;
        const std::int32_t FieldB4 = 0;
        const std::int32_t FieldB8 = 0;
        const std::int32_t MuzzleOffset = 0;
        const std::int32_t BombCooldown = 0;
        const std::int32_t BombSelfRadius = 0;
        const std::int32_t BombSelfRadiusSquared = 0;
        const std::int32_t BombRadius = 0;
        const std::int32_t BombRadiusSquared = 0;
        const std::int32_t BombJumpSpeed = 0;
        const std::int32_t BombRefillTime = 0;
        const std::int16_t BombDamage = 0;
        const std::int16_t BombEnemyDamage = 0;
        const std::int16_t LockOnSnapTime = 0;
        const std::int16_t SpawnInvulnerability = 0;
        const std::uint16_t AimMinTouchTime = 0;
        const std::uint16_t PaddingE6 = 0;
        const std::int32_t AutoAimFindTolerance = 0;
        const std::int32_t AutoAimHoldTolerance = 0;
        const std::int32_t SwayStartTime = 0;
        const std::int32_t SwayIncrement = 0;
        const std::int32_t SwayLimit = 0;
        const std::int32_t GunIdleTime = 0;
        const std::int16_t MpAmmoCap = 0;
        const std::uint8_t AmmoRecharge = 0;
        const std::uint8_t Padding103 = 0;
        const std::uint16_t EnergyTank = 0;
        const std::int16_t AmmoTank = 0;
        const std::uint8_t AltFormStrafe = 0;
        const std::uint8_t Padding109 = 0;
        const std::uint16_t Padding10A = 0;
        const std::int32_t FallDamageSpeed = 0;
        const std::int32_t FallDamageMax = 0;
        const std::int32_t ViewTiltIncrement = 0;
        const std::int32_t ViewTiltFactor = 0;
        const std::int32_t JumpPadSlideFactor = 0;
        const std::int32_t AltTiltAngleCap = 0;
        const std::int32_t AltMinWobble = 0;
        const std::int32_t AltMaxWobble = 0;
        const std::int32_t AltMinSpinAccel = 0;
        const std::int32_t AltMaxSpinAccel = 0;
        const std::int32_t AltMinSpinSpeed = 0;
        const std::int32_t AltMaxSpinSpeed = 0;
        const std::int32_t AltTiltAngleMax = 0;
        const std::int32_t AltBounceWobble = 0;
        const std::int32_t AltBounceTilt = 0;
        const std::int32_t AltBounceSpin = 0;
        const std::int32_t AltAttackKnockbackAccel = 0;
        const std::int16_t AltAttackKnockbackTime = 0;
        const std::uint16_t AltAttackStartup = 0;
        const std::int32_t Field154 = 0;
        const std::int32_t Field158 = 0;
        const std::int32_t LungeHSpeed = 0;
        const std::int32_t LungeVSpeed = 0;
        const std::uint16_t AltAttackDamage = 0;
        const std::int16_t AltAttackCooldown = 0;

        PlayerValues() noexcept = default;
        PlayerValues(MphRead::Hunter hunter, std::int32_t walkBipedTraction,
            std::int32_t strafeBipedTraction, std::int32_t walkSpeedCap,
            std::int32_t strafeSpeedCap, std::int32_t altMinHSpeed,
            std::int32_t boostSpeedCap, std::int32_t bipedGravity,
            std::int32_t altAirGravity, std::int32_t altGroundGravity,
            std::int32_t jumpSpeed, std::int32_t walkSpeedFactor,
            std::int32_t altGroundSpeedFactor, std::int32_t strafeSpeedFactor,
            std::int32_t airSpeedFactor, std::int32_t standSpeedFactor,
            std::int32_t rollAltTraction, std::int32_t altColRadius,
            std::int32_t altColYPos, std::uint16_t boostChargeMin,
            std::uint16_t boostChargeMax, std::int32_t boostSpeedMin,
            std::int32_t boostSpeedMax, std::int32_t altHSpeedCapIncrement,
            std::int32_t field58, std::int32_t field5C,
            std::int32_t walkBobMax, std::int32_t aimDistance,
            std::uint16_t camSwitchTime, std::uint16_t padding6A,
            std::int32_t normalFov, std::int32_t zoomSensitivityFactor,
            std::int32_t aimYOffset, std::int32_t field78,
            std::int32_t field7C, std::int32_t field80,
            std::int32_t field84, std::int32_t field88,
            std::int32_t field8C, std::int32_t field90,
            std::int32_t minPickupHeight, std::int32_t maxPickupHeight,
            std::int32_t bipedColRadius, std::int32_t lockOnTolerance,
            std::int32_t lockOnMinDistance, std::int32_t lockOnMaxDistance,
            std::int16_t damageInvuln, std::uint16_t damageFlashTime,
            std::int32_t fieldB0, std::int32_t fieldB4,
            std::int32_t fieldB8, std::int32_t muzzleOffset,
            std::int32_t bombCooldown, std::int32_t bombSelfRadius,
            std::int32_t bombSelfRadiusSquared, std::int32_t bombRadius,
            std::int32_t bombRadiusSquared, std::int32_t bombJumpSpeed,
            std::int32_t bombRefillTime, std::int16_t bombDamage,
            std::int16_t bombEnemyDamage, std::int16_t lockOnSnapTime,
            std::int16_t spawnInvulnerability, std::uint16_t aimMinTouchTime,
            std::uint16_t paddingE6, std::int32_t autoAimFindTolerance,
            std::int32_t autoAimHoldTolerance, std::int32_t swayStartTime,
            std::int32_t swayIncrement, std::int32_t swayLimit,
            std::int32_t gunIdleTime, std::int16_t mpAmmoCap,
            std::uint8_t ammoRecharge, std::uint8_t padding103,
            std::uint16_t energyTank, std::int16_t ammoTank,
            std::uint8_t altFormStrafe, std::uint8_t padding109,
            std::uint16_t padding10A, std::int32_t fallDamageSpeed,
            std::int32_t fallDamageMax, std::int32_t viewTiltIncrement,
            std::int32_t viewTiltFactor, std::int32_t jumpPadSlideFactor,
            std::int32_t altTiltAngleCap, std::int32_t altMinWobble,
            std::int32_t altMaxWobble, std::int32_t altMinSpinAccel,
            std::int32_t altMaxSpinAccel, std::int32_t altMinSpinSpeed,
            std::int32_t altMaxSpinSpeed, std::int32_t altTiltAngleMax,
            std::int32_t altBounceWobble, std::int32_t altBounceTilt,
            std::int32_t altBounceSpin, std::int32_t altAttackKnockbackAccel,
            std::int16_t altAttackKnockbackTime, std::uint16_t altAttackStartup,
            std::int32_t field154, std::int32_t field158,
            std::int32_t lungeHSpeed, std::int32_t lungeVSpeed,
            std::uint16_t altAttackDamage, std::int16_t altAttackCooldown) noexcept;
    };

    class PlayerEntity : public DynamicLightEntityBase
    {
        // NetCombatCheck reads private state the way the C# reads it by reflection.
        friend class ::MphRead::Mods::Diagnostics::WeavelAltFormCheck;
        friend class ::MphRead::Mods::Diagnostics::AimCheck;
        friend class HalfturretEntity;
        friend class ::MphRead::Mods::Network::NetCombatCheck;
        friend class ::MphRead::Mods::Network::DialancheCombatCheck;
        friend class ::MphRead::Mods::Network::SpireAltPoseCheck;
        friend class ::MphRead::Mods::Input::PointerCheck;

    public:
        static constexpr std::int32_t SlotCapacity = 8;
        [[nodiscard]] static constexpr std::uint16_t RespawnTime() noexcept { return 90 * 2; }

        PlayerEntity(const PlayerEntity&) = delete;
        PlayerEntity& operator=(const PlayerEntity&) = delete;
        PlayerEntity(PlayerEntity&&) = delete;
        PlayerEntity& operator=(PlayerEntity&&) = delete;
        ~PlayerEntity() override = default;

        [[nodiscard]] std::shared_ptr<MphRead::ModelInstance> BipedModel2() const noexcept { return _bipedModel2; }
        [[nodiscard]] std::shared_ptr<MphRead::ModelInstance> DoubleDamageModel() const noexcept { return _doubleDmgModel; }
        [[nodiscard]] std::int32_t DoubleDmgBindingId() const noexcept { return _doubleDmgBindingId; }
        [[nodiscard]] const std::array<::OpenTK::Mathematics::Vector3, 5>& KandenSegPos() const noexcept { return _kandenSegPos; }
        [[nodiscard]] std::uint8_t SyluxBombCount() const noexcept { return _syluxBombCount; }
        void SetSyluxBombCount(std::uint8_t value) noexcept { _syluxBombCount = value; }
        [[nodiscard]] std::array<std::shared_ptr<BombEntity>, 3>& SyluxBombs() noexcept { return _syluxBombs; }
        [[nodiscard]] const std::array<std::shared_ptr<BombEntity>, 3>& SyluxBombs() const noexcept { return _syluxBombs; }

        [[nodiscard]] static std::int32_t MainPlayerIndex() noexcept { return _mainPlayerIndex; }
        static void SetMainPlayerIndex(std::int32_t value) noexcept { _mainPlayerIndex = value; }
        [[nodiscard]] static std::int32_t PlayerCount() noexcept { return _playerCount; }
        static void SetPlayerCount(std::int32_t value) noexcept { _playerCount = value; }
        [[nodiscard]] static std::int32_t MaxPlayers() noexcept { return _maxPlayers; }
        static void SetMaxPlayers(std::int32_t value) noexcept { _maxPlayers = value; }
        [[nodiscard]] static std::int32_t PlayersCreated() noexcept { return _playersCreated; }
        static void SetPlayersCreated(std::int32_t value) noexcept { _playersCreated = value; }
        [[nodiscard]] static std::shared_ptr<PlayerEntity> Main();
        [[nodiscard]] static const std::array<std::shared_ptr<PlayerEntity>, SlotCapacity>& Players() noexcept { return _players; }
        [[nodiscard]] bool IsMainPlayer() const;

        [[nodiscard]] std::int32_t Health() const noexcept { return _health; }
        void SetHealth(std::int32_t value) noexcept { _health = value; }
        [[nodiscard]] std::int32_t HealthMax() const noexcept { return _healthMax; }
        [[nodiscard]] AvailableArray& AvailableWeapons() noexcept { return _availableWeapons; }
        [[nodiscard]] const AvailableArray& AvailableWeapons() const noexcept { return _availableWeapons; }
        [[nodiscard]] std::shared_ptr<MphRead::EquipInfo> EquipInfo() const noexcept { return _equipInfo; }
        [[nodiscard]] MphRead::BeamType CurrentWeapon() const noexcept { return _currentWeapon; }
        [[nodiscard]] MphRead::BeamType PreviousWeapon() const noexcept { return _previousWeapon; }
        [[nodiscard]] MphRead::BeamType WeaponSelection() const noexcept { return _weaponSelection; }
        [[nodiscard]] Entities::GunAnimation GunAnimation() const noexcept { return _gunAnimation; }

        [[nodiscard]] MphRead::Team Team() const noexcept { return _team; }
        void SetTeam(MphRead::Team value) noexcept { _team = value; }
        [[nodiscard]] std::int32_t TeamIndex() const noexcept { return _teamIndex; }
        void SetTeamIndex(std::int32_t value) noexcept { _teamIndex = value; }
        [[nodiscard]] std::int32_t SlotIndex() const noexcept { return _slotIndex; }
        [[nodiscard]] bool IsBot() const noexcept { return _isBot; }
        void SetIsBot(bool value) noexcept { _isBot = value; }
        [[nodiscard]] Entities::LoadFlags LoadFlags() const noexcept { return _loadFlags; }
        void SetLoadFlags(Entities::LoadFlags value) noexcept { _loadFlags = value; }
        [[nodiscard]] MphRead::Hunter Hunter() const noexcept { return _hunter; }
        [[nodiscard]] const PlayerValues& Values() const noexcept { return _values; }
        [[nodiscard]] bool IsPrimeHunter() const;

        [[nodiscard]] const MphRead::CollisionVolume& Volume() const noexcept { return _volume; }
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 FacingVector() const override { return _facingVector; }
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 UpVector() const override { return _upVector; }
        [[nodiscard]] float Field70() const noexcept { return _field70; }
        [[nodiscard]] float Field74() const noexcept { return _field74; }
        [[nodiscard]] bool Field6D0() const noexcept { return _field6D0; }

        [[nodiscard]] std::shared_ptr<HalfturretEntity> Halfturret() const noexcept { return _halfturret; }
        // A hit shared with the turret: the turret's part of `damage` (the
        // half whichever of the two has more health takes), and taking that
        // part off the turret, destroying it if it is all it had.
        [[nodiscard]] std::uint32_t HalfturretShare(std::uint32_t damage) const;
        void DamageHalfturret(std::uint32_t damage);
        // A life-draining hit's heal: `health` more, shared with a Weavel's
        // turret, never past the maximum, and nothing for the dead.
        void GainDrainedHealth(std::uint32_t health);
        [[nodiscard]] std::shared_ptr<EnemySpawnEntity> EnemySpawner() const noexcept { return _enemySpawner; }
        void SetEnemySpawner(std::shared_ptr<EnemySpawnEntity> value) noexcept { _enemySpawner = std::move(value); }
        [[nodiscard]] std::shared_ptr<EnemyInstanceEntity> AttachedEnemy() const noexcept { return _attachedEnemy; }
        void SetAttachedEnemy(std::shared_ptr<EnemyInstanceEntity> value) noexcept { _attachedEnemy = std::move(value); }
        [[nodiscard]] std::shared_ptr<MorphCameraEntity> MorphCamera() const noexcept { return _morphCamera; }
        void SetMorphCamera(std::shared_ptr<MorphCameraEntity> value) noexcept { _morphCamera = std::move(value); }
        [[nodiscard]] std::shared_ptr<OctolithFlagEntity> OctolithFlag() const noexcept { return _octolithFlag; }
        void SetOctolithFlag(std::shared_ptr<OctolithFlagEntity> value) noexcept { _octolithFlag = std::move(value); }
        [[nodiscard]] std::shared_ptr<EntityBase> BurnedBy() const noexcept { return _burnedBy; }
        [[nodiscard]] std::shared_ptr<EntityBase> ShockCoilTarget() const noexcept { return _shockCoilTarget; }

        [[nodiscard]] bool IsAltForm() const noexcept;
        // Whether a hit taken now would be swallowed by spawn or damage
        // invulnerability (TakeDamage's own gate; diagnostics only).
        [[nodiscard]] bool ModInvulnerable() const noexcept { return _spawnInvulnTimer > 0 || _damageInvulnTimer > 0; }
        [[nodiscard]] bool IsMorphing() const noexcept;
        [[nodiscard]] bool IsUnmorphing() const noexcept;
        [[nodiscard]] PlayerFlags1 Flags1() const noexcept { return _flags1; }
        [[nodiscard]] PlayerFlags2 Flags2() const noexcept { return _flags2; }
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Speed() const noexcept { return _speed; }
        void SetSpeed(::OpenTK::Mathematics::Vector3 value) noexcept { _speed = value; }
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Acceleration() const noexcept { return _acceleration; }
        void SetAcceleration(::OpenTK::Mathematics::Vector3 value) noexcept { _acceleration = value; }
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 PrevSpeed() const noexcept { return _prevSpeed; }
        void SetPrevSpeed(::OpenTK::Mathematics::Vector3 value) noexcept { _prevSpeed = value; }
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 PrevPosition() const noexcept { return _prevPosition; }
        void SetPrevPosition(::OpenTK::Mathematics::Vector3 value) noexcept { _prevPosition = value; }
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 IdlePosition() const noexcept { return _idlePosition; }
        [[nodiscard]] std::uint16_t TimeSinceShot() const noexcept { return _timeSinceShot; }
        void SetTimeSinceShot(std::uint16_t value) noexcept
        {
            _timeSinceShot = value;
            _nativeTimeSinceShot = static_cast<std::uint8_t>(value / 2U > 255U ? 255U : value / 2U);
        }
        [[nodiscard]] std::uint8_t NativeTimeSinceShot() const noexcept { return _nativeTimeSinceShot; }
        [[nodiscard]] std::uint16_t RespawnTimer() const noexcept { return _respawnTimer; }
        void SetRespawnTimer(std::uint16_t value) noexcept { _respawnTimer = value; }
        [[nodiscard]] float DeathCountdown() const noexcept { return _deathCountdown; }
        [[nodiscard]] bool DoubleDamage() const noexcept { return _doubleDmgTimer > 0; }
        [[nodiscard]] bool DeathaltRunning() const noexcept { return _deathaltTimer > 0; }
        [[nodiscard]] std::uint16_t ShockCoilTimer() const noexcept { return _shockCoilTimer; }
        [[nodiscard]] float CurAlpha() const noexcept { return _curAlpha; }
        [[nodiscard]] bool IgnoreItemPickups() const noexcept { return _ignoreItemPickups; }
        void SetIgnoreItemPickups(bool value) noexcept { _ignoreItemPickups = value; }
        [[nodiscard]] std::optional<::OpenTK::Mathematics::Vector3> ForcedSpawnPos() const noexcept { return _forcedSpawnPos; }
        void SetForcedSpawnPos(std::optional<::OpenTK::Mathematics::Vector3> value) noexcept { _forcedSpawnPos = value; }
        [[nodiscard]] bool ReloadInit() const noexcept { return _reloadInit; }
        void SetReloadInit(bool value) noexcept { _reloadInit = value; }

        static void Construct(MphRead::Scene* scene);
        static void Reset();
        // A discarded player lets go of every entity and effect it points at.
        // The halfturret, the beams, the AI and the last attacker all point
        // back, which the C# garbage collector never minded and shared_ptr
        // cannot see through: without this every player of every room stays
        // alive with its models, a room's worth per match or map change.
        void ReleaseReferences() noexcept;
        [[nodiscard]] static std::shared_ptr<PlayerEntity> Create(MphRead::Hunter hunter, std::int32_t recolor);
        void CreateHalfturret();
        void Initialize() override;
        void Spawn(::OpenTK::Mathematics::Vector3 pos,
            ::OpenTK::Mathematics::Vector3 facing,
            ::OpenTK::Mathematics::Vector3 up,
            MphRead::Formats::Culling::NodeRef nodeRef, bool respawn);
        void InitEnemyHunter();
        void SaveStatus();
        void ResetReferences();
        void GetPosition(::OpenTK::Mathematics::Vector3& position) override;
        void GetVectors(::OpenTK::Mathematics::Vector3& position,
            ::OpenTK::Mathematics::Vector3& up,
            ::OpenTK::Mathematics::Vector3& facing) override;
        [[nodiscard]] bool GetTargetable() override;
        [[nodiscard]] std::int32_t GetScanId(bool alternate = false) override;
        [[nodiscard]] bool ScanVisible() override;
        void Teleport(::OpenTK::Mathematics::Vector3 position,
            ::OpenTK::Mathematics::Vector3 facing,
            MphRead::Formats::Culling::NodeRef nodeRef);
        void Reposition(::OpenTK::Mathematics::Vector3 position,
            ::OpenTK::Mathematics::Vector3 facing,
            MphRead::Formats::Culling::NodeRef nodeRef);
        void Reposition(::OpenTK::Mathematics::Vector3 offset,
            MphRead::Formats::Culling::NodeRef nodeRef);
        void BlockFormSwitch();
        void SetBipedStuck(bool stuck);
        [[nodiscard]] bool CheckHitByBomb(BombEntity* bomb, bool halfturret);
        void OnHalfturretDied();
        void UpdateZoom(bool zoom);
        void TakeDamage(std::int32_t damage, DamageFlags flags,
            std::optional<::OpenTK::Mathematics::Vector3> direction,
            EntityBase* source);
        void TakeDamage(std::uint32_t damage, DamageFlags flags,
            std::optional<::OpenTK::Mathematics::Vector3> direction,
            EntityBase* source);

        static void LoadWeaponNames();
        [[nodiscard]] static const std::array<std::optional<std::string>, 8>& AltAttackNames() noexcept { return _altAttackNames; }
        [[nodiscard]] static const std::array<std::optional<std::string>, 8>& HunterNames() noexcept { return _hunterNames; }
        [[nodiscard]] static const std::array<std::optional<std::string>, 9>& WeaponNames() noexcept { return _weaponNames; }
        static void GeneratePlayerVolumes();
        static void GenerateKandenAltNodeDistances();

        // C# public readonly fields: references are fixed but elements remain mutable.
        std::array<Effectiveness, 9> BeamEffectiveness{};
        inline static std::array<std::shared_ptr<PlayerEntity>, SlotCapacity> _players{};
        inline static std::array<std::array<MphRead::CollisionVolume, 3>, 8> PlayerVolumes{};
        inline static std::array<float, 4> KandenAltNodeDistances{};

        MPHREAD_PLAYER_AI_MEMBERS
        MPHREAD_PLAYER_ENTITY_ICON_BOUNDS_MEMBERS
        MPHREAD_PLAYER_CAMERA_MEMBERS
        MPHREAD_PLAYER_COLLISION_MEMBERS
        MPHREAD_PLAYER_DIALOG_MEMBERS
        MPHREAD_PLAYER_DRAW_MEMBERS
        MPHREAD_PLAYER_HUD_MEMBERS
        MPHREAD_PLAYER_INPUT_MEMBERS
        MPHREAD_PLAYER_PAUSE_MEMBERS
        MPHREAD_PLAYER_PROCESS_MEMBERS
        MPHREAD_PLAYER_SCAN_MEMBERS
        MPHREAD_PLAYER_SOUND_MEMBERS
        MPHREAD_PLAYER_ENTITY_CHAT_HUD_MEMBERS
        MPHREAD_PLAYER_ENTITY_NET_AIM_MEMBERS
        MPHREAD_PLAYER_ENTITY_NET_HUD_MEMBERS
        MPHREAD_PLAYER_ENTITY_AMMO_CLEAR_MEMBERS
        MPHREAD_PLAYER_ENTITY_END_SCREEN_MEMBERS
        MPHREAD_PLAYER_STYLUS_HUD_MEMBERS
        MPHREAD_PLAYER_TEAM_SCOREBOARD_MEMBERS
        MPHREAD_PLAYER_MAP_PICK_MEMBERS
        MPHREAD_PLAYER_VOTE_HUD_MEMBERS
        MPHREAD_PLAYER_ENTITY_AIM_ASSIST_MEMBERS
        MPHREAD_PLAYER_ENTITY_HAPTICS_MEMBERS
        MPHREAD_PLAYER_GOLDEN_CAPTURE_MEMBERS

    private:
        explicit PlayerEntity(std::int32_t slotIndex, MphRead::Scene* scene);
        void AdvanceNativeWeaponTimers();

        [[nodiscard]] MphRead::WeaponInfo& EquipWeapon() const;
        void SetFlags1(PlayerFlags1 value) noexcept { _flags1 = value; }
        void SetFlags2(PlayerFlags2 value) noexcept { _flags2 = value; }
        void SetCurrentWeapon(MphRead::BeamType value) noexcept { _currentWeapon = value; }
        void SetPreviousWeapon(MphRead::BeamType value) noexcept { _previousWeapon = value; }
        void SetWeaponSelection(MphRead::BeamType value) noexcept { _weaponSelection = value; }
        void SetGunAnimationValue(Entities::GunAnimation value) noexcept { _gunAnimation = value; }
        void SetIdlePosition(::OpenTK::Mathematics::Vector3 value) noexcept { _idlePosition = value; }
        [[nodiscard]] PlayerAnimation Biped1Anim() const;
        [[nodiscard]] PlayerAnimation Biped2Anim() const;
        [[nodiscard]] std::int32_t Biped1Frame() const;
        [[nodiscard]] std::int32_t Biped2Frame() const;
        [[nodiscard]] std::int32_t Biped1FrameCount() const;
        [[nodiscard]] std::int32_t Biped2FrameCount() const;
        [[nodiscard]] MphRead::AnimFlags Biped1Flags() const;
        void SetBiped1Flags(MphRead::AnimFlags value);
        [[nodiscard]] MphRead::AnimFlags Biped2Flags() const;
        void SetBiped2Flags(MphRead::AnimFlags value);
        void UpdateScanIds();
        void SetBiped1Animation(PlayerAnimation anim, MphRead::AnimFlags animFlags);
        void SetBiped2Animation(PlayerAnimation anim, MphRead::AnimFlags animFlags);
        void SetBipedAnimation(PlayerAnimation anim, MphRead::AnimFlags animFlags,
            bool setBiped1 = true, bool setBiped2 = true, bool setIfMorphing = true);
        void ResetMorphBallTrail();
        void UpdateMorphBallTrail();
        void InitializeWeapon();
        // caller: only the -debuglog [weapon] line reads it.
        [[nodiscard]] bool TryEquipWeapon(MphRead::BeamType beam, bool silent = false, bool debug = false,
            std::source_location caller = std::source_location::current());
        void ShowNoAmmoMessage();
        void UpdateAffinityWeaponSlot(MphRead::BeamType beam, std::int32_t slot = 2);
        void UnequipOmegaCannon();
        void SetGunAnimation(Entities::GunAnimation anim, MphRead::AnimFlags animFlags = MphRead::AnimFlags::None);
        void UpdateGunAnimation();

        static constexpr std::int32_t UA = 0;
        static constexpr std::int32_t Missiles = 1;
        static constexpr std::int32_t _mbTrailSegments = 9 * 2;

        std::array<std::shared_ptr<MphRead::ModelInstance>, 2> _bipedModelLods{};
        std::shared_ptr<MphRead::ModelInstance> _bipedModel1{};
        std::shared_ptr<MphRead::ModelInstance> _bipedModel2{};
        std::shared_ptr<MphRead::ModelInstance> _altModel{};
        std::shared_ptr<MphRead::ModelInstance> _gunModel{};
        std::shared_ptr<MphRead::ModelInstance> _gunSmokeModel{};
        std::shared_ptr<MphRead::ModelInstance> _doubleDmgModel{};
        std::shared_ptr<MphRead::ModelInstance> _altIceModel{};
        std::shared_ptr<MphRead::ModelInstance> _bipedIceModel{};
        std::shared_ptr<MphRead::ModelInstance> _trailModel{};
        std::shared_ptr<MphRead::ModelInstance> _octolithSimpleModel{};
        std::array<std::shared_ptr<MphRead::Node>, 2> _spineNodes{};
        std::array<std::shared_ptr<MphRead::Node>, 2> _shootNodes{};
        std::int32_t _trailBindingId1 = 0;
        std::int32_t _trailBindingId2 = 0;
        std::int32_t _doubleDmgBindingId = 0;
        std::array<::OpenTK::Mathematics::Matrix4, 19> _bipedIceTransforms{};
        std::array<std::shared_ptr<MphRead::Node>, 4> _spireAltNodes{};
        ::OpenTK::Mathematics::Vector3 _spireRockPosL{};
        ::OpenTK::Mathematics::Vector3 _spireRockPosR{};
        DialancheNativeCollision _dialancheNativeCollision{};
        ::OpenTK::Mathematics::Vector3 _spireAltFacing{};
        ::OpenTK::Mathematics::Vector3 _spireAltUp{};
        std::array<::OpenTK::Mathematics::Vector3, 16> _spireAltVecs{};
        std::array<::OpenTK::Mathematics::Vector3, 5> _kandenSegPos{};
        std::array<::OpenTK::Mathematics::Matrix4, 5> _kandenSegMtx{};
        std::uint8_t _syluxBombCount = 0;
        std::array<std::shared_ptr<BombEntity>, 3> _syluxBombs{};

        std::int32_t _altScanId = 0;
        std::int32_t _healthMax = 0;
        std::int32_t _health = 0;
        std::int32_t _healthRecovery = 0;
        bool _tickedHealthRecovery = false;
        std::array<std::int32_t, 2> _ammoMax{};
        std::array<std::int32_t, 2> _ammo{};
        std::array<std::int32_t, 2> _ammoRecovery{};
        std::array<bool, 2> _tickedAmmoRecovery{};
        std::array<MphRead::BeamType, 3> _weaponSlots{};
        AvailableArray _availableWeapons{};
        AvailableArray _availableCharges{};
        AbilityFlags _abilities{};
        std::shared_ptr<MphRead::BeamProjectileArray> _beams{};
        std::shared_ptr<MphRead::EquipInfo> _equipInfo{};
        MphRead::BeamType _currentWeapon{};
        MphRead::BeamType _previousWeapon{};
        MphRead::BeamType _weaponSelection{};
        Entities::GunAnimation _gunAnimation{};
        std::uint16_t _bombCooldown = 0;
        std::uint16_t _bombRefillTimer = 0;
        std::uint8_t _bombAmmo = 0;
        std::uint16_t _bombOveruse = 0;
        std::uint16_t _boostCharge = 0;
        std::uint16_t _boostDamage = 0;
        std::uint16_t _altAttackCooldown = 0;
        std::uint16_t _altAttackTime = 0;
        float _altSpinSpeed = 0.0F;
        std::int32_t _missileSfxHandle = -1;
        float _walkSfxTimer = 0.0F;
        std::int32_t _walkSfxIndex = 0;
        float _burnSfxAmount = 0.0F;
        float _moveSfxAmount = 0.0F;

        MphRead::Team _team = MphRead::Team::None;
        std::int32_t _teamIndex = -1;
        std::int32_t _slotIndex = 0;
        bool _isBot = false;
        Entities::LoadFlags _loadFlags = Entities::LoadFlags::None;
        MphRead::Hunter _hunter{};
        PlayerValues _values{};

        ::OpenTK::Mathematics::Matrix4 _modelTransform{
            ::OpenTK::Mathematics::Vector4(1.0F, 0.0F, 0.0F, 0.0F),
            ::OpenTK::Mathematics::Vector4(0.0F, 1.0F, 0.0F, 0.0F),
            ::OpenTK::Mathematics::Vector4(0.0F, 0.0F, 1.0F, 0.0F),
            ::OpenTK::Mathematics::Vector4(0.0F, 0.0F, 0.0F, 1.0F)};
        MphRead::CollisionVolume _volumeUnxf{};
        MphRead::CollisionVolume _volume{};
        ::OpenTK::Mathematics::Vector3 _facingVector{};
        ::OpenTK::Mathematics::Vector3 _upVector{};
        ::OpenTK::Mathematics::Vector3 _gunVec1{};
        ::OpenTK::Mathematics::Vector3 _gunVec2{};
        ::OpenTK::Mathematics::Vector3 _aimPosition{};
        float _gunViewBob = 0.0F;
        float _walkViewBob = 0.0F;
        ::OpenTK::Mathematics::Vector3 _muzzlePos{};
        ::OpenTK::Mathematics::Vector3 _gunDrawPos{};
        ::OpenTK::Mathematics::Vector3 _aimVec{};
        float _field70 = 0.0F;
        float _field74 = 0.0F;
        float _field78 = 0.0F;
        float _field7C = 0.0F;
        float _field80 = 0.0F;
        float _field84 = 0.0F;
        float _aimY = 0.0F;
        float _field40C = 0.0F;
        ::OpenTK::Mathematics::Vector3 _field410{};
        ::OpenTK::Mathematics::Vector3 _field41C{};
        ::OpenTK::Mathematics::Vector3 _field428{};
        std::uint16_t _timeIdle = 0;
        std::uint8_t _crushBits = 0;
        ::OpenTK::Mathematics::Vector3 _field4E8{};
        float _altTiltX = 0.0F;
        float _altTiltZ = 0.0F;
        float _altSpinRot = 0.0F;
        float _altWobble = 0.0F;
        std::uint8_t _field551 = 0;
        std::uint8_t _field552 = 0;
        std::uint8_t _field553 = 0;
        float _viewTiltAngleH = 0.0F;
        float _viewTiltAngleV = 0.0F;
        bool _field6D0 = false;
        float _altRollFbX = 0.0F;
        float _altRollFbZ = 0.0F;
        float _altRollLrX = 0.0F;
        float _altRollLrZ = 0.0F;

        std::shared_ptr<HalfturretEntity> _halfturret{};
        std::shared_ptr<EnemySpawnEntity> _enemySpawner{};
        std::shared_ptr<EnemyInstanceEntity> _attachedEnemy{};
        std::shared_ptr<EntityBase> _field35C{};
        std::shared_ptr<MorphCameraEntity> _morphCamera{};
        std::shared_ptr<OctolithFlagEntity> _octolithFlag{};
        std::shared_ptr<JumpPadEntity> _lastJumpPad{};
        std::shared_ptr<EntityBase> _burnedBy{};
        // The launch frame of the shot that set this player burning: a burn
        // tick is that shot's damage, however long after it lands.
        std::uint32_t _burnLaunchFrame = 0;
        std::shared_ptr<EntityBase> _lastTarget{};
        std::shared_ptr<EntityBase> _shockCoilTarget{};

        PlayerFlags1 _flags1 = PlayerFlags1::None;
        PlayerFlags2 _flags2 = PlayerFlags2::None;
        ::OpenTK::Mathematics::Vector3 _speed{};
        ::OpenTK::Mathematics::Vector3 _acceleration{};
        ::OpenTK::Mathematics::Vector3 _prevSpeed{};
        ::OpenTK::Mathematics::Vector3 _prevPosition{};
        ::OpenTK::Mathematics::Vector3 _idlePosition{};
        std::uint16_t _accelerationTimer = 0;
        float _hSpeedCap = 0.0F;
        float _hSpeedMag = 0.0F;
        float _gravity = 0.0F;
        std::int32_t _slipperiness = 0;
        MphRead::Terrain _standTerrain{};
        bool _terrainDamage = false;
        std::uint16_t _jumpPadControlLock = 0;
        std::uint16_t _jumpPadControlLockMin = 0;
        std::uint16_t _timeSinceJumpPad = 0;
        ::OpenTK::Mathematics::Vector3 _jumpPadAccel{};
        std::uint16_t _autofireCooldown = 0;
        std::uint16_t _powerBeamAutofire = 0;
        std::uint16_t _timeSinceInput = 0;
        std::uint16_t _timeSinceShot = 0;
        std::uint8_t _nativeTimeSinceShot = 0;
        std::uint64_t _nativeWeaponTimerFrame = std::numeric_limits<std::uint64_t>::max();
        WeavelLungeInput _weavelLungeInput{};
        bool _weavelNativeAttackPress = false;
        bool _weavelAltLife = false;
        WeavelReplicaTransition _weavelReplicaTransition{};
        WeavelOwnedTurret _weavelOwnedTurret{};
        std::uint16_t _timeSinceDamage = 0;
        std::uint16_t _timeSincePickup = 0;
        std::uint16_t _timeSinceHeal = 0;
        std::uint16_t _respawnTimer = 0;
        float _deathCountdown = 0.0F;
        bool _deathProcessed = false;
        bool _deathLostOctolithSfxPlayed = false;
        bool _deathLostOctolithDialogShown = false;
        std::int32_t _lostOctolithEnemyIndex = -1;
        ::OpenTK::Mathematics::Vector3 _lostOctolithDrawPos{};
        float _lostOctolithSpeed = 0.0F;
        std::uint16_t _damageInvulnTimer = 0;
        std::uint16_t _spawnInvulnTimer = 0;
        std::uint16_t _camSwitchTimer = 0;
        std::uint16_t _doubleDmgTimer = 0;
        std::uint16_t _cloakTimer = 0;
        std::uint16_t _deathaltTimer = 0;
        std::uint16_t _frozenTimer = 0;
        std::uint16_t _frozenGfxTimer = 0;
        bool _drawIceLayer = false;
        std::uint16_t _disruptedTimer = 0;
        std::uint16_t _burnTimer = 0;
        std::uint16_t _timeSinceFrozen = 0;
        std::uint16_t _timeSinceDead = 0;
        std::uint16_t _hidingTimer = 0;
        std::uint16_t _timeStanding = 0;
        std::uint16_t _timeSinceStanding = 0;
        std::uint16_t _timeSinceGrounded = 0;
        std::uint16_t _timeBeforeLanding = 0;
        ::OpenTK::Mathematics::Vector3 _fieldC0{};
        std::uint16_t _field449 = 0;
        float _field44C = 0.0F;
        std::uint16_t _timeSinceHitTarget = 0;
        std::uint16_t _shockCoilTimer = 0;
        std::uint16_t _timeSinceMorphCamera = 0;
        std::uint16_t _horizColTimer = 0;

        std::shared_ptr<MphRead::Effects::EffectEntry> _deathaltEffect{};
        std::shared_ptr<MphRead::Effects::EffectEntry> _doubleDmgEffect{};
        std::shared_ptr<MphRead::Effects::EffectEntry> _burnEffect{};
        std::shared_ptr<MphRead::Effects::EffectEntry> _furlEffect{};
        std::shared_ptr<MphRead::Effects::EffectEntry> _boostEffect{};
        std::shared_ptr<MphRead::Effects::EffectEntry> _muzzleEffect{};
        std::shared_ptr<MphRead::Effects::EffectEntry> _chargeEffect{};
        float _curAlpha = 1.0F;
        float _targetAlpha = 1.0F;
        float _smokeAlpha = 0.0F;
        bool _ignoreItemPickups = false;
        std::optional<::OpenTK::Mathematics::Vector3> _forcedSpawnPos{};
        bool _reloadInit = false;

        inline static std::int32_t _mainPlayerIndex = 0;
        inline static std::int32_t _playerCount = 0;
        inline static std::int32_t _maxPlayers = 4;
        inline static std::int32_t _playersCreated = 0;
        inline static std::array<std::array<::OpenTK::Mathematics::Matrix4, _mbTrailSegments>, SlotCapacity> _mbTrailMatrices{};
        inline static std::array<std::array<float, _mbTrailSegments>, SlotCapacity> _mbTrailAlphas{};
        inline static std::array<std::int32_t, SlotCapacity> _mbTrailIndices{};
        inline static std::array<std::optional<std::string>, 8> _altAttackNames{};
        inline static std::array<std::optional<std::string>, 8> _hunterNames{};
        inline static std::array<std::optional<std::string>, 9> _weaponNames{};
    };
}

#undef MPHREAD_PLAYER_ENTITY_CANONICAL_HEADER

#include "PlayerAi.hpp"
