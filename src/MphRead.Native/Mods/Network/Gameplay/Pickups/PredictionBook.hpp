#pragma once

#include "Mods/Network/Domain/PowerUps.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace MphRead::Mods::Network
{
    // A client's own power-up pickups that the authority has not ruled on
    // yet, and what each answer means for the effect shown -- kept apart from
    // the player and the wire (NetPowerUpPrediction) so every order the
    // answers can arrive in is checked on its own.
    //
    // Predictions are ordered by generation. A refusal rolls the effect back
    // to what was shown before the pickup, unless a later prediction of the
    // same kind is still waiting: that one owns what is shown, and takes over
    // the refused one's "before" as its own. Nothing made in one life survives
    // into the next, and nothing is rolled back onto a dead player.
    class PredictionBook final
    {
    public:
        static constexpr std::size_t Capacity = 4;
        // A granted pickup waits this long for a snapshot to carry its grant.
        static constexpr std::uint32_t UnseenFrames = 120;
        // And a confirmed one this long for the item sync to show it gone.
        static constexpr std::uint32_t HoldFrames = 600;

        struct Prediction
        {
            bool Live = false;
            std::uint16_t ClaimId = 0;
            PowerUp Kind = PowerUp::DoubleDamage;
            std::int16_t SpawnId = 0;
            std::uint16_t SpawnCount = 0;
            std::uint16_t Life = 0;
            std::uint32_t Since = 0;
            std::uint32_t Generation = 0;
            bool Granted = false;
            // Granted, and a snapshot has shown the grant.
            bool Confirmed = false;
            std::uint8_t Sequence = 0;
            // The grant held when this was predicted, and what it had left:
            // what a refusal puts back.
            std::uint8_t SeenBefore = 0;
            std::uint16_t RemainingBefore = 0;
        };

        // What a refusal does to the effect shown: `Kind` to show `Frames`.
        struct Rollback
        {
            PowerUp Kind = PowerUp::DoubleDamage;
            std::uint16_t Frames = 0;
        };

        [[nodiscard]] bool HasRoom() const noexcept;
        // A pickup shown now, while `remainingBefore` of the same kind ran.
        void Add(std::uint16_t claimId, PowerUp kind, std::int16_t spawnId, std::uint16_t spawnCount,
            std::uint16_t life, std::uint32_t now, std::uint16_t remainingBefore) noexcept;
        [[nodiscard]] bool Holds(std::int16_t spawnId, std::uint16_t spawnCount) const noexcept;
        [[nodiscard]] bool Awaiting(PowerUp kind) const noexcept;
        // The authority's answer. On a refusal, what the effect of that kind
        // is to show now; nothing when it is to be left as it is.
        [[nodiscard]] std::optional<Rollback> Settle(std::uint16_t claimId, bool granted, std::uint8_t sequence,
            std::uint32_t now, std::uint16_t life, bool alive) noexcept;
        // A snapshot says this player holds grant `sequence` of `kind`.
        void Seen(PowerUp kind, std::uint8_t sequence) noexcept;
        // This player's life is `life`: predictions from any other are dropped.
        void BeginLife(std::uint16_t life) noexcept;
        // Retires what is done; `settled(spawnId, spawnCount)` says the item
        // sync no longer shows that spawn out.
        template <typename SettledPredicate>
        void Tick(std::uint32_t now, const SettledPredicate& settled) noexcept
        {
            for (Prediction& prediction : _predictions)
            {
                if (!prediction.Live)
                {
                    continue;
                }
                const std::uint32_t age = now - prediction.Since;
                if (prediction.Granted && !prediction.Confirmed && age > UnseenFrames)
                {
                    prediction.Confirmed = true;
                }
                bool itemSettled = false;
                if (prediction.Confirmed)
                {
                    if constexpr (requires { settled.operator bool(); })
                    {
                        itemSettled = settled.operator bool()
                            && settled(prediction.SpawnId, prediction.SpawnCount);
                    }
                    else
                    {
                        itemSettled = settled(prediction.SpawnId, prediction.SpawnCount);
                    }
                }
                if (prediction.Confirmed && (itemSettled || age > HoldFrames))
                {
                    prediction.Live = false;
                }
            }
        }
        void Reset() noexcept;

    private:
        [[nodiscard]] Prediction* Find(std::uint16_t claimId) noexcept;
        [[nodiscard]] Prediction* Successor(const Prediction& prediction) noexcept;

        std::array<Prediction, Capacity> _predictions{};
        std::array<std::uint8_t, PowerUpCount> _seen{};
        std::uint32_t _nextGeneration = 0;
    };
}
