#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Per-session hit-claim counters and the compact damage-agreement sample
    // used by netcheck and the server diagnostics.
    class HitClaimMetrics final
    {
    public:
        enum class Counter : std::uint8_t
        {
            Applied,
            Duplicate,
            RefusedDeadShooter,
            RefusedDeadVictim,
            RefusedOther,
            ServerCopiesSuppressed,
            Received,
            AppliedHere,
            DuplicateHere,
            VoidedDeadShooter,
            VoidedDeadVictim,
            RefusedHere,
            BusyHere,
            UndoubledHere,
            CappedHere,
            TooOldHere,
            RepeatsHere,
            MatchedByLaunch,
            MatchedByWindow,
            RescuedDamage,
            RescuedKills,
            RescuedHeadshots,
            SuppressedHere,
            FinishedHere,
            ImpactRefused,
            AfflictionsStripped,
            CausesVouched,
            CausesStripped,
            AckRefused,
            RayRefused,
            EventRefused,
            EventMatched,
            EventNamed,
            EventUnknown,
            LineOfSightRefused,
            RateRefused,
            Count
        };

        static constexpr std::size_t DamageBucketCount = 11;

        struct DamageAgreement final
        {
            std::int64_t Agreed = 0;
            std::int64_t Differed = 0;
            std::int64_t Claimed = 0;
            std::int64_t Resolved = 0;
        };

        void Increment(Counter counter) noexcept;
        void Add(Counter counter, std::int64_t amount) noexcept;
        [[nodiscard]] std::int64_t Value(Counter counter) const noexcept;

        // Returns true when this sample contributes a disagreement log entry.
        [[nodiscard]] bool RecordDamageAgreement(
            std::size_t bucket, std::int32_t claimed, std::int32_t resolved) noexcept;
        [[nodiscard]] DamageAgreement AgreementAt(std::size_t bucket) const noexcept;
        [[nodiscard]] bool TakeDisagreementLogSlot() noexcept;

        void Reset() noexcept;

    private:
        static constexpr std::size_t CounterCount = static_cast<std::size_t>(Counter::Count);

        std::array<std::int64_t, CounterCount> _counters{};
        std::array<DamageAgreement, DamageBucketCount> _damageAgreement{};
        std::int32_t _disagreementsLogged = 0;
    };
}
