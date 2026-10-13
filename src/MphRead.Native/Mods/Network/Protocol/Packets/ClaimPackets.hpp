#pragma once

#include "Mods/Network/Protocol/Packets/ImpactOffset.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace MphRead::Mods::Network
{
    struct PickupClaimPacket
    {
        static constexpr std::int32_t Size = 2 + 8 + 2 + 2 + 2 + 2 + 2 + 4 + 12;
        static constexpr std::int32_t MaxPerPacket = 4;

        std::uint16_t MatchId = 0;
        std::uint64_t AuthorityEpoch = 0;
        std::uint16_t SlotGeneration = 0;
        std::uint16_t LifeId = 0;
        std::uint16_t ClaimId = 0;
        // The spawner, and which of its spawns: a claim for an item that has
        // since come back is not a claim for the new one.
        std::int16_t SpawnId = 0;
        std::uint16_t SpawnCount = 0;
        // The authority frame the claimant's world was at when it touched it:
        // where a granted power-up starts.
        std::uint32_t ContactFrame = 0;
        ::OpenTK::Mathematics::Vector3 Position{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static PickupClaimPacket Read(std::span<const std::uint8_t> src);
    };

    struct PickupVerdictPacket
    {
        static constexpr std::int32_t HeaderSize = 15;
        static constexpr std::int32_t EntrySize = 4;
        static constexpr std::int32_t MaxPerPacket = 8;

        static constexpr std::uint8_t ResultGranted = 0;
        // Somebody else had it first.
        static constexpr std::uint8_t ResultTaken = 1;
        // Not the spawn the claim names: gone, or come back since.
        static constexpr std::uint8_t ResultGone = 2;
        static constexpr std::uint8_t ResultOutOfReach = 3;
        static constexpr std::uint8_t ResultTooOld = 4;
        static constexpr std::uint8_t ResultWrongLife = 5;
        static constexpr std::uint8_t ResultNotClaimable = 6;

        struct Entry
        {
            std::uint16_t ClaimId = 0;
            std::uint8_t Result = 0;
            // The grant's sequence (PowerUpLedger) when granted.
            std::uint8_t Sequence = 0;
        };

        static void Write(std::span<std::uint8_t> dest, std::span<const Entry> entries,
            std::uint16_t matchId, std::uint64_t epoch, std::uint16_t generation, std::uint16_t lifeId);
        [[nodiscard]] static std::string Describe(std::uint8_t result);
    };

    struct HitClaimPacket
    {
        std::uint16_t MatchId = 0;
        std::uint64_t AuthorityEpoch = 0;
        std::uint16_t ShooterGeneration = 0;
        std::uint16_t ShooterLifeId = 0;
        std::uint16_t VictimGeneration = 0;
        std::uint16_t VictimLifeId = 0;
        // + 4: protocol 21's ShotSequence; + 2: protocol 23's TurretDamage;
        // + 1: protocol 26's Cause.
        static constexpr std::int32_t Size = 2 + 4 + 4 + 4 + 1 + 1 + 2 + 1 + 12 + 18 + 12 + 3 + 4 + 2 + 1;

        static constexpr std::int32_t MaxPerPacket = 6;

        static constexpr std::uint8_t NoBeam = 0xFF;

        static constexpr std::uint8_t FlagHeadshot = 1U << 0;
        static constexpr std::uint8_t FlagLethal = 1U << 1;
        static constexpr std::uint8_t FlagFrozen = 1U << 2;
        static constexpr std::uint8_t FlagBurning = 1U << 3;
        static constexpr std::uint8_t FlagDisrupted = 1U << 4;
        // Impulse holds the knockback the shooter's machine applied; the
        // authority applies the same one, since it no longer resolves the hit.
        static constexpr std::uint8_t FlagImpulse = 1U << 5;
        // The hit was the projectile's splash, not its body: Impact is then
        // the blast's centre relative to the victim.
        static constexpr std::uint8_t FlagSplash = 1U << 6;
        // The damage was doubled on the shooter's machine (protocol 29): held
        // against the double damage the authority granted, at the frame the
        // shot was fired, and halved when it was not running then.
        static constexpr std::uint8_t FlagDoubled = 1U << 7;
        // What did the damage, past the weapon (protocol 26): what the
        // victim's screen names a kill after, and what the authority
        // applies it as. Taken only when the authority can vouch for it --
        // a Death Alt it granted, running in the world the hit was resolved
        // in (NetPowerUps). A burn's ticks are never claimed: the
        // authority applies them itself.
        static constexpr std::uint8_t CauseHit = 0;
        static constexpr std::uint8_t CauseDeathalt = 1;

        std::uint16_t ClaimId = 0;
        std::uint32_t Frame = 0;
        std::uint32_t AckFrame = 0;
        std::uint32_t LaunchFrame = 0;
        std::uint8_t VictimSlot = 0;
        std::uint8_t Beam = 0;
        std::uint16_t Damage = 0;
        std::uint8_t Flags = 0;
        ::OpenTK::Mathematics::Vector3 HitPoint{};
        ::OpenTK::Mathematics::Vector3 Impulse{};
        ImpactOffset Impact{};
        // The shot event (IntentPacket::ShotEvent) the hit came from; 0 for
        // one with none (continuous fire, a turret's shot).
        std::uint32_t ShotSequence = 0;
        // A hit on a Weavel's turret: the share of it the turret took on the
        // shooter's machine, Damage being the rest (the body's). The
        // authority applies the two together as the turret hit they were,
        // split by its own health. Without it the authority applied only the
        // body's share, and nobody but the authority could destroy a turret.
        std::uint16_t TurretDamage = 0;
        std::uint8_t Cause = CauseHit;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static HitClaimPacket Read(std::span<const std::uint8_t> src);
    };

    struct HitVerdictPacket
    {
        static constexpr std::int32_t HeaderSize = 15;
        static constexpr std::int32_t EntrySize = 3;
        static constexpr std::int32_t MaxPerPacket = 16;

        static constexpr std::uint8_t ResultApplied = 0;
        static constexpr std::uint8_t ResultDuplicate = 1;
        static constexpr std::uint8_t ResultDeadShooter = 2;
        static constexpr std::uint8_t ResultDeadVictim = 3;
        static constexpr std::uint8_t ResultRefused = 4;
        static constexpr std::uint8_t ResultTooOld = 5;
        static constexpr std::uint8_t ResultWrongLife = 6;
        static constexpr std::uint8_t ResultGeometry = 7;
        static constexpr std::uint8_t ResultDamageLimit = 8;
        static constexpr std::uint8_t ResultInvalidLaunch = 9;
        static constexpr std::uint8_t ResultNoDamage = 10;
        // Every claim the authority can hold was already waiting: refused,
        // and answered, rather than never answered.
        static constexpr std::uint8_t ResultBusy = 11;

        std::uint16_t ClaimId = 0;
        std::uint8_t Result = 0;

        static void Write(std::span<std::uint8_t> dest,
            std::span<const std::pair<std::uint16_t, std::uint8_t>> entries,
            std::uint16_t matchId, std::uint64_t epoch, std::uint16_t generation,
            std::uint16_t lifeId);
        [[nodiscard]] static std::string Describe(std::uint8_t result);
    };

}
