#pragma once

#include "Mods/Input/NativeTouchState.hpp"
#include "Mods/Network/Protocol/PowerUpSync.hpp"
#include "Mods/Network/Domain/PowerUps.hpp"
#include "Mods/Network/Protocol/Packets/ImpactOffset.hpp"
#include "Mods/Network/Protocol/Packets/ShotRayValidation.hpp"
#include "Formats/Types.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    enum class IntentButtons : std::uint32_t
    {
        None = 0,
        MoveLeft = 1U << 0,
        MoveRight = 1U << 1,
        MoveUp = 1U << 2,
        MoveDown = 1U << 3,
        Shoot = 1U << 4,
        Zoom = 1U << 5,
        Jump = 1U << 6,
        Morph = 1U << 7,
        Boost = 1U << 8,
        AltAttack = 1U << 9,
        ScanVisor = 1U << 10,
        NextWeapon = 1U << 11,
        PrevWeapon = 1U << 12,
        RollLeft = 1U << 13,
        RollRight = 1U << 14,
        RollUp = 1U << 15,
        RollDown = 1U << 16,
        ZoomedState = 1U << 17,
        AltFormState = 1U << 18,
        InPlayState = 1U << 19,
        SpectatingState = 1U << 20,
        ReadyState = 1U << 21
    };

    [[nodiscard]] constexpr IntentButtons operator|(IntentButtons left, IntentButtons right) noexcept
    {
        return static_cast<IntentButtons>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr IntentButtons operator&(IntentButtons left, IntentButtons right) noexcept
    {
        return static_cast<IntentButtons>(
            static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr IntentButtons operator^(IntentButtons left, IntentButtons right) noexcept
    {
        return static_cast<IntentButtons>(
            static_cast<std::uint32_t>(left) ^ static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] constexpr IntentButtons operator~(IntentButtons value) noexcept
    {
        return static_cast<IntentButtons>(~static_cast<std::uint32_t>(value));
    }

    constexpr IntentButtons& operator|=(IntentButtons& left, IntentButtons right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr IntentButtons& operator&=(IntentButtons& left, IntentButtons right) noexcept
    {
        left = left & right;
        return left;
    }

    constexpr IntentButtons& operator^=(IntentButtons& left, IntentButtons right) noexcept
    {
        left = left ^ right;
        return left;
    }

    struct IntentPacket
    {
        std::uint16_t MatchId = 0;
        std::uint64_t AuthorityEpoch = 0;
        std::uint16_t SlotGeneration = 0;
        std::uint16_t LifeId = 0;
        static constexpr std::int32_t PressHistory = 8;
        static constexpr std::int32_t Size = 4 + 4 + 12 + 1 + 4 * PressHistory + 12 + 2 + 2 + 4 + 1 + 14;

        // Four bytes appended past Size, carrying the state that decides what
        // this player's next shot is worth: the charge, the alt-form ram's
        // strength and the two multipliers. Appended rather than folded in, so
        // a build from before finds none and behaves as it always did.
        static constexpr std::int32_t StateSize = 4;
        // The owner's DS touch state (NativeTouchState::Reported): its flags
        // in the state block's spare fourth byte, then Delta4X and Delta4Y in
        // four bytes after it. The authority runs the owner's touch roll and
        // touch/shoulder boost arbitration from it. A payload that stops at
        // StateSize -- a demo recorded before -- reads as no contact.
        static constexpr std::int32_t LegacyTouchSize = 4;
        // Protocol 16 appends the native sample sequence. The sibling bit
        // lives in TouchFlags; identical deltas still identify distinct ticks.
        static constexpr std::int32_t TouchSize = LegacyTouchSize + 4;
        static constexpr std::int32_t FullSize = Size + StateSize + TouchSize;
        // The ray of the shot this frame actually fired, after spread: the
        // authority fires the same ray instead of rebuilding one from a body
        // position and a gun vector a frame older than the trigger.
        //
        // Shot events (protocol 20, rays and worlds since 21). A shot is an
        // event with a sequence number and everything it left with -- weapon,
        // charge, the snapshot frame its shooter was looking at, and its
        // ray -- taken the moment it was fired. A remote player's copy fires
        // each event, and nothing else (NetShotEvents); a hit claim names its
        // shot by sequence (HitClaimPacket::ShotSequence) and is checked
        // against it. The weapon held stays WeaponSelect's.
        //
        // The ray is any shot this frame fired, continuous fire included (it
        // makes no events). The history is the sender's latest events, oldest
        // first, repeated in every intent while a receiver could still fire
        // them: an intent lost, or refused for arriving behind a newer one,
        // loses no shot, and the receiver deduplicates on the sequence.
        //
        // Past FullSize only what there is (protocol 27) -- an idle intent was
        // 388 bytes of zeros and repeats, eight players relaying them all to
        // each other at 60 Hz:
        //   sections u8     SectionRay | SectionShots | SectionBombs
        //   ray             origin xyz, direction xyz                  24
        //   shots           count u8, then count x (sequence u32, frame u32,
        //                   ack u32, weapon u8, charge u8, 2 reserved,
        //                   origin xyz, direction xyz)                 1 + n x 40
        //   bombs           count u8, gone mask u8, then
        //                   count x (sequence u32, xyz)                2 + n x 16
        // each present only when its bit is set. A section cut short is read
        // as absent, never half read.
        static constexpr std::int32_t ShotSize = 24;
        static constexpr std::int32_t ShotEventSize = 40;
        static constexpr std::int32_t ShotHistoryCount = 4;
        static constexpr std::uint8_t SectionRay = 1U << 0;
        static constexpr std::uint8_t SectionShots = 1U << 1;
        static constexpr std::uint8_t SectionBombs = 1U << 2;
        static constexpr std::uint8_t NoWeapon = 0xFF;
        struct ShotEvent
        {
            std::uint32_t Sequence = 0;
            // The sender's frame it was fired on.
            std::uint32_t Frame = 0;
            std::uint8_t WeaponId = NoWeapon;
            // EquipInfo::ChargeLevel as the shot left, clamped to a byte.
            std::uint8_t Charge = 0;
            // The snapshot frame the shooter was looking at (the shot's
            // launch frame), and the ray the shot left on.
            std::uint32_t AckFrame = 0;
            ::OpenTK::Mathematics::Vector3 Origin{};
            ::OpenTK::Mathematics::Vector3 Direction{};
            [[nodiscard]] bool HasRay() const noexcept
            {
                return ShotRayValidation::IsValidDirection(Direction);
            }
        };
        bool HasShot = false;
        ::OpenTK::Mathematics::Vector3 ShotOrigin{};
        ::OpenTK::Mathematics::Vector3 ShotDirection{};
        std::array<ShotEvent, ShotHistoryCount> ShotHistory{};
        std::uint8_t ShotHistoryLength = 0;

        // The sender's bombs (protocol 24), so a copy lays and loses exactly
        // the bombs its owner has (NetBombs): every one standing, and every
        // one gone in the last few frames -- a Lockjaw that closes a triangle
        // nobody is in goes off the next frame, and a bomb standing for one
        // frame is otherwise lost with the one intent that carried it.
        static constexpr std::int32_t BombCount = 6;
        static constexpr std::int32_t BombSize = 16;
        static constexpr std::int32_t MaxWireSize = FullSize + 1 + ShotSize
            + 1 + ShotEventSize * ShotHistoryCount + 2 + BombSize * BombCount;
        struct Bomb
        {
            std::uint32_t Sequence = 0;
            ::OpenTK::Mathematics::Vector3 Position{};
            // Already gone on the sender's machine.
            bool Gone = false;
        };
        std::array<Bomb, BombCount> Bombs{};
        std::uint8_t BombsLength = 0;
        // Whether the sender reported its bombs at all.
        bool HasBombs = false;

        // The bytes Write fills: FullSize and the sections there are.
        [[nodiscard]] std::size_t WireSize() const noexcept;

    private:
        void WriteSections(std::span<std::uint8_t> dest) const;
        void ReadSections(std::span<const std::uint8_t> src);

    public:

        std::uint8_t ChargeLevel = 0;
        std::uint8_t BoostDamage = 0;
        std::uint8_t ShotFlags = 0;

        // Bit 0 was the owner's double damage, which the authority now grants
        // and records itself (NetPowerUps); it is never set.
        // Sent but not applied: who the Prime Hunter is is the authority's
        // own state. It travels so that a mismatch shows up in a log.
        static constexpr std::uint8_t FlagPrimeHunter = 1U << 1;

        // Whether the sender included the block at all.
        bool HasState = false;

        static constexpr std::uint8_t TouchPresent = 1U << 0;
        static constexpr std::uint8_t TouchDown = 1U << 1;
        static constexpr std::uint8_t TouchContinued = 1U << 2;
        static constexpr std::uint8_t TouchSamplePresent = 1U << 3;
        static constexpr std::uint8_t TouchSecondStep = 1U << 4;
        std::uint8_t TouchFlags = 0;
        std::int16_t TouchDelta4X = 0;
        std::int16_t TouchDelta4Y = 0;
        std::uint32_t TouchSampleSequence = 0;
        [[nodiscard]] bool HasTouch() const noexcept { return (TouchFlags & TouchPresent) != 0; }
        [[nodiscard]] bool HasTouchSample() const noexcept { return (TouchFlags & TouchSamplePresent) != 0; }
        void SetTouchReport(const ::MphRead::Mods::Input::NativeTouchState::Reported& touch) noexcept;
        [[nodiscard]] ::MphRead::Mods::Input::NativeTouchState::Reported TouchReport() const noexcept;

        std::uint32_t Frame = 0;
        IntentButtons Buttons = IntentButtons::None;
        // Recent per-frame button edges, oldest last. Alt attacks travel as
        // the same native input event as local play; attack-specific behavior
        // and animation stay in PlayerEntity instead of a network duplicate.
        std::shared_ptr<std::vector<std::uint32_t>> Presses{};
        ::OpenTK::Mathematics::Vector3 Aim{};
        ::OpenTK::Mathematics::Vector3 Position{};
        std::uint8_t WeaponSelect = 0;
        std::uint16_t AmmoUa = 0;
        std::uint16_t AmmoMissiles = 0;
        std::uint32_t AckFrame = 0;
        // How far past AckFrame the world this client was looking at actually
        // sat, in 1/256ths of a frame. Zero from a client that does not
        // interpolate.
        std::uint8_t AckSubFrame = 0;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static IntentPacket Read(std::span<const std::uint8_t> src);
    };

    // Where on a hunter a hit landed, relative to their Position, in 1/64
    // units on a signed byte per axis (+-1.98): what the shooter's machine
    // resolved, carried by its claim and handed on to the victim with the
    // damage, so the victim's screen shows the hit where the shooter's did.
    struct DamageEvent
    {
        static constexpr std::int32_t Size = 15;

        std::uint16_t EventId = 0;
        std::uint16_t AttackerGeneration = 0;
        std::uint16_t Damage = 0;
        std::uint8_t AttackerSlot = 0;
        std::uint8_t Beam = 0;
        std::uint8_t Flags = 0;
        ::OpenTK::Mathematics::Vector3 Direction{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static DamageEvent Read(std::span<const std::uint8_t> src);

    private:
        static constexpr float DirectionScale = 16384.0F;
        [[nodiscard]] static std::int16_t PackDirection(float value) noexcept;
        [[nodiscard]] static float UnpackDirection(std::int16_t value) noexcept;
    };

    // The authority's remaining time for each timed status effect. Policies
    // decide how each effect starts, updates, and ends on a replica.
    struct TimedEffectTimers
    {
        // Burn uses 9 bits (0..300 frames), disrupt 7 bits (0..120), and
        // freeze keeps its existing byte (0..150). All three are exact for
        // current game durations while costing only two additional bytes.
        static constexpr std::int32_t Size = 3;
        static constexpr std::uint16_t MaxBurningFrames = 0x1FF;
        static constexpr std::uint16_t MaxDisruptedFrames = 0x7F;
        static constexpr std::uint16_t MaxFrozenFrames = 0xFF;

        std::uint16_t BurningFrames = 0;
        std::uint16_t DisruptedFrames = 0;
        std::uint16_t FrozenFrames = 0;

        [[nodiscard]] static constexpr std::uint16_t Clamp(std::uint16_t frames,
            std::uint16_t maximum) noexcept
        {
            return frames > maximum ? maximum : frames;
        }

        [[nodiscard]] constexpr std::uint16_t PackBurnAndDisrupt() const noexcept
        {
            return static_cast<std::uint16_t>(Clamp(BurningFrames, MaxBurningFrames)
                | (Clamp(DisruptedFrames, MaxDisruptedFrames) << 9));
        }

        constexpr void UnpackBurnAndDisrupt(std::uint16_t packed) noexcept
        {
            BurningFrames = static_cast<std::uint16_t>(packed & MaxBurningFrames);
            DisruptedFrames = static_cast<std::uint16_t>((packed >> 9) & MaxDisruptedFrames);
        }
    };

    struct PlayerState
    {
        std::uint16_t SlotGeneration = 0;
        std::uint16_t LifeId = 0;
        static constexpr std::int32_t DamageHistory = 4;
        static constexpr std::int32_t LegacySize = 54 + DamageEvent::Size * DamageHistory;
        // + Weavel (14) + the newest damage event's confirmed impact (3)
        // + the power-ups the authority granted (protocol 29, 2 each).
        static constexpr std::int32_t PowerUpOffset = LegacySize + 14 + 3;
        // Protocol 30's freeze byte stays at the same offset; protocol 31
        // adds the packed burn/disrupt timers after it.
        static constexpr std::int32_t FrozenFramesOffset
            = PowerUpOffset + 2 * static_cast<std::int32_t>(PowerUpCount);
        static constexpr std::int32_t BurnDisruptFramesOffset = FrozenFramesOffset + 1;
        static constexpr std::int32_t TimedEffectsOffset = FrozenFramesOffset;
        static constexpr std::int32_t Size = TimedEffectsOffset + TimedEffectTimers::Size;

        std::uint8_t SlotIndex = 0;
        std::uint8_t Flags = 0;
        ::OpenTK::Mathematics::Vector3 Position{};
        ::OpenTK::Mathematics::Vector3 Speed{};
        ::OpenTK::Mathematics::Vector3 Facing{};
        std::uint16_t Health = 0;
        std::uint8_t CurrentWeapon = 0;
        std::uint8_t Team = 0;
        std::uint16_t DamageEventId = 0;
        DamageEvent Damage0{};
        DamageEvent Damage1{};
        DamageEvent Damage2{};
        DamageEvent Damage3{};
        [[nodiscard]] DamageEvent EventAt(std::int32_t index) const;
        // Derived on Read from the newest damage event.
        std::uint8_t AttackerSlot = 0;
        std::uint8_t DamageBeam = 0;
        std::uint8_t DamageFlags = 0;
        ::OpenTK::Mathematics::Vector3 HitDirection{};
        std::int16_t Points = 0;
        std::uint16_t Kills = 0;
        std::uint16_t Deaths = 0;

        // Protocol 22's Weavel/turret byte. For other hunters, its two
        // transition bits and upper nibble carry form-animation state; old
        // Protocol 30 peers ignore this field for those hunters.
        std::uint8_t WeavelFlags = 0;
        std::uint8_t HalfturretHealth = 0;
        ::OpenTK::Mathematics::Vector3 HalfturretPosition{};
        // The newest damage event (DamageEventId): the low byte of its shot's
        // launch frame, and where on the body the shooter saw it land --
        // height in 1/64 unit, bearing in 256 steps, drawn at ImpactRadius.
        // Per player and not per event, so eight states still leave the
        // health sync its room; an older event in the history goes without.
        std::uint8_t ImpactLaunchLow = 0;
        ImpactOffset Impact{};
        // Double damage, cloak, Death Alt (PowerUp order): the authority's
        // grant and what it has left (NetPowerUps).
        std::array<PowerUpReport, PowerUpCount> PowerUps{};
        TimedEffectTimers StatusEffects{};
        static constexpr float ImpactRadius = 0.55F;
        static constexpr std::uint8_t WeavelFlagTurretActive = 1U << 0;
        static constexpr std::uint8_t WeavelFlagTurretGrounded = 1U << 1;
        // The authority's copy is playing a transition (protocol 22): every
        // other copy starts the same animation now, rather than once the
        // authority's has ended and FlagAltForm says so. For non-Weavel
        // hunters, the upper nibble also sends the animation phase in 16 steps.
        static constexpr std::uint8_t WeavelFlagMorphing = 1U << 2;
        static constexpr std::uint8_t WeavelFlagUnmorphing = 1U << 3;
        // For non-Weavel hunters the upper nibble carries a quantized biped
        // morph-animation phase. Older Protocol 30 peers ignore this byte for
        // those hunters, so the layout and Weavel interpretation stay intact.
        static constexpr std::uint8_t FormTransitionProgressShift = 4;
        static constexpr std::uint8_t FormTransitionProgressMask = 0xFU << FormTransitionProgressShift;
        static constexpr std::uint8_t WeavelFlagsKnown = WeavelFlagTurretActive | WeavelFlagTurretGrounded
            | WeavelFlagMorphing | WeavelFlagUnmorphing;

        [[nodiscard]] std::uint8_t FormTransitionProgress() const noexcept
        {
            return static_cast<std::uint8_t>((WeavelFlags & FormTransitionProgressMask)
                >> FormTransitionProgressShift);
        }

        void SetFormTransitionProgress(std::uint8_t progress) noexcept
        {
            const std::uint8_t bounded = progress > 15 ? 15 : progress;
            WeavelFlags = static_cast<std::uint8_t>((WeavelFlags & ~FormTransitionProgressMask)
                | (bounded << FormTransitionProgressShift));
        }

        // The form the player is in or on the way to.
        [[nodiscard]] bool HeadingAlt() const noexcept
        {
            if ((WeavelFlags & WeavelFlagMorphing) != 0)
            {
                return true;
            }
            if ((WeavelFlags & WeavelFlagUnmorphing) != 0)
            {
                return false;
            }
            return (Flags & FlagAltForm) != 0;
        }

        static constexpr std::uint8_t FlagActive = 1U << 0;
        static constexpr std::uint8_t FlagAltForm = 1U << 1;
        static constexpr std::uint8_t FlagSpawned = 1U << 2;
        static constexpr std::uint8_t FlagZoomed = 1U << 3;
        static constexpr std::uint8_t FlagSpectating = 1U << 4;
        static constexpr std::uint8_t FlagFrozen = 1U << 5;
        static constexpr std::uint8_t FlagDisrupted = 1U << 6;
        static constexpr std::uint8_t FlagBurning = 1U << 7;

        [[nodiscard]] bool StatusEffectFlagsMatchTimers() const noexcept
        {
            return ((Flags & FlagBurning) != 0) == (StatusEffects.BurningFrames > 0)
                && ((Flags & FlagDisrupted) != 0) == (StatusEffects.DisruptedFrames > 0)
                && ((Flags & FlagFrozen) != 0) == (StatusEffects.FrozenFrames > 0);
        }

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static PlayerState Read(std::span<const std::uint8_t> src);

    private:
        static void WriteVec(std::span<std::uint8_t> dest,
            ::OpenTK::Mathematics::Vector3 value);
        [[nodiscard]] static ::OpenTK::Mathematics::Vector3 ReadVec(
            std::span<const std::uint8_t> src);
    };

    struct SnapshotHeader
    {
        std::uint16_t MatchId = 0;
        std::uint64_t AuthorityEpoch = 0;
        static constexpr std::int32_t Size = 4 + 4 + 4 + 1 + 10;

        std::uint32_t Frame = 0;
        std::uint32_t Rng1 = 0;
        std::uint32_t Rng2 = 0;
        std::uint8_t PlayerCount = 0;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static SnapshotHeader Read(std::span<const std::uint8_t> src);
    };

}
