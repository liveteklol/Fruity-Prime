#pragma once

#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    // Owns the bounded client-side HitClaim queue, its sequence, resend
    // schedule, and queue-specific counters. Verdict interpretation and
    // prediction settlement remain with NetHitClaims.
    class HitClaimOutbox final
    {
    public:
        static constexpr std::size_t Capacity = 32;
        static constexpr std::int32_t MaxSends = 6;
        static constexpr std::int32_t MaxAge = 60;

        struct Entry final
        {
            std::uint32_t ShotSequence = 0;
            std::uint16_t MatchId = 0;
            std::uint64_t AuthorityEpoch = 0;
            std::uint16_t ShooterGeneration = 0;
            std::uint16_t ShooterLifeId = 0;
            std::uint16_t VictimGeneration = 0;
            std::uint16_t VictimLifeId = 0;
            std::uint16_t Id = 0;
            std::uint32_t Frame = 0;
            std::uint32_t AckFrame = 0;
            std::uint32_t LaunchFrame = 0;
            std::uint8_t VictimSlot = 0;
            std::uint8_t Beam = 0;
            std::uint16_t Damage = 0;
            std::uint16_t TurretDamage = 0;
            std::uint8_t Cause = HitClaimPacket::CauseHit;
            std::uint8_t Flags = 0;
            OpenTK::Mathematics::Vector3 HitPoint{};
            OpenTK::Mathematics::Vector3 Impulse{};
            ImpactOffset Impact{};
            std::int32_t Age = 0;
            std::int32_t Sends = 0;
            // Age at the last send: what the next resend is timed from.
            std::int32_t LastSentAge = 0;
            bool Live = false;
        };

        [[nodiscard]] std::uint16_t Enqueue(Entry entry) noexcept;
        [[nodiscard]] std::int32_t Compose(std::span<std::uint8_t> destination, std::int32_t resendInterval);
        template <typename TOnExpired>
        void Tick(std::int32_t maxAge, TOnExpired&& onExpired)
        {
            for (Entry& entry : _entries)
            {
                if (!entry.Live)
                {
                    continue;
                }
                entry.Age++;
                if (entry.Age > maxAge)
                {
                    entry.Live = false;
                    _unanswered++;
                    onExpired(entry);
                }
            }
        }
        [[nodiscard]] Entry* FindLive(std::uint16_t id) noexcept;
        void Complete(std::uint16_t id) noexcept;
        void ForgetSlot(std::int32_t slot, std::int32_t localSlot) noexcept;
        void ClearPending() noexcept;
        void Reset() noexcept;

        [[nodiscard]] std::int64_t Declared() const noexcept { return _declared; }
        [[nodiscard]] std::int64_t Unanswered() const noexcept { return _unanswered; }
        [[nodiscard]] std::int64_t Resends() const noexcept { return _resends; }

    private:
        std::array<Entry, Capacity> _entries{};
        std::uint16_t _nextId = 1;
        std::int64_t _declared = 0;
        std::int64_t _unanswered = 0;
        std::int64_t _resends = 0;
    };
}
