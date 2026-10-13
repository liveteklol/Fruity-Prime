#pragma once

#include "Mods/Network/Gameplay/Pickups/PowerUpLedger.hpp"
#include "Mods/Network/Domain/PowerUps.hpp"
#include "Entities/Players/PlayerEntity.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    struct PlayerState;

    // Power-ups in a networked match. The authority grants every one --
    // from a contact it saw itself or a client's claim it accepted -- records
    // it (PowerUpLedger) and publishes it in every snapshot; each client
    // takes its copies' state from there (PowerUpSync), its own player's
    // included, once its prediction has been answered.
    class NetPowerUps final
    {
    public:
        NetPowerUps() = delete;

        // Authority: `player` touched a power-up here. A remote player's
        // contact happened in the world its last intent acknowledged.
        static void GrantContact(Entities::PlayerEntity& player, ::MphRead::ItemType type);
        // Authority: a grant beginning at `start` (an authority frame at or
        // before now), applied to the authority's copy for what is left of it.
        static PowerUpLedger::Grant Grant(Entities::PlayerEntity& player, PowerUp kind, std::uint32_t start);
        // Authority, once a simulation step: a power-up the game ended before
        // its time (a death) ends there in the ledger too.
        static void Observe();
        // Whether the player had it at `frame`, in life `life`.
        [[nodiscard]] static bool RanAt(std::int32_t slot, PowerUp kind, std::uint32_t frame, std::uint16_t life);
        // Authority: the player begins a new life, which carries no power-up
        // over -- whatever the last one still had ends now, whether or not a
        // step saw its timers drop (Observe).
        static void BeginLife(std::int32_t slot);
        [[nodiscard]] static PowerUpLedger::Grant Current(std::int32_t slot, PowerUp kind);

        // Authority: what the snapshot says of `player`'s power-ups.
        static void Capture(const Entities::PlayerEntity& player, PlayerState& state);
        // Client: one snapshot's word about `player`'s power-ups.
        static void Apply(Entities::PlayerEntity& player, const PlayerState& state, bool isLocal);

        static void ForgetSlot(std::int32_t slot);
        static void Reset();
        [[nodiscard]] static std::string Describe();

    private:
        static constexpr std::size_t Slots = PowerUpLedger::Slots;

        [[nodiscard]] static std::uint32_t ContactFrame(const Entities::PlayerEntity& player);

        inline static PowerUpLedger _ledger{};
        // Client: the grant sequence each copy last took.
        inline static std::array<std::array<std::uint8_t, PowerUpCount>, Slots> _seen{};
        inline static std::int64_t _granted = 0;
        inline static std::int64_t _endedEarly = 0;
        inline static std::int64_t _started = 0;
        inline static std::int64_t _retimed = 0;
        inline static std::int64_t _resumed = 0;
        inline static std::int64_t _ended = 0;
    };
}
