#pragma once

#include "Mods/Network/Domain/PowerUps.hpp"
#include "Mods/Network/Gameplay/Pickups/PredictionBook.hpp"

#include <cstdint>
#include <string>

namespace MphRead::Entities
{
    class ItemInstanceEntity;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // A client's own pickups of a power-up, shown before the authority has
    // said so: the effect starts and the item goes the frame it is touched,
    // a claim goes out (NetPickupClaims), and the answer either confirms it
    // or takes it back. What each answer means is the PredictionBook's; this
    // applies it to this machine's player. The effect a prediction starts
    // only ever changes what this machine shows -- a doubled hit is still
    // judged against the authority's grant (NetPowerUps::RanAt).
    class NetPowerUpPrediction final
    {
    public:
        NetPowerUpPrediction() = delete;

        // This machine's player touched a power-up a spawner put out.
        static void Predict(Entities::PlayerEntity& player, Entities::ItemInstanceEntity& item);
        // The item that spawn put out is shown taken here: not drawn again
        // on the strength of an item sync that has not caught up.
        [[nodiscard]] static bool Holds(std::int16_t spawnId, std::uint16_t spawnCount) noexcept;
        // A prediction of `kind` the snapshots have not caught up with yet,
        // which a snapshot saying it is not running must not end.
        [[nodiscard]] static bool Awaiting(PowerUp kind) noexcept;
        // The authority's answer to a claim: granted under `sequence`, or not.
        static void Settle(std::uint16_t claimId, bool granted, std::uint8_t sequence);
        // A snapshot says this machine's player holds grant `sequence`.
        static void Seen(PowerUp kind, std::uint8_t sequence);
        static void Tick();
        static void Reset();
        [[nodiscard]] static std::string Describe();

    private:
        inline static PredictionBook _book{};
        inline static std::int64_t _predicted = 0;
        inline static std::int64_t _confirmed = 0;
        inline static std::int64_t _refused = 0;
        inline static std::int64_t _undone = 0;
    };
}
