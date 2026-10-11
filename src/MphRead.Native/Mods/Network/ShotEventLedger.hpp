#pragma once

#include "NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace MphRead::Mods::Network
{
    // The shot events one remote player has reported, by sequence: what a hit
    // claim naming a shot (HitClaimPacket::ShotSequence) is checked against --
    // the weapon it left with, the world it was aimed in and its ray.
    //
    // Unlike RemoteShotQueue, nothing is spent here: a shot can be named by
    // several claims (splash, ricochets, several victims), and for as long as
    // its projectile can fly.
    class ShotEventLedger final
    {
    public:
        static constexpr std::size_t Capacity = 64;

        // Keeps the event unless its entry already holds a newer shot (a
        // late intent's event Capacity shots older shares the entry).
        void Note(const IntentPacket::ShotEvent& event) noexcept;
        [[nodiscard]] std::optional<IntentPacket::ShotEvent> Find(std::uint32_t sequence) const noexcept;
        // Kept across a Reset: they describe the connection, not the life.
        void Reset() noexcept { _events = {}; }

        // Events refused for being older than the shot their entry holds,
        // and events contradicting the one kept under the same sequence.
        [[nodiscard]] std::uint64_t StaleRefused() const noexcept { return _staleRefused; }
        [[nodiscard]] std::uint64_t Conflicts() const noexcept { return _conflicts; }

    private:
        std::array<IntentPacket::ShotEvent, Capacity> _events{};
        std::uint64_t _staleRefused = 0;
        std::uint64_t _conflicts = 0;
    };
}
