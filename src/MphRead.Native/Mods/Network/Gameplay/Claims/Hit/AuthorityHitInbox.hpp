#pragma once

#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    // Owns the bounded authority-side queue of claims awaiting arbitration.
    // Judging order and applying damage remain with NetHitClaims.
    class AuthorityHitInbox final
    {
    public:
        static constexpr std::size_t Capacity = 64;

        struct Entry final
        {
            std::uint16_t MatchId = 0;
            std::uint64_t AuthorityEpoch = 0;
            std::uint16_t ShooterGeneration = 0;
            std::uint16_t ShooterLifeId = 0;
            std::uint16_t VictimGeneration = 0;
            std::uint16_t VictimLifeId = 0;
            std::uint16_t Id = 0;
            std::uint8_t ShooterSlot = 0;
            std::uint8_t VictimSlot = 0;
            std::uint8_t Beam = 0;
            std::uint16_t Damage = 0;
            std::uint16_t TurretDamage = 0;
            std::uint8_t Cause = HitClaimPacket::CauseHit;
            std::uint8_t Flags = 0;
            std::uint32_t AckFrame = 0;
            std::uint32_t LaunchFrame = 0;
            std::uint32_t ShotSequence = 0;
            OpenTK::Mathematics::Vector3 HitPoint{};
            OpenTK::Mathematics::Vector3 Impulse{};
            ImpactOffset Impact{};
            std::uint32_t Arrived = 0;
            std::int32_t Grace = 0;
            bool Live = false;
            bool RequireAuthorityHit = false;
        };

        [[nodiscard]] Entry* TryAllocate() noexcept;
        [[nodiscard]] std::span<Entry, Capacity> Entries() noexcept { return _entries; }
        [[nodiscard]] std::span<const Entry, Capacity> Entries() const noexcept { return _entries; }
        [[nodiscard]] Entry& At(std::size_t index) noexcept { return _entries[index]; }
        [[nodiscard]] const Entry& At(std::size_t index) const noexcept { return _entries[index]; }

        void ForgetSlot(std::int32_t slot) noexcept;
        void Clear() noexcept;

    private:
        std::array<Entry, Capacity> _entries{};
    };
}
